// ---------------------------------------------------------------------------
// Core-IR structural verifier (RFC 0026 P3 / KR6.4)
// ---------------------------------------------------------------------------
//
// See core_verify.hpp for the invariant catalogue. The verifier is pure and
// fail-closed: it never throws and never mutates the program; every violation
// becomes an ERROR diagnostic with a stable `core.verify.*` code and (where a
// node carries one) a source range.

#include "ahfl/compiler/ir/core_verify.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <variant>
#include <vector>

namespace ahfl::ir::core {

namespace {

/// One flow's verification context: the program (for cross-table lookups), the
/// flow being checked, and its resolved target agent (for state bounds).
class Verifier {
  public:
    explicit Verifier(const CoreProgram &program) : program_(program) {}

    [[nodiscard]] std::vector<CoreLowerDiagnostic> run() {
        verify_types();
        for (const CoreAgentDecl &agent : program_.agents) {
            verify_agent(agent);
        }
        for (const CoreCapabilityDecl &cap : program_.capabilities) {
            verify_capability_shell(cap);
        }
        for (const CoreFlowDecl &flow : program_.flows) {
            verify_flow(flow);
        }
        for (std::uint32_t i = 0; i < program_.workflows.size(); ++i) {
            verify_workflow(program_.workflows[i], i);
        }
        verify_instances();
        return std::move(diags_);
    }

  private:
    // --- diagnostics ---
    void error(std::string_view code, std::string message, SourceRangeOpt range) {
        diags_.push_back(CoreLowerDiagnostic{CoreDiagnosticSeverity::Error, std::string(code),
                                             std::move(message), std::move(range)});
    }

    // --- typed-ID bounds predicates ---
    [[nodiscard]] bool type_in_range(CoreTypeId t) const {
        return t.value != CoreTypeId::kInvalid && t.value < program_.types.size();
    }
    [[nodiscard]] bool is_struct(CoreTypeId t) const {
        return type_in_range(t) && program_.types[t.value].kind == CoreTypeDecl::Kind::Struct;
    }
    [[nodiscard]] bool is_enum(CoreTypeId t) const {
        return type_in_range(t) && program_.types[t.value].kind == CoreTypeDecl::Kind::Enum;
    }

    // The CoreAgentInstance a CoreInstanceId resolves to, or nullptr when the id
    // is kInvalid / out of range / not an Agent-kind instance. Lets a workflow
    // node's invocation target resolve to its concrete agent instance.
    [[nodiscard]] const CoreAgentInstance *agent_instance_of(CoreInstanceId id) const {
        if (id.value == CoreInstanceId::kInvalid || id.value >= program_.instances.size()) {
            return nullptr;
        }
        return std::get_if<CoreAgentInstance>(&program_.instances[id.value].payload);
    }

    // --- type table ---
    //
    // The type table is the domain every downstream check (projection, construct,
    // and — next slice — pattern) trusts for field/variant arity. If its own
    // shape is malformed (parallel arrays out of sync, a struct carrying enum
    // metadata, a payload whose kind and vectors disagree), those checks build on
    // sand. So the table's SELF-CONSISTENCY is proven first, fail-closed, with a
    // dedicated code — never conflated with an ID-out-of-range.
    void verify_types() {
        for (std::uint32_t i = 0; i < program_.types.size(); ++i) {
            const CoreTypeDecl &t = program_.types[i];
            const auto shape_error = [&](std::string msg) {
                error(verify::kTypeTableShapeInvalid, "type '" + t.name + "': " + std::move(msg),
                      std::nullopt);
            };
            if (t.kind == CoreTypeDecl::Kind::Struct) {
                // Struct parallel arrays must all match; struct carries NO enum
                // metadata.
                if (t.field_types.size() != t.fields.size()) {
                    shape_error("field_types size (" + std::to_string(t.field_types.size()) +
                                ") != fields size (" + std::to_string(t.fields.size()) + ")");
                }
                if (t.field_has_default.size() != t.fields.size()) {
                    shape_error("field_has_default size (" +
                                std::to_string(t.field_has_default.size()) + ") != fields size (" +
                                std::to_string(t.fields.size()) + ")");
                }
                if (!t.variants.empty() || !t.variant_payloads.empty()) {
                    shape_error("a struct must not carry enum variants/payloads");
                }
            } else { // Enum
                // Enum parallel arrays must match; enum carries NO struct metadata.
                if (t.variant_payloads.size() != t.variants.size()) {
                    shape_error("variant_payloads size (" +
                                std::to_string(t.variant_payloads.size()) + ") != variants size (" +
                                std::to_string(t.variants.size()) + ")");
                }
                if (!t.fields.empty() || !t.field_types.empty() || !t.field_has_default.empty()) {
                    shape_error("an enum must not carry struct field metadata");
                }
                // Each variant payload's kind must agree with its vectors.
                for (std::uint32_t v = 0; v < t.variant_payloads.size(); ++v) {
                    const auto &p = t.variant_payloads[v];
                    using PK = CoreTypeDecl::VariantPayload::Kind;
                    if (p.kind == PK::Unit) {
                        if (!p.slot_types.empty() || !p.field_names.empty()) {
                            shape_error("variant #" + std::to_string(v) +
                                        " is Unit but carries payload slots");
                        }
                    } else if (p.kind == PK::Tuple) {
                        if (!p.field_names.empty()) {
                            shape_error("variant #" + std::to_string(v) +
                                        " is Tuple but carries field names");
                        }
                    } else { // Struct payload
                        if (p.field_names.size() != p.slot_types.size()) {
                            shape_error("variant #" + std::to_string(v) +
                                        " struct payload field_names/slot_types size mismatch");
                        }
                    }
                    // Every non-kInvalid payload slot type id must be in range.
                    for (const CoreTypeId st : p.slot_types) {
                        if (st.value != CoreTypeId::kInvalid && st.value >= program_.types.size()) {
                            shape_error("variant #" + std::to_string(v) +
                                        " payload slot references out-of-range type id " +
                                        std::to_string(st.value));
                        }
                    }
                }
            }
            // Each valid field_types entry must be an in-range type id (it need
            // not be a struct — an enum field type is legal, though it cannot be
            // projected THROUGH; that is enforced at the projection site).
            for (std::uint32_t f = 0; f < t.field_types.size(); ++f) {
                const CoreTypeId ft = t.field_types[f];
                if (ft.value != CoreTypeId::kInvalid && ft.value >= program_.types.size()) {
                    error(verify::kTypeIdOutOfRange,
                          "type '" + t.name + "' field #" + std::to_string(f) +
                              " references out-of-range type id " + std::to_string(ft.value),
                          std::nullopt);
                }
            }
        }
    }

    // --- agent state machine + typed shell ---
    void verify_agent(const CoreAgentDecl &agent) {
        const auto state_count = static_cast<std::uint32_t>(agent.states.size());
        // A runnable agent has at least one state; an empty state table cannot
        // host an initial/final state and is structurally invalid.
        if (state_count == 0) {
            error(verify::kAgentStateInvalid,
                  "agent '" + agent.name + "' declares no states", std::nullopt);
        }
        const auto check_state = [&](CoreStateId s, const char *what) {
            if (s.value >= state_count) {
                error(verify::kStateIdOutOfRange,
                      "agent '" + agent.name + "' " + what + " state id " +
                          std::to_string(s.value) + " is out of range (" +
                          std::to_string(state_count) + " states)",
                      std::nullopt);
            }
        };
        // The initial state is always required (unconditional bounds check).
        check_state(agent.initial, "initial");
        for (const CoreStateId f : agent.finals) {
            check_state(f, "final");
        }
        for (const CoreTransition &tr : agent.transitions) {
            check_state(tr.from, "transition-from");
            check_state(tr.to, "transition-to");
        }
        // Typed shell (Sema schema boundary): input/output MUST be a valid
        // Struct; a kInvalid or non-struct shell is a broken reference, never a
        // legal "absent". Context is Struct when explicit (`has_context`), else
        // the default Unit context — for which the type id MUST stay kInvalid
        // (kInvalid must not double as "valid default" and "broken ref").
        const auto require_struct = [&](CoreTypeId t, const char *what) {
            if (!is_struct(t)) {
                error(verify::kTypedShellInvalid,
                      "agent '" + agent.name + "' " + what +
                          " type must be a valid struct type",
                      std::nullopt);
            }
        };
        require_struct(agent.input_type, "input");
        require_struct(agent.output_type, "output");
        if (agent.context_kind == CoreAgentDecl::ContextKind::Struct) {
            require_struct(agent.context_type, "context");
        } else if (agent.context_type.value != CoreTypeId::kInvalid) {
            error(verify::kTypedShellInvalid,
                  "agent '" + agent.name +
                      "' has a Unit context but its context type id is set",
                  std::nullopt);
        }
    }

    // --- capability import shell ---
    void verify_capability_shell(const CoreCapabilityDecl &) {
        // Param/return TypeRefs are verification-layer clones (already checked
        // upstream); nothing index-based to bound here. Arity is checked at each
        // call site against this signature.
    }

