// ---------------------------------------------------------------------------
// Fn direct-call recursion-depth lattice (RFC 0026 FB-2 /
// CORE-FNBODY-DESIGN §8.1 rule 6).
// ---------------------------------------------------------------------------
//
// See core_recursion.hpp for the formal rule. This file is a PURE analysis over
// lowered Core-ANF. It classifies SSA integer values into a tiny affine term
// fragment (literal / pre-bound param +/- constant / bounded-container length
// word +/- constant), partitions the fn direct-call graph into SCCs, and seals
// one static depth bound per recursion group. It never trusts `decreases`
// (erased before Core) and never emits code.

#include "ahfl/compiler/ir/core_recursion.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl::ir::core {

namespace {

// One invocation edge observed in a body storage. A DIRECT edge is a
// CoreCallExpr (plain wasm `call`); an INDIRECT edge is one possible runtime
// target of a CoreCallClosureExpr (wasm `call_indirect`, FB-3b). One closure
// call site contributes 0..N indirect CallEdges (N = signature-matching
// closure constructions).
struct CallEdge {
    std::uint32_t caller_fn{CoreFnId::kInvalid}; // kInvalid = flow/workflow root
    std::uint32_t callee_fn{CoreFnId::kInvalid};
    const std::vector<CoreValueId> *args{nullptr};
    const CoreBodyStorage *storage{nullptr}; // caller storage (roots too)
    std::uint32_t expr{CoreExprId::kInvalid}; // the call expr's arena id
    bool indirect{false};                     // CoreCallClosureExpr possible target
    SourceRangeOpt range;
};

// Affine integer terms the lattice can reason about.
enum class TermKind { Unknown, Const, Param, Len };

struct Term {
    TermKind kind{TermKind::Unknown};
    std::int64_t c{0};      // literal value, or constant offset
    CoreValueId ref{};      // Param: param SSA value; Len: base container value
};

// Closed integer interval (lo <= hi), or unbounded.
struct Interval {
    std::int64_t lo{0};
    std::int64_t hi{0};
    bool finite{false};
};

// Saturating signed add/sub: hostile integer literals can carry any i64 value,
// and the affine offset arithmetic must never wrap (a wrapped depth would
// underflow and look finite). Saturated values are caught by the ceiling gate.
[[nodiscard]] std::int64_t sat_add(std::int64_t a, std::int64_t b) {
    if (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) {
        return std::numeric_limits<std::int64_t>::max();
    }
    if (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return a + b;
}
[[nodiscard]] std::int64_t sat_sub(std::int64_t a, std::int64_t b) {
    return sat_add(a, b == std::numeric_limits<std::int64_t>::min()
                         ? std::numeric_limits<std::int64_t>::max()
                         : -b);
}

// Saturating ceiling division of a NON-NEGATIVE span by a POSITIVE step:
// ceil(span / step), pinned at INT64_MAX instead of wrapping the
// `span + step - 1` numerator. A saturated quotient is far above the depth
// ceiling, so the caller rejects it as FN_RECURSION_DEPTH.
[[nodiscard]] std::int64_t sat_div_ceil(std::int64_t span, std::int64_t step) {
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    if (span <= 0 || step <= 0) {
        return 0;
    }
    if (span > kMax - (step - 1)) {
        return kMax;
    }
    return (span + step - 1) / step;
}

// Symbolic evaluator over one body storage. Resolves SSA values through
// let-bound chains (a full value->expr index is built by walking every region).
class TermAnalyzer {
  public:
    TermAnalyzer(const CoreBodyStorage &storage, const CoreRegion &body,
                 std::vector<CoreValueId> params, const CoreProgram &program)
        : storage_(storage), program_(program) {
        for (std::uint32_t slot = 0; slot < params.size(); ++slot) {
            params_.emplace(params[slot].value, slot);
        }
        index_region(body);
    }

    // Root-storage constructor (flow / workflow): no pre-bound params. The
    // caller indexes the storage's handler/node regions via `index_region`.
    TermAnalyzer(const CoreBodyStorage &storage, const CoreProgram &program)
        : storage_(storage), program_(program) {}

    // Index every let binding in one (flow-handler / workflow-node) region and
    // its nested branches/match arms. Safe to call repeatedly for the multiple
    // regions sharing one root storage.
    void index_region(const CoreRegion &region) { index_region_impl(region); }

    [[nodiscard]] Term eval_value(CoreValueId v) const {
        if (const auto slot = params_.find(v.value); slot != params_.end()) {
            return Term{TermKind::Param, 0, v};
        }
        const auto let = lets_.find(v.value);
        if (let == lets_.end()) {
            return {};
        }
        if (let->second >= storage_.exprs.size()) {
            return {};
        }
        return eval_node(storage_.exprs[let->second]);
    }

    [[nodiscard]] Term eval_expr(CoreExprId id) const {
        if (id.value >= storage_.exprs.size()) {
            return {};
        }
        return eval_node(storage_.exprs[id.value]);
    }

    [[nodiscard]] const CoreExpr &expr(CoreExprId id) const { return storage_.exprs[id.value]; }

    // The let expr id bound to `v`, if any (the verifier's SSA rule makes the
    // binding unique).
    [[nodiscard]] std::optional<CoreExprId> let_expr_of(CoreValueId v) const {
        const auto found = lets_.find(v.value);
        if (found == lets_.end()) {
            return std::nullopt;
        }
        return CoreExprId{found->second};
    }

    // Capacity when `v`'s logical type is a bounded nominal container.
    [[nodiscard]] std::optional<std::uint64_t> container_capacity(CoreValueId v) const {
        if (v.value >= storage_.value_types.size()) {
            return std::nullopt;
        }
        return nominal_capacity(storage_.value_types[v.value]);
    }

    [[nodiscard]] bool is_int(CoreValueId v) const {
        if (v.value >= storage_.value_types.size()) {
            return false;
        }
        const CoreValueTypeId ty = storage_.value_types[v.value];
        if (ty.value >= program_.value_types.size()) {
            return false;
        }
        return std::holds_alternative<CoreVtInt>(program_.value_types[ty.value].node);
    }

    [[nodiscard]] std::optional<std::uint32_t> param_slot(CoreValueId v) const {
        const auto found = params_.find(v.value);
        return found == params_.end() ? std::nullopt : std::optional{found->second};
    }

  private:
    void index_region_impl(const CoreRegion &region) {
        for (const CoreStmt &stmt : region.statements) {
            if (const auto *let = std::get_if<CoreLetStmt>(&stmt.node)) {
                lets_.emplace(let->result.value, let->expr.value);
            }
            if (const auto *branch = std::get_if<CoreIfStmt>(&stmt.node)) {
                if (branch->then_region) {
                    index_region(*branch->then_region);
                }
                if (branch->else_region) {
                    index_region(*branch->else_region);
                }
            }
            if (const auto *match = std::get_if<CoreMatchStmt>(&stmt.node)) {
                for (const CoreMatchArm &arm : match->arms) {
                    if (arm.guard_region) {
                        index_region(*arm.guard_region);
                    }
                    if (arm.body) {
                        index_region(*arm.body);
                    }
                }
                if (match->fallback_region) {
                    index_region(*match->fallback_region);
                }
            }
        }
    }

    [[nodiscard]] std::optional<std::uint64_t>
    nominal_capacity(CoreValueTypeId ty) const {
        if (ty.value == CoreValueTypeId::kInvalid || ty.value >= program_.value_types.size()) {
            return std::nullopt;
        }
        const auto *nominal =
            std::get_if<CoreVtNominal>(&program_.value_types[ty.value].node);
        if (nominal == nullptr || !nominal->capacity.has_value() ||
            nominal->base.value >= program_.types.size() ||
            !capacity_allowed(program_.types[nominal->base.value].role)) {
            return std::nullopt;
        }
        return *nominal->capacity;
    }

    [[nodiscard]] static std::optional<std::int64_t> parse_int(const std::string &spelling) {
        if (spelling.empty()) {
            return std::nullopt;
        }
        try {
            std::size_t consumed = 0;
            const std::int64_t parsed = std::stoll(spelling, &consumed, 10);
            return consumed == spelling.size() ? std::optional{parsed} : std::nullopt;
        } catch (...) {
            return std::nullopt;
        }
    }

    [[nodiscard]] Term eval_node(const CoreExpr &expr) const {
        if (const auto *lit = std::get_if<CoreLiteralExpr>(&expr.node)) {
            if (lit->kind == CoreLiteralKind::Integer) {
                if (const auto v = parse_int(lit->spelling)) {
                    return Term{TermKind::Const, *v, {}};
                }
            }
            return {};
        }
        if (const auto *ref = std::get_if<CoreValueRefExpr>(&expr.node)) {
            return eval_value(ref->value);
        }
        if (const auto *unary = std::get_if<CoreUnaryExpr>(&expr.node)) {
            if (unary->op == CoreUnaryOp::Neg) {
                const Term inner = eval_expr(unary->operand);
                if (inner.kind == TermKind::Const) {
                    // Saturating negation: -INT64_MIN is unrepresentable and the
                    // raw unary minus is signed overflow UB.
                    return Term{TermKind::Const, sat_sub(0, inner.c), {}};
                }
            }
            return {};
        }
        if (const auto *collection = std::get_if<CoreCollectionExpr>(&expr.node)) {
            if (collection->op == CoreCollectionOpKind::Len &&
                container_capacity(collection->base).has_value()) {
                // Only a BOUNDED container length is a compile-time inductive
                // fact; an unbounded length stays Unknown.
                return Term{TermKind::Len, 0, collection->base};
            }
            return {};
        }
        if (const auto *binary = std::get_if<CoreBinaryExpr>(&expr.node)) {
            if (binary->op != CoreBinaryOp::Add && binary->op != CoreBinaryOp::Sub) {
                return {};
            }
            const Term lhs = eval_expr(binary->lhs);
            const Term rhs = eval_expr(binary->rhs);
            // Every constant fold is saturated: a hostile i64 literal (Int is
            // i64; the frontend accepts INT64_MAX) must not wrap the offset into
            // a small/negative value that would seal a tiny finite depth. A
            // saturated quantity reaches the ceiling gate below.
            const auto combine = [&](std::int64_t a, std::int64_t b) {
                return binary->op == CoreBinaryOp::Add ? sat_add(a, b) : sat_sub(a, b);
            };
            if (lhs.kind == TermKind::Const && rhs.kind == TermKind::Const) {
                return Term{TermKind::Const, combine(lhs.c, rhs.c), {}};
            }
            if ((lhs.kind == TermKind::Param || lhs.kind == TermKind::Len) &&
                rhs.kind == TermKind::Const) {
                return Term{lhs.kind, combine(lhs.c, rhs.c), lhs.ref};
            }
            if (binary->op == CoreBinaryOp::Add && lhs.kind == TermKind::Const &&
                (rhs.kind == TermKind::Param || rhs.kind == TermKind::Len)) {
                return Term{rhs.kind, sat_add(rhs.c, lhs.c), rhs.ref};
            }
            return {};
        }
        return {};
    }

    const CoreBodyStorage &storage_;
    const CoreProgram &program_;
    std::unordered_map<std::uint32_t, std::uint32_t> params_;
    std::unordered_map<std::uint32_t, std::uint32_t> lets_;
};

// One let binding observed in a region (recursing into if/match): the bound
// result SSA value and its arena expr id.
struct LetBinding {
    CoreValueId result{};
    CoreExprId expr{};
};

void collect_let_bindings_impl(const CoreRegion &region, const CoreBodyStorage &storage,
                               std::vector<LetBinding> &out) {
    for (const CoreStmt &stmt : region.statements) {
        if (const auto *let = std::get_if<CoreLetStmt>(&stmt.node)) {
            if (let->expr.value < storage.exprs.size()) {
                out.push_back({let->result, let->expr});
            }
        }
        if (const auto *branch = std::get_if<CoreIfStmt>(&stmt.node)) {
            if (branch->then_region) {
                collect_let_bindings_impl(*branch->then_region, storage, out);
            }
            if (branch->else_region) {
                collect_let_bindings_impl(*branch->else_region, storage, out);
            }
        }
        if (const auto *match = std::get_if<CoreMatchStmt>(&stmt.node)) {
            for (const CoreMatchArm &arm : match->arms) {
                if (arm.guard_region) {
                    collect_let_bindings_impl(*arm.guard_region, storage, out);
                }
                if (arm.body) {
                    collect_let_bindings_impl(*arm.body, storage, out);
                }
            }
            if (match->fallback_region) {
                collect_let_bindings_impl(*match->fallback_region, storage, out);
            }
        }
    }
}

// The let bindings of one fn body (rooted at its single region) and of every
// region sharing a flow/workflow root storage.
[[nodiscard]] std::vector<LetBinding>
storage_let_bindings(const CoreProgram &program, const CoreBodyStorage &storage) {
    std::vector<LetBinding> out;
    for (const CoreFnDecl &fn : program.fns) {
        if (&fn.storage == &storage) {
            collect_let_bindings_impl(fn.body, storage, out);
        }
    }
    for (const CoreFlowDecl &flow : program.flows) {
        if (&flow.storage == &storage) {
            for (const CoreFlowState &state : flow.states) {
                collect_let_bindings_impl(state.body, storage, out);
            }
        }
    }
    for (const CoreWorkflowDecl &wf : program.workflows) {
        if (&wf.storage == &storage) {
            for (const CoreWorkflowNode &node : wf.nodes) {
                if (node.input_region) {
                    collect_let_bindings_impl(*node.input_region, storage, out);
                }
            }
            if (wf.return_region) {
                collect_let_bindings_impl(*wf.return_region, storage, out);
            }
        }
    }
    return out;
}

// The value-bearing return operands of one fn body (any nesting depth).
void collect_return_values_impl(const CoreRegion &region, const CoreBodyStorage &storage,
                                std::vector<CoreValueId> &out) {
    for (const CoreStmt &stmt : region.statements) {
        if (const auto *ret = std::get_if<CoreReturnStmt>(&stmt.node);
            ret != nullptr && ret->has_value && ret->value.value < storage.value_count) {
            out.push_back(ret->value);
        }
        if (const auto *branch = std::get_if<CoreIfStmt>(&stmt.node)) {
            if (branch->then_region) {
                collect_return_values_impl(*branch->then_region, storage, out);
            }
            if (branch->else_region) {
                collect_return_values_impl(*branch->else_region, storage, out);
            }
        }
        if (const auto *match = std::get_if<CoreMatchStmt>(&stmt.node)) {
            for (const CoreMatchArm &arm : match->arms) {
                if (arm.guard_region) {
                    collect_return_values_impl(*arm.guard_region, storage, out);
                }
                if (arm.body) {
                    collect_return_values_impl(*arm.body, storage, out);
                }
            }
            if (match->fallback_region) {
                collect_return_values_impl(*match->fallback_region, storage, out);
            }
        }
    }
}

[[nodiscard]] std::optional<std::uint32_t>
resolve_instance_body(const CoreProgram &program, CoreInstanceId instance) {
    if (instance.value == CoreInstanceId::kInvalid ||
        instance.value >= program.instances.size()) {
        return std::nullopt;
    }
    const auto *payload =
        std::get_if<CoreFnInstance>(&program.instances[instance.value].payload);
    if (payload == nullptr || payload->body.value == CoreFnId::kInvalid ||
        payload->body.value >= program.fns.size()) {
        return std::nullopt;
    }
    return payload->body.value;
}

// The mutable fixed-point engine behind ClosurePointsTo.
struct ClosureFlowEngine {
    const CoreProgram &program;
    // storage identity -> dense per-SSA-slot closure sets.
    std::unordered_map<const CoreBodyStorage *, std::vector<std::vector<std::uint32_t>>> sets;
    // storage identity -> its let bindings (collected once, reused every pass).
    std::unordered_map<const CoreBodyStorage *, std::vector<LetBinding>> bindings;
    std::vector<std::uint32_t> dead; // returned for an out-of-range/foreign slot
    std::vector<std::vector<CoreValueId>> return_values; // per fn

    explicit ClosureFlowEngine(const CoreProgram &p) : program(p) {
        const auto allocate = [&](const CoreBodyStorage &storage) {
            sets.emplace(&storage,
                         std::vector<std::vector<std::uint32_t>>(storage.value_count));
            bindings.emplace(&storage, storage_let_bindings(program, storage));
        };
        for (const CoreFlowDecl &flow : program.flows) {
            allocate(flow.storage);
        }
        for (const CoreWorkflowDecl &wf : program.workflows) {
            allocate(wf.storage);
        }
        for (const CoreFnDecl &fn : program.fns) {
            allocate(fn.storage);
        }
        return_values.reserve(program.fns.size());
        for (const CoreFnDecl &fn : program.fns) {
            std::vector<CoreValueId> rets;
            collect_return_values_impl(fn.body, fn.storage, rets);
            return_values.push_back(std::move(rets));
        }
    }

    [[nodiscard]] std::vector<std::uint32_t> &slot(const CoreBodyStorage &storage,
                                                    CoreValueId v) {
        auto found = sets.find(&storage);
        if (found == sets.end() || v.value >= found->second.size()) {
            return dead;
        }
        return found->second[v.value];
    }

    bool add(std::vector<std::uint32_t> &set, std::uint32_t fn) {
        if (std::find(set.begin(), set.end(), fn) != set.end()) {
            return false;
        }
        set.push_back(fn);
        return true;
    }

    bool merge_into(std::vector<std::uint32_t> &dst,
                    const std::vector<std::uint32_t> &src) {
        bool changed = false;
        for (const std::uint32_t fn : src) {
            changed = add(dst, fn) || changed;
        }
        return changed;
    }

    // The closures a fn currently RETURNS (recomputed per pass).
    [[nodiscard]] std::vector<std::uint32_t> return_set(std::uint32_t fn) {
        std::vector<std::uint32_t> out;
        if (fn >= program.fns.size()) {
            return out;
        }
        const CoreFnDecl &decl = program.fns[fn];
        for (const CoreValueId v : return_values[fn]) {
            merge_into(out, slot(decl.storage, v));
        }
        return out;
    }

    // Propagate one call's argument closure sets into a target fn's params.
    bool propagate_args(const CoreBodyStorage &caller, const std::vector<CoreValueId> &args,
                        std::uint32_t target_fn) {
        bool changed = false;
        const CoreFnDecl &target = program.fns[target_fn];
        const std::size_t n = std::min(args.size(), target.params.size());
        for (std::size_t i = 0; i < n; ++i) {
            changed = merge_into(slot(target.storage, target.params[i]),
                                 slot(caller, args[i])) || changed;
        }
        return changed;
    }

    bool run_pass() {
        bool changed = false;
        const auto propagate_storage = [&](const CoreBodyStorage &storage) {
            for (const LetBinding &b : bindings.at(&storage)) {
                const CoreExpr &expr = storage.exprs[b.expr.value];
                if (const auto *closure = std::get_if<CoreClosureExpr>(&expr.node)) {
                    if (closure->fn.value < program.fns.size()) {
                        changed = add(slot(storage, b.result), closure->fn.value) || changed;
                        // The construction's env operands flow into the lifted
                        // fn's pre-bound environment slots.
                        const CoreFnDecl &target = program.fns[closure->fn.value];
                        const std::size_t n =
                            std::min(closure->env.size(), target.env_bindings.size());
                        for (std::size_t i = 0; i < n; ++i) {
                            changed = merge_into(slot(target.storage, target.env_bindings[i]),
                                                 slot(storage, closure->env[i])) || changed;
                        }
                    }
                    continue;
                }
                if (const auto *alias = std::get_if<CoreValueRefExpr>(&expr.node)) {
                    changed = merge_into(slot(storage, b.result),
                                         slot(storage, alias->value)) || changed;
                    continue;
                }
                if (const auto *call = std::get_if<CoreCallExpr>(&expr.node)) {
                    const auto target = resolve_instance_body(program, call->callee);
                    if (!target.has_value()) {
                        continue;
                    }
                    changed = propagate_args(storage, call->args, *target) || changed;
                    changed = merge_into(slot(storage, b.result), return_set(*target)) || changed;
                    continue;
                }
                if (const auto *icall = std::get_if<CoreCallClosureExpr>(&expr.node)) {
                    // Dispatch to every closure the callee slot currently holds.
                    const std::vector<std::uint32_t> targets =
                        slot(storage, icall->callee);
                    for (const std::uint32_t target : targets) {
                        changed = propagate_args(storage, icall->args, target) || changed;
                        changed =
                            merge_into(slot(storage, b.result), return_set(target)) || changed;
                    }
                }
            }
        };
        for (const CoreFlowDecl &flow : program.flows) {
            propagate_storage(flow.storage);
        }
        for (const CoreWorkflowDecl &wf : program.workflows) {
            propagate_storage(wf.storage);
        }
        for (const CoreFnDecl &fn : program.fns) {
            propagate_storage(fn.storage);
        }
        return changed;
    }

    void run() {
        // Sets only grow; each slot's set is bounded by the number of closure
        // constructions (== number of fns), so this reaches a fixed point.
        while (run_pass()) {
        }
        for (auto &[storage, per_slot] : sets) {
            static_cast<void>(storage);
            for (auto &set : per_slot) {
                std::sort(set.begin(), set.end());
                set.erase(std::unique(set.begin(), set.end()), set.end());
            }
        }
    }
};

} // namespace

ClosurePointsTo ClosurePointsTo::analyze(const CoreProgram &program) {
    ClosureFlowEngine engine(program);
    engine.run();
    ClosurePointsTo out;
    out.sets_ = std::move(engine.sets);
    return out;
}

const std::vector<std::uint32_t> &ClosurePointsTo::slot_set(
    const CoreBodyStorage &storage, CoreValueId slot) const {
    static const std::vector<std::uint32_t> kEmpty;
    const auto found = sets_.find(&storage);
    if (found == sets_.end() || slot.value >= found->second.size()) {
        return kEmpty;
    }
    return found->second[slot.value];
}

std::vector<std::uint32_t> ClosurePointsTo::targets_of(
    const CoreBodyStorage &caller_storage, const CoreCallClosureExpr &call) const {
    return slot_set(caller_storage, call.callee);
}

namespace {

// Tarjan SCC with an EXPLICIT work stack (no recursion — the graph is small but
// the verifier avoids native recursion on hostile input elsewhere too).
class Tarjan {
  public:
    explicit Tarjan(std::vector<std::vector<std::uint32_t>> graph)
        : graph_(std::move(graph)), index_(graph_.size(), kUnseen), low_(graph_.size(), 0) {}

