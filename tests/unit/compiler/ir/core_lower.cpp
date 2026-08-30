#include <doctest.h>

#include "ahfl/compiler/ir/core_ir.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/lowering.hpp"
#include "ahfl/compiler/ir/program.hpp"
#include "ahfl/compiler/ir/typed_hir_lower.hpp"
#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

#include <optional>
#include <cctype>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

// RFC 0026 P3 (KR6.4): A-normal-form Core-IR lowering of agent state machines,
// capability imports, and flow handler bodies. These tests cover:
//   * agent + capability projection (hand-built AhflIr, isolated);
//   * the ANF invariants Codex's review required, driven through the REAL front
//     end (parse -> resolve -> typecheck -> lower to AhflIr -> lower to Core):
//       - a nested capability call inside a pure constructor is NOT dropped and
//         is hoisted to its own ordered CapabilityCall statement (P0-1);
//       - if/else branches keep mutual exclusion as distinct regions, never a
//         flattened statement list (P0-2);
//       - goto targets resolve to typed CoreStateId;
//       - state retry/timeout policy is preserved;
//       - an unresolved capability call fails closed with a diagnostic + range.

namespace {

using namespace ahfl;

// --------------------------------------------------------------------------
// Hand-built AhflIr helpers (isolate the decl-level projection).
// --------------------------------------------------------------------------

// Append two minimal input/output structs to `program` and point `agent` at
// them. Sema (and now the Core-IR verifier) require an agent's input/output to
// be Struct types; hand-built fixtures that only exercise state-machine / body
// lowering still need a valid typed shell to pass the auto-wired verifier.
void give_shell_structs(ir::AhflIr &program, ir::AgentDecl &agent, const std::string &prefix) {
    ir::StructDecl in;
    in.name = prefix + "In";
    in.symbol_ref.kind = ir::SymbolRefKind::Type;
    in.symbol_ref.canonical_name = "app::" + prefix + "In";
    program.declarations.emplace_back(std::move(in));

    ir::StructDecl out;
    out.name = prefix + "Out";
    out.symbol_ref.kind = ir::SymbolRefKind::Type;
    out.symbol_ref.canonical_name = "app::" + prefix + "Out";
    program.declarations.emplace_back(std::move(out));

    agent.input_type_ref.kind = ir::TypeRefKind::Struct;
    agent.input_type_ref.canonical_name = "app::" + prefix + "In";
    agent.output_type_ref.kind = ir::TypeRefKind::Struct;
    agent.output_type_ref.canonical_name = "app::" + prefix + "Out";
    // A hand-built agent leaves context_type_ref defaulted (Unresolved), which
    // the lowerer would treat as a broken (non-Unit) context. Unless a caller
    // has already given an explicit context struct, mark it the Unit default so
    // the (now Sema-aligned) verifier accepts a stateless agent.
    if (agent.context_type_ref.kind == ir::TypeRefKind::Unresolved) {
        agent.context_type_ref.kind = ir::TypeRefKind::Unit;
    }
}

ir::AhflIr make_single_agent_program() {
    ir::AhflIr program;
    ir::AgentDecl agent;
    agent.name = "Classifier";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::Classifier";
    agent.symbol_ref.local_name = "Classifier";
    agent.symbol_ref.id = 7;
    agent.states = {"Init", "Working", "Done"};
    agent.initial_state = "Init";
    agent.final_states = {"Done"};
    agent.quota = {ir::QuotaItem{"max_tool_calls", "10"}};
    agent.transitions = {ir::TransitionDecl{"Init", "Working"},
                         ir::TransitionDecl{"Working", "Done"}};
    give_shell_structs(program, agent, "Cls");
    program.declarations.emplace_back(std::move(agent));

    ir::ContractDecl contract; // verification-only: must be erased
    contract.target_ref.kind = ir::SymbolRefKind::Agent;
    contract.target_ref.canonical_name = "app::Classifier";
    program.declarations.emplace_back(std::move(contract));
    return program;
}

ir::AhflIr make_capability_program() {
    ir::AhflIr program;
    ir::CapabilityDecl cap;
    cap.name = "ChargeCard";
    cap.symbol_ref.kind = ir::SymbolRefKind::Capability;
    cap.symbol_ref.canonical_name = "app::ChargeCard";
    cap.symbol_ref.local_name = "ChargeCard";
    cap.symbol_ref.id = 11;
    ir::ParamDecl amount;
    amount.name = "amount";
    amount.type_ref.kind = ir::TypeRefKind::Int;
    amount.type_ref.display_name = "Int";
    cap.params.push_back(std::move(amount));
    cap.return_type_ref.kind = ir::TypeRefKind::Bool;
    cap.return_type_ref.display_name = "Bool";
    cap.effect.declared = true;
    cap.effect.kind = ir::CapabilityEffectKind::FinancialWrite;
    cap.effect.domain = "payments";
    cap.effect.receipt_mode = ir::CapabilityReceiptMode::Required;
    program.declarations.emplace_back(std::move(cap));

    ir::AgentDecl agent;
    agent.name = "Classifier";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::Classifier";
    agent.symbol_ref.local_name = "Classifier";
    agent.symbol_ref.id = 7;
    agent.states = {"Init", "Done"};
    agent.initial_state = "Init";
    agent.final_states = {"Done"};
    agent.transitions = {ir::TransitionDecl{"Init", "Done"}};
    give_shell_structs(program, agent, "Cap");
    program.declarations.emplace_back(std::move(agent));
    return program;
}

// --------------------------------------------------------------------------
// Real front-end driver: source -> AhflIr (parse/resolve/typecheck/lower).
// Returns nullopt if any stage errors (so a corpus regression is visible).
// --------------------------------------------------------------------------
std::optional<ir::AhflIr> lower_source_to_ahfl_ir(const std::string &label,
                                                  const std::string &source) {
    const Frontend frontend;
    auto parse = frontend.parse_text(label + ".ahfl", source);
    if (parse.has_errors() || parse.program == nullptr) {
        return std::nullopt;
    }
    const Resolver resolver;
    const auto resolve = resolver.resolve(*parse.program);
    if (resolve.has_errors()) {
        return std::nullopt;
    }
    const TypeChecker checker;
    const auto typecheck = checker.check(*parse.program, resolve);
    if (typecheck.has_errors()) {
        return std::nullopt;
    }
    return lower_program_ir(*parse.program, resolve, typecheck);
}

// Count capability-call statements reachable in a region (recursing branches).
void collect_calls(const ir::core::CoreRegion &region,
                   std::vector<const ir::core::CoreCapabilityCallStmt *> &out) {
    for (const auto &stmt : region.statements) {
        std::visit(
            [&](const auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ir::core::CoreCapabilityCallStmt>) {
                    out.push_back(&node);
                } else if constexpr (std::is_same_v<T, ir::core::CoreIfStmt>) {
                    if (node.then_region) {
                        collect_calls(*node.then_region, out);
                    }
                    if (node.else_region) {
                        collect_calls(*node.else_region, out);
                    }
                }
            },
            stmt.node);
    }
}

// True if a (possibly module-qualified) callee names the given capability, e.g.
// "nestflow::Charge" or "Charge" both name "Charge".
bool callee_is(const std::string &callee, const std::string &name) {
    return callee == name || (callee.size() > name.size() &&
                              callee.compare(callee.size() - name.size() - 2, 2, "::") == 0 &&
                              callee.compare(callee.size() - name.size(), name.size(), name) == 0);
}

// Does a verify result carry an ERROR diagnostic with this stable code?
bool has_verify_code(const ir::core::CoreVerifyResult &r, std::string_view code) {
    for (const auto &d : r.diagnostics) {
        if (d.severity == ir::core::CoreDiagnosticSeverity::Error && d.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("lower_ahfl_to_core lowers an agent state machine into Core-IR") {
    const auto result = ir::core::lower_ahfl_to_core(make_single_agent_program());
    CHECK(result.ok());
    CHECK(result.is_executable);
    // The ContractDecl is a verification construct: it produces no Core decl.
    REQUIRE(result.program.agents.size() == 1);
    CHECK(result.program.flows.empty());
    const auto &agent = result.program.agents[0];
    CHECK(agent.name == "Classifier");
    REQUIRE(agent.states.size() == 3);
    CHECK(agent.states[agent.initial.value] == "Init");
    REQUIRE(agent.finals.size() == 1);
    CHECK(agent.states[agent.finals[0].value] == "Done");
    REQUIRE(agent.transitions.size() == 2);
    CHECK(result.program.format_version == std::string(ir::core::kCoreFormatVersion));
}

TEST_CASE("lower_ahfl_to_core lowers a capability into an explicit import decl") {
    const auto result = ir::core::lower_ahfl_to_core(make_capability_program());
    CHECK(result.ok());
    REQUIRE(result.program.capabilities.size() == 1);
    const auto &cap = result.program.capabilities[0];
    CHECK(cap.name == "ChargeCard");
    CHECK(cap.symbol_ref.canonical_name == "app::ChargeCard");
    CHECK(cap.effect_kind == ir::CapabilityEffectKind::FinancialWrite);
    REQUIRE(cap.param_types.size() == 1);
    CHECK(cap.param_types[0].kind == ir::TypeRefKind::Int);
    CHECK(cap.return_type_ref.kind == ir::TypeRefKind::Bool);
}

TEST_CASE("lower_ahfl_to_core is deterministic (decl level)") {
    const auto program = make_capability_program();
    const auto a = ir::core::lower_ahfl_to_core(program);
    const auto b = ir::core::lower_ahfl_to_core(program);
    REQUIRE(a.program.capabilities.size() == b.program.capabilities.size());
    REQUIRE(a.program.agents.size() == b.program.agents.size());
    // Agent identity is its symbol (instance_key was removed from CoreAgentDecl;
    // execution/dispatch identity now lives only on CoreInstanceDecl).
    CHECK(a.program.agents[0].symbol_ref.canonical_name ==
          b.program.agents[0].symbol_ref.canonical_name);
    CHECK(a.program.instances.size() == b.program.instances.size());
}

// ==========================================================================
// ANF flow lowering (real front end).
// ==========================================================================

// A self-contained program exercising: a nested capability call inside an enum
// constructor, an if/else with a capability call in one branch, a goto, and a
// state policy. Deliberately small but structurally identical to refund/audit.
const char *const kFlowSource = R"AHFL(
module payflow;

struct Order {
    total: Int;
}

struct Ctx {
    charged: Bool = false;
}

struct Outcome {
    ok: Bool = false;
}

capability Fetch(id: Int) -> Order;
capability Charge(amount: Int) -> Bool;

agent Payer {
    input: Order;
    context: Ctx;
    output: Outcome;
    states: [Init, Work, Done];
    initial: Init;
    final: [Done];
    capabilities: [Fetch, Charge];

    transition Init -> Work;
    transition Work -> Done;
}

flow for Payer {
    state Init {
        goto Work;
    }

    state Work with {
        retry: 2;
        timeout: 30s;
    } {
        let order = Fetch(input.total);
        if order.total > 0 {
            ctx.charged = Charge(order.total);
            goto Done;
        } else {
            goto Done;
        }
    }

    state Done {
        return Outcome { ok: ctx.charged };
    }
}
)AHFL";

TEST_CASE("flow lowering: capability call inside a branch is preserved (P0-2 mutual exclusion)") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("payflow", kFlowSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok()); // member projections now resolve; fully executable
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];
    REQUIRE(flow.states.size() == 3);

    // Find the Work state.
    const ir::core::CoreFlowState *work = nullptr;
    for (const auto &s : flow.states) {
        if (s.state_name == "Work") {
            work = &s;
        }
    }
    REQUIRE(work != nullptr);

    // The Charge call must live INSIDE the then-branch region — mutual
    // exclusion preserved, NOT flattened into the top-level statement list.
    bool found_if = false;
    bool charge_at_top_level = false;
    for (const auto &stmt : work->body.statements) {
        if (std::holds_alternative<ir::core::CoreIfStmt>(stmt.node)) {
            found_if = true;
            const auto &if_stmt = std::get<ir::core::CoreIfStmt>(stmt.node);
            REQUIRE(if_stmt.then_region);
            std::vector<const ir::core::CoreCapabilityCallStmt *> then_calls;
            collect_calls(*if_stmt.then_region, then_calls);
            CHECK(then_calls.size() == 1); // Charge is in the then-branch
            if (!then_calls.empty()) {
                CHECK(callee_is(then_calls[0]->callee_name, "Charge"));
            }
        }
        if (std::holds_alternative<ir::core::CoreCapabilityCallStmt>(stmt.node)) {
            const auto &call = std::get<ir::core::CoreCapabilityCallStmt>(stmt.node);
            if (callee_is(call.callee_name, "Charge")) {
                charge_at_top_level = true;
            }
        }
    }
    CHECK(found_if);
    CHECK_FALSE(charge_at_top_level); // Charge must NOT be hoisted out of its branch
}