    // --- projection (shared by CorePathExpr reads and CorePlace stores) ---
    void verify_projection(CoreTypeId root_type, const std::vector<CoreProjectionStep> &projection,
                           bool projection_resolved, const std::string &display_root,
                           SourceRangeOpt range) {
        if (projection.empty()) {
            return; // a bare root (no members) has nothing to project
        }
        if (!projection_resolved) {
            error(verify::kProjectionUnresolved,
                  "projection on '" + display_root + "' is unresolved in an executable program",
                  range);
            return;
        }
        // Invariant 1: root_type is a valid struct and equals step[0].owner_type.
        if (!is_struct(root_type)) {
            error(verify::kProjectionOwnerNotStruct,
                  "projection root '" + display_root + "' does not name a struct type", range);
            return;
        }
        if (projection.front().owner_type != root_type) {
            error(verify::kProjectionRootMismatch,
                  "projection on '" + display_root +
                      "': first step owner type does not equal the root type",
                  range);
            return;
        }
        for (std::uint32_t i = 0; i < projection.size(); ++i) {
            const CoreProjectionStep &step = projection[i];
            if (!is_struct(step.owner_type)) {
                error(verify::kProjectionOwnerNotStruct,
                      "projection on '" + display_root + "' step #" + std::to_string(i) +
                          " owner type does not name a struct",
                      range);
                return;
            }
            const CoreTypeDecl &owner = program_.types[step.owner_type.value];
            if (step.field.value >= owner.fields.size()) {
                error(verify::kProjectionFieldInvalid,
                      "projection on '" + display_root + "' step #" + std::to_string(i) +
                          " field id " + std::to_string(step.field.value) +
                          " is out of range for struct '" + owner.name + "'",
                      range);
                return;
            }
            // result_type must match the owner's DECLARED field type (the typed
            // step chain must agree with the type table).
            const CoreTypeId declared = step.field.value < owner.field_types.size()
                                            ? owner.field_types[step.field.value]
                                            : CoreTypeId{};
            if (!(step.result_type == declared)) {
                error(verify::kProjectionDiscontinuity,
                      "projection on '" + display_root + "' step #" + std::to_string(i) +
                          " result type disagrees with the field's declared type",
                      range);
                return;
            }
            const bool is_last = (i + 1 == projection.size());
            if (step.result_type.value == CoreTypeId::kInvalid) {
                // Invariant 3: a primitive/leaf step must be the last.
                if (!is_last) {
                    error(verify::kProjectionPrimitiveNotLast,
                          "projection on '" + display_root + "' step #" + std::to_string(i) +
                              " reaches a non-struct but is not the final step",
                          range);
                    return;
                }
            } else if (!is_last) {
                // Invariant 2: continuity — next step's owner is this result.
                if (projection[i + 1].owner_type != step.result_type) {
                    error(verify::kProjectionDiscontinuity,
                          "projection on '" + display_root + "' step #" + std::to_string(i + 1) +
                              " owner type does not equal the previous step's result type",
                          range);
                    return;
                }
            }
        }
    }

    // --- shared arena view ---
    //
    // A non-owning view over the arenas a lowered body owns (a CoreFlowDecl OR a
    // CoreWorkflowDecl). The static arena checks (expr / pattern bounds +
    // acyclicity), the per-path region walk, and the match verifier all consume
    // THIS rather than a concrete owner, so the flow and workflow verify arms
    // share one implementation (no duplicated walk -> no future drift). `label`
    // is the owner phrase used in diagnostics (e.g. "flow 'Decider'" or
    // "workflow 'IncidentWorkflow' node 'decide'"). Flow-specific concerns
    // (state_count, goto targets) are passed to the region walk separately.
    // The kind of body an ArenaView belongs to. Governs which path roots are
    // LEGAL in that body: a flow body may root at input / ctx / a local / an
    // identifier, but NEVER at a workflow value; a workflow node-input / return
    // region may root ONLY at the workflow input, an upstream node output, or a
    // local (a match-arm binding) — a bare identifier there is an UNRESOLVED
    // reference (fail-closed), not a legal free variable.
    enum class OwnerKind { Flow, Workflow };

    struct ArenaView {
        const std::vector<CoreExpr> &exprs;
        std::uint32_t value_count;
        const std::vector<CorePattern> &patterns;
        std::string label; // owner phrase for diagnostics (bare, no leading "flow"/"in")
        OwnerKind owner{OwnerKind::Flow};
        // For a Workflow arena: node id -> that node's target agent output type,
        // so a WorkflowNodeOutput path root's type + node-id bounds are checked
        // over EVERY arena expr (not just region-reachable ones). Empty for Flow.
        const std::vector<CoreTypeId> *node_output_types{nullptr};
        CoreTypeId workflow_input_type{}; // Workflow only: the workflow input struct
    };

    // --- pure expression arena (static, order-independent checks) ---
    //
    // Bounds every CoreExprId reference, rejects any CoreUnsupportedExpr (an
    // executable program has none), checks typed identity + projection of
    // path/construct/qualified nodes, AND proves the expr reference graph is
    // acyclic (a self- or mutually-referential expr would make a backend's
    // recursive codegen diverge). EVERY arena expr is checked, not only those a
    // statement reaches, so an unused-but-malformed expr cannot slip past a
    // consumption-boundary re-verify. Value-use ORDER is checked separately in
    // the per-state statement walk.
    void verify_expr_arena(const ArenaView &flow) {
        const auto expr_count = static_cast<std::uint32_t>(flow.exprs.size());
        const auto check_expr_id = [&](CoreExprId e, SourceRangeOpt range) {
            if (e.value >= expr_count) {
                error(verify::kExprIdOutOfRange,
                      "expression id " + std::to_string(e.value) + " is out of range in flow '" +
                          flow.label + "'",
                      range);
            }
        };
        // Static bounds on every CoreValueId EMBEDDED in an expr, independent of
        // whether a statement reaches this expr. (Def-before-use / scope is a
        // separate, statement-reachable check in verify_region.) Without this, a
        // malformed but unreferenced arena node — e.g. CoreValueRefExpr{999} —
        // would slip past a consumption-boundary re-verify.
        const auto check_value_id = [&](CoreValueId v, SourceRangeOpt range) {
            if (v.value >= flow.value_count) {
                error(verify::kValueIdOutOfRange,
                      "value id " + std::to_string(v.value) +
                          " embedded in an expression is out of range in flow '" +
                          flow.label + "'",
                      range);
            }
        };
        for (const CoreExpr &expr : flow.exprs) {
            std::visit(Overloaded{
                           [&](const CoreLiteralExpr &) {},
                           [&](const CoreValueRefExpr &r) { check_value_id(r.value, expr.source_range); },
                           [&](const CorePathExpr &p) {
                               if (p.has_local) {
                                   check_value_id(p.local, expr.source_range);
                               }
                               // Root / identity consistency (Principle 2): a Local
                               // root iff has_local; a WorkflowNodeOutput root iff a
                               // valid workflow_node id; any OTHER root must carry a
                               // kInvalid workflow_node (a stray node id on an
                               // input/ctx/local path is a malformed IR).
                               const bool has_node =
                                   p.workflow_node.value != CoreWorkflowNodeId::kInvalid;
                               if ((p.root == CorePathRoot::Local) != p.has_local) {
                                   error(verify::kWorkflowPathRootInvalid,
                                         "path expression root/has_local mismatch in '" + flow.label +
                                             "'",
                                         expr.source_range);
                               }
                               if ((p.root == CorePathRoot::WorkflowNodeOutput) != has_node) {
                                   error(verify::kWorkflowPathRootInvalid,
                                         "path expression WorkflowNodeOutput root must carry a valid "
                                         "workflow node id (and only that root may) in '" +
                                             flow.label + "'",
                                         expr.source_range);
                               }
                               // Owner-domain legality (P0-2): flow and workflow
                               // bodies do not share path roots. A workflow value
                               // root in a flow, or a flow root / bare identifier in
                               // a workflow, is a malformed IR — the 4th workflow
                               // path (an unresolved identifier) is fail-closed, NOT
                               // a legal free variable.
                               if (flow.owner == OwnerKind::Flow) {
                                   if (p.root == CorePathRoot::WorkflowInput ||
                                       p.root == CorePathRoot::WorkflowNodeOutput) {
                                       error(verify::kWorkflowPathRootInvalid,
                                             "flow '" + flow.label +
                                                 "' path expression carries a workflow-only root",
                                             expr.source_range);
                                   }
                               } else { // Workflow
                                   const bool ok_root = p.root == CorePathRoot::WorkflowInput ||
                                                        p.root == CorePathRoot::WorkflowNodeOutput ||
                                                        p.root == CorePathRoot::Local;
                                   if (!ok_root) {
                                       error(verify::kWorkflowPathRootInvalid,
                                             "workflow '" + flow.label +
                                                 "' path expression has an unresolved / non-workflow "
                                                 "root (only workflow input / node output / local are "
                                                 "legal)",
                                             expr.source_range);
                                   }
                                   // Full-arena (P1): node-id bounds + producer
                                   // output root-type + workflow-input root-type are
                                   // static invariants that must hold on EVERY expr,
                                   // not only region-reachable ones. (Ancestor
                                   // observability stays a region-reachable check.)
                                   check_workflow_root_static(flow, p, expr.source_range);
                               }
                               verify_projection(p.root_type, p.projection, p.projection_resolved,
                                                 p.root_name, expr.source_range);
                           },
                           [&](const CoreQualifiedExpr &q) { verify_qualified(q, expr.source_range); },
                           [&](const CoreUnaryExpr &u) { check_expr_id(u.operand, expr.source_range); },
                           [&](const CoreBinaryExpr &b) {
                               check_expr_id(b.lhs, expr.source_range);
                               check_expr_id(b.rhs, expr.source_range);
                           },
                           [&](const CoreConstructExpr &c) {
                               for (const CoreConstructArg &arg : c.args) {
                                   check_value_id(arg.value, expr.source_range);
                               }
                               verify_construct(c, expr.source_range);
                           },
                           [&](const CoreUnsupportedExpr &u) {
                               error(verify::kUnsupportedExpr,
                                     "executable program contains an unlowered '" + u.source_kind +
                                         "' expression",
                                     u.source_range);
                           },
                       },
                       expr.node);
        }
        verify_expr_arena_acyclic(flow);
    }