    [[nodiscard]] std::vector<std::vector<std::uint32_t>> run() {
        for (std::uint32_t root = 0; root < graph_.size(); ++root) {
            if (index_[root] == kUnseen) {
                connect(root);
            }
        }
        // SCCs stay in their genuine POP order: Tarjan emits sink components
        // before their predecessors, which is a deterministic reverse
        // topological order of the condensation (the DFS root order is fixed by
        // the caller). Members WITHIN a component are sorted in `connect`, but
        // the components themselves must NOT be re-sorted by front() — doing so
        // destroys the emission order and breaks every consumer that walks the
        // condensation as a DAG (the documented contract in
        // FnRecursionAnalysis::components). Consumers that need an order
        // independent of this one derive their own topological walk.
        return std::move(components_);
    }

  private:
    void connect(std::uint32_t root) {
        struct Frame {
            std::uint32_t v;
            std::size_t next{0};
        };
        std::vector<Frame> stack;
        const auto push = [&](std::uint32_t v) {
            index_[v] = clock_;
            low_[v] = clock_;
            ++clock_;
            on_stack_.push_back(v);
            stack.push_back(Frame{v, 0});
        };
        push(root);
        while (!stack.empty()) {
            Frame &frame = stack.back();
            if (frame.next < graph_[frame.v].size()) {
                const std::uint32_t w = graph_[frame.v][frame.next++];
                if (index_[w] == kUnseen) {
                    push(w);
                } else if (std::find(on_stack_.begin(), on_stack_.end(), w) != on_stack_.end()) {
                    low_[frame.v] = std::min(low_[frame.v], index_[w]);
                }
            } else {
                const std::uint32_t v = frame.v;
                if (low_[v] == index_[v]) {
                    std::vector<std::uint32_t> component;
                    while (true) {
                        const std::uint32_t w = on_stack_.back();
                        on_stack_.pop_back();
                        component.push_back(w);
                        if (w == v) {
                            break;
                        }
                    }
                    std::sort(component.begin(), component.end());
                    components_.push_back(std::move(component));
                }
                stack.pop_back();
                if (!stack.empty()) {
                    low_[stack.back().v] = std::min(low_[stack.back().v], low_[v]);
                }
            }
        }
    }