TEST_CASE("flow lowering: goto resolves to a typed CoreStateId and policy is preserved") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("payflow", kFlowSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    REQUIRE(result.ok()); // member projections now resolve; fully executable
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];

    const ir::core::CoreFlowState *work = nullptr;
    for (const auto &s : flow.states) {
        if (s.state_name == "Work") {
            work = &s;
        }
    }
    REQUIRE(work != nullptr);
    // retry/timeout policy carried onto the state.
    CHECK(work->policy.retry_limit.has_value());
    CHECK(work->policy.timeout.has_value());

    // The Init state's goto must resolve to a typed state id (not a bare string).
    const ir::core::CoreFlowState *init = nullptr;
    for (const auto &s : flow.states) {
        if (s.state_name == "Init") {
            init = &s;
        }
    }
    REQUIRE(init != nullptr);
    bool found_goto = false;
    for (const auto &stmt : init->body.statements) {
        if (std::holds_alternative<ir::core::CoreGotoStmt>(stmt.node)) {
            found_goto = true;
            const auto &g = std::get<ir::core::CoreGotoStmt>(stmt.node);
            CHECK(g.target_name == "Work");
            // Work is state index 1 in [Init, Work, Done].
            CHECK(g.target.value == 1);
        }
    }
    CHECK(found_goto);
}

// Nested capability call inside an enum constructor (the refund/audit shape):
//   ctx.ticket = Option::Some(Charge(order.total));
// The inner Charge MUST become its own ordered CapabilityCall statement whose
// result feeds the outer pure Some(...) construct — never dropped (P0-1).
const char *const kNestedSource = R"AHFL(
module nestflow;

enum Box {
    Wrap(Bool),
    Empty,
}

struct Order {
    total: Int;
}

struct Ctx {
    ticket: Box = Box::Empty;
}

struct Outcome {
    ok: Bool = false;
}

capability Charge(amount: Int) -> Bool;

agent Payer {
    input: Order;
    context: Ctx;
    output: Outcome;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [Charge];

    transition Init -> Done;
}

flow for Payer {
    state Init {
        ctx.ticket = Box::Wrap(Charge(input.total));
        goto Done;
    }

    state Done {
        return Outcome { ok: false };
    }
}
)AHFL";

TEST_CASE("flow lowering: nested capability call inside a constructor is not dropped (P0-1)") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("nestflow", kNestedSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok()); // member projections now resolve; fully executable
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];

    const ir::core::CoreFlowState *init = nullptr;
    for (const auto &s : flow.states) {
        if (s.state_name == "Init") {
            init = &s;
        }
    }
    REQUIRE(init != nullptr);

    std::vector<const ir::core::CoreCapabilityCallStmt *> calls;
    collect_calls(init->body, calls);
    // The inner Charge call survives as an explicit ordered capability-call stmt.
    REQUIRE(calls.size() == 1);
    CHECK(callee_is(calls[0]->callee_name, "Charge"));
    // Its result value id must be consumed by a later pure construct (Some).
    const ir::core::CoreValueId charge_result = calls[0]->result;

    bool result_consumed_by_construct = false;
    for (const auto &expr : flow.exprs) {
        if (std::holds_alternative<ir::core::CoreConstructExpr>(expr.node)) {
            const auto &ctor = std::get<ir::core::CoreConstructExpr>(expr.node);
            for (const auto &arg : ctor.args) {
                if (arg.value == charge_result) {
                    result_consumed_by_construct = true;
                    // The Wrap variant resolves to a typed index (Wrap=0).
                    CHECK(ctor.is_enum_variant);
                    CHECK(ctor.resolved);
                    CHECK(ctor.variant.value == 0u);
                }
            }
        }
    }
    CHECK(result_consumed_by_construct);
}

// ==========================================================================
// Fail-closed: unresolved capability call (hand-built AhflIr).
// ==========================================================================

TEST_CASE("flow lowering fails closed on an unresolved capability call") {
    // A flow that calls a capability whose declaration is absent from the
    // program: lowering must emit an error diagnostic with a range and mark the
    // program non-executable — never silently drop the call.
    ir::AhflIr program;

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    give_shell_structs(program, agent, "Hnd");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";

    // A capability CallExpr whose callee_ref is NOT backed by any CapabilityDecl.
    ir::CallExpr call;
    call.callee = "Ghost";
    call.callee_ref.kind = ir::SymbolRefKind::Capability;
    call.callee_ref.canonical_name = "app::Ghost";
    call.callee_ref.id = 999;
    ir::ExprRef call_ref = program.expr_arena.make(std::move(call), SourceRange{10, 20});

    auto stmt = std::make_unique<ir::Statement>();
    stmt->node = ir::ExprStatement{call_ref};
    stmt->source_range = SourceRange{10, 20};

    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(stmt));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    CHECK(result.has_errors());
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnresolvedCapabilityCall);
    CHECK(result.diagnostics[0].source_range.has_value());
}

// ==========================================================================
// Builtin Option/Result variant order must match the sysroot declaration.
// ==========================================================================

namespace {

// A variant's declaration shape parsed from the sysroot: name + payload kind +
// payload arity. Enough to prove the builtin SSOT mirrors the real stdlib
// declaration (not just the name order).
struct SysrootVariant {
    std::string name;
    ir::core::CoreTypeDecl::VariantPayload::Kind kind{
        ir::core::CoreTypeDecl::VariantPayload::Kind::Unit};
    std::uint32_t arity{0};
};

// Count top-level (depth-1) comma-separated items in a payload body, i.e. the
// payload arity. `Some(T)` -> 1, `Pair(A, B)` -> 2, `Rec { a: T, b: U }` -> 2.
[[nodiscard]] std::uint32_t count_payload_items(const std::string &body) {
    // An empty/whitespace body means zero items.
    bool any = false;
    for (char c : body) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            any = true;
            break;
        }
    }
    if (!any) {
        return 0;
    }
    std::uint32_t items = 1;
    int depth = 0;
    for (char c : body) {
        if (c == '(' || c == '<' || c == '{' || c == '[') {
            ++depth;
        } else if (c == ')' || c == '>' || c == '}' || c == ']') {
            --depth;
        } else if (c == ',' && depth == 0) {
            ++items;
        }
    }
    return items;
}

// Extract the variants (name + payload kind + arity) of the first `enum <Name>`
// block in an AHFL source file, in declaration order. Minimal textual scan (no
// full parse) — enough to pin the sysroot declaration the builtin table mirrors.
std::vector<SysrootVariant> read_enum_variants(const std::string &path,
                                               const std::string &enum_name) {
    std::vector<SysrootVariant> variants;
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return variants;
    }
    std::string text;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        text.append(buf, n);
    }
    std::fclose(f);

    // The enum header may be `enum Name` or `enum Name<...>`; match the bare name
    // then the following `{`.
    const auto enum_pos = text.find("enum " + enum_name);
    if (enum_pos == std::string::npos) {
        return variants;
    }
    const auto open = text.find('{', enum_pos);
    if (open == std::string::npos) {
        return variants;
    }
    // Find the matching close brace for the enum body (variants may contain
    // nested `{...}` struct payloads, so track depth).
    std::size_t close = std::string::npos;
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            if (--depth == 0) {
                close = i;
                break;
            }
        }
    }
    if (close == std::string::npos) {
        return variants;
    }
    const std::string body = text.substr(open + 1, close - open - 1);
    // Walk top-level (depth-0) comma-separated variant entries. Each entry is an
    // identifier optionally followed by `(...)` (tuple) or `{...}` (struct).
    using PK = ir::core::CoreTypeDecl::VariantPayload::Kind;
    std::size_t i = 0;
    while (i < body.size()) {
        // Skip separators / whitespace.
        while (i < body.size() && !(std::isalnum(static_cast<unsigned char>(body[i])) ||
                                    body[i] == '_')) {
            ++i;
        }
        if (i >= body.size()) {
            break;
        }
        std::string name;
        while (i < body.size() &&
               (std::isalnum(static_cast<unsigned char>(body[i])) || body[i] == '_')) {
            name.push_back(body[i++]);
        }
        // Skip whitespace to peek at an optional payload delimiter.
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) {
            ++i;
        }
        SysrootVariant v;
        v.name = name;
        if (i < body.size() && (body[i] == '(' || body[i] == '{')) {
            const char open_ch = body[i];
            const char close_ch = open_ch == '(' ? ')' : '}';
            v.kind = open_ch == '(' ? PK::Tuple : PK::Struct;
            int d = 0;
            const std::size_t payload_start = i + 1;
            std::size_t payload_end = i;
            for (; i < body.size(); ++i) {
                if (body[i] == open_ch) {
                    ++d;
                } else if (body[i] == close_ch) {
                    if (--d == 0) {
                        payload_end = i;
                        ++i;
                        break;
                    }
                }
            }
            v.arity = count_payload_items(body.substr(payload_start, payload_end - payload_start));
        } else {
            v.kind = PK::Unit;
            v.arity = 0;
        }
        variants.push_back(std::move(v));
        // Advance past a trailing top-level comma.
        while (i < body.size() && body[i] != ',') {
            // Skip anything up to the next separator only if it's whitespace; a
            // stray token would be malformed source, which the parser would have
            // rejected — here we simply resync on the next comma.
            if (!std::isspace(static_cast<unsigned char>(body[i]))) {
                break;
            }
            ++i;
        }
        if (i < body.size() && body[i] == ',') {
            ++i;
        }
    }
    return variants;
}

} // namespace

TEST_CASE("builtin variant table matches stdlib declaration (name + payload kind + arity)") {
    // Reads the ACTUAL production builtin table (builtin_enum_table()) and the
    // sysroot std/*.ahfl, and asserts each descriptor's variants match the
    // sysroot declaration by NAME, PAYLOAD KIND, and ARITY. Reordering or
    // reshaping EITHER side (e.g. changing std Option's `Some(T)` to a struct
    // payload) now fails — closing the "sync test doesn't compare std" gap.
    const auto &table = ir::core::builtin_enum_table();
    REQUIRE_FALSE(table.empty());
    for (const auto &desc : table) {
        const std::string name(desc.name);
        const std::string path = "std/" + [&] {
            std::string lower;
            for (char c : name) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return lower;
        }() + ".ahfl";
        const auto sysroot = read_enum_variants(path, name);
        INFO("enum " << name << " from " << path);
        REQUIRE(sysroot.size() == desc.variants.size());
        for (std::size_t i = 0; i < desc.variants.size(); ++i) {
            INFO("variant #" << i);
            CHECK(std::string(desc.variants[i].name) == sysroot[i].name);
            CHECK(desc.variants[i].payload_kind == sysroot[i].kind);
            CHECK(desc.variants[i].payload_arity == sysroot[i].arity);
        }
    }
}