    // Static (all-arena) workflow root-type + node-id checks for ONE path expr:
    // a WorkflowInput root must be typed as the workflow input struct; a
    // WorkflowNodeOutput root's node id must be in range and its root type must
    // equal that node's target agent output type. Runs over EVERY arena expr (a
    // malformed unreferenced expr cannot slip past a re-verify). The ancestor
    // (dependency-observability) check is separate and region-reachable.
    void check_workflow_root_static(const ArenaView &flow, const CorePathExpr &p,
                                    SourceRangeOpt range) {
        if (p.root == CorePathRoot::WorkflowInput) {
            if (!(p.root_type == flow.workflow_input_type)) {
                error(verify::kWorkflowPathRootInvalid,
                      "workflow '" + flow.label +
                          "' input path root type does not equal the workflow input type",
                      range);
            }
            return;
        }
        if (p.root == CorePathRoot::WorkflowNodeOutput) {
            const std::uint32_t nid = p.workflow_node.value;
            const auto node_count = flow.node_output_types
                                        ? static_cast<std::uint32_t>(flow.node_output_types->size())
                                        : 0u;
            if (nid >= node_count) {
                error(verify::kWorkflowNodeRefInvalid,
                      "workflow '" + flow.label + "' references out-of-range node id " +
                          std::to_string(nid),
                      range);
                return;
            }
            const CoreTypeId expect = (*flow.node_output_types)[nid];
            if (!(p.root_type == expect)) {
                error(verify::kWorkflowPathRootInvalid,
                      "workflow '" + flow.label +
                          "' node-output path root type does not equal the referenced node's target "
                          "agent output type",
                      range);
            }
        }
    }

    // 3-color DFS (White/Gray/Black) over the expr reference graph. A back edge
    // to a Gray node is a cycle (a node reachable from itself through operand
    // edges); a Black node is a finished shared DAG node (legal, revisited
    // cheaply). Only CoreUnaryExpr/CoreBinaryExpr carry intra-arena edges in
    // this slice — the other nodes reference values, not exprs.
    void verify_expr_arena_acyclic(const ArenaView &flow) {
        enum class Color : std::uint8_t { White, Gray, Black };
        const auto expr_count = static_cast<std::uint32_t>(flow.exprs.size());
        std::vector<Color> color(expr_count, Color::White);
        // Iterative DFS with an explicit stack so a deep chain cannot overflow
        // the C++ stack while we are proving the IR itself is bounded.
        for (std::uint32_t root = 0; root < expr_count; ++root) {
            if (color[root] != Color::White) {
                continue;
            }
            std::vector<std::uint32_t> stack{root};
            while (!stack.empty()) {
                const std::uint32_t id = stack.back();
                if (color[id] == Color::White) {
                    color[id] = Color::Gray;
                    const auto push_edge = [&](CoreExprId e) {
                        if (e.value >= expr_count) {
                            return; // out-of-range already reported in verify_expr_arena
                        }
                        if (color[e.value] == Color::Gray) {
                            error(verify::kExprCycle,
                                  "expression #" + std::to_string(e.value) +
                                      " participates in a reference cycle in flow '" +
                                      flow.label + "'",
                                  flow.exprs[id].source_range);
                        } else if (color[e.value] == Color::White) {
                            stack.push_back(e.value);
                        }
                    };
                    std::visit(Overloaded{
                                   [&](const CoreUnaryExpr &u) { push_edge(u.operand); },
                                   [&](const CoreBinaryExpr &b) {
                                       push_edge(b.lhs);
                                       push_edge(b.rhs);
                                   },
                                   [&](const auto &) {},
                               },
                               flow.exprs[id].node);
                } else {
                    if (color[id] == Color::Gray) {
                        color[id] = Color::Black;
                    }
                    stack.pop_back();
                }
            }
        }
    }

    // --- match-pattern arena ---
    //
    // Every CorePattern in the flow's pattern arena is checked for id bounds,
    // acyclicity (iterative 3-color DFS over child-pattern edges), and per-node
    // shape: a variant pattern's owner enum + variant id are in range and its
    // payload shape matches the declared variant (tuple arity; struct slot
    // domain + no duplicate); an or-pattern has >= 2 alternatives. (Arm-binding
    // reference validity and or-alternative binding-set consistency are checked
    // with the match arm in the next slice, where the arm's binding list lives.)
    void verify_pattern_arena(const ArenaView &flow) {
        const auto pat_count = static_cast<std::uint32_t>(flow.patterns.size());
        const auto check_id = [&](CorePatternId p, SourceRangeOpt range) {
            if (p.value >= pat_count) {
                error(verify::kPatternIdOutOfRange,
                      "pattern id " + std::to_string(p.value) + " is out of range in flow '" +
                          flow.label + "'",
                      range);
            }
        };
        for (const CorePattern &pat : flow.patterns) {
            std::visit(Overloaded{
                           [&](const CoreWildcardPat &) {},
                           [&](const CoreLiteralPat &) {},
                           [&](const CoreIntRangePat &r) {
                               // AHFL `..` is a closed interval [start, end]; a
                               // reverse range is empty and never authored. Sema
                               // rejects it, but the standalone verifier guards
                               // the JSON / backend consumption boundary too.
                               if (r.start > r.end) {
                                   error(verify::kPatternShapeInvalid,
                                         "int-range pattern has start (" + std::to_string(r.start) +
                                             ") greater than end (" + std::to_string(r.end) + ")",
                                         pat.source_range);
                               }
                           },
                           [&](const CoreBindingPat &b) {
                               if (b.has_nested) {
                                   check_id(b.nested, pat.source_range);
                               }
                           },
                           [&](const CoreVariantPat &v) {
                               verify_variant_pattern(v, pat_count, pat.source_range);
                           },
                           [&](const CoreTuplePat &t) {
                               for (const CorePatternId e : t.elements) {
                                   check_id(e, pat.source_range);
                               }
                           },
                           [&](const CoreOrPat &o) {
                               if (o.alternatives.size() < 2) {
                                   error(verify::kPatternShapeInvalid,
                                         "or-pattern must have at least two alternatives", pat.source_range);
                               }
                               for (const CorePatternId alt : o.alternatives) {
                                   check_id(alt, pat.source_range);
                               }
                           },
                       },
                       pat.node);
        }
        verify_pattern_arena_acyclic(flow);
    }

    void verify_variant_pattern(const CoreVariantPat &v, std::uint32_t pat_count,
                                SourceRangeOpt range) {
        const auto check_id = [&](CorePatternId p) {
            if (p.value >= pat_count) {
                error(verify::kPatternIdOutOfRange,
                      "pattern id " + std::to_string(p.value) + " is out of range", range);
            }
        };
        if (!is_enum(v.owner_enum)) {
            error(verify::kPatternVariantInvalid,
                  "variant pattern owner type id does not name an enum", range);
            return;
        }
        const CoreTypeDecl &enum_decl = program_.types[v.owner_enum.value];
        if (v.variant.value >= enum_decl.variants.size()) {
            error(verify::kPatternVariantInvalid,
                  "variant pattern variant id " + std::to_string(v.variant.value) +
                      " is out of range for enum '" + enum_decl.name + "'",
                  range);
            return;
        }
        if (v.variant.value >= enum_decl.variant_payloads.size()) {
            error(verify::kTypeTableShapeInvalid,
                  "enum '" + enum_decl.name + "' variant #" + std::to_string(v.variant.value) +
                      " has no payload metadata (table is malformed)",
                  range);
            return;
        }
        const auto &payload = enum_decl.variant_payloads[v.variant.value];
        const auto arity = static_cast<std::uint32_t>(payload.slot_types.size());
        using PK = CoreTypeDecl::VariantPayload::Kind;
        if (payload.kind == PK::Struct) {
            // Struct payload: slot-identified fields; slots in range, no
            // duplicate, and (absent `..`) complete coverage.
            std::unordered_set<std::uint32_t> seen;
            for (const CoreVariantPatField &f : v.struct_fields) {
                check_id(f.pattern);
                if (f.slot.value >= arity) {
                    error(verify::kPatternFieldInvalid,
                          "struct-payload pattern slot id " + std::to_string(f.slot.value) +
                              " is out of range",
                          range);
                } else if (!seen.insert(f.slot.value).second) {
                    error(verify::kPatternFieldInvalid,
                          "struct-payload pattern binds slot id " + std::to_string(f.slot.value) +
                              " more than once",
                          range);
                }
            }
            if (!v.has_rest && seen.size() != arity) {
                error(verify::kPatternPayloadArity,
                      "struct-payload pattern without `..` must cover all " +
                          std::to_string(arity) + " fields (covered " + std::to_string(seen.size()) +
                          ")",
                      range);
            }
            if (!v.tuple_subpatterns.empty()) {
                error(verify::kPatternShapeInvalid,
                      "struct-payload pattern must not use positional subpatterns", range);
            }
        } else {
            // Tuple / unit payload: positional subpatterns, exact arity, no
            // struct fields / rest.
            if (!v.struct_fields.empty() || v.has_rest) {
                error(verify::kPatternShapeInvalid,
                      "non-struct-payload pattern must not use named fields or `..`", range);
            }
            if (v.tuple_subpatterns.size() != arity) {
                error(verify::kPatternPayloadArity,
                      "variant pattern has " + std::to_string(v.tuple_subpatterns.size()) +
                          " subpatterns but the variant payload arity is " + std::to_string(arity),
                      range);
            }
            for (const CorePatternId sub : v.tuple_subpatterns) {
                check_id(sub);
            }
        }
    }