    static constexpr std::int64_t kUnseen = -1;
    std::vector<std::vector<std::uint32_t>> graph_;
    std::int64_t clock_{0};
    std::vector<std::int64_t> index_;
    std::vector<std::int64_t> low_;
    std::vector<std::uint32_t> on_stack_;
    std::vector<std::vector<std::uint32_t>> components_;
};

// Whether every path through a region leaves the fn (a return statement at
// top level, or an if whose BOTH branches leave). Mirrors the strict subset of
// the verifier's RegionExit a rank base-guard needs.
bool region_diverges(const CoreRegion &region) {
    for (const CoreStmt &stmt : region.statements) {
        if (std::holds_alternative<CoreReturnStmt>(stmt.node)) {
            return true;
        }
        if (const auto *branch = std::get_if<CoreIfStmt>(&stmt.node)) {
            if (branch->then_region && branch->else_region &&
                region_diverges(*branch->then_region) && region_diverges(*branch->else_region)) {
                return true;
            }
        }
    }
    return false;
}

class RecursionLattice {
  public:
    explicit RecursionLattice(const CoreProgram &program) : program_(program) {}

    FnRecursionAnalysis run() {
        collect_edges();
        build_analyzers();

        FnRecursionAnalysis out;
        out.fn_depth_bound.assign(program_.fns.size(), -1);

        const std::vector<std::vector<std::uint32_t>> components = Tarjan(graph_).run();
        out.components = components;
        out.component_of.assign(program_.fns.size(), 0);
        for (std::uint32_t ci = 0; ci < components.size(); ++ci) {
            for (const std::uint32_t fn : components[ci]) {
                out.component_of[fn] = ci;
            }
        }
        out.condensation_edges.assign(components.size(), {});
        for (std::uint32_t fi = 0; fi < graph_.size(); ++fi) {
            for (const std::uint32_t to : graph_[fi]) {
                const std::uint32_t from_c = out.component_of[fi];
                const std::uint32_t to_c = out.component_of[to];
                if (from_c != to_c) {
                    out.condensation_edges[from_c].push_back(to_c);
                }
            }
        }
        for (auto &outs : out.condensation_edges) {
            std::sort(outs.begin(), outs.end());
            outs.erase(std::unique(outs.begin(), outs.end()), outs.end());
        }

        for (const std::vector<std::uint32_t> &component : components) {
            if (is_nontrivial(component)) {
                analyze_scc(component, out);
            }
        }
        return out;
    }