TEST_CASE("builtin enum variant resolves via the builtin path by symbol identity") {
    // A hand-built AhflIr that constructs std::option::Option::Some(<int>) with
    // a callee_ref to the Option ENUM symbol (as the front end emits). No user
    // Option is declared, so resolution must go through the builtin table — and
    // by SYMBOL IDENTITY / canonical name, not by unqualified-name hijack.
    ir::AhflIr program;

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    give_shell_structs(program, agent, "Hnd");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;

    // Option::Some(0) as a variant-constructor CallExpr (callee_ref -> Enum).
    ir::ExprRef arg = program.expr_arena.make(ir::IntegerLiteralExpr{"0"});
    ir::CallExpr call;
    call.callee = "std::option::Option::Some";
    call.callee_ref.kind = ir::SymbolRefKind::Type; // the front end resolves to the Enum symbol
    call.callee_ref.canonical_name = "std::option::Option";
    call.arguments.push_back(arg);
    ir::ExprRef call_ref = program.expr_arena.make(std::move(call));

    auto stmt = std::make_unique<ir::Statement>();
    stmt->node = ir::ExprStatement{call_ref};
    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(stmt));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    CHECK(result.ok());
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow_out = result.program.flows[0];

    bool found_some = false;
    for (const auto &expr : flow_out.exprs) {
        if (std::holds_alternative<ir::core::CoreConstructExpr>(expr.node)) {
            const auto &ctor = std::get<ir::core::CoreConstructExpr>(expr.node);
            if (ctor.is_enum_variant && ctor.variant_name == "Some") {
                found_some = true;
                CHECK(ctor.resolved);
                CHECK(ctor.variant.value == 0u); // Some=0 in the builtin table
                // The owning type id points at the builtin Option in the type table.
                REQUIRE(ctor.type_id.value < result.program.types.size());
                CHECK(result.program.types[ctor.type_id.value].name == "std::option::Option");
            }
        }
    }
    CHECK(found_some);
}

// ==========================================================================
// Fail-closed executable completeness + scope + target (Codex re-review).
// ==========================================================================

// Build a minimal flow whose single handler statement is provided by `make_stmt`.
ir::AhflIr make_single_handler_flow(
    const std::function<ir::StatementPtr(ir::AhflIr &)> &make_stmt) {
    ir::AhflIr program;
    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    give_shell_structs(program, agent, "Hnd");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(make_stmt(program));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));
    return program;
}

TEST_CASE("unsupported statement makes the program non-executable (P0-1)") {
    // An `assert(...)` is not yet lowered; dropping it would turn a failing
    // program into a no-op, so lowering must FAIL closed (Error), not Warning.
    const auto program = make_single_handler_flow([](ir::AhflIr &p) {
        ir::ExprRef cond = p.expr_arena.make(ir::BoolLiteralExpr{false});
        auto stmt = std::make_unique<ir::Statement>();
        stmt->node = ir::AssertStatement{cond, ir::ExprRef{}};
        stmt->source_range = ahfl::SourceRange{1, 2};
        return stmt;
    });
    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnloweredStatement);
    CHECK(result.diagnostics[0].severity == ir::core::CoreDiagnosticSeverity::Error);
}

TEST_CASE("branch-local let bindings do not leak across branches or past the if (P0-2)") {
    // let x = 1; if true { let x = 2; } else { let x = 3; } return x;
    // The returned value must be the OUTER x (value id from `let x = 1`), not
    // the else-branch's shadow. We assert the return's value id equals the
    // outer binding's, proving branch scopes are snapshot/restored.
    ir::AhflIr program;
    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    give_shell_structs(program, agent, "Hnd");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;

    ir::StateHandler handler;
    handler.state_name = "S";

    const auto make_let = [&](const std::string &name, const std::string &val) {
        ir::ExprRef init = program.expr_arena.make(ir::IntegerLiteralExpr{val});
        auto s = std::make_unique<ir::Statement>();
        ir::LetStatement let;
        let.name = name;
        let.initializer = init;
        s->node = std::move(let);
        return s;
    };
    // let x = 1;
    handler.body.statements.push_back(make_let("x", "1"));
    // if true { let x = 2; } else { let x = 3; }
    {
        ir::ExprRef cond = program.expr_arena.make(ir::BoolLiteralExpr{true});
        ir::IfStatement if_stmt;
        if_stmt.condition = cond;
        auto then_block = std::make_unique<ir::Block>();
        then_block->statements.push_back(make_let("x", "2"));
        auto else_block = std::make_unique<ir::Block>();
        else_block->statements.push_back(make_let("x", "3"));
        if_stmt.then_block = std::move(then_block);
        if_stmt.else_block = std::move(else_block);
        auto s = std::make_unique<ir::Statement>();
        s->node = std::move(if_stmt);
        handler.body.statements.push_back(std::move(s));
    }
    // return x;
    {
        ir::PathExpr px;
        px.path.root_name = "x";
        ir::ExprRef xref = program.expr_arena.make(std::move(px));
        auto s = std::make_unique<ir::Statement>();
        s->node = ir::ReturnStatement{xref};
        handler.body.statements.push_back(std::move(s));
    }
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    REQUIRE(result.ok());
    const auto &body = result.program.flows[0].states[0].body;

    // Outer `let x = 1` is the FIRST let statement; capture its result value id.
    std::optional<ir::core::CoreValueId> outer_x;
    for (const auto &stmt : body.statements) {
        if (std::holds_alternative<ir::core::CoreLetStmt>(stmt.node)) {
            outer_x = std::get<ir::core::CoreLetStmt>(stmt.node).result;
            break;
        }
    }
    REQUIRE(outer_x.has_value());

    // The return reads `x`; after the if, the local `x` must still be the outer
    // binding, so its lowered value-ref chain must originate from outer_x —
    // never the else-branch's `3`. We check the return statement's value id
    // traces to a pure expr that references outer_x (a bare local ref returns
    // the binding's value id directly).
    std::optional<ir::core::CoreValueId> returned;
    for (const auto &stmt : body.statements) {
        if (std::holds_alternative<ir::core::CoreReturnStmt>(stmt.node)) {
            const auto &r = std::get<ir::core::CoreReturnStmt>(stmt.node);
            REQUIRE(r.has_value);
            returned = r.value;
        }
    }
    REQUIRE(returned.has_value());
    CHECK(*returned == *outer_x); // returns OUTER x, branch shadows discarded
}

TEST_CASE("a branch shadow with a DIFFERENT nominal type does not corrupt the outer local's "
          "type (P0-1)") {
    // let x: AType;  (AType has field `a`)
    // if true { let x: BType = …; }  (BType has field `b`, NOT `a`)
    // ctx.out = x.a;  -- after the if, x must STILL be AType, so `x.a` resolves.
    //
    // If lower_if snapshot/restored only the value id (not the type), the
    // branch-local `x: BType` would leave the outer `x`'s tracked type as BType,
    // and `x.a` would fail-closed (BType has no `a`). This asserts the merged
    // scope snapshot restores value AND type together.
    ir::AhflIr program;

    const auto make_struct = [&](const std::string &name, const std::string &canonical,
                                 std::size_t id, const std::string &field) {
        ir::StructDecl s;
        s.name = name;
        s.symbol_ref.kind = ir::SymbolRefKind::Type;
        s.symbol_ref.canonical_name = canonical;
        s.symbol_ref.id = id;
        ir::FieldDecl f;
        f.name = field;
        f.type_ref.kind = ir::TypeRefKind::Int;
        s.fields.push_back(std::move(f));
        program.declarations.emplace_back(std::move(s));
    };
    make_struct("AType", "app::AType", 60, "a");
    make_struct("BType", "app::BType", 61, "b");

    // Ctx has a single Int field `out` (the store target).
    {
        ir::StructDecl ctx;
        ctx.name = "Ctx";
        ctx.symbol_ref.kind = ir::SymbolRefKind::Type;
        ctx.symbol_ref.canonical_name = "app::Ctx";
        ctx.symbol_ref.id = 62;
        ir::FieldDecl f;
        f.name = "out";
        f.type_ref.kind = ir::TypeRefKind::Int;
        ctx.fields.push_back(std::move(f));
        program.declarations.emplace_back(std::move(ctx));
    }

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    agent.context_type_ref.kind = ir::TypeRefKind::Struct;
    agent.context_type_ref.canonical_name = "app::Ctx";
    give_shell_structs(program, agent, "Shd");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;

    ir::StateHandler handler;
    handler.state_name = "S";

    const auto make_typed_let = [&](const std::string &name, const std::string &type_canonical) {
        ir::ExprRef init = program.expr_arena.make(ir::IntegerLiteralExpr{"0"});
        auto s = std::make_unique<ir::Statement>();
        ir::LetStatement let;
        let.name = name;
        let.type_ref.kind = ir::TypeRefKind::Struct;
        let.type_ref.canonical_name = type_canonical;
        let.initializer = init;
        s->node = std::move(let);
        return s;
    };
    // let x: AType = 0;
    handler.body.statements.push_back(make_typed_let("x", "app::AType"));
    // if true { let x: BType = 0; }
    {
        ir::ExprRef cond = program.expr_arena.make(ir::BoolLiteralExpr{true});
        ir::IfStatement if_stmt;
        if_stmt.condition = cond;
        auto then_block = std::make_unique<ir::Block>();
        then_block->statements.push_back(make_typed_let("x", "app::BType"));
        if_stmt.then_block = std::move(then_block);
        auto s = std::make_unique<ir::Statement>();
        s->node = std::move(if_stmt);
        handler.body.statements.push_back(std::move(s));
    }
    // ctx.out = x.a;
    {
        ir::PathExpr xa;
        xa.path.root_name = "x";
        xa.path.members = {"a"};
        ir::ExprRef xaref = program.expr_arena.make(std::move(xa));
        ir::AssignStatement assign;
        assign.target.root_kind = ir::PathRootKind::Context;
        assign.target.root_name = "ctx";
        assign.target.members = {"out"};
        assign.value = xaref;
        auto s = std::make_unique<ir::Statement>();
        s->node = std::move(assign);
        handler.body.statements.push_back(std::move(s));
    }
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    // `x.a` must resolve against AType (field `a`), NOT the branch-local BType.
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    // The AType CoreTypeId, to compare the resolved read's owner against.
    std::optional<ir::core::CoreTypeId> atype_id;
    for (std::uint32_t i = 0; i < result.program.types.size(); ++i) {
        if (result.program.types[i].name == "app::AType") {
            atype_id = ir::core::CoreTypeId{i};
        }
    }
    REQUIRE(atype_id.has_value());

    bool saw_x_a = false;
    for (const auto &expr : result.program.flows[0].exprs) {
        if (const auto *p = std::get_if<ir::core::CorePathExpr>(&expr.node)) {
            if (p->root == ir::core::CorePathRoot::Local && p->members.size() == 1 &&
                p->members[0] == "a") {
                saw_x_a = true;
                CHECK(p->projection_resolved);
                CHECK(p->root_type == *atype_id);  // outer AType, not branch BType
                REQUIRE(p->projection.size() == 1);
                CHECK(p->projection[0].owner_type == *atype_id);
                CHECK(p->projection[0].field.value == 0u); // AType.a is field 0
            }
        }
    }
    CHECK(saw_x_a);
}