    // Iterative 3-color DFS over the pattern reference graph (binding.nested,
    // variant tuple/struct children, or alternatives). A back edge to a Gray
    // node is a cycle.
    void verify_pattern_arena_acyclic(const ArenaView &flow) {
        enum class Color : std::uint8_t { White, Gray, Black };
        const auto count = static_cast<std::uint32_t>(flow.patterns.size());
        std::vector<Color> color(count, Color::White);
        for (std::uint32_t root = 0; root < count; ++root) {
            if (color[root] != Color::White) {
                continue;
            }
            std::vector<std::uint32_t> stack{root};
            while (!stack.empty()) {
                const std::uint32_t id = stack.back();
                if (color[id] == Color::White) {
                    color[id] = Color::Gray;
                    const auto push = [&](CorePatternId e) {
                        if (e.value >= count) {
                            return; // out-of-range already reported
                        }
                        if (color[e.value] == Color::Gray) {
                            error(verify::kPatternCycle,
                                  "pattern #" + std::to_string(e.value) +
                                      " participates in a reference cycle in flow '" +
                                      flow.label + "'",
                                  flow.patterns[id].source_range);
                        } else if (color[e.value] == Color::White) {
                            stack.push_back(e.value);
                        }
                    };
                    std::visit(Overloaded{
                                   [&](const CoreBindingPat &b) {
                                       if (b.has_nested) {
                                           push(b.nested);
                                       }
                                   },
                                   [&](const CoreVariantPat &v) {
                                       for (const CorePatternId s : v.tuple_subpatterns) {
                                           push(s);
                                       }
                                       for (const CoreVariantPatField &f : v.struct_fields) {
                                           push(f.pattern);
                                       }
                                   },
                                   [&](const CoreTuplePat &t) {
                                       for (const CorePatternId e : t.elements) {
                                           push(e);
                                       }
                                   },
                                   [&](const CoreOrPat &o) {
                                       for (const CorePatternId a : o.alternatives) {
                                           push(a);
                                       }
                                   },
                                   [&](const auto &) {},
                               },
                               flow.patterns[id].node);
                } else {
                    if (color[id] == Color::Gray) {
                        color[id] = Color::Black;
                    }
                    stack.pop_back();
                }
            }
        }
    }

    void verify_qualified(const CoreQualifiedExpr &q, SourceRangeOpt range) {
        if (!q.resolved) {
            error(verify::kQualifiedUnresolved,
                  "qualified value '" + q.name + "' is unresolved in an executable program", range);
            return;
        }
        if (!is_enum(q.type_id)) {
            error(verify::kConstructTypeInvalid,
                  "qualified value '" + q.name + "' type id does not name an enum", range);
            return;
        }
        if (q.variant.value >= program_.types[q.type_id.value].variants.size()) {
            error(verify::kQualifiedVariantInvalid,
                  "qualified value '" + q.name + "' variant id " + std::to_string(q.variant.value) +
                      " is out of range",
                  range);
        }
    }

    void verify_construct(const CoreConstructExpr &c, SourceRangeOpt range) {
        if (!c.resolved) {
            error(verify::kConstructUnresolved,
                  "constructor '" + c.type_name + "' is unresolved in an executable program",
                  range);
            return;
        }
        if (!type_in_range(c.type_id)) {
            error(verify::kConstructTypeInvalid,
                  "constructor '" + c.type_name + "' type id is out of range", range);
            return;
        }
        const CoreTypeDecl &type = program_.types[c.type_id.value];
        // The constructor FORM must match the type's kind: an enum-variant
        // constructor targets an Enum, a struct literal targets a Struct.
        // Otherwise a backend receives an impossible "struct-construct an enum"
        // (or vice versa) node — a struct-construct of an enum would slip past
        // the struct branch (an enum carries no fields) with zero args.
        if (c.is_enum_variant && type.kind != CoreTypeDecl::Kind::Enum) {
            error(verify::kConstructTypeInvalid,
                  "enum-variant constructor '" + c.type_name + "::" + c.variant_name +
                      "' targets a type that is not an enum",
                  range);
            return;
        }
        if (!c.is_enum_variant && type.kind != CoreTypeDecl::Kind::Struct) {
            error(verify::kConstructTypeInvalid,
                  "struct-literal constructor '" + c.type_name +
                      "' targets a type that is not a struct",
                  range);
            return;
        }
        if (c.is_enum_variant) {
            if (c.variant.value >= type.variants.size()) {
                error(verify::kConstructVariantInvalid,
                      "constructor '" + c.type_name + "::" + c.variant_name + "' variant id " +
                          std::to_string(c.variant.value) + " is out of range",
                      range);
            } else if (c.variant.value >= type.variant_payloads.size()) {
                // Every enum (user or builtin) now carries complete payload
                // metadata, so a MISSING entry is a malformed table — not an
                // "unknown arity" to skip (that was a bypass). Fail closed.
                error(verify::kTypeTableShapeInvalid,
                      "enum '" + c.type_name + "' variant #" + std::to_string(c.variant.value) +
                          " has no payload metadata (table is malformed)",
                      range);
            } else {
                // Enum-variant payload args are positional slots; their count
                // must match the declared payload arity.
                const auto &payload = type.variant_payloads[c.variant.value];
                const auto arity = payload.slot_types.size();
                if (c.args.size() != arity) {
                    error(verify::kConstructPayloadArity,
                          "constructor '" + c.type_name + "::" + c.variant_name + "' passes " +
                              std::to_string(c.args.size()) + " payload slots but the variant has " +
                              std::to_string(arity),
                          range);
                }
                // A slot id must address a real payload slot.
                for (const CoreConstructArg &arg : c.args) {
                    if (arg.field.value >= arity) {
                        error(verify::kConstructFieldInvalid,
                              "constructor '" + c.type_name + "::" + c.variant_name +
                                  "' payload slot id " + std::to_string(arg.field.value) +
                                  " is out of range",
                              range);
                    }
                }
                // A struct-payload variant is MATERIALIZED-COMPLETE: every
                // declared slot must be assigned exactly once (the frontend
                // materialized omitted defaults). Missing/duplicate slots would
                // leave a field undefined or ambiguous.
                if (payload.kind == CoreTypeDecl::VariantPayload::Kind::Struct) {
                    std::unordered_set<std::uint32_t> assigned;
                    for (const CoreConstructArg &arg : c.args) {
                        assigned.insert(arg.field.value);
                    }
                    for (std::uint32_t s = 0; s < arity; ++s) {
                        if (assigned.find(s) == assigned.end()) {
                            error(verify::kConstructFieldMissing,
                                  "constructor '" + c.type_name + "::" + c.variant_name +
                                      "' does not assign struct-payload slot #" + std::to_string(s) +
                                      " (must be materialized-complete)",
                                  range);
                        }
                    }
                }
            }
        } else {
            // Struct literal: each arg's field id must be a real field of `type`.
            for (const CoreConstructArg &arg : c.args) {
                if (arg.field.value >= type.fields.size()) {
                    error(verify::kConstructFieldInvalid,
                          "constructor '" + c.type_name + "' field id " +
                              std::to_string(arg.field.value) + " is out of range",
                          range);
                }
            }
            // Completeness: every REQUIRED field (no default) must be assigned.
            std::unordered_set<std::uint32_t> assigned;
            for (const CoreConstructArg &arg : c.args) {
                assigned.insert(arg.field.value);
            }
            for (std::uint32_t f = 0; f < type.fields.size(); ++f) {
                const bool has_default =
                    f < type.field_has_default.size() && type.field_has_default[f];
                if (!has_default && assigned.find(f) == assigned.end()) {
                    error(verify::kConstructFieldMissing,
                          "constructor '" + c.type_name + "' does not assign required field '" +
                              type.fields[f] + "'",
                          range);
                }
            }
        }
        // No two args may target the same field/slot id (Principle: identity, not
        // write order — a duplicate would make the assignment ambiguous).
        std::unordered_set<std::uint32_t> seen;
        for (const CoreConstructArg &arg : c.args) {
            if (!seen.insert(arg.field.value).second) {
                error(verify::kConstructFieldDuplicated,
                      "constructor '" + c.type_name + "' assigns field id " +
                          std::to_string(arg.field.value) + " more than once",
                      range);
            }
        }
    }

    // --- flow wiring + per-state statement discipline ---
    void verify_flow(const CoreFlowDecl &flow) {
        const ArenaView av{flow.exprs, flow.value_count, flow.patterns, flow.agent_name};
        verify_expr_arena(av);
        verify_pattern_arena(av);

        if (flow.target.value >= program_.agents.size()) {
            error(verify::kFlowTargetInvalid,
                  "flow '" + flow.agent_name + "' target agent id " +
                      std::to_string(flow.target.value) + " is out of range",
                  std::nullopt);
            return; // cannot bound states without the target agent
        }
        const CoreAgentDecl &agent = program_.agents[flow.target.value];
        const auto state_count = static_cast<std::uint32_t>(agent.states.size());
        // SSA single-definition is a FLOW-GLOBAL property: a CoreValueId is
        // allocated once from the flow's value counter, so it may be defined at
        // most once across ALL states and ALL branches. `all_definitions` is
        // shared for the whole flow and never rolled back — a second definition
        // anywhere (sibling branch, later state) is a redefinition. Def-before-
        // use / scope is a SEPARATE, region-local property handled by the
        // `visible` set (copied per branch) in verify_region.
        std::unordered_set<std::uint32_t> all_definitions;
        for (const CoreFlowState &state : flow.states) {
            if (state.state.value >= state_count) {
                error(verify::kStateIdOutOfRange,
                      "flow '" + flow.agent_name + "' handler state id " +
                          std::to_string(state.state.value) + " is out of range for agent '" +
                          agent.name + "'",
                      std::nullopt);
                continue;
            }
            std::unordered_set<std::uint32_t> visible; // fresh scope per state body
            static_cast<void>(verify_region(av, state_count, state.body, all_definitions, visible));
        }
    }