  private:
    void collect_edges() {
        graph_.assign(program_.fns.size(), {});
        // FB-3b: compute the program's closure points-to set ONCE; every
        // CoreCallClosureExpr adds one INDIRECT invocation edge per fn its
        // callee slot can hold (the flow-sensitive call_indirect target set),
        // so a cycle that closes through a closure value is visible to the
        // SCC/rank lattice exactly like a direct cycle.
        const ClosurePointsTo points_to = ClosurePointsTo::analyze(program_);
        const auto scan = [&](std::uint32_t caller_fn, const CoreBodyStorage &storage) {
            for (std::uint32_t ei = 0; ei < storage.exprs.size(); ++ei) {
                const CoreExpr &expr = storage.exprs[ei];
                if (const auto *call = std::get_if<CoreCallExpr>(&expr.node)) {
                    const auto callee = resolve_fn(call->callee);
                    if (!callee.has_value()) {
                        continue;
                    }
                    edges_.push_back(CallEdge{caller_fn, *callee, &call->args, &storage, ei,
                                              /*indirect=*/false, expr.source_range});
                    if (caller_fn != CoreFnId::kInvalid) {
                        graph_[caller_fn].push_back(*callee);
                    }
                    continue;
                }
                if (const auto *indirect_call =
                        std::get_if<CoreCallClosureExpr>(&expr.node)) {
                    for (const std::uint32_t target :
                         points_to.targets_of(storage, *indirect_call)) {
                        edges_.push_back(
                            CallEdge{caller_fn, target, &indirect_call->args, &storage, ei,
                                     /*indirect=*/true, expr.source_range});
                        if (caller_fn != CoreFnId::kInvalid) {
                            graph_[caller_fn].push_back(target);
                        }
                    }
                }
            }
        };
        for (const CoreFlowDecl &flow : program_.flows) {
            scan(CoreFnId::kInvalid, flow.storage);
        }
        for (const CoreWorkflowDecl &wf : program_.workflows) {
            scan(CoreFnId::kInvalid, wf.storage);
        }
        for (std::uint32_t fi = 0; fi < program_.fns.size(); ++fi) {
            scan(fi, program_.fns[fi].storage);
        }
    }

    [[nodiscard]] std::optional<std::uint32_t> resolve_fn(CoreInstanceId instance) const {
        if (instance.value == CoreInstanceId::kInvalid ||
            instance.value >= program_.instances.size()) {
            return std::nullopt;
        }
        const auto *payload =
            std::get_if<CoreFnInstance>(&program_.instances[instance.value].payload);
        if (payload == nullptr || payload->body.value == CoreFnId::kInvalid ||
            payload->body.value >= program_.fns.size()) {
            return std::nullopt;
        }
        return payload->body.value;
    }

    void build_analyzers() {
        fn_analyzers_.clear();
        fn_analyzers_.reserve(program_.fns.size());
        for (const CoreFnDecl &fn : program_.fns) {
            fn_analyzers_.push_back(std::make_unique<TermAnalyzer>(
                fn.storage, fn.body, fn.params, program_));
        }
    }

    [[nodiscard]] const TermAnalyzer *edge_caller_analyzer(const CallEdge &edge) const {
        if (edge.caller_fn != CoreFnId::kInvalid) {
            return fn_analyzers_[edge.caller_fn].get();
        }
        const void *key = edge.storage;
        if (const auto found = root_analyzers_.find(key); found != root_analyzers_.end()) {
            return found->second.get();
        }
        // Index every handler/node region sharing this flow/workflow storage so
        // an entry call's let-bound rank argument resolves.
        auto analyzer = std::make_unique<TermAnalyzer>(*edge.storage, program_);
        for (const CoreFlowDecl &flow : program_.flows) {
            if (&flow.storage != edge.storage) {
                continue;
            }
            for (const CoreFlowState &state : flow.states) {
                analyzer->index_region(state.body);
            }
        }
        for (const CoreWorkflowDecl &wf : program_.workflows) {
            if (&wf.storage != edge.storage) {
                continue;
            }
            for (const CoreWorkflowNode &node : wf.nodes) {
                if (node.input_region) {
                    analyzer->index_region(*node.input_region);
                }
            }
            if (wf.return_region) {
                analyzer->index_region(*wf.return_region);
            }
        }
        const auto placed = root_analyzers_.emplace(key, std::move(analyzer));
        return placed.first->second.get();
    }