TEST_CASE("flow with an unresolved target agent fails closed (P1-1)") {
    ir::AhflIr program;
    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::Ghost";
    flow.target_ref.local_name = "Ghost";
    flow.target_ref.id = 404;
    ir::StateHandler handler;
    handler.state_name = "S";
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnresolvedFlowTarget);
}

TEST_CASE("flow handler naming an unknown state fails closed, never defaults to state 0 (P1-1)") {
    ir::AhflIr program;
    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"Init"};
    agent.initial_state = "Init";
    agent.final_states = {"Init"};
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    ir::StateHandler handler;
    handler.state_name = "Nonexistent"; // not a declared state
    handler.source_range = ahfl::SourceRange{5, 9};
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnknownHandlerState);
    CHECK(result.diagnostics[0].source_range.has_value());
    // The bogus handler was NOT lowered onto state 0.
    REQUIRE(result.program.flows.size() == 1);
    CHECK(result.program.flows[0].states.empty());
}

TEST_CASE("pure unsupported expression makes the program non-executable (P0-1 round 3)") {
    // A MethodCallExpr (no capability) is not yet lowered to Core-IR. Even
    // without an effect, it must FAIL closed — a CoreUnsupportedExpr the backend
    // cannot execute may never be reported as executable.
    const auto program = make_single_handler_flow([](ir::AhflIr &p) {
        // return input.method() — a pure method call, still unsupported.
        ir::ExprRef base = p.expr_arena.make(
            ir::PathExpr{.path = ir::Path{.root_name = "input"}});
        ir::MethodCallExpr call;
        call.receiver = base;
        call.method = "compute";
        ir::ExprRef cref = p.expr_arena.make(std::move(call));
        auto stmt = std::make_unique<ir::Statement>();
        stmt->node = ir::ReturnStatement{cref};
        return stmt;
    });
    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnloweredExpression);
    CHECK(result.diagnostics[0].severity == ir::core::CoreDiagnosticSeverity::Error);
}

TEST_CASE("struct constructor binds values by field identity, not source write order (P0-2 round 3)") {
    // struct Pair { a: Int; b: Int; }  return Pair { b: 2, a: 1 };
    // Fields are WRITTEN b-then-a but must bind by identity: the arg tagged
    // CoreFieldId a(=0) is the value `1`, and b(=1) is `2` — never positional.
    const char *const source = R"AHFL(
module pairflow;

struct Pair {
    a: Int;
    b: Int;
}

struct Req {
    n: Int;
}

struct Ctx {
    seen: Bool = false;
}

agent A {
    input: Req;
    context: Ctx;
    output: Pair;
    states: [S];
    initial: S;
    final: [S];
    capabilities: [];
}

flow for A {
    state S {
        return Pair { b: 2, a: 1 };
    }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("pairflow", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    // This program has NO member projections and all fields resolve, so it must
    // be fully executable — a hard assertion, not the weak field-projection
    // tolerance used by the ANF-structure tests above.
    REQUIRE(result.ok());
    CHECK(result.is_executable);
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];

    // Find the Pair construct and the literal each arg's value came from.
    const ir::core::CoreConstructExpr *pair = nullptr;
    for (const auto &expr : flow.exprs) {
        if (std::holds_alternative<ir::core::CoreConstructExpr>(expr.node)) {
            const auto &c = std::get<ir::core::CoreConstructExpr>(expr.node);
            if (c.type_name.find("Pair") != std::string::npos && !c.is_enum_variant) {
                pair = &c;
            }
        }
    }
    REQUIRE(pair != nullptr);
    REQUIRE(pair->resolved);
    REQUIRE(pair->args.size() == 2);

    // Map each field id -> the integer literal spelling that produced its value.
    const auto literal_for_value = [&](ir::core::CoreValueId v) -> std::string {
        // The value is bound by a CoreLetStmt whose expr is an integer literal.
        for (const auto &state : flow.states) {
            for (const auto &stmt : state.body.statements) {
                if (const auto *let = std::get_if<ir::core::CoreLetStmt>(&stmt.node)) {
                    if (let->result == v && let->expr.value < flow.exprs.size()) {
                        const auto &e = flow.exprs[let->expr.value].node;
                        if (const auto *lit = std::get_if<ir::core::CoreLiteralExpr>(&e)) {
                            return lit->spelling;
                        }
                    }
                }
            }
        }
        return {};
    };

    std::string field_a_val;
    std::string field_b_val;
    for (const auto &arg : pair->args) {
        if (arg.field.value == 0u) { // field `a` (declared first)
            field_a_val = literal_for_value(arg.value);
        } else if (arg.field.value == 1u) { // field `b` (declared second)
            field_b_val = literal_for_value(arg.value);
        }
    }
    // Despite `Pair { b: 2, a: 1 }`, identity binds a<-1 and b<-2.
    CHECK(field_a_val == "1");
    CHECK(field_b_val == "2");
}

// ==========================================================================
// Struct-field member projection lowering (resolves to typed CoreFieldId).
// ==========================================================================

const char *const kProjectionSource = R"AHFL(
module projflow;

struct Inner {
    n: Int;
}

struct Req {
    top: Int;
    inner: Inner;
}

struct Ctx {
    saved: Int = 0;
    nested: Inner = Inner { n: 0 };
}

struct Reply {
    ok: Bool = false;
}

agent A {
    input: Req;
    context: Ctx;
    output: Reply;
    states: [S];
    initial: S;
    final: [S];
    capabilities: [];
}

flow for A {
    state S {
        ctx.saved = input.top;
        ctx.nested.n = input.inner.n;
    }
}
)AHFL";

TEST_CASE("member projection resolves reads and stores to typed CoreFieldId (fail-closed lifted)") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("projflow", kProjectionSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diagnostics: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    // With member projections now resolved, the program is fully executable —
    // NO more field-projection fail-closed diagnostics.
    REQUIRE(result.ok());
    CHECK(result.is_executable);
    REQUIRE(result.program.flows.size() == 1);
    const auto &flow = result.program.flows[0];
    REQUIRE(flow.states.size() == 1);
    const auto &body = flow.states[0].body;

    // Gather the resolved read (CorePathExpr) and store (CoreStoreStmt) projections.
    // Reads live in the expr arena; stores are statements.
    bool saw_input_top_read = false;   // input.top -> field 0 of Req
    bool saw_input_inner_n_read = false; // input.inner.n -> [1, 0]
    for (const auto &expr : flow.exprs) {
        if (const auto *p = std::get_if<ir::core::CorePathExpr>(&expr.node)) {
            if (p->root == ir::core::CorePathRoot::Input && p->members.size() == 1 &&
                p->members[0] == "top") {
                saw_input_top_read = true;
                CHECK(p->projection_resolved);
                CHECK(p->root_type.value != ir::core::CoreTypeId::kInvalid);
                REQUIRE(p->projection.size() == 1);
                CHECK(p->projection[0].owner_type == p->root_type); // step owner == root
                CHECK(p->projection[0].field.value == 0u); // Req.top is field 0
            }
            if (p->root == ir::core::CorePathRoot::Input && p->members.size() == 2 &&
                p->members[0] == "inner" && p->members[1] == "n") {
                saw_input_inner_n_read = true;
                CHECK(p->projection_resolved);
                CHECK(p->root_type.value != ir::core::CoreTypeId::kInvalid);
                REQUIRE(p->projection.size() == 2);
                CHECK(p->projection[0].owner_type == p->root_type); // Req
                CHECK(p->projection[0].field.value == 1u);          // Req.inner is field 1
                // Step continuity: step 1's owner is step 0's result (Inner).
                CHECK(p->projection[1].owner_type == p->projection[0].result_type);
                CHECK(p->projection[1].owner_type.value != ir::core::CoreTypeId::kInvalid);
                CHECK(p->projection[1].field.value == 0u); // Inner.n is field 0
                // Terminal step reaches a primitive (Int): no result type.
                CHECK(p->projection[1].result_type.value == ir::core::CoreTypeId::kInvalid);
            }
        }
    }
    CHECK(saw_input_top_read);
    CHECK(saw_input_inner_n_read);

    bool saw_ctx_saved_store = false;    // ctx.saved -> field 0 of Ctx
    bool saw_ctx_nested_n_store = false; // ctx.nested.n -> [1, 0]
    for (const auto &stmt : body.statements) {
        if (const auto *store = std::get_if<ir::core::CoreStoreStmt>(&stmt.node)) {
            const auto &pl = store->place;
            if (pl.root == ir::core::CorePathRoot::Context && pl.members.size() == 1 &&
                pl.members[0] == "saved") {
                saw_ctx_saved_store = true;
                CHECK(pl.projection_resolved);
                CHECK(pl.root_type.value != ir::core::CoreTypeId::kInvalid);
                REQUIRE(pl.projection.size() == 1);
                CHECK(pl.projection[0].owner_type == pl.root_type);
                CHECK(pl.projection[0].field.value == 0u); // Ctx.saved is field 0
            }
            if (pl.root == ir::core::CorePathRoot::Context && pl.members.size() == 2 &&
                pl.members[0] == "nested" && pl.members[1] == "n") {
                saw_ctx_nested_n_store = true;
                CHECK(pl.projection_resolved);
                CHECK(pl.root_type.value != ir::core::CoreTypeId::kInvalid);
                REQUIRE(pl.projection.size() == 2);
                CHECK(pl.projection[0].owner_type == pl.root_type);  // Ctx
                CHECK(pl.projection[0].field.value == 1u);           // Ctx.nested is field 1
                CHECK(pl.projection[1].owner_type == pl.projection[0].result_type); // Inner
                CHECK(pl.projection[1].field.value == 0u);           // Inner.n is field 0
                CHECK(pl.projection[1].result_type.value == ir::core::CoreTypeId::kInvalid);
            }
        }
    }
    CHECK(saw_ctx_saved_store);
    CHECK(saw_ctx_nested_n_store);
}

TEST_CASE("member projection on an unknown field fails closed") {
    // Hand-built AhflIr: agent input is a struct with field `a`, but the flow
    // reads `input.ghost` (no such field) -> fail-closed diagnostic, not a
    // silently-executable projection.
    ir::AhflIr program;

    ir::StructDecl req;
    req.name = "Req";
    req.symbol_ref.kind = ir::SymbolRefKind::Type;
    req.symbol_ref.canonical_name = "app::Req";
    req.symbol_ref.id = 50;
    ir::FieldDecl fa;
    fa.name = "a";
    fa.type_ref.kind = ir::TypeRefKind::Int;
    req.fields.push_back(std::move(fa));
    program.declarations.emplace_back(std::move(req));

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    agent.input_type_ref.kind = ir::TypeRefKind::Struct;
    agent.input_type_ref.canonical_name = "app::Req";
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    // return input.ghost;
    ir::PathExpr px;
    px.path.root_kind = ir::PathRootKind::Input;
    px.path.root_name = "input";
    px.path.members = {"ghost"};
    ir::ExprRef pref = program.expr_arena.make(std::move(px), SourceRange{3, 9});
    auto stmt = std::make_unique<ir::Statement>();
    stmt->node = ir::ReturnStatement{pref};
    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(stmt));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics[0].code == ir::core::diag::kUnloweredFieldProjection);
}

// ==========================================================================
// Auto-verify wiring: lower_ahfl_to_core runs the Core-IR verifier on a
// lowering-clean candidate and merges verifier Errors; a lowering that already
// errored is NOT re-verified (no duplicate core.verify.* noise).
// ==========================================================================