    // Collect every CoreValueId a pure expr USES (through the arena). ITERATIVE
    // with a visited set (`visiting`): the verifier must be total on a deep or
    // cyclic arena (a self-/mutually-referential expr is rejected elsewhere with
    // EXPR_CYCLE, but this walk must still RETURN, never recurse the native stack
    // to a SIGSEGV). A visited node is not re-expanded, so a cycle is walked once.
    void collect_expr_uses(const ArenaView &flow, CoreExprId id,
                           std::vector<CoreValueId> &out,
                           std::unordered_set<std::uint32_t> &visiting) const {
        const auto expr_count = static_cast<std::uint32_t>(flow.exprs.size());
        std::vector<std::uint32_t> stack;
        if (id.value < expr_count) {
            stack.push_back(id.value);
        }
        while (!stack.empty()) {
            const std::uint32_t cur = stack.back();
            stack.pop_back();
            if (cur >= expr_count || !visiting.insert(cur).second) {
                continue; // out of range (reported elsewhere) or already walked
            }
            const auto push = [&](CoreExprId e) {
                if (e.value < expr_count && visiting.find(e.value) == visiting.end()) {
                    stack.push_back(e.value);
                }
            };
            std::visit(Overloaded{
                           [&](const CoreLiteralExpr &) {},
                           [&](const CoreValueRefExpr &r) { out.push_back(r.value); },
                           [&](const CorePathExpr &p) {
                               if (p.has_local) {
                                   out.push_back(p.local);
                               }
                           },
                           [&](const CoreQualifiedExpr &) {},
                           [&](const CoreUnaryExpr &u) { push(u.operand); },
                           [&](const CoreBinaryExpr &b) {
                               push(b.lhs);
                               push(b.rhs);
                           },
                           [&](const CoreConstructExpr &c) {
                               for (const CoreConstructArg &arg : c.args) {
                                   out.push_back(arg.value);
                               }
                           },
                           [&](const CoreUnsupportedExpr &) {},
                       },
                       flow.exprs[cur].node);
        }
    }

    // Verify one region's statements in order. Two definition sets:
    //   * `all_definitions` (flow-global, shared, never rolled back): SSA single
    //     definition — a value defined twice ANYWHERE is a redefinition.
    //   * `visible` (region-local, copied into each branch): def-before-use +
    //     scope — a branch-local definition must not be visible to a sibling
    //     branch or after the `if`.
    //
    // `ctx` is the region's role, which governs where a CoreYieldStmt is legal
    // and its arity: Flow (ordinary handler body — no yield allowed), Guard and
    // MatchArmValue (must yield a value), MatchArmUnit (must yield no value). An
    // `if` branch inherits its parent's context (a yield nested in an `if` inside
    // an arm body still yields from that arm). `terminated_out` (optional)
    // reports whether the region ended in a terminator/yield, so a match arm can
    // require its body/guard to end well.
    enum class RegionContext { Flow, Guard, MatchArmValue, MatchArmUnit, WorkflowNodeInput, WorkflowReturn };

    // A workflow node-input / return region is a value-producing region with NO
    // flow-state control flow: it must yield a value (or Trap) on every path and
    // may NOT goto / return. These two contexts share that discipline.
    [[nodiscard]] static bool is_workflow_ctx(RegionContext ctx) {
        return ctx == RegionContext::WorkflowNodeInput || ctx == RegionContext::WorkflowReturn;
    }

    // A region's control-flow exit summary, MERGED across all paths. `fallthrough`
    // = at least one path runs off the region end; the diverge/yield flags = at
    // least one path exits that way. A well-formed match-arm value body, for
    // instance, must have `!fallthrough && !yields_unit` and every path either
    // yields_value or diverges.
    struct RegionExit {
        bool fallthrough{false};
        bool yields_value{false};
        bool yields_unit{false};
        bool diverges_control{false}; // Return / Goto (escapes the handler)
        bool diverges_trap{false};    // Trap (diverges but stays in-handler)
        void merge(const RegionExit &o) {
            fallthrough |= o.fallthrough;
            yields_value |= o.yields_value;
            yields_unit |= o.yields_unit;
            diverges_control |= o.diverges_control;
            diverges_trap |= o.diverges_trap;
        }
    };

    // Verify one region's statements in order, returning its merged exit summary.
    // Definition sets: `all_definitions` (flow-global, never rolled back — SSA
    // single definition) and `visible` (region-local, copied into each branch —
    // def-before-use + scope). `ctx` is the region's role (governs where a
    // CoreYieldStmt is legal); yield/context legality is enforced by the CALLER
    // from the returned RegionExit, except that a yield in a `Flow` region is an
    // immediate error here.
    [[nodiscard]] RegionExit verify_region(const ArenaView &flow, std::uint32_t state_count,
                                           const CoreRegion &region,
                                           std::unordered_set<std::uint32_t> &all_definitions,
                                           std::unordered_set<std::uint32_t> &visible,
                                           RegionContext ctx = RegionContext::Flow) {
        const auto use_value = [&](CoreValueId v, SourceRangeOpt range) {
            if (v.value >= flow.value_count) {
                error(verify::kValueIdOutOfRange,
                      "value id " + std::to_string(v.value) + " is out of range in flow '" +
                          flow.label + "'",
                      range);
                return;
            }
            if (visible.find(v.value) == visible.end()) {
                error(verify::kValueUseBeforeDef,
                      "value id " + std::to_string(v.value) +
                          " is used before it is defined (or is out of scope)",
                      range);
            }
        };
        const auto define_value = [&](CoreValueId v, SourceRangeOpt range) {
            if (v.value >= flow.value_count) {
                error(verify::kValueIdOutOfRange,
                      "defined value id " + std::to_string(v.value) + " is out of range in flow '" +
                          flow.label + "'",
                      range);
                return;
            }
            if (!all_definitions.insert(v.value).second) {
                error(verify::kValueRedefined,
                      "value id " + std::to_string(v.value) + " is defined more than once", range);
            }
            visible.insert(v.value);
        };
        const auto use_expr = [&](CoreExprId e, SourceRangeOpt range) {
            std::vector<CoreValueId> uses;
            std::unordered_set<std::uint32_t> visiting;
            collect_expr_uses(flow, e, uses, visiting);
            for (const CoreValueId v : uses) {
                use_value(v, range);
            }
        };

        // `live` tracks the current path; once it exits (yield/return/goto/trap)
        // the following statements are unreachable. `exit` accumulates HOW the
        // current straight-line path leaves; when `live` is still true at the end
        // the region falls through.
        bool live = true;
        RegionExit exit;
        for (const CoreStmt &stmt : region.statements) {
            if (!live) {
                error(verify::kStmtAfterTerminator,
                      "statement follows a terminator in flow '" + flow.label + "'",
                      stmt.source_range);
            }
            std::visit(Overloaded{
                           [&](const CoreLetStmt &s) {
                               use_expr(s.expr, stmt.source_range);
                               define_value(s.result, stmt.source_range);
                           },
                           [&](const CoreCapabilityCallStmt &s) {
                               if (s.capability.value >= program_.capabilities.size()) {
                                   error(verify::kCapabilityIdOutOfRange,
                                         "capability call '" + s.callee_name + "' capability id " +
                                             std::to_string(s.capability.value) + " is out of range",
                                         stmt.source_range);
                               } else {
                                   const auto arity =
                                       program_.capabilities[s.capability.value].param_types.size();
                                   if (s.args.size() != arity) {
                                       error(verify::kCapabilityArityMismatch,
                                             "capability call '" + s.callee_name + "' passes " +
                                                 std::to_string(s.args.size()) +
                                                 " args but the import signature has " +
                                                 std::to_string(arity),
                                             stmt.source_range);
                                   }
                               }
                               for (const CoreValueId a : s.args) {
                                   use_value(a, stmt.source_range);
                               }
                               define_value(s.result, stmt.source_range);
                           },
                           [&](const CoreStoreStmt &s) {
                               verify_projection(s.place.root_type, s.place.projection,
                                                 s.place.projection_resolved, s.place.root_name,
                                                 stmt.source_range);
                               use_value(s.value, stmt.source_range);
                           },
                           [&](const CoreIfStmt &s) {
                               use_value(s.condition, stmt.source_range);
                               // Each branch: own `visible` copy (defs don't
                               // escape), shared `all_definitions`, same `ctx`.
                               // The else-less branch is an implicit fallthrough.
                               RegionExit then_exit;
                               RegionExit else_exit;
                               else_exit.fallthrough = true;
                               if (s.then_region) {
                                   auto bv = visible;
                                   then_exit = verify_region(flow, state_count, *s.then_region,
                                                             all_definitions, bv, ctx);
                               } else {
                                   then_exit.fallthrough = true;
                               }
                               if (s.else_region) {
                                   auto bv = visible;
                                   else_exit = verify_region(flow, state_count, *s.else_region,
                                                             all_definitions, bv, ctx);
                               }
                               RegionExit merged;
                               merged.merge(then_exit);
                               merged.merge(else_exit);
                               // The `if` yields/diverges only if BOTH branches
                               // leave; it falls through if EITHER branch can.
                               exit.yields_value |= merged.yields_value;
                               exit.yields_unit |= merged.yields_unit;
                               exit.diverges_control |= merged.diverges_control;
                               exit.diverges_trap |= merged.diverges_trap;
                               live = then_exit.fallthrough || else_exit.fallthrough;
                           },
                           [&](const CoreGotoStmt &s) {
                               if (is_workflow_ctx(ctx)) {
                                   error(verify::kWorkflowRegionYield,
                                         "workflow region '" + flow.label +
                                             "' contains a goto (no flow-state control flow in a "
                                             "workflow node input / return region)",
                                         stmt.source_range);
                               } else if (s.target.value >= state_count) {
                                   error(verify::kGotoTargetInvalid,
                                         "goto target state id " + std::to_string(s.target.value) +
                                             " is out of range in flow '" + flow.label + "'",
                                         stmt.source_range);
                               }
                               exit.diverges_control = true;
                               live = false;
                           },
                           [&](const CoreReturnStmt &s) {
                               if (is_workflow_ctx(ctx)) {
                                   error(verify::kWorkflowRegionYield,
                                         "workflow region '" + flow.label +
                                             "' contains a return (a node input / return region "
                                             "yields its value, it does not return)",
                                         stmt.source_range);
                               }
                               if (s.has_value) {
                                   use_value(s.value, stmt.source_range);
                               }
                               exit.diverges_control = true;
                               live = false;
                           },
                           [&](const CoreYieldStmt &s) {
                               if (ctx == RegionContext::Flow) {
                                   error(verify::kYieldOutsideMatchArm,
                                         "yield outside a match arm / guard region in flow '" +
                                             flow.label + "'",
                                         stmt.source_range);
                               }
                               if (s.has_value) {
                                   use_value(s.value, stmt.source_range);
                                   exit.yields_value = true;
                               } else {
                                   exit.yields_unit = true;
                               }
                               live = false;
                           },
                           [&](const CoreTrapStmt &) {
                               exit.diverges_trap = true;
                               live = false;
                           },
                           [&](const CoreMatchStmt &s) {
                               const RegionExit m =
                                   verify_match(flow, state_count, s, all_definitions, visible,
                                                stmt.source_range);
                               // A match consumes its arms' yields; only control /
                               // trap divergence propagates to the parent, plus a
                               // fallthrough when the match can normally complete.
                               exit.diverges_control |= m.diverges_control;
                               exit.diverges_trap |= m.diverges_trap;
                               live = m.fallthrough;
                           },
                       },
                       stmt.node);
        }
        if (live) {
            exit.fallthrough = true;
        }
        return exit;
    }