    [[nodiscard]] bool is_nontrivial(const std::vector<std::uint32_t> &component) const {
        if (component.size() > 1) {
            return true;
        }
        const std::uint32_t only = component.front();
        return std::find(graph_[only].begin(), graph_[only].end(), only) != graph_[only].end();
    }

    [[nodiscard]] std::vector<std::uint32_t>
    edges_into(const std::unordered_set<std::uint32_t> &members, bool internal) const {
        std::vector<std::uint32_t> out;
        for (std::uint32_t i = 0; i < edges_.size(); ++i) {
            if (!members.contains(edges_[i].callee_fn)) {
                continue;
            }
            const bool caller_inside =
                edges_[i].caller_fn != CoreFnId::kInvalid &&
                members.contains(edges_[i].caller_fn);
            if (internal == caller_inside) {
                out.push_back(i);
            }
        }
        return out;
    }

    // R1 rank-progression match: callee slot binds caller-rank +/- c.
    struct Progression {
        bool ok{false};
        std::int64_t step{0};
    };
    [[nodiscard]] Progression progresses(const CallEdge &edge, std::uint32_t caller_slot,
                                         std::uint32_t callee_slot, bool ascending) const {
        if (callee_slot >= edge.args->size()) {
            return {};
        }
        const TermAnalyzer &ta = *fn_analyzers_[edge.caller_fn];
        const CoreValueId rank = program_.fns[edge.caller_fn].params[caller_slot];
        const Term t = ta.eval_value((*edge.args)[callee_slot]);
        if (t.kind != TermKind::Param || t.ref != rank) {
            return {};
        }
        if (ascending ? t.c <= 0 : t.c >= 0) {
            return {};
        }
        // Saturating negation of the descending offset (which can be INT64_MIN).
        return Progression{true, ascending ? t.c : sat_sub(0, t.c)};
    }

    // R2 divergent guard over a member's rank, at top level of its body.
    struct Guard {
        bool found{false};
        Term rank_side; // Param(rank, a)
        Term bound;     // Const / Len / invariant Param (+ b)
        bool ascending{true};
        bool strict{false}; // Gt / Lt rather than Ge / Le
        // The guard `if` statement's index in the body's TOP-LEVEL statement
        // list. The dominance rule (R2b) only permits recursive edges on the
        // post-guard continue path; this anchors "before / inside / after".
        std::uint32_t body_index{0};
    };
    [[nodiscard]] Guard find_guard(std::uint32_t fn, std::uint32_t rank_slot,
                                   const std::unordered_set<std::uint32_t> &invariant_slots) const {
        const CoreFnDecl &decl = program_.fns[fn];
        const TermAnalyzer &ta = *fn_analyzers_[fn];
        const CoreValueId rank = decl.params[rank_slot];
        Guard guard;
        for (std::uint32_t si = 0; si < decl.body.statements.size(); ++si) {
            const CoreStmt &stmt = decl.body.statements[si];
            const auto *branch = std::get_if<CoreIfStmt>(&stmt.node);
            if (branch == nullptr) {
                continue;
            }
            // The base case is the THEN branch and it must leave the fn on
            // every path. Restricting to the THEN side is what keeps the guard
            // direction meaningful: `if (r >= B) return` with ascending r stops
            // when r reaches B. An inverted `else return` shape would recurse
            // AWAY from the comparison threshold and is rejected here rather
            // than sealed as a (wrong) finite bound. This is exactly the one
            // normal form the accepted stdlib helpers use.
            if (!branch->then_region || !region_diverges(*branch->then_region)) {
                continue;
            }
            const auto cond_id = ta.let_expr_of(branch->condition);
            if (!cond_id.has_value()) {
                continue;
            }
            const auto *binary =
                std::get_if<CoreBinaryExpr>(&ta.expr(*cond_id).node);
            if (binary == nullptr) {
                continue;
            }
            const bool asc_op = binary->op == CoreBinaryOp::Ge || binary->op == CoreBinaryOp::Gt;
            const bool desc_op = binary->op == CoreBinaryOp::Le || binary->op == CoreBinaryOp::Lt;
            if (!asc_op && !desc_op) {
                continue;
            }
            const Term lhs = ta.eval_expr(binary->lhs);
            const Term rhs = ta.eval_expr(binary->rhs);
            if (lhs.kind != TermKind::Param || lhs.ref != rank) {
                continue;
            }
            const bool bound_ok = [&] {
                if (rhs.kind == TermKind::Const) {
                    return true;
                }
                if (rhs.kind == TermKind::Len) {
                    const Term base = ta.eval_value(rhs.ref);
                    if (base.kind != TermKind::Param) {
                        return false;
                    }
                    const auto slot = ta.param_slot(base.ref);
                    return slot.has_value() && invariant_slots.contains(*slot);
                }
                if (rhs.kind == TermKind::Param) {
                    const auto slot = ta.param_slot(rhs.ref);
                    return slot.has_value() && *slot != rank_slot &&
                           invariant_slots.contains(*slot);
                }
                return false;
            }();
            if (!bound_ok) {
                continue;
            }
            guard.found = true;
            guard.rank_side = lhs;
            guard.bound = rhs;
            guard.ascending = asc_op;
            guard.strict = binary->op == CoreBinaryOp::Gt || binary->op == CoreBinaryOp::Lt;
            guard.body_index = si;
            break;
        }
        return guard;
    }