namespace {
[[nodiscard]] bool has_verify_code(const ir::core::CoreLowerResult &r) {
    for (const auto &d : r.diagnostics) {
        if (d.code.rfind("core.verify.", 0) == 0) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST_CASE("lowering-clean but verifier-rejected program merges verify errors and is not executable") {
    // An agent whose INPUT type is an enum (not a struct). The lowerer does not
    // enforce Sema's schema boundary, so lowering itself is clean; the auto-wired
    // verifier must reject it (input shell must be a struct) and the merged
    // result must be non-executable.
    ir::AhflIr program;

    ir::EnumDecl mode;
    mode.name = "Mode";
    mode.symbol_ref.kind = ir::SymbolRefKind::Type;
    mode.symbol_ref.canonical_name = "app::Mode";
    mode.symbol_ref.id = 70;
    mode.variants.push_back(ir::EnumVariantDecl{"A", ir::EnumVariantPayloadKind::Unit, {}, {}, {}});
    mode.variants.push_back(ir::EnumVariantDecl{"B", ir::EnumVariantPayloadKind::Unit, {}, {}, {}});
    program.declarations.emplace_back(std::move(mode));

    ir::StructDecl reply;
    reply.name = "Reply";
    reply.symbol_ref.kind = ir::SymbolRefKind::Type;
    reply.symbol_ref.canonical_name = "app::Reply";
    reply.symbol_ref.id = 71;
    program.declarations.emplace_back(std::move(reply));

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    agent.input_type_ref.kind = ir::TypeRefKind::Enum; // <-- not a struct
    agent.input_type_ref.canonical_name = "app::Mode";
    agent.output_type_ref.kind = ir::TypeRefKind::Struct;
    agent.output_type_ref.canonical_name = "app::Reply";
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    ir::StateHandler handler;
    handler.state_name = "S";
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    CHECK(has_verify_code(result)); // the verifier's TYPED_SHELL_INVALID merged in
}

TEST_CASE("a lowering-error program is NOT re-verified (no duplicate core.verify.* noise)") {
    // A flow that calls an unresolved capability -> lowering Error. Because the
    // program is already non-executable, the verifier must NOT run, so no
    // core.verify.* diagnostics appear.
    ir::AhflIr program;

    ir::StructDecl req;
    req.name = "Req";
    req.symbol_ref.kind = ir::SymbolRefKind::Type;
    req.symbol_ref.canonical_name = "app::Req";
    req.symbol_ref.id = 80;
    program.declarations.emplace_back(std::move(req));

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    agent.input_type_ref.kind = ir::TypeRefKind::Struct;
    agent.input_type_ref.canonical_name = "app::Req";
    agent.output_type_ref.kind = ir::TypeRefKind::Struct;
    agent.output_type_ref.canonical_name = "app::Req";
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    // return Ghost(); -- unresolved capability callee
    ir::CallExpr call;
    call.callee = "Ghost";
    call.callee_ref.kind = ir::SymbolRefKind::Capability;
    call.callee_ref.canonical_name = "app::Ghost";
    ir::ExprRef cref = program.expr_arena.make(std::move(call), SourceRange{4, 9});
    auto stmt = std::make_unique<ir::Statement>();
    stmt->node = ir::ExprStatement{cref};
    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(stmt));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.is_executable);
    CHECK(has_verify_code(result) == false); // verifier skipped on a partial program
}

TEST_CASE("enum struct-payload construct with a missing slot fails closed in the verifier (2b)") {
    // Hand-built AHFL-IR: an Open{ id } literal with NO owner (as if a
    // deserialized IR dropped the materialized default). The Core lowerer must
    // NOT re-materialize; the verifier reports the missing struct-payload slot.
    ir::AhflIr program;

    ir::EnumDecl ticket;
    ticket.name = "Ticket";
    ticket.symbol_ref.kind = ir::SymbolRefKind::Type;
    ticket.symbol_ref.canonical_name = "app::Ticket";
    ticket.symbol_ref.id = 90;
    ir::EnumVariantDecl open;
    open.name = "Open";
    open.payload_kind = ir::EnumVariantPayloadKind::Struct;
    ir::EnumVariantFieldDecl f_id;
    f_id.name = "id";
    f_id.type_ref.kind = ir::TypeRefKind::Int;
    ir::EnumVariantFieldDecl f_owner;
    f_owner.name = "owner";
    f_owner.type_ref.kind = ir::TypeRefKind::String;
    open.fields.push_back(std::move(f_id));
    open.fields.push_back(std::move(f_owner));
    ticket.variants.push_back(std::move(open));
    program.declarations.emplace_back(std::move(ticket));

    ir::AgentDecl agent;
    agent.name = "A";
    agent.symbol_ref.kind = ir::SymbolRefKind::Agent;
    agent.symbol_ref.canonical_name = "app::A";
    agent.symbol_ref.id = 1;
    agent.states = {"S"};
    agent.initial_state = "S";
    agent.final_states = {"S"};
    give_shell_structs(program, agent, "Mv");
    program.declarations.emplace_back(std::move(agent));

    ir::FlowDecl flow;
    flow.target_ref.kind = ir::SymbolRefKind::Agent;
    flow.target_ref.canonical_name = "app::A";
    flow.target_ref.local_name = "A";
    flow.target_ref.id = 1;
    // let t = Ticket::Open { id: 1 };  (owner deliberately omitted, not materialized)
    ir::StructLiteralExpr lit;
    lit.is_enum_variant = true;
    lit.enum_name = "app::Ticket";
    lit.variant_name = "Open";
    lit.type_name = "app::Ticket::Open";
    ir::ExprRef id_val = program.expr_arena.make(ir::IntegerLiteralExpr{"1"});
    lit.fields.push_back(ir::StructFieldInit{"id", id_val});
    ir::ExprRef lit_ref = program.expr_arena.make(std::move(lit), SourceRange{5, 9});
    auto let = std::make_unique<ir::Statement>();
    ir::LetStatement let_stmt;
    let_stmt.name = "t";
    let_stmt.type_ref.kind = ir::TypeRefKind::Enum;
    let_stmt.type_ref.canonical_name = "app::Ticket";
    let_stmt.initializer = lit_ref;
    let->node = std::move(let_stmt);
    ir::StateHandler handler;
    handler.state_name = "S";
    handler.body.statements.push_back(std::move(let));
    flow.state_handlers.push_back(std::move(handler));
    program.declarations.emplace_back(std::move(flow));

    const auto result = ir::core::lower_ahfl_to_core(program);
    CHECK_FALSE(result.ok());
    CHECK(has_verify_code(result)); // missing struct-payload slot caught by verifier
}

// ---------------------------------------------------------------------------
// (3)-3b typed pattern identity bridge: after lowering a real match program to
// AHFL-IR, each variant pattern carries its owner-enum SymbolRef + declaration-
// stable variant name (not just the `path` spelling), and EVERY pattern carries
// the matched-enum fact from the scrutinee's type. This is what lets (3)-3c
// resolve variant identity by symbol and turn a literal `none` into Option::None.
// ---------------------------------------------------------------------------
namespace {

// The first MatchExpr reachable in the AHFL-IR expression arena, or nullptr.
const ir::MatchExpr *find_match_expr(const ir::AhflIr &program) {
    for (const ir::Expr *expr : program.all_exprs()) {
        if (expr == nullptr) {
            continue;
        }
        if (const auto *m = std::get_if<ir::MatchExpr>(&expr->node)) {
            return m;
        }
    }
    return nullptr;
}

const ir::VariantPattern *variant_of(const ir::MatchPattern &pattern) {
    return std::get_if<ir::VariantPattern>(&pattern.node);
}

} // namespace

TEST_CASE("(3)-3b: variant patterns carry typed owner-enum identity + variant name") {
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let m: Maybe = Maybe::Some(input.id);
        let r: Int = match m {
            Some(x) => x,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    const auto program = lower_source_to_ahfl_ir("match_identity", source);
    REQUIRE(program.has_value());
    const ir::MatchExpr *match = find_match_expr(*program);
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 2);

    // Arm 0: Some(x) — tuple-payload variant. Owner enum resolved by symbol,
    // variant name declaration-stable, matched-enum fact present.
    const ir::VariantPattern *some = variant_of(match->arms[0].pattern);
    REQUIRE(some != nullptr);
    CHECK(some->variant_name == "Some");
    CHECK(some->owner_enum.kind == ir::SymbolRefKind::Type);
    CHECK(some->owner_enum.canonical_name.find("Maybe") != std::string::npos);
    CHECK(match->arms[0].pattern.matched_enum.canonical_name.find("Maybe") != std::string::npos);

    // Arm 1: None — unit variant. Same typed identity.
    const ir::VariantPattern *none = variant_of(match->arms[1].pattern);
    REQUIRE(none != nullptr);
    CHECK(none->variant_name == "None");
    CHECK(none->owner_enum.kind == ir::SymbolRefKind::Type);
    CHECK(none->owner_enum.canonical_name.find("Maybe") != std::string::npos);
}

TEST_CASE("(3)-3b: a non-variant (wildcard) arm still carries the matched-enum fact") {
    // A wildcard / literal arm is NOT a variant, so it has no owner_enum of its
    // own; its enum identity comes from the scrutinee's matched type. The
    // matched_enum fact must be set on such a pattern so (3)-3c can still resolve
    // the scrutinee enum (the same mechanism a literal `none` relies on).
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Colour { Red, Green, Blue, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let c: Colour = Colour::Red;
        let r: Int = match c {
            Red => 1,
            _ => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    const auto program = lower_source_to_ahfl_ir("match_wildcard_identity", source);
    REQUIRE(program.has_value());
    const ir::MatchExpr *match = find_match_expr(*program);
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 2);
    // Arm 1 is the wildcard `_` — a non-variant pattern that must still know its
    // matched enum by symbol identity.
    const ir::MatchPattern &wild = match->arms[1].pattern;
    CHECK(std::holds_alternative<ir::WildcardPattern>(wild.node));
    CHECK(wild.matched_enum.kind == ir::SymbolRefKind::Type);
    CHECK(wild.matched_enum.canonical_name.find("Colour") != std::string::npos);
}

TEST_CASE("(3)-3b: variant pattern identity survives IR JSON round-trip byte-exact") {
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let m: Maybe = Maybe::Some(input.id);
        let r: Int = match m {
            Some(x) => x,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    const auto program = lower_source_to_ahfl_ir("match_roundtrip", source);
    REQUIRE(program.has_value());

    std::ostringstream first;
    ahfl::print_program_ir_json(*program, first);
    const std::string json1 = first.str();

    const auto parsed = ahfl::parse_program_ir_json(json1);
    REQUIRE(parsed.has_value());

    std::ostringstream second;
    ahfl::print_program_ir_json(*parsed, second);
    const std::string json2 = second.str();

    // Byte-exact: print -> parse -> print reproduces the JSON verbatim, so the
    // owner_enum SymbolRef + variant_name + matched_enum fields survive.
    CHECK(json1 == json2);

    // And the parsed-back program still carries typed variant identity.
    const ir::MatchExpr *match = find_match_expr(*parsed);
    REQUIRE(match != nullptr);
    REQUIRE(match->arms.size() == 2);
    const ir::VariantPattern *some = variant_of(match->arms[0].pattern);
    REQUIRE(some != nullptr);
    CHECK(some->variant_name == "Some");
    CHECK(some->owner_enum.canonical_name.find("Maybe") != std::string::npos);
    CHECK(match->arms[0].pattern.matched_enum.canonical_name.find("Maybe") != std::string::npos);
}

// ---------------------------------------------------------------------------
// (3)-3b forward-fix P0-1 / P0-2: the typed pattern identity bridge must FAIL
// CLOSED when the typed pattern index / child is missing or misaligned. These
// drive the real lowerer with a surgically-corrupted TypedProgram and assert it
// throws rather than silently degrading to AST-derived identity.
// ---------------------------------------------------------------------------
namespace {

struct FrontendState {
    ahfl::ParseResult parse;
    std::optional<ahfl::ResolveResult> resolve;
    std::optional<ahfl::TypeCheckResult> typecheck;
    [[nodiscard]] bool ok() const {
        return typecheck.has_value() && !typecheck->has_errors();
    }
};

// Parse + resolve + typecheck a source, RETAINING all state so a test can mutate
// the TypedProgram before lowering. Returns nullopt if any stage errors.
std::optional<FrontendState> frontend_state(const std::string &label, const std::string &source) {
    FrontendState st;
    const Frontend frontend;
    st.parse = frontend.parse_text(label + ".ahfl", source);
    if (st.parse.has_errors() || st.parse.program == nullptr) {
        return std::nullopt;
    }
    const Resolver resolver;
    st.resolve = resolver.resolve(*st.parse.program);
    if (st.resolve->has_errors()) {
        return std::nullopt;
    }
    const TypeChecker checker;
    st.typecheck = checker.check(*st.parse.program, *st.resolve);
    if (st.typecheck->has_errors()) {
        return std::nullopt;
    }
    return st;
}

// Index of the first Match expression in the typed program (UINT32_MAX if none).
std::uint32_t find_typed_match(const ahfl::TypedProgram &tp) {
    for (std::uint32_t i = 0; i < tp.expressions.size(); ++i) {
        if (tp.expressions[i].kind == ahfl::ast::ExprSyntaxKind::Match) {
            return i;
        }
    }
    return UINT32_MAX;
}

const std::string kMatchProgram = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let m: Maybe = Maybe::Some(input.id);
        let r: Int = match m {
            Some(x) => x,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";

} // namespace

TEST_CASE("(3)-3b P0-1: match with EMPTY pattern-index list fails closed (no AST fallback)") {
    auto st = frontend_state("empty_idx", kMatchProgram);
    REQUIRE(st.has_value());
    const auto mi = find_typed_match(st->typecheck->typed_program);
    REQUIRE(mi != UINT32_MAX);
    // Corrupt: drop the persisted arm pattern indexes entirely.
    st->typecheck->typed_program.expressions[mi].match_arm_pattern_indexes.clear();
    CHECK_THROWS_AS(
        static_cast<void>(ahfl::lower_typed_program(st->typecheck->typed_program, *st->parse.program)),
        std::logic_error);
}

TEST_CASE("(3)-3b P0-1: match with a SHORT pattern-index list fails closed") {
    auto st = frontend_state("short_idx", kMatchProgram);
    REQUIRE(st.has_value());
    const auto mi = find_typed_match(st->typecheck->typed_program);
    REQUIRE(mi != UINT32_MAX);
    auto &idxs = st->typecheck->typed_program.expressions[mi].match_arm_pattern_indexes;
    REQUIRE(idxs.size() == 2);
    idxs.pop_back(); // one fewer than the 2 arms
    CHECK_THROWS_AS(
        static_cast<void>(ahfl::lower_typed_program(st->typecheck->typed_program, *st->parse.program)),
        std::logic_error);
}

TEST_CASE("(3)-3b P0-1: match with an EXTRA pattern-index entry fails closed") {
    auto st = frontend_state("long_idx", kMatchProgram);
    REQUIRE(st.has_value());
    const auto mi = find_typed_match(st->typecheck->typed_program);
    REQUIRE(mi != UINT32_MAX);
    auto &idxs = st->typecheck->typed_program.expressions[mi].match_arm_pattern_indexes;
    idxs.push_back(idxs.front()); // one more than the 2 arms
    CHECK_THROWS_AS(
        static_cast<void>(ahfl::lower_typed_program(st->typecheck->typed_program, *st->parse.program)),
        std::logic_error);
}

TEST_CASE("(3)-3b P0-1: match with an OUT-OF-RANGE pattern index fails closed") {
    auto st = frontend_state("oor_idx", kMatchProgram);
    REQUIRE(st.has_value());
    const auto mi = find_typed_match(st->typecheck->typed_program);
    REQUIRE(mi != UINT32_MAX);
    auto &idxs = st->typecheck->typed_program.expressions[mi].match_arm_pattern_indexes;
    REQUIRE_FALSE(idxs.empty());
    idxs[0] = 999999; // out of TypedProgram::patterns range
    CHECK_THROWS_AS(
        static_cast<void>(ahfl::lower_typed_program(st->typecheck->typed_program, *st->parse.program)),
        std::logic_error);
}

TEST_CASE("(3)-3b P0-2: deleting a nested TypedPatternChild fails closed") {
    // Some(x) is a tuple-payload variant with one child (the `x` binding). Drop
    // the child so the recursive lower_pattern cannot find its typed sub-pattern.
    auto st = frontend_state("del_child", kMatchProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    // Find the Some variant pattern (has >= 1 child) and clear its children.
    bool corrupted = false;
    for (auto &pat : tp.patterns) {
        if (pat.kind == ahfl::TypedPatternKind::Variant && !pat.children.empty()) {
            pat.children.clear();
            corrupted = true;
            break;
        }
    }
    REQUIRE(corrupted);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

TEST_CASE("(3)-3b P0-1: AST variant vs typed Wildcard kind mismatch fails closed") {
    // Corrupt a variant arm's TypedPattern to Wildcard: the AST node is still a
    // VariantPattern, so the AST-vs-typed kind check must fail closed (never
    // lower an AST variant shape with a non-variant typed identity).
    auto st = frontend_state("kind_mismatch_v", kMatchProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    bool corrupted = false;
    for (auto &pat : tp.patterns) {
        if (pat.kind == ahfl::TypedPatternKind::Variant) {
            pat.kind = ahfl::TypedPatternKind::Wildcard;
            corrupted = true;
            break;
        }
    }
    REQUIRE(corrupted);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

TEST_CASE("(3)-3b P0-1: AST variant vs typed Literal kind mismatch fails closed") {
    // A different mismatch: an AST VariantPattern resolved (corrupted) to a
    // Literal typed kind. This is NOT the legal binding->unit-variant divergence.
    auto st = frontend_state("kind_mismatch_l", kMatchProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    bool corrupted = false;
    for (auto &pat : tp.patterns) {
        if (pat.kind == ahfl::TypedPatternKind::Variant) {
            pat.kind = ahfl::TypedPatternKind::Literal;
            corrupted = true;
            break;
        }
    }
    REQUIRE(corrupted);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

// --- (3)-3b round-3: guard existence must agree with the AST arm ---

// A match with a GUARDED first arm (Some(x) if <guard> => ...). Used to prove a
// deleted guard child fails closed.
const std::string kGuardedMatchProgram = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        let r: Int = match mm {
            Some(x) if x > 0 => x,
            Some(y) => y,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";

TEST_CASE("(3)-3b P0-1: deleting a real MatchArmGuard child fails closed") {
    auto st = frontend_state("del_guard", kGuardedMatchProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    const auto mi = find_typed_match(tp);
    REQUIRE(mi != UINT32_MAX);
    // Drop the FIRST MatchArmGuard child: the AST arm still has an `if` guard, so
    // the guard-existence check must reject the mismatch.
    auto &children = tp.expressions[mi].children;
    bool removed = false;
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (it->role == ahfl::TypedExprChildRole::MatchArmGuard) {
            children.erase(it);
            removed = true;
            break;
        }
    }
    REQUIRE(removed);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

TEST_CASE("(3)-3b P0-1: injecting a guard child on a guardless arm fails closed") {
    auto st = frontend_state("inject_guard", kMatchProgram); // no guards in source
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    const auto mi = find_typed_match(tp);
    REQUIRE(mi != UINT32_MAX);
    // Inject a spurious MatchArmGuard child before the first body child, reusing
    // an existing body child's expr index. The AST arm has no guard, so this must
    // be rejected.
    auto &children = tp.expressions[mi].children;
    std::uint32_t some_body_expr = UINT32_MAX;
    for (const auto &c : children) {
        if (c.role == ahfl::TypedExprChildRole::MatchArmBody) {
            some_body_expr = c.expr_index;
            break;
        }
    }
    REQUIRE(some_body_expr != UINT32_MAX);
    ahfl::TypedExprChild guard_child;
    guard_child.role = ahfl::TypedExprChildRole::MatchArmGuard;
    guard_child.expr_index = some_body_expr;
    children.insert(children.begin(), guard_child);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

// --- (3)-3b round-3 P0-2: if-let must resolve its AST pattern or fail closed ---

const std::string kIfLetProgram = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { total: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        if let Some(x) = mm {
            ctx.total = x;
        }
        return Resp { id: ctx.total };
    }
}
)AHFL";

TEST_CASE("(3)-3b P0-2: an if-let whose AST lookup fails does NOT degrade to wildcard") {
    auto st = frontend_state("iflet_lookup", kIfLetProgram);
    REQUIRE(st.has_value());
    auto &tp = st->typecheck->typed_program;
    // Corrupt the if-let TypedStatement's range so find_ast_if_let_stmt (which
    // matches by range) can no longer locate the AST statement. The lowerer must
    // throw rather than lower the pattern as a wildcard.
    bool corrupted = false;
    for (auto &stmt : tp.statements) {
        if (stmt.kind == ahfl::TypedStmtKind::IfLet) {
            stmt.range = ahfl::SourceRange{999990, 999999};
            corrupted = true;
            break;
        }
    }
    REQUIRE(corrupted);
    CHECK_THROWS_AS(static_cast<void>(ahfl::lower_typed_program(tp, *st->parse.program)), std::logic_error);
}

// ---------------------------------------------------------------------------
// (3)-3c CoreMatchStmt lowering: a real match / if-let program lowers into a
// structured CoreMatchStmt (scrutinee + arms with typed patterns + fallback),
// resolving variant identity by owner_enum symbol. The auto-wired Core verifier
// accepting the program (result.ok()) already proves the (3)-2 invariants hold.
// ---------------------------------------------------------------------------
namespace {

// The first CoreMatchStmt reachable in a flow state body (top-level only).
const ir::core::CoreMatchStmt *find_core_match(const ir::core::CoreProgram &program) {
    for (const auto &flow : program.flows) {
        for (const auto &state : flow.states) {
            for (const auto &stmt : state.body.statements) {
                if (const auto *m = std::get_if<ir::core::CoreMatchStmt>(&stmt.node)) {
                    return m;
                }
            }
        }
    }
    return nullptr;
}

bool region_ends_with_trap(const ir::core::CoreRegion *region) {
    if (region == nullptr || region->statements.empty()) {
        return false;
    }
    return std::holds_alternative<ir::core::CoreTrapStmt>(region->statements.back().node);
}

} // namespace

TEST_CASE("(3)-3c: an expression match lowers into a CoreMatchStmt with typed arms + trap fallback") {
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        let r: Int = match mm {
            Some(x) => x,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("core_match_expr", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    const ir::core::CoreMatchStmt *m = find_core_match(result.program);
    REQUIRE(m != nullptr);
    // Expression match: defines a result value in the parent scope.
    CHECK(m->has_result);
    REQUIRE(m->arms.size() == 2);
    // Fallback is a mandatory trap (non-exhaustive by construction).
    CHECK(region_ends_with_trap(m->fallback_region.get()));

    // Arm 0: Some(x) - a variant pattern with one tuple sub-pattern binding, one
    // arm binding (x). Arm 1: None - a unit variant, no bindings.
    const auto &pats = result.program.flows[0].patterns;
    const auto &arm0 = m->arms[0];
    REQUIRE(arm0.pattern.value < pats.size());
    const auto *v0 = std::get_if<ir::core::CoreVariantPat>(&pats[arm0.pattern.value].node);
    REQUIRE(v0 != nullptr);
    CHECK(v0->tuple_subpatterns.size() == 1);
    CHECK(arm0.bindings.size() == 1); // x
    const auto &arm1 = m->arms[1];
    REQUIRE(arm1.pattern.value < pats.size());
    const auto *v1 = std::get_if<ir::core::CoreVariantPat>(&pats[arm1.pattern.value].node);
    REQUIRE(v1 != nullptr);
    CHECK(v1->tuple_subpatterns.empty());
    CHECK(arm1.bindings.empty());
    // The two arms name distinct variants of the same enum.
    CHECK(v0->owner_enum.value == v1->owner_enum.value);
    CHECK(v0->variant.value != v1->variant.value);
}

TEST_CASE("(3)-3c: an if-let lowers into a statement CoreMatchStmt with an else fallback") {
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { total: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        let out: Int = 0;
        if let Some(x) = mm {
            ctx.total = x;
        } else {
            ctx.total = 0;
        }
        return Resp { id: ctx.total };
    }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("core_if_let", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    const ir::core::CoreMatchStmt *m = find_core_match(result.program);
    REQUIRE(m != nullptr);
    // Statement match: no result value.
    CHECK_FALSE(m->has_result);
    REQUIRE(m->arms.size() == 1);
    CHECK(m->arms[0].bindings.size() == 1); // x
    // The fallback is the `else` block (a store), NOT a trap.
    REQUIRE(m->fallback_region);
    CHECK_FALSE(region_ends_with_trap(m->fallback_region.get()));
}

// (3)-3c forward-fix P0-1: an if-let's ELSE branch is lexically scoped exactly
// like its then branch — a branch-local `let` that SHADOWS an outer local must
// NOT leak past the if-let. Previously the else used a bare lower_block with no
// scope snapshot/restore, so the else-local shadow overwrote scope_ and a later
// reference to the OUTER local resolved to the else-local's value id. That value
// id is defined only inside the else region (the verifier's per-branch `visible`
// set never propagates it to the parent), so the leak surfaces as a
// use-before-def and the program is non-executable. The fix restores the outer
// scope around BOTH branches, so the trailing reference resolves to the OUTER
// binding and the program lowers clean.
TEST_CASE("(3)-3c FF P0-1: an if-let else-branch shadow does not leak past the if-let") {
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { total: Int = 0; done: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        let x: Int = input.id;
        if let Some(y) = mm {
            ctx.total = y;
        } else {
            let x: Int = 999;
            ctx.total = x;
        }
        ctx.done = x;
        return Resp { id: ctx.done };
    }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("core_iflet_else_shadow", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    // With the leak, `ctx.done = x` would use the else-local value id (not
    // visible in the parent) -> use-before-def -> not ok / not executable.
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    // Structural: `ctx.done = x` after the if-let stores the OUTER `x` — a value
    // defined at the TOP LEVEL before the match — never a value defined only
    // inside the else region (which would be the leaked shadow). (is_executable
    // already excludes the leak via use-before-def; this pins WHICH value flows.)
    const ir::core::CoreFlowState *state = nullptr;
    for (const auto &flow : result.program.flows) {
        for (const auto &st : flow.states) {
            for (const auto &stmt : st.body.statements) {
                if (std::holds_alternative<ir::core::CoreMatchStmt>(stmt.node)) {
                    state = &st;
                }
            }
        }
    }
    REQUIRE(state != nullptr);
    const auto &stmts = state->body.statements;

    // Value ids defined at the top level BEFORE the match (the parent scope).
    std::unordered_set<std::uint32_t> pre_match_defs;
    const ir::core::CoreStoreStmt *done_store = nullptr;
    const ir::core::CoreMatchStmt *iflet = nullptr;
    for (const auto &stmt : stmts) {
        if (const auto *m = std::get_if<ir::core::CoreMatchStmt>(&stmt.node)) {
            iflet = m;
            continue;
        }
        if (iflet == nullptr) {
            if (const auto *l = std::get_if<ir::core::CoreLetStmt>(&stmt.node)) {
                pre_match_defs.insert(l->result.value);
            } else if (const auto *c =
                           std::get_if<ir::core::CoreCapabilityCallStmt>(&stmt.node)) {
                pre_match_defs.insert(c->result.value);
            }
        } else if (const auto *s = std::get_if<ir::core::CoreStoreStmt>(&stmt.node)) {
            done_store = s; // first store AFTER the match is `ctx.done = x`
            break;
        }
    }
    REQUIRE(iflet != nullptr);
    REQUIRE(done_store != nullptr);
    // The stored value is an outer, pre-match binding — not the else-local shadow.
    CHECK(pre_match_defs.count(done_store->value.value) == 1);
}

// --- (3)-3c forward-fix: payload binding types, none->variant, path fallthrough ---

TEST_CASE("(3)-3c FF P0-1: a struct-payload binding carries its type so u.field resolves") {
    // enum with a struct-typed payload; the arm binds `u: User` and projects
    // `u.id`. Without the binding's typed CoreTypeId this fails with
    // UNLOWERED_FIELD_PROJECTION.
    const std::string source = R"AHFL(
module m;

struct User { id: Int; }
struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum MaybeUser { Some(User), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mu: MaybeUser = MaybeUser::Some(User { id: input.id });
        let r: Int = match mu {
            Some(u) => u.id,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("core_struct_binding", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok()); // u.id member projection resolved via the binding type
    CHECK(result.is_executable);
    const ir::core::CoreMatchStmt *m = find_core_match(result.program);
    REQUIRE(m != nullptr);
    REQUIRE(m->arms.size() == 2);
    // The Some(u) arm's binding carries a nominal (non-kInvalid) type.
    REQUIRE(m->arms[0].bindings.size() == 1);
    CHECK(m->arms[0].bindings[0].binding_type.value != ir::core::CoreTypeId::kInvalid);
}

TEST_CASE("(3)-3c FF P0-3: a match arm whose both if-branches terminate needs no trailing yield") {
    // An if-let then-block whose only statement is an if/else where BOTH branches
    // goto must NOT get a trailing Yield (that would be a stmt-after-terminator).
    // result.ok() proves the auto-wired verifier accepts it (no STMT_AFTER_TERMINATOR).
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Start, Left, Right];
    initial: Start;
    final: [Left, Right];
    capabilities: [];

    transition Start -> Left;
    transition Start -> Right;
}

flow for A {
    state Start {
        let mm: Maybe = Maybe::Some(input.id);
        if let Some(x) = mm {
            if x > 0 { goto Left; } else { goto Right; }
        } else {
            goto Right;
        }
    }
    state Left { return Resp { id: 1 }; }
    state Right { return Resp { id: 2 }; }
}
)AHFL";
    const auto ahfl_ir = lower_source_to_ahfl_ir("core_both_terminate", source);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    INFO("diag: " << (result.diagnostics.empty() ? "none" : result.diagnostics[0].message));
    REQUIRE(result.ok()); // no STMT_AFTER_TERMINATOR: the then-arm did NOT get a trailing yield
    CHECK(result.is_executable);
}

TEST_CASE("(3)-3c FF P1: a variant pattern whose source shape disagrees with metadata fails closed") {
    // Lower a real match to AHFL-IR, then corrupt the `Some(x)` arm's variant
    // pattern kind from Tuple to Unit (a shape that disagrees with the declared
    // tuple payload). The Core lowerer must fail closed, not normalize it.
    const std::string source = R"AHFL(
module m;

struct Req { id: Int; }
struct Ctx { seen: Int = 0; }
struct Resp { id: Int; }

enum Maybe { Some(Int), None, }

agent A {
    input: Req;
    context: Ctx;
    output: Resp;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

flow for A {
    state Done {
        let mm: Maybe = Maybe::Some(input.id);
        let r: Int = match mm {
            Some(x) => x,
            None => 0,
        };
        return Resp { id: r };
    }
}
)AHFL";
    auto ahfl_ir = lower_source_to_ahfl_ir("core_shape_mismatch", source);
    REQUIRE(ahfl_ir.has_value());
    // Find the MatchExpr (non-const) and corrupt the first variant arm's kind.
    bool corrupted = false;
    for (ir::Expr *expr : ahfl_ir->all_exprs()) {
        if (expr == nullptr) {
            continue;
        }
        if (auto *mtch = std::get_if<ir::MatchExpr>(&expr->node)) {
            for (auto &arm : mtch->arms) {
                if (auto *v = std::get_if<ir::VariantPattern>(&arm.pattern.node);
                    v != nullptr && v->kind == ir::VariantPatternKind::Tuple) {
                    v->kind = ir::VariantPatternKind::Unit; // disagrees with metadata
                    corrupted = true;
                    break;
                }
            }
        }
        if (corrupted) {
            break;
        }
    }
    REQUIRE(corrupted);
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    CHECK_FALSE(result.ok()); // shape mismatch is fail-closed, not normalized
}

// --- KR6.4 workflow lowering (RFC 0026 P3 workflow vertical slice) ---

namespace {
// A self-contained multi-node DAG workflow: two agents, a workflow whose second
// node depends on the first and reads its output, and a return that reads the
// last node. Mirrors the shape of examples/execution-demo (input root, node.field
// projection, struct-literal node input, `after` deps, bare-node return).
const std::string kWorkflowSource = R"AHFL(
module wf;

struct Req { amount: Int; }
struct Mid { total: Int; }
struct Reply { ok: Bool; }

agent First {
    input: Req;
    output: Mid;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

agent Second {
    input: Mid;
    output: Reply;
    states: [Done];
    initial: Done;
    final: [Done];
    capabilities: [];
}

workflow Pipe {
    input: Req;
    output: Reply;

    node first: First(input);
    node second: Second(Mid { total: first.total }) after [first];

    return: second;
}
)AHFL";

const ir::core::CoreWorkflowDecl *find_workflow(const ir::core::CoreProgram &program,
                                                const std::string &name) {
    for (const auto &wf : program.workflows) {
        // The lowered name is the canonical name (e.g. "wf::Pipe"); match by the
        // trailing simple name so the test is module-path agnostic.
        if (wf.name == name || (wf.name.size() > name.size() &&
                                wf.name.compare(wf.name.size() - name.size() - 2, 2, "::") == 0 &&
                                wf.name.compare(wf.name.size() - name.size(), name.size(), name) ==
                                    0)) {
            return &wf;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("KR6.4 workflow: a multi-node DAG lowers into a CoreWorkflowDecl and verifies") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("wf_pipe", kWorkflowSource);
    REQUIRE(ahfl_ir.has_value());
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &d : result.diagnostics) {
        INFO("unexpected core diagnostic: " << d.code << " — " << d.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    CHECK(result.is_executable);

    const ir::core::CoreWorkflowDecl *wf = find_workflow(result.program, "Pipe");
    REQUIRE(wf != nullptr);
    // Typed shell resolved to structs.
    CHECK(wf->input_type.value != ir::core::CoreTypeId::kInvalid);
    CHECK(wf->output_type.value != ir::core::CoreTypeId::kInvalid);
    // Two nodes, ids == declaration index.
    REQUIRE(wf->nodes.size() == 2);
    CHECK(wf->nodes[0].id.value == 0u);
    CHECK(wf->nodes[1].id.value == 1u);
    CHECK(wf->nodes[0].node_name == "first");
    CHECK(wf->nodes[1].node_name == "second");
    // `second after [first]` resolved to a typed node id (not a name string).
    REQUIRE(wf->nodes[1].after.size() == 1);
    CHECK(wf->nodes[1].after[0].value == 0u);
    CHECK(wf->nodes[0].after.empty());
    // Every node has an ANF input region ending in a value-yield; return region too.
    REQUIRE(wf->nodes[0].input_region);
    REQUIRE(wf->nodes[1].input_region);
    REQUIRE(wf->return_region);
    const auto ends_in_value_yield = [](const ir::core::CoreRegion &r) {
        if (r.statements.empty()) {
            return false;
        }
        const auto *y = std::get_if<ir::core::CoreYieldStmt>(&r.statements.back().node);
        return y != nullptr && y->has_value;
    };
    CHECK(ends_in_value_yield(*wf->nodes[0].input_region));
    CHECK(ends_in_value_yield(*wf->nodes[1].input_region));
    CHECK(ends_in_value_yield(*wf->return_region));

    // The node-output reads resolved to WorkflowNodeOutput path roots carrying the
    // typed producer node id: `first.total` in node `second`'s input reads node 0
    // (with a resolved `.total` projection); `return: second` reads node 1 (bare).
    bool saw_first_total = false;
    bool saw_return_second = false;
    for (const auto &expr : wf->exprs) {
        if (const auto *p = std::get_if<ir::core::CorePathExpr>(&expr.node)) {
            if (p->root != ir::core::CorePathRoot::WorkflowNodeOutput) {
                continue;
            }
            if (p->workflow_node.value == 0u && !p->projection.empty()) {
                saw_first_total = true;
                CHECK(p->projection_resolved); // `.total` resolved to a typed step
            }
            if (p->workflow_node.value == 1u) {
                saw_return_second = true; // bare `second` node reference
            }
        }
    }
    CHECK(saw_first_total);
    CHECK(saw_return_second);

    // Each workflow node is LINKED to a concrete agent instance: its
    // target_instance resolves to a CoreAgentInstance whose base + output type
    // match the nominal target agent. (The instance registry + invocation link
    // pass consumed the frontend's ir::InstanceDecls; nothing re-mangles here.)
    for (const auto &node : wf->nodes) {
        REQUIRE(node.target_instance.value != ir::core::CoreInstanceId::kInvalid);
        REQUIRE(node.target_instance.value < result.program.instances.size());
        const auto &inst = result.program.instances[node.target_instance.value];
        const auto *ai = std::get_if<ir::core::CoreAgentInstance>(&inst.payload);
        REQUIRE(ai != nullptr);
        CHECK(ai->base.value < result.program.agents.size());
        // instance_key came verbatim from the frontend (never empty, never re-mangled).
        CHECK_FALSE(inst.instance_key.empty());
    }
    // node `first` outputs WMid, node `second` outputs WOut (Reply) — the two
    // instances carry distinct output shells.
    const auto &first_ai =
        std::get<ir::core::CoreAgentInstance>(
            result.program.instances[wf->nodes[0].target_instance.value].payload);
    const auto &second_ai =
        std::get<ir::core::CoreAgentInstance>(
            result.program.instances[wf->nodes[1].target_instance.value].payload);
    CHECK(first_ai.output_type.value != second_ai.output_type.value);
    // safety/liveness are erased: the Core workflow has no such field (structural).
}

// --- KR6.4 monomorphization Slice 1: instance registry / dispatch identity ---

namespace {
// A generic `fn id<T>(x: T) -> T` invoked at Int + a workflow whose agents give
// Agent instances. Exercises the Fn-instance + Agent-instance consumption path.
const std::string kGenericInstanceSource = R"AHFL(
module gi;

struct Req { amount: Int; }
struct Reply { ok: Bool; }

fn id<T>(x: T) -> T { return x; }

fn use_it(n: Int) -> Int { return id<Int>(n); }
)AHFL";
} // namespace

TEST_CASE("KR6.4 mono Slice 1: a generic fn instance is consumed into CoreProgram.instances") {
    const auto ahfl_ir = lower_source_to_ahfl_ir("gen_inst", kGenericInstanceSource);
    REQUIRE(ahfl_ir.has_value());
    // Count AHFL-IR InstanceDecls so the test is meaningful even if the frontend
    // emits zero (then the consumption path has nothing to prove and we skip the
    // strong assertions rather than assert a false invariant).
    std::size_t ahfl_fn_instances = 0;
    for (const auto &d : ahfl_ir->declarations) {
        if (const auto *inst = std::get_if<ir::InstanceDecl>(&d)) {
            if (inst->kind == ir::InstanceKind::Fn) {
                ++ahfl_fn_instances;
            }
        }
    }
    // The frontend MUST emit exactly one Fn instance for `id<Int>` — if it ever
    // regressed to zero, the consumption assertions below would pass vacuously.
    REQUIRE(ahfl_fn_instances == 1);
    const auto result = ir::core::lower_ahfl_to_core(*ahfl_ir);
    for (const auto &diag : result.diagnostics) {
        INFO("unexpected core diagnostic: " << diag.code << " — " << diag.message);
        CHECK(false);
    }
    REQUIRE(result.ok());
    // Every AHFL-IR InstanceDecl becomes exactly one CoreInstanceDecl, byte-exact
    // key, and (Fn kind) a CoreFnInstance payload.
    std::size_t core_fn_instances = 0;
    for (const auto &inst : result.program.instances) {
        CHECK_FALSE(inst.instance_key.empty());
        if (std::holds_alternative<ir::core::CoreFnInstance>(inst.payload)) {
            ++core_fn_instances;
        }
    }
    CHECK(core_fn_instances == ahfl_fn_instances);
    // The Core key equals the AHFL InstanceDecl name byte-for-byte (no re-mangle).
    for (const auto &d : ahfl_ir->declarations) {
        if (const auto *inst = std::get_if<ir::InstanceDecl>(&d)) {
            bool found = false;
            for (const auto &ci : result.program.instances) {
                if (ci.instance_key == inst->name) {
                    found = true;
                    break;
                }
            }
            INFO("AHFL instance '" << inst->name << "' has no byte-exact Core instance");
            CHECK(found);
        }
    }
    // P4-A2: the Fn instance's dispatch descriptor is now interned into the
    // program's value_types arena. `id<Int>` dispatches on Int, so at least one
    // dispatch id must resolve to a CoreVtInt, and every dispatch id must be a
    // valid arena index.
    bool saw_int_dispatch = false;
    for (const auto &inst : result.program.instances) {
        if (!std::holds_alternative<ir::core::CoreFnInstance>(inst.payload)) {
            continue;
        }
        for (const auto &vt : inst.dispatch_types) {
            REQUIRE(vt.value < result.program.value_types.size());
            if (std::holds_alternative<ir::core::CoreVtInt>(
                    result.program.value_types[vt.value].node)) {
                saw_int_dispatch = true;
            }
        }
    }
    CHECK(saw_int_dispatch);
}

// --- mono Slice 1 verifier negatives (hand-built Core instance table) ---

TEST_CASE("P4-A2 forward-fix P0-2: a REAL inlined std generic decl is decorated from the SSOT") {
    // A program that INLINES a real std::collections::List struct declaration
    // (as include_stdlib / a deserialized program would) must get role=List +
    // type_param_count=1 via register_type's SSOT decoration — NOT left
    // Ordinary/0 (which would make a legal List<Int> fail "expects 0 args").
    ir::AhflIr program;
    ir::StructDecl list;
    list.name = "List";
    list.symbol_ref.kind = ir::SymbolRefKind::Type;
    list.symbol_ref.canonical_name = "std::collections::List";
    list.symbol_ref.id = 900;
    program.declarations.emplace_back(std::move(list));

    const auto result = ir::core::lower_ahfl_to_core(program);
    const ir::core::CoreTypeDecl *list_decl = nullptr;
    for (const auto &t : result.program.types) {
        if (t.name == "std::collections::List") {
            list_decl = &t;
        }
    }
    REQUIRE(list_decl != nullptr);
    CHECK(list_decl->role == ir::core::CoreNominalRole::List);
    CHECK(list_decl->type_param_count == 1);
    // add_builtins must NOT have added a second synthetic List (real decl wins).
    std::size_t list_count = 0;
    for (const auto &t : result.program.types) {
        if (t.name == "std::collections::List") {
            ++list_count;
        }
    }
    CHECK(list_count == 1);

    // A legal List<Int> now lowers against the decorated real decl. Re-run the
    // whole lowering into a fresh mutable program (CoreProgram is move-only, so
    // we can't copy result.program) and intern into that arena.
    auto fresh = ir::core::lower_ahfl_to_core(program);
    ir::TypeRef list_ref;
    list_ref.kind = ir::TypeRefKind::Struct;
    list_ref.canonical_name = "std::collections::List";
    list_ref.nominal_ref = ir::SymbolRef{.kind = ir::SymbolRefKind::Type,
                                         .canonical_name = "std::collections::List",
                                         .id = std::size_t{900}};
    auto elem = std::make_unique<ir::TypeRef>();
    elem->kind = ir::TypeRefKind::Int;
    list_ref.params.push_back(std::move(elem));
    std::string reason;
    const auto id = ir::core::lower_value_type_into(fresh.program, list_ref, &reason);
    INFO(reason);
    CHECK(id.has_value());
}

TEST_CASE("mono verifier: a duplicate instance key is fail-closed") {
    ir::core::CoreProgram p;
    ir::core::CoreInstanceDecl a;
    a.id = ir::core::CoreInstanceId{0};
    a.instance_key = "_inst_dup";
    a.payload = ir::core::CoreFnInstance{};
    ir::core::CoreInstanceDecl b;
    b.id = ir::core::CoreInstanceId{1};
    b.instance_key = "_inst_dup"; // same key
    b.payload = ir::core::CoreFnInstance{};
    p.instances.push_back(std::move(a));
    p.instances.push_back(std::move(b));
    const auto result = ir::core::verify_core_program(p);
    CHECK_FALSE(result.ok());
    CHECK(has_verify_code(result, ir::core::verify::kInstanceKeyDuplicated));
}

TEST_CASE("mono verifier: an empty instance key is fail-closed") {
    ir::core::CoreProgram p;
    ir::core::CoreInstanceDecl a;
    a.id = ir::core::CoreInstanceId{0};
    a.instance_key = ""; // empty
    a.payload = ir::core::CoreFnInstance{};
    p.instances.push_back(std::move(a));
    const auto result = ir::core::verify_core_program(p);
    CHECK_FALSE(result.ok());
    CHECK(has_verify_code(result, ir::core::verify::kInstanceKeyEmpty));
}

TEST_CASE("mono verifier: an out-of-range dispatch value-type id is fail-closed") {
    // Concreteness is enforced at lowering time (lower_value_type); the verifier's
    // consumer-context check rejects a dispatch id with no arena entry.
    ir::core::CoreProgram p;
    ir::core::CoreInstanceDecl a;
    a.id = ir::core::CoreInstanceId{0};
    a.instance_key = "_inst_bad_dispatch";
    a.dispatch_types.push_back(ir::core::CoreValueTypeId{7}); // empty arena -> out of range
    a.payload = ir::core::CoreFnInstance{};
    p.instances.push_back(std::move(a));
    const auto result = ir::core::verify_core_program(p);
    CHECK_FALSE(result.ok());
    CHECK(has_verify_code(result, ir::core::verify::kInstanceDispatchTypeInvalid));
}

TEST_CASE("mono verifier: an out-of-range instance base is fail-closed") {
    ir::core::CoreProgram p;
    ir::core::CoreInstanceDecl a;
    a.id = ir::core::CoreInstanceId{0};
    a.instance_key = "_inst_bad_base";
    a.payload = ir::core::CoreCapabilityInstance{ir::core::CoreCapabilityId{9}}; // no caps
    p.instances.push_back(std::move(a));
    const auto result = ir::core::verify_core_program(p);
    CHECK_FALSE(result.ok());
    CHECK(has_verify_code(result, ir::core::verify::kInstanceBaseInvalid));
}