    // Verify a match statement and return its exit summary for the PARENT region
    // (arm yields are consumed here; control/trap divergence propagates up).
    [[nodiscard]] RegionExit verify_match(const ArenaView &flow, std::uint32_t state_count,
                                          const CoreMatchStmt &m,
                                          std::unordered_set<std::uint32_t> &all_definitions,
                                          std::unordered_set<std::uint32_t> &visible,
                                          SourceRangeOpt range) {
        const auto pat_count = static_cast<std::uint32_t>(flow.patterns.size());
        // scrutinee in scope.
        if (m.scrutinee.value >= flow.value_count) {
            error(verify::kValueIdOutOfRange,
                  "match scrutinee value id " + std::to_string(m.scrutinee.value) +
                      " is out of range",
                  range);
        } else if (visible.find(m.scrutinee.value) == visible.end()) {
            error(verify::kValueUseBeforeDef, "match scrutinee is used before it is defined", range);
        }
        const RegionContext body_ctx =
            m.has_result ? RegionContext::MatchArmValue : RegionContext::MatchArmUnit;

        RegionExit propagated; // what the match contributes to the parent region

        const auto require_arm_region = [&](const CoreRegion &region, RegionContext ctx,
                                            std::unordered_set<std::uint32_t> &vis, bool is_guard) {
            const RegionExit e = verify_region(flow, state_count, region, all_definitions, vis, ctx);
            // Per-path legality by context.
            if (ctx == RegionContext::Guard) {
                // Every path must yield a Bool value or trap; no fallthrough,
                // no unit yield, no control escape (a source guard is pure).
                if (e.fallthrough || e.yields_unit || e.diverges_control) {
                    error(verify::kMatchArmYield,
                          "guard region must yield a value on every path (no fallthrough / unit / "
                          "return / goto)",
                          range);
                }
            } else if (ctx == RegionContext::MatchArmValue) {
                if (e.fallthrough || e.yields_unit) {
                    error(verify::kMatchArmYield,
                          "expression match arm must yield a value on every path (no fallthrough / "
                          "unit yield)",
                          range);
                }
            } else { // MatchArmUnit
                if (e.fallthrough || e.yields_value) {
                    error(verify::kMatchArmYield,
                          "statement match arm must yield no value on every path (no fallthrough / "
                          "value yield)",
                          range);
                }
            }
            static_cast<void>(is_guard);
            return e;
        };

        for (const CoreMatchArm &arm : m.arms) {
            // Pattern id in range.
            if (arm.pattern.value >= pat_count) {
                error(verify::kPatternIdOutOfRange,
                      "match arm pattern id " + std::to_string(arm.pattern.value) +
                          " is out of range",
                      range);
            }
            // Arm bindings define fresh flow-global values, visible only in this
            // arm's guard + body (a copy of the outer visible set).
            auto arm_visible = visible;
            for (const CorePatternBinding &b : arm.bindings) {
                if (b.value.value >= flow.value_count) {
                    error(verify::kValueIdOutOfRange,
                          "arm binding value id " + std::to_string(b.value.value) +
                              " is out of range",
                          range);
                    continue;
                }
                if (!all_definitions.insert(b.value.value).second) {
                    error(verify::kValueRedefined,
                          "arm binding value id " + std::to_string(b.value.value) +
                              " is defined more than once",
                          range);
                }
                arm_visible.insert(b.value.value);
            }
            // Pattern binding-references + or-alternative binding-set consistency.
            verify_arm_pattern_bindings(flow, arm, range);

            if (arm.guard_region) {
                auto guard_visible = arm_visible;
                require_arm_region(*arm.guard_region, RegionContext::Guard, guard_visible,
                                   /*is_guard=*/true);
            }
            if (arm.body) {
                auto body_visible = arm_visible;
                const RegionExit be =
                    require_arm_region(*arm.body, body_ctx, body_visible, /*is_guard=*/false);
                propagated.diverges_control |= be.diverges_control;
                propagated.diverges_trap |= be.diverges_trap;
                if (be.yields_value || be.yields_unit) {
                    propagated.fallthrough = true; // a completing arm => match completes
                }
            } else {
                error(verify::kMatchArmYield, "match arm has no body region", range);
            }
        }

        // Fallback region is mandatory (structural totality — not a mutable flag).
        if (!m.fallback_region) {
            error(verify::kMatchNotTotal,
                  "match has no fallback region (must be exhaustive by construction)", range);
        } else {
            auto fb_visible = visible;
            const RegionExit fe =
                require_arm_region(*m.fallback_region, body_ctx, fb_visible, /*is_guard=*/false);
            propagated.diverges_control |= fe.diverges_control;
            propagated.diverges_trap |= fe.diverges_trap;
            if (fe.yields_value || fe.yields_unit) {
                propagated.fallthrough = true;
            }
        }

        // The match result is defined ONCE, in the parent scope, iff expression.
        if (m.has_result) {
            if (m.result.value >= flow.value_count) {
                error(verify::kValueIdOutOfRange,
                      "match result value id " + std::to_string(m.result.value) +
                          " is out of range",
                      range);
            } else if (!all_definitions.insert(m.result.value).second) {
                error(verify::kValueRedefined,
                      "match result value id " + std::to_string(m.result.value) +
                          " is defined more than once",
                      range);
            } else {
                visible.insert(m.result.value);
            }
        }
        return propagated;
    }

    // Enforce the arm.bindings <-> pattern binding-site BIJECTION. `collect`
    // returns the occurrence multiset (binding id -> count) a pattern introduces:
    //   * within a single non-or pattern tree, each binding id must occur exactly
    //     once (two payload slots naming one binding is ambiguous);
    //   * an or-pattern's alternatives must each independently satisfy
    //     occurrence==1 and bind the SAME id set (shared across alternatives);
    //   * the root pattern's binding set must equal the arm binding domain
    //     {0 .. bindings.size-1} exactly — no unused declared binding, none out
    //     of range.
    void verify_arm_pattern_bindings(const ArenaView &flow, const CoreMatchArm &arm,
                                     SourceRangeOpt range) {
        const auto pat_count = static_cast<std::uint32_t>(flow.patterns.size());
        const auto binding_count = static_cast<std::uint32_t>(arm.bindings.size());
        using Occ = std::map<std::uint32_t, std::uint32_t>; // binding id -> count
        const auto add = [&](Occ &into, const Occ &from) {
            for (const auto &[id, n] : from) {
                into[id] += n;
            }
        };
        std::function<Occ(CorePatternId, std::unordered_set<std::uint32_t> &)> collect;
        collect = [&](CorePatternId pid, std::unordered_set<std::uint32_t> &visiting) -> Occ {
            Occ out;
            if (pid.value >= pat_count || !visiting.insert(pid.value).second) {
                return out; // out of range / cycle already reported elsewhere
            }
            std::visit(Overloaded{
                           [&](const CoreWildcardPat &) {},
                           [&](const CoreLiteralPat &) {},
                           [&](const CoreIntRangePat &) {},
                           [&](const CoreBindingPat &b) {
                               if (b.binding.value >= binding_count) {
                                   error(verify::kPatternBindingInvalid,
                                         "binding pattern references arm binding id " +
                                             std::to_string(b.binding.value) + " out of range",
                                         range);
                               } else {
                                   out[b.binding.value] += 1;
                               }
                               if (b.has_nested) {
                                   add(out, collect(b.nested, visiting));
                               }
                           },
                           [&](const CoreVariantPat &v) {
                               for (const CorePatternId s : v.tuple_subpatterns) {
                                   add(out, collect(s, visiting));
                               }
                               for (const CoreVariantPatField &f : v.struct_fields) {
                                   add(out, collect(f.pattern, visiting));
                               }
                           },
                           [&](const CoreTuplePat &t) {
                               for (const CorePatternId e : t.elements) {
                                   add(out, collect(e, visiting));
                               }
                           },
                           [&](const CoreOrPat &o) {
                               // Each alternative independently: occurrence == 1;
                               // and all alternatives bind the SAME id set. The
                               // or contributes each shared id ONCE (alternatives
                               // are mutually exclusive at runtime).
                               std::optional<std::set<std::uint32_t>> common;
                               for (const CorePatternId alt : o.alternatives) {
                                   const Occ alt_occ = collect(alt, visiting);
                                   std::set<std::uint32_t> alt_set;
                                   for (const auto &[id, n] : alt_occ) {
                                       if (n != 1) {
                                           error(verify::kPatternBindingInvalid,
                                                 "binding id " + std::to_string(id) +
                                                     " is bound more than once in one pattern",
                                                 range);
                                       }
                                       alt_set.insert(id);
                                   }
                                   if (!common) {
                                       common = alt_set;
                                   } else if (*common != alt_set) {
                                       error(verify::kOrBindingSetMismatch,
                                             "or-pattern alternatives bind different variable sets",
                                             range);
                                   }
                               }
                               if (common) {
                                   for (const std::uint32_t id : *common) {
                                       out[id] += 1;
                                   }
                               }
                           },
                       },
                       flow.patterns[pid.value].node);
            visiting.erase(pid.value);
            return out;
        };
        std::unordered_set<std::uint32_t> visiting;
        const Occ root = collect(arm.pattern, visiting);
        // Occurrence == 1 across the whole (non-or-collapsed) tree.
        std::set<std::uint32_t> bound;
        for (const auto &[id, n] : root) {
            if (n != 1) {
                error(verify::kPatternBindingInvalid,
                      "arm binding id " + std::to_string(id) +
                          " is bound at more than one pattern position",
                      range);
            }
            bound.insert(id);
        }
        // Bijection with the arm binding domain: every declared binding is bound.
        for (std::uint32_t i = 0; i < binding_count; ++i) {
            if (bound.find(i) == bound.end()) {
                error(verify::kPatternBindingInvalid,
                      "arm declares binding id " + std::to_string(i) +
                          " but the pattern never binds it",
                      range);
            }
        }
    }