    // R2b (dominance / placement): a divergent base guard seals the recursion
    // ONLY when every internal recursive edge is reachable on the guard's
    // CONTINUE path (the path taken when the guard condition is false). The
    // accepted normal form has a divergent THEN (the stop path) and the
    // recursion after the `if` or in its ELSE. Rank progression plus a
    // structurally-correct guard alone are insufficient: a self-call placed
    // UNCONDITIONALLY before the guard, or inside the divergent THEN stop
    // branch, satisfies both and still recurses forever at runtime.
    [[nodiscard]] bool
    recursive_edge_guard_dominated(std::uint32_t fn, const Guard &guard,
                                   const std::vector<std::uint32_t> &internal,
                                   std::optional<std::uint32_t> &bad_edge) const {
        bad_edge.reset();
        const CoreFnDecl &decl = program_.fns[fn];
        const auto *guard_if =
            std::get_if<CoreIfStmt>(&decl.body.statements[guard.body_index].node);
        if (guard_if == nullptr) {
            // The guard index was derived from an if; this cannot happen. Fail
            // closed rather than silently accept placement.
            bad_edge = internal.empty() ? std::nullopt
                                        : std::optional{internal.front()};
            return internal.empty();
        }

        // A recursive invocation expr id: a direct CoreCallExpr or an indirect
        // CoreCallClosureExpr (FB-3b). The rank lattice treats both as edges.
        const auto is_call_expr = [&](CoreExprId id) {
            if (id.value >= decl.storage.exprs.size()) {
                return false;
            }
            const CoreExprNode &node = decl.storage.exprs[id.value].node;
            return std::holds_alternative<CoreCallExpr>(node) ||
                   std::holds_alternative<CoreCallClosureExpr>(node);
        };

        // Expr ids of invocation exprs at any nesting depth inside ONE region.
        const auto calls_in = [&](const CoreRegion &region,
                                  std::unordered_set<std::uint32_t> &out) {
            const auto walk = [&](auto &&self, const CoreRegion &r) -> void {
                for (const CoreStmt &stmt : r.statements) {
                    if (const auto *let = std::get_if<CoreLetStmt>(&stmt.node)) {
                        if (is_call_expr(let->expr)) {
                            out.insert(let->expr.value);
                        }
                    }
                    if (const auto *branch = std::get_if<CoreIfStmt>(&stmt.node)) {
                        if (branch->then_region) {
                            self(self, *branch->then_region);
                        }
                        if (branch->else_region) {
                            self(self, *branch->else_region);
                        }
                    }
                    if (const auto *match = std::get_if<CoreMatchStmt>(&stmt.node)) {
                        for (const CoreMatchArm &arm : match->arms) {
                            if (arm.guard_region) {
                                self(self, *arm.guard_region);
                            }
                            if (arm.body) {
                                self(self, *arm.body);
                            }
                        }
                        if (match->fallback_region) {
                            self(self, *match->fallback_region);
                        }
                    }
                }
            };
            walk(walk, region);
        };
        std::unordered_set<std::uint32_t> stop_calls;
        if (guard_if->then_region) {
            calls_in(*guard_if->then_region, stop_calls);
        }

        // The top-level statement index that lexically CONTAINS each call expr
        // in the fn body (recursing into if/match regions).
        std::unordered_map<std::uint32_t, std::uint32_t> top_index;
        const auto index_region_calls = [&](auto &&self, const CoreRegion &region,
                                           std::uint32_t top) -> void {
            for (const CoreStmt &stmt : region.statements) {
                if (const auto *let = std::get_if<CoreLetStmt>(&stmt.node)) {
                    if (is_call_expr(let->expr)) {
                        top_index.emplace(let->expr.value, top);
                    }
                }
                if (const auto *branch = std::get_if<CoreIfStmt>(&stmt.node)) {
                    if (branch->then_region) {
                        self(self, *branch->then_region, top);
                    }
                    if (branch->else_region) {
                        self(self, *branch->else_region, top);
                    }
                }
                if (const auto *match = std::get_if<CoreMatchStmt>(&stmt.node)) {
                    for (const CoreMatchArm &arm : match->arms) {
                        if (arm.guard_region) {
                            self(self, *arm.guard_region, top);
                        }
                        if (arm.body) {
                            self(self, *arm.body, top);
                        }
                    }
                    if (match->fallback_region) {
                        self(self, *match->fallback_region, top);
                    }
                }
            }
        };
        for (std::uint32_t si = 0; si < decl.body.statements.size(); ++si) {
            const CoreStmt &stmt = decl.body.statements[si];
            if (const auto *let = std::get_if<CoreLetStmt>(&stmt.node)) {
                if (is_call_expr(let->expr)) {
                    top_index.emplace(let->expr.value, si);
                }
            }
            if (const auto *branch = std::get_if<CoreIfStmt>(&stmt.node)) {
                if (branch->then_region) {
                    index_region_calls(index_region_calls, *branch->then_region, si);
                }
                if (branch->else_region) {
                    index_region_calls(index_region_calls, *branch->else_region, si);
                }
            }
            if (const auto *match = std::get_if<CoreMatchStmt>(&stmt.node)) {
                for (const CoreMatchArm &arm : match->arms) {
                    if (arm.guard_region) {
                        index_region_calls(index_region_calls, *arm.guard_region, si);
                    }
                    if (arm.body) {
                        index_region_calls(index_region_calls, *arm.body, si);
                    }
                }
                if (match->fallback_region) {
                    index_region_calls(index_region_calls, *match->fallback_region, si);
                }
            }
        }

        for (const std::uint32_t ei : internal) {
            const CallEdge &edge = edges_[ei];
            if (edge.caller_fn != fn) {
                continue;
            }
            // Inside the divergent stop (THEN) region: the call runs exactly
            // when the rank reached its bound.
            if (stop_calls.contains(edge.expr)) {
                bad_edge = ei;
                return false;
            }
            const auto placed = top_index.find(edge.expr);
            if (placed == top_index.end()) {
                bad_edge = ei; // unplaceable edge: fail closed
                return false;
            }
            // In the guard's ELSE (same top index, not in THEN) or after it:
            // continue path, legal. Before it: unconditional first-activation
            // recursion the guard never gates.
            if (placed->second < guard.body_index) {
                bad_edge = ei;
                return false;
            }
        }
        return true;
    }

    // Static finite interval of an SSA value at an entry call site (evaluated
    // in the caller storage). Unknown for anything but literals and bounded-
    // container length affine combinations.
    [[nodiscard]] Interval entry_interval(const TermAnalyzer &ta, CoreValueId v) const {
        const Term t = ta.eval_value(v);
        if (t.kind == TermKind::Const) {
            return Interval{t.c, t.c, true};
        }
        if (t.kind == TermKind::Len) {
            const auto capacity = ta.container_capacity(t.ref);
            if (!capacity.has_value() ||
                *capacity > static_cast<std::uint64_t>(
                                std::numeric_limits<std::int64_t>::max())) {
                return {};
            }
            const std::int64_t cap = static_cast<std::int64_t>(*capacity);
            // The upper endpoint cap + offset is saturated so a hostile affine
            // offset cannot wrap it below the lower endpoint.
            return Interval{t.c, sat_add(cap, t.c), true};
        }
        return {};
    }

    void analyze_scc(const std::vector<std::uint32_t> &component, FnRecursionAnalysis &out) {
        std::unordered_set<std::uint32_t> members(component.begin(), component.end());
        const std::vector<std::uint32_t> internal = edges_into(members, /*internal=*/true);
        const std::vector<std::uint32_t> entries = edges_into(members, /*internal=*/false);

        // R0: each member has at least one Int parameter candidate.
        std::vector<std::vector<std::uint32_t>> candidates(program_.fns.size());
        for (const std::uint32_t fn : component) {
            const TermAnalyzer &ta = *fn_analyzers_[fn];
            for (std::uint32_t slot = 0; slot < program_.fns[fn].params.size(); ++slot) {
                if (ta.is_int(program_.fns[fn].params[slot])) {
                    candidates[fn].push_back(slot);
                }
            }
            if (candidates[fn].empty()) {
                report(FnRecursionIssueKind::RankNotInteger, fn, fn, std::nullopt,
                       program_.fns[fn].source_range, out);
                return;
            }
        }

        // R1: enumerate rank-slot assignments under both uniform directions.
        std::vector<std::uint32_t> chosen(program_.fns.size(),
                                          std::numeric_limits<std::uint32_t>::max());
        bool ascending = true;
        std::int64_t min_step = 0;
        if (!try_assignments(component, internal, candidates, chosen, ascending, min_step)) {
            const CallEdge &edge = edges_[internal.front()];
            report(FnRecursionIssueKind::NoRankProgression, edge.caller_fn, edge.callee_fn,
                   std::nullopt, edge.range, out);
            return;
        }

        // Invariant (non-rank) slots: threaded verbatim (exact param SSA value,
        // zero offset) on EVERY internal edge leaving the member.
        std::vector<std::unordered_set<std::uint32_t>> invariant(program_.fns.size());
        for (const std::uint32_t fn : component) {
            for (std::uint32_t slot = 0; slot < program_.fns[fn].params.size(); ++slot) {
                if (slot == chosen[fn] || !is_invariant_slot(fn, slot, internal)) {
                    continue;
                }
                invariant[fn].insert(slot);
            }
        }

        // R2: every member has a divergent rank guard in the accepted direction.
        std::vector<Guard> guards(program_.fns.size());
        for (const std::uint32_t fn : component) {
            Guard guard = find_guard(fn, chosen[fn], invariant[fn]);
            if (!guard.found || guard.ascending != ascending) {
                report(FnRecursionIssueKind::NoBaseGuard, fn, fn, std::nullopt,
                       program_.fns[fn].source_range, out);
                return;
            }
            guards[fn] = guard;
        }

        // R2b: every internal recursive edge must be dominated by the member's
        // accepted guard (reachable only on the guard's continue path). A call
        // before the guard or inside its divergent stop branch escapes the
        // lattice even though the guard structurally exists.
        for (const std::uint32_t fn : component) {
            std::optional<std::uint32_t> bad_edge;
            if (!recursive_edge_guard_dominated(fn, guards[fn], internal, bad_edge)) {
                if (bad_edge.has_value()) {
                    const CallEdge &edge = edges_[*bad_edge];
                    report(FnRecursionIssueKind::EdgeNotDominatedByGuard,
                           edge.caller_fn, edge.callee_fn, std::nullopt, edge.range, out);
                } else {
                    report(FnRecursionIssueKind::EdgeNotDominatedByGuard, fn, fn,
                           std::nullopt, program_.fns[fn].source_range, out);
                }
                return;
            }
        }

        // No-entry groups are unreachable (codegen only emits reachable fns);
        // seal them at depth 1 — they can never activate.
        if (entries.empty()) {
            FnRecursionScc sealed;
            sealed.members = component;
            sealed.depth_bound = 1;
            for (const std::uint32_t fn : component) {
                out.fn_depth_bound[fn] = 1;
            }
            out.sccs.push_back(std::move(sealed));
            return;
        }

        // R3a: every entry edge binds the rank slot to a finite interval.
        std::optional<std::int64_t> entry_rank;
        for (const std::uint32_t ei : entries) {
            const CallEdge &edge = edges_[ei];
            const TermAnalyzer *caller = edge_caller_analyzer(edge);
            const std::uint32_t slot = chosen[edge.callee_fn];
            if (caller == nullptr || slot >= edge.args->size()) {
                report(FnRecursionIssueKind::EntryRankNotStatic, edge.caller_fn, edge.callee_fn,
                       slot, edge.range, out);
                return;
            }
            const Interval iv = entry_interval(*caller, (*edge.args)[slot]);
            if (!iv.finite) {
                report(FnRecursionIssueKind::EntryRankNotStatic, edge.caller_fn, edge.callee_fn,
                       slot, edge.range, out);
                return;
            }
            entry_rank = entry_rank.has_value()
                             ? (ascending ? std::min(*entry_rank, iv.lo)
                                          : std::max(*entry_rank, iv.hi))
                             : (ascending ? std::optional{iv.lo} : std::optional{iv.hi});
        }

        // R3b: each guard's static stop value. Const / bounded-container length
        // resolve directly; an invariant scalar parameter resolves to the max
        // finite value its entry edges bind it to.
        std::optional<std::int64_t> stop_value;
        for (const std::uint32_t fn : component) {
            const Guard &guard = guards[fn];
            const TermAnalyzer &ta = *fn_analyzers_[fn];
            // `base` is the term's DYNAMIC component; for a Const the whole
            // value already lives in guard.bound.c (the constant offset).
            std::int64_t base = 0;
            if (guard.bound.kind == TermKind::Len) {
                const Term b = ta.eval_value(guard.bound.ref);
                const auto capacity = b.kind == TermKind::Param
                                          ? ta.container_capacity(b.ref)
                                          : std::nullopt;
                if (!capacity.has_value()) {
                    report(FnRecursionIssueKind::BoundNotStatic, fn, fn, std::nullopt,
                           program_.fns[fn].source_range, out);
                    return;
                }
                base = static_cast<std::int64_t>(*capacity);
            } else if (guard.bound.kind == TermKind::Param) {
                const auto slot = ta.param_slot(guard.bound.ref);
                if (!slot.has_value() || !invariant[fn].contains(*slot)) {
                    report(FnRecursionIssueKind::BoundNotStatic, fn, fn, *slot,
                           program_.fns[fn].source_range, out);
                    return;
                }
                std::optional<std::int64_t> ceiling;
                for (const std::uint32_t ei : entries) {
                    const CallEdge &edge = edges_[ei];
                    if (edge.callee_fn != fn) {
                        continue;
                    }
                    const TermAnalyzer *caller =
                        edge_caller_analyzer(edge);
                    if (caller == nullptr || *slot >= edge.args->size()) {
                        ceiling.reset();
                        break;
                    }
                    const Interval iv = entry_interval(*caller, (*edge.args)[*slot]);
                    if (!iv.finite) {
                        ceiling.reset();
                        break;
                    }
                    ceiling = ceiling.has_value() ? std::max(*ceiling, iv.hi) : iv.hi;
                }
                if (!ceiling.has_value()) {
                    report(FnRecursionIssueKind::BoundNotStatic, fn, fn, *slot,
                           program_.fns[fn].source_range, out);
                    return;
                }
                base = *ceiling;
            }
            // Normalize the guard to the rank value at which the base case
            // triggers: condition `(r + a) </<=/>= (B + b)`.
            //   asc Ge -> r >= B+b-a ; asc Gt -> r >= B+b-a+1
            //   desc Le -> r <= B+b-a ; desc Lt -> r <= B+b-a-1
            std::int64_t stop = sat_sub(sat_add(base, guard.bound.c), guard.rank_side.c);
            if (guard.strict) {
                stop = ascending ? sat_add(stop, 1) : sat_sub(stop, 1);
            }
            stop_value = stop_value.has_value()
                             ? (ascending ? std::max(*stop_value, stop)
                                          : std::min(*stop_value, stop))
                             : stop;
        }

        // Sealed depth: activations r0, r0 +/- c, ... up to the triggering rank,
        // INCLUSIVE of the base-case activation. The minimum step maximizes the
        // number of steps; capacities keep all quantities far from the 64-bit
        // edges the literals live in. Saturating arithmetic feeds the ceiling
        // gate rather than wrapping a hostile literal into a tiny depth.
        std::int64_t span = ascending ? sat_sub(*stop_value, *entry_rank)
                                      : sat_sub(*entry_rank, *stop_value);
        if (span < 0) {
            span = 0;
        }
        // depth = ceil(span / min_step) + 1, every op saturated; a saturated
        // span / step / quotient is pinned at INT64_MAX and therefore exceeds
        // kFnRecursionDepthCeiling in the overflow gate below.
        std::int64_t depth = sat_add(sat_div_ceil(span, min_step), 1);
        if (depth < 1) {
            depth = 1;
        }
        if (static_cast<std::uint64_t>(depth) > kFnRecursionDepthCeiling) {
            FnRecursionScc overflow;
            overflow.members = component;
            overflow.depth_bound = static_cast<std::uint64_t>(depth);
            out.overflow_sccs.push_back(std::move(overflow));
            return;
        }
        FnRecursionScc sealed;
        sealed.members = component;
        sealed.depth_bound = static_cast<std::uint64_t>(depth);
        for (const std::uint32_t fn : component) {
            out.fn_depth_bound[fn] = depth;
        }
        out.sccs.push_back(std::move(sealed));
    }

    [[nodiscard]] bool is_invariant_slot(std::uint32_t fn, std::uint32_t slot,
                                         const std::vector<std::uint32_t> &internal) const {
        bool seen = false;
        for (const std::uint32_t ei : internal) {
            const CallEdge &edge = edges_[ei];
            if (edge.caller_fn != fn) {
                continue;
            }
            if (slot >= edge.args->size()) {
                return false;
            }
            const TermAnalyzer &ta = *fn_analyzers_[fn];
            const Term t = ta.eval_value((*edge.args)[slot]);
            if (t.kind != TermKind::Param ||
                t.ref != program_.fns[fn].params[slot] || t.c != 0) {
                return false;
            }
            seen = true;
        }
        // Every member of a nontrivial SCC has at least one outgoing internal
        // edge (SCC definition), so a non-invariant slot always surfaces here.
        return seen;
    }