    // --- workflow (multi-agent DAG orchestration) ---
    //
    // Verifies one CoreWorkflowDecl: shell types, node identity + target, the
    // `after` dependency DAG (bounds / self / duplicate edge + acyclicity), the
    // node-output path references (a node input may only read a node that is a
    // direct or transitive dependency; the return region may read any node), and
    // each node input / return region (ANF, value-yielding, no flow control). SSA
    // single-definition is workflow-global (one shared `all_definitions`); each
    // region gets a fresh `visible` scope. safety / liveness are ERASED (no field
    // exists here), which this proves structurally by their absence.
    //
    // HONEST P4 BOUNDARY: CoreValue has no physical value type yet, so the
    // verifier proves each region "yields A value", NOT that the yielded value's
    // type equals the target agent input / workflow output schema. Exact-schema
    // agreement remains a Sema guarantee until the P4 value-representation slice.
    void verify_workflow(const CoreWorkflowDecl &wf, std::uint32_t index) {
        const std::string label = "workflow '" + wf.name + "'";
        // Identity: id == index into CoreProgram::workflows (Principle 2).
        if (wf.id.value != index) {
            error(verify::kWorkflowIdOutOfRange,
                  label + " id " + std::to_string(wf.id.value) +
                      " does not equal its index " + std::to_string(index),
                  std::nullopt);
        }
        // Shell: input / output must be valid Struct types (like an agent).
        if (!is_struct(wf.input_type)) {
            error(verify::kWorkflowShellInvalid, label + " input type is not a valid struct",
                  std::nullopt);
        }
        if (!is_struct(wf.output_type)) {
            error(verify::kWorkflowShellInvalid, label + " output type is not a valid struct",
                  std::nullopt);
        }

        const auto node_count = static_cast<std::uint32_t>(wf.nodes.size());
        // Node identity + target + `after` edge bounds / self / duplicate.
        for (std::uint32_t i = 0; i < node_count; ++i) {
            const CoreWorkflowNode &node = wf.nodes[i];
            if (node.id.value != i) {
                error(verify::kWorkflowNodeInvalid,
                      label + " node #" + std::to_string(i) + " id " +
                          std::to_string(node.id.value) + " does not equal its index",
                      std::nullopt);
            }
            // The node's invocation target must be a valid Agent INSTANCE (a
            // concrete monomorphized invocation), resolved via target_instance ->
            // instances[] -> CoreAgentInstance. kInvalid / out-of-range / non-Agent
            // is fail-closed (the link pass must have bound every node).
            if (agent_instance_of(node.target_instance) == nullptr) {
                error(verify::kWorkflowInvocationInvalid,
                      label + " node '" + node.node_name +
                          "' target_instance does not resolve to a valid agent instance",
                      std::nullopt);
            }
            std::set<std::uint32_t> seen_edges;
            for (const CoreWorkflowNodeId dep : node.after) {
                if (dep.value >= node_count) {
                    error(verify::kWorkflowEdgeInvalid,
                          label + " node '" + node.node_name + "' depends on out-of-range node id " +
                              std::to_string(dep.value),
                          std::nullopt);
                    continue;
                }
                if (dep.value == i) {
                    error(verify::kWorkflowEdgeInvalid,
                          label + " node '" + node.node_name + "' depends on itself",
                          std::nullopt);
                }
                if (!seen_edges.insert(dep.value).second) {
                    error(verify::kWorkflowEdgeInvalid,
                          label + " node '" + node.node_name +
                              "' has a duplicate dependency edge to node id " +
                              std::to_string(dep.value),
                          std::nullopt);
                }
            }
        }

        // DAG acyclicity: ONE 3-color DFS over the `after` edges (single SSOT for
        // the cycle check — no parallel Kahn). A back edge to a Gray node is a
        // cycle. Only reachable through in-range edges (OOR already reported).
        enum class Color : std::uint8_t { White, Gray, Black };
        std::vector<Color> color(node_count, Color::White);
        bool acyclic = true;
        for (std::uint32_t root = 0; root < node_count; ++root) {
            if (color[root] != Color::White) {
                continue;
            }
            std::vector<std::uint32_t> stack{root};
            while (!stack.empty()) {
                const std::uint32_t id = stack.back();
                if (color[id] == Color::White) {
                    color[id] = Color::Gray;
                    for (const CoreWorkflowNodeId dep : wf.nodes[id].after) {
                        if (dep.value >= node_count) {
                            continue;
                        }
                        if (color[dep.value] == Color::Gray) {
                            acyclic = false;
                            error(verify::kWorkflowCycle,
                                  label + " node '" + wf.nodes[id].node_name +
                                      "' participates in a dependency cycle",
                                  std::nullopt);
                        } else if (color[dep.value] == Color::White) {
                            stack.push_back(dep.value);
                        }
                    }
                } else {
                    if (color[id] == Color::Gray) {
                        color[id] = Color::Black;
                    }
                    stack.pop_back();
                }
            }
        }

        // Ancestor (transitive dependency) closure per node — only meaningful when
        // the DAG is acyclic. A node input may reference ONLY a node in its own
        // ancestor set; the return region may reference ANY node.
        std::vector<std::set<std::uint32_t>> ancestors(node_count);
        if (acyclic) {
            // Nodes in Kahn-free topological-ish order: since it is a DAG, a simple
            // memoized closure works (dependencies have strictly-earlier reachable
            // sets; recompute via DFS over `after`).
            for (std::uint32_t i = 0; i < node_count; ++i) {
                std::vector<std::uint32_t> stack(wf.nodes[i].after.size());
                for (std::size_t k = 0; k < wf.nodes[i].after.size(); ++k) {
                    stack[k] = wf.nodes[i].after[k].value;
                }
                while (!stack.empty()) {
                    const std::uint32_t d = stack.back();
                    stack.pop_back();
                    if (d >= node_count || !ancestors[i].insert(d).second) {
                        continue;
                    }
                    for (const CoreWorkflowNodeId dd : wf.nodes[d].after) {
                        stack.push_back(dd.value);
                    }
                }
            }
        }

        // Per-node target-agent output type (kInvalid if the node's target
        // instance is unresolved) — the expected type of a WorkflowNodeOutput root
        // reading that node. Resolved through target_instance -> CoreAgentInstance
        // -> output_type. Used by the FULL-arena root-type/bounds check (every
        // expr), so a malformed unreferenced WorkflowNodeOutput cannot slip past.
        std::vector<CoreTypeId> node_output_types(node_count);
        for (std::uint32_t i = 0; i < node_count; ++i) {
            const CoreAgentInstance *ai = agent_instance_of(wf.nodes[i].target_instance);
            node_output_types[i] = ai ? ai->output_type : CoreTypeId{};
        }

        // The shared arena (expr + pattern) + workflow-global SSA. Value ids are
        // allocated once from wf.value_count across ALL node regions + the return
        // region, so a single `all_definitions` set catches any redefinition. The
        // arena pass runs in the Workflow domain, so it rejects flow-only roots,
        // an unresolved identifier, and (over EVERY expr) a bad node-id / mistyped
        // workflow root.
        const auto make_view = [&](std::string lbl) {
            return ArenaView{wf.exprs,       wf.value_count,     wf.patterns, std::move(lbl),
                             OwnerKind::Workflow, &node_output_types, wf.input_type};
        };
        verify_expr_arena(make_view(wf.name));
        verify_pattern_arena(make_view(wf.name));
        std::unordered_set<std::uint32_t> all_definitions;

        // Each node input region: value-yielding, no flow control, and its
        // NodeOutput references must be dependency-reachable (ancestor check;
        // bounds + root-type already proven over the whole arena above).
        for (std::uint32_t i = 0; i < node_count; ++i) {
            const CoreWorkflowNode &node = wf.nodes[i];
            if (!node.input_region) {
                error(verify::kWorkflowRegionYield,
                      label + " node '" + node.node_name + "' has no input region", std::nullopt);
                continue;
            }
            const ArenaView node_av = make_view(wf.name + "' node '" + node.node_name);
            std::unordered_set<std::uint32_t> visible;
            const RegionExit e = verify_region(node_av, /*state_count=*/0, *node.input_region,
                                                all_definitions, visible,
                                                RegionContext::WorkflowNodeInput);
            require_workflow_region(e, node_av.label);
            verify_workflow_node_refs(wf, *node.input_region, ancestors[i], acyclic, node_av.label);
        }
        // Return region: value-yielding, no flow control, may reference any node.
        if (!wf.return_region) {
            error(verify::kWorkflowRegionYield, label + " has no return region", std::nullopt);
        } else {
            const ArenaView ret_av = make_view(wf.name + "' return");
            std::unordered_set<std::uint32_t> visible;
            const RegionExit e = verify_region(ret_av, /*state_count=*/0, *wf.return_region,
                                                all_definitions, visible,
                                                RegionContext::WorkflowReturn);
            require_workflow_region(e, ret_av.label);
            std::set<std::uint32_t> all_nodes;
            for (std::uint32_t i = 0; i < node_count; ++i) {
                all_nodes.insert(i);
            }
            verify_workflow_node_refs(wf, *wf.return_region, all_nodes, /*enforce=*/true,
                                      ret_av.label);
        }
    }