    // Mixed-radix enumeration of rank-slot choices. Returns the first
    // assignment satisfying R1 for EVERY internal edge under one direction.
    [[nodiscard]] bool
    try_assignments(const std::vector<std::uint32_t> &component,
                    const std::vector<std::uint32_t> &internal,
                    const std::vector<std::vector<std::uint32_t>> &candidates,
                    std::vector<std::uint32_t> &chosen, bool &ascending,
                    std::int64_t &min_step) const {
        std::vector<std::size_t> pick(component.size(), 0);
        while (true) {
            for (bool asc : {true, false}) {
                for (std::size_t i = 0; i < component.size(); ++i) {
                    chosen[component[i]] = candidates[component[i]][pick[i]];
                }
                bool all = true;
                std::int64_t step = std::numeric_limits<std::int64_t>::max();
                for (const std::uint32_t ei : internal) {
                    const CallEdge &edge = edges_[ei];
                    const Progression p = progresses(edge, chosen[edge.caller_fn],
                                                      chosen[edge.callee_fn], asc);
                    if (!p.ok) {
                        all = false;
                        break;
                    }
                    step = std::min(step, p.step);
                }
                if (all && step != std::numeric_limits<std::int64_t>::max()) {
                    ascending = asc;
                    min_step = step;
                    return true;
                }
            }
            std::size_t digit = 0;
            while (digit < component.size()) {
                ++pick[digit];
                if (pick[digit] < candidates[component[digit]].size()) {
                    break;
                }
                pick[digit] = 0;
                ++digit;
            }
            if (digit == component.size()) {
                return false;
            }
        }
    }

    void report(FnRecursionIssueKind kind, std::uint32_t from, std::uint32_t to,
                std::optional<std::uint32_t> slot, SourceRangeOpt range,
                FnRecursionAnalysis &out) const {
        FnRecursionIssue issue;
        issue.kind = kind;
        issue.edge_from = from;
        issue.edge_to = to;
        issue.slot = slot;
        issue.range = std::move(range);
        out.unbounded_issues.push_back(std::move(issue));
    }

    const CoreProgram &program_;
    std::vector<CallEdge> edges_;
    std::vector<std::vector<std::uint32_t>> graph_;
    std::vector<std::unique_ptr<TermAnalyzer>> fn_analyzers_;
    mutable std::unordered_map<const void *, std::unique_ptr<TermAnalyzer>> root_analyzers_;
};

} // namespace

FnRecursionAnalysis analyze_fn_recursion(const CoreProgram &program) {
    return RecursionLattice(program).run();
}

std::uint64_t max_native_fn_call_depth(const CoreProgram &program,
                                       const FnRecursionAnalysis &analysis,
                                       const std::vector<bool> *reachable) {
    if (program.fns.empty()) {
        return 0;
    }
    const auto is_live = [&](std::uint32_t fn) {
        return reachable == nullptr || (fn < reachable->size() && (*reachable)[fn]);
    };
    // Rebuild the invocation graph (same deterministic derivation). A direct
    // CoreCallExpr contributes one edge; a CoreCallClosureExpr contributes one
    // edge per closure its callee slot can hold (FB-3b call_indirect, from the
    // flow-sensitive points-to set), so a cycle that closes only through a
    // closure value still weights the native stack path. The verifier already
    // rejects an unsealed indirect cycle; reachable targets only participate.
    std::vector<std::vector<std::uint32_t>> graph(program.fns.size());
    const ClosurePointsTo points_to = ClosurePointsTo::analyze(program);
    for (std::uint32_t fi = 0; fi < program.fns.size(); ++fi) {
        if (!is_live(fi)) {
            continue;
        }
        const CoreFnDecl &caller = program.fns[fi];
        for (const CoreExpr &expr : caller.storage.exprs) {
            if (const auto *call = std::get_if<CoreCallExpr>(&expr.node)) {
                if (call->callee.value >= program.instances.size()) {
                    continue;
                }
                const auto *payload =
                    std::get_if<CoreFnInstance>(&program.instances[call->callee.value].payload);
                if (payload == nullptr || payload->body.value == CoreFnId::kInvalid ||
                    payload->body.value >= program.fns.size() ||
                    !is_live(payload->body.value)) {
                    continue;
                }
                graph[fi].push_back(payload->body.value);
            } else if (const auto *indirect_call =
                           std::get_if<CoreCallClosureExpr>(&expr.node)) {
                for (const std::uint32_t target :
                     points_to.targets_of(caller.storage, *indirect_call)) {
                    if (is_live(target)) {
                        graph[fi].push_back(target);
                    }
                }
            }
        }
    }
    const std::vector<std::vector<std::uint32_t>> components = Tarjan(graph).run();
    std::vector<std::uint32_t> component_of(program.fns.size(), 0);
    std::vector<std::uint64_t> weight(components.size(), 1);
    for (std::uint32_t ci = 0; ci < components.size(); ++ci) {
        for (const std::uint32_t fn : components[ci]) {
            component_of[fn] = ci;
        }
        if (!is_live(components[ci][0])) {
            weight[ci] = 0; // not emitted
        } else if (components[ci].size() > 1) {
            weight[ci] = static_cast<std::uint64_t>(analysis.fn_depth_bound[components[ci][0]]);
        } else {
            const std::uint32_t only = components[ci][0];
            weight[ci] = analysis.fn_depth_bound[only] >= 0
                             ? static_cast<std::uint64_t>(analysis.fn_depth_bound[only])
                             : 1ULL;
        }
    }
    // Condensation edges (dedup).
    std::vector<std::vector<std::uint32_t>> dag(components.size());
    for (std::uint32_t fi = 0; fi < graph.size(); ++fi) {
        for (const std::uint32_t to : graph[fi]) {
            if (component_of[fi] != component_of[to]) {
                dag[component_of[fi]].push_back(component_of[to]);
            }
        }
    }
    for (auto &outs : dag) {
        std::sort(outs.begin(), outs.end());
        outs.erase(std::unique(outs.begin(), outs.end()), outs.end());
    }
    // Longest weighted path over the condensation DAG:
    //   best[C] = weight[C] + max(best[D] for every condensation edge C -> D).
    // This must be correct regardless of how the SCC components happen to be
    // ordered in the vector (helpers declared before their callers make the
    // common case edges toward a LOWER fn id, so the component order is the
    // opposite of the call direction). Derive an explicit topological order of
    // the DAG with Kahn (sources first) and walk it in REVERSE, so every
    // successor D is finalized before C is weighted.
    std::vector<std::uint32_t> indegree(components.size(), 0);
    for (const std::vector<std::uint32_t> &outs : dag) {
        for (const std::uint32_t to : outs) {
            ++indegree[to];
        }
    }
    std::vector<std::uint32_t> topo;
    topo.reserve(components.size());
    for (std::uint32_t ci = 0; ci < components.size(); ++ci) {
        if (indegree[ci] == 0) {
            topo.push_back(ci);
        }
    }
    for (std::size_t head = 0; head < topo.size(); ++head) {
        const std::uint32_t ci = topo[head];
        for (const std::uint32_t to : dag[ci]) {
            if (--indegree[to] == 0) {
                topo.push_back(to);
            }
        }
    }
    std::vector<std::uint64_t> best(components.size(), 0);
    std::uint64_t global = 0;
    for (auto it = topo.rbegin(); it != topo.rend(); ++it) {
        const std::uint32_t ci = *it;
        std::uint64_t follow = 0;
        for (const std::uint32_t to : dag[ci]) {
            follow = std::max(follow, best[to]);
        }
        best[ci] = weight[ci] + follow;
        global = std::max(global, best[ci]);
    }
    return global;
}

} // namespace ahfl::ir::core