    // A workflow node input / return region must yield a VALUE on every path (or
    // Trap): no fallthrough, no unit yield, no control divergence (goto/return are
    // already rejected in-context, which shows up as diverges_control).
    void require_workflow_region(const RegionExit &e, const std::string &label) {
        if (e.fallthrough || e.yields_unit || e.diverges_control) {
            error(verify::kWorkflowRegionYield,
                  "workflow region '" + label +
                      "' must yield a value on every path (no fallthrough / unit yield / control "
                      "escape)",
                  std::nullopt);
        }
    }

    // Walk a workflow region's referenced exprs and check every WorkflowNodeOutput
    // path root refers to an ALLOWED node: for a node input, one of its transitive
    // dependencies (`allowed`); for the return region, any node. Node id bounds are
    // also enforced here. `enforce` is false when the DAG had a cycle (ancestor
    // sets are unreliable), so we only bounds-check then.
    void verify_workflow_node_refs(const CoreWorkflowDecl &wf, const CoreRegion &region,
                                   const std::set<std::uint32_t> &allowed, bool enforce,
                                   const std::string &label) {
        const auto node_count = static_cast<std::uint32_t>(wf.nodes.size());
        for_each_region_path_expr(wf, region, [&](const CorePathExpr &p, SourceRangeOpt range) {
            if (p.root != CorePathRoot::WorkflowNodeOutput) {
                return;
            }
            if (p.workflow_node.value >= node_count) {
                error(verify::kWorkflowNodeRefInvalid,
                      "workflow region '" + label + "' references out-of-range node id " +
                          std::to_string(p.workflow_node.value),
                      range);
                return;
            }
            if (enforce && allowed.find(p.workflow_node.value) == allowed.end()) {
                error(verify::kWorkflowNodeRefInvalid,
                      "workflow region '" + label + "' references node '" +
                          wf.nodes[p.workflow_node.value].node_name +
                          "' which is not a (transitive) dependency it can observe",
                      range);
            }
        });
    }

    // Visit every CorePathExpr REACHABLE from a region's statements (through the
    // workflow's shared expr arena), invoking `fn(path, range)`. Reuses the same
    // arena the region's value ids index into.
    template <class Fn>
    void for_each_region_path_expr(const CoreWorkflowDecl &wf, const CoreRegion &region, Fn &&fn) {
        for (const CoreStmt &stmt : region.statements) {
            std::visit(Overloaded{
                           [&](const CoreLetStmt &s) {
                               visit_expr_paths(wf, s.expr, stmt.source_range, fn);
                           },
                           [&](const CoreCapabilityCallStmt &) {},
                           [&](const CoreStoreStmt &) {},
                           [&](const CoreYieldStmt &) {},
                           [&](const CoreReturnStmt &) {},
                           [&](const CoreGotoStmt &) {},
                           [&](const CoreTrapStmt &) {},
                           [&](const CoreIfStmt &s) {
                               if (s.then_region) {
                                   for_each_region_path_expr(wf, *s.then_region, fn);
                               }
                               if (s.else_region) {
                                   for_each_region_path_expr(wf, *s.else_region, fn);
                               }
                           },
                           [&](const CoreMatchStmt &s) {
                               for (const CoreMatchArm &arm : s.arms) {
                                   if (arm.guard_region) {
                                       for_each_region_path_expr(wf, *arm.guard_region, fn);
                                   }
                                   if (arm.body) {
                                       for_each_region_path_expr(wf, *arm.body, fn);
                                   }
                               }
                               if (s.fallback_region) {
                                   for_each_region_path_expr(wf, *s.fallback_region, fn);
                               }
                           },
                       },
                       stmt.node);
        }
    }

    // Visit every CorePathExpr reachable from a single arena expr root, invoking
    // `fn(path, range)`. ITERATIVE with an explicit visited set (P0-1): this MUST
    // be total on its own — a reachable self- or mutually-referential expr
    // (rejected separately with EXPR_CYCLE, but the verifier must still RETURN,
    // never SIGSEGV) and an arbitrarily deep operand chain must both terminate
    // without recursing the native stack. A visited node is not re-expanded, so a
    // cycle is walked at most once.
    template <class Fn>
    void visit_expr_paths(const CoreWorkflowDecl &wf, CoreExprId root, SourceRangeOpt range,
                          Fn &&fn) {
        const auto expr_count = static_cast<std::uint32_t>(wf.exprs.size());
        std::vector<std::uint32_t> stack;
        std::unordered_set<std::uint32_t> visited;
        if (root.value < expr_count) {
            stack.push_back(root.value);
        }
        while (!stack.empty()) {
            const std::uint32_t id = stack.back();
            stack.pop_back();
            if (id >= expr_count || !visited.insert(id).second) {
                continue; // out of range (reported elsewhere) or already walked
            }
            const auto push = [&](CoreExprId e) {
                if (e.value < expr_count && visited.find(e.value) == visited.end()) {
                    stack.push_back(e.value);
                }
            };
            std::visit(Overloaded{
                           [&](const CorePathExpr &p) { fn(p, range); },
                           [&](const CoreUnaryExpr &u) { push(u.operand); },
                           [&](const CoreBinaryExpr &b) {
                               push(b.lhs);
                               push(b.rhs);
                           },
                           [&](const auto &) {},
                       },
                       wf.exprs[id].node);
        }
    }

    // --- monomorphized instance table ---
    //
    // Verifies CoreProgram.instances: id == index; instance_key non-empty +
    // globally unique (a duplicate key is a re-definition, fail-closed); every
    // dispatch type is CONCRETE (a mangle dispatch descriptor with an
    // Unresolved/Any/Never shape is malformed); and each payload's base id (where
    // a Core base table exists — Capability / Agent / Workflow) is in range and,
    // for Agent/Workflow, its instance shell matches the nominal base's shell.
    // Predicate / Fn keep only the origin (no Core base table yet), so nothing to
    // bound. NOTE: this does NOT require every call site to have an instance
    // (stdlib deliberate omission when include_stdlib_ == false is a legal
    // exception), and it consumes the emit_instantiated_declarations closure — the
    // budgeted run_monomorphization closure is not yet the same SSOT.
    void verify_instances() {
        std::unordered_set<std::string> seen_keys;
        for (std::uint32_t i = 0; i < program_.instances.size(); ++i) {
            const CoreInstanceDecl &inst = program_.instances[i];
            if (inst.id.value != i) {
                error(verify::kInstanceBaseInvalid,
                      "instance #" + std::to_string(i) + " id " + std::to_string(inst.id.value) +
                          " does not equal its index",
                      std::nullopt);
            }
            if (inst.instance_key.empty()) {
                error(verify::kInstanceKeyEmpty,
                      "instance #" + std::to_string(i) + " has an empty instance key", std::nullopt);
            } else if (!seen_keys.insert(inst.instance_key).second) {
                error(verify::kInstanceKeyDuplicated,
                      "instance key '" + inst.instance_key + "' is defined more than once",
                      std::nullopt);
            }
            for (const ir::TypeRef &t : inst.dispatch_types) {
                if (!type_ref_is_concrete(t)) {
                    error(verify::kInstanceDispatchTypeInvalid,
                          "instance '" + inst.instance_key +
                              "' has a non-concrete dispatch type (Unresolved / Any / Never)",
                          std::nullopt);
                }
            }
            std::visit(Overloaded{
                           [&](const CoreCapabilityInstance &p) {
                               if (p.base.value >= program_.capabilities.size()) {
                                   error(verify::kInstanceBaseInvalid,
                                         "capability instance '" + inst.instance_key +
                                             "' base id is out of range",
                                         std::nullopt);
                               }
                           },
                           [&](const CorePredicateInstance &) {},
                           [&](const CoreAgentInstance &p) {
                               if (p.base.value >= program_.agents.size()) {
                                   error(verify::kInstanceBaseInvalid,
                                         "agent instance '" + inst.instance_key +
                                             "' base id is out of range",
                                         std::nullopt);
                                   return;
                               }
                               const CoreAgentDecl &base = program_.agents[p.base.value];
                               if (!(p.input_type == base.input_type) ||
                                   p.context_kind != base.context_kind ||
                                   !(p.context_type == base.context_type) ||
                                   !(p.output_type == base.output_type)) {
                                   error(verify::kInstanceShellMismatch,
                                         "agent instance '" + inst.instance_key +
                                             "' shell does not match its nominal agent '" +
                                             base.name + "'",
                                         std::nullopt);
                               }
                           },
                           [&](const CoreWorkflowInstance &p) {
                               if (p.base.value >= program_.workflows.size()) {
                                   error(verify::kInstanceBaseInvalid,
                                         "workflow instance '" + inst.instance_key +
                                             "' base id is out of range",
                                         std::nullopt);
                                   return;
                               }
                               const CoreWorkflowDecl &base = program_.workflows[p.base.value];
                               if (!(p.input_type == base.input_type) ||
                                   !(p.output_type == base.output_type)) {
                                   error(verify::kInstanceShellMismatch,
                                         "workflow instance '" + inst.instance_key +
                                             "' shell does not match its nominal workflow '" +
                                             base.name + "'",
                                         std::nullopt);
                               }
                           },
                           [&](const CoreFnInstance &) {},
                       },
                       inst.payload);
        }
    }

    // A dispatch type must be a CONCRETE structural type: not Unresolved / Any /
    // Never. (Deep structural well-formedness of nested container/Fn params is a
    // P4 value-type-arena concern; here we reject the top-level non-concrete
    // shapes the mangler should never have emitted for a real instance.)
    [[nodiscard]] static bool type_ref_is_concrete(const ir::TypeRef &t) {
        return t.kind != ir::TypeRefKind::Unresolved && t.kind != ir::TypeRefKind::Any &&
               t.kind != ir::TypeRefKind::Never;
    }

    const CoreProgram &program_;
    std::vector<CoreLowerDiagnostic> diags_;
};

} // namespace

CoreVerifyResult verify_core_program(const CoreProgram &program) {
    Verifier verifier(program);
    return CoreVerifyResult{verifier.run()};
}

} // namespace ahfl::ir::core
