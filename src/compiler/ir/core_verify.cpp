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
    void verify_expr_arena(const CoreFlowDecl &flow) {
        const auto expr_count = static_cast<std::uint32_t>(flow.exprs.size());
        const auto check_expr_id = [&](CoreExprId e, SourceRangeOpt range) {
            if (e.value >= expr_count) {
                error(verify::kExprIdOutOfRange,
                      "expression id " + std::to_string(e.value) + " is out of range in flow '" +
                          flow.agent_name + "'",
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
                          flow.agent_name + "'",
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

    // 3-color DFS (White/Gray/Black) over the expr reference graph. A back edge
    // to a Gray node is a cycle (a node reachable from itself through operand
    // edges); a Black node is a finished shared DAG node (legal, revisited
    // cheaply). Only CoreUnaryExpr/CoreBinaryExpr carry intra-arena edges in
    // this slice — the other nodes reference values, not exprs.
    void verify_expr_arena_acyclic(const CoreFlowDecl &flow) {
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
                                      flow.agent_name + "'",
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
                const auto arity = type.variant_payloads[c.variant.value].slot_types.size();
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
        verify_expr_arena(flow);

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
            verify_region(flow, state_count, state.body, all_definitions, visible);
        }
    }

    // Recursively collect every CoreValueId a pure expr USES (through the arena).
    void collect_expr_uses(const CoreFlowDecl &flow, CoreExprId id,
                           std::vector<CoreValueId> &out,
                           std::unordered_set<std::uint32_t> &visiting) const {
        if (id.value >= flow.exprs.size() || !visiting.insert(id.value).second) {
            return; // out of range (already reported) or a cycle guard
        }
        std::visit(Overloaded{
                       [&](const CoreLiteralExpr &) {},
                       [&](const CoreValueRefExpr &r) { out.push_back(r.value); },
                       [&](const CorePathExpr &p) {
                           if (p.has_local) {
                               out.push_back(p.local);
                           }
                       },
                       [&](const CoreQualifiedExpr &) {},
                       [&](const CoreUnaryExpr &u) { collect_expr_uses(flow, u.operand, out, visiting); },
                       [&](const CoreBinaryExpr &b) {
                           collect_expr_uses(flow, b.lhs, out, visiting);
                           collect_expr_uses(flow, b.rhs, out, visiting);
                       },
                       [&](const CoreConstructExpr &c) {
                           for (const CoreConstructArg &arg : c.args) {
                               out.push_back(arg.value);
                           }
                       },
                       [&](const CoreUnsupportedExpr &) {},
                   },
                   flow.exprs[id.value].node);
    }

    // Verify one region's statements in order. Two definition sets:
    //   * `all_definitions` (flow-global, shared, never rolled back): SSA single
    //     definition — a value defined twice ANYWHERE is a redefinition.
    //   * `visible` (region-local, copied into each branch): def-before-use +
    //     scope — a branch-local definition must not be visible to a sibling
    //     branch or after the `if`.
    void verify_region(const CoreFlowDecl &flow, std::uint32_t state_count, const CoreRegion &region,
                       std::unordered_set<std::uint32_t> &all_definitions,
                       std::unordered_set<std::uint32_t> &visible) {
        const auto use_value = [&](CoreValueId v, SourceRangeOpt range) {
            if (v.value >= flow.value_count) {
                error(verify::kValueIdOutOfRange,
                      "value id " + std::to_string(v.value) + " is out of range in flow '" +
                          flow.agent_name + "'",
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
                          flow.agent_name + "'",
                      range);
                return;
            }
            // Flow-global single definition: reject a second definition anywhere.
            if (!all_definitions.insert(v.value).second) {
                error(verify::kValueRedefined,
                      "value id " + std::to_string(v.value) + " is defined more than once", range);
            }
            // Also mark it visible in the current scope for subsequent uses.
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

        bool terminated = false;
        for (const CoreStmt &stmt : region.statements) {
            if (terminated) {
                error(verify::kStmtAfterTerminator,
                      "statement follows a terminator (goto/return) in flow '" + flow.agent_name +
                          "'",
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
                               // Branch-local definitions must not escape: each
                               // branch gets its OWN copy of `visible` (rolled
                               // back after), but SHARES `all_definitions` so a
                               // value defined in both branches is still caught
                               // as a flow-global redefinition.
                               if (s.then_region) {
                                   auto branch_visible = visible;
                                   verify_region(flow, state_count, *s.then_region, all_definitions,
                                                 branch_visible);
                               }
                               if (s.else_region) {
                                   auto branch_visible = visible;
                                   verify_region(flow, state_count, *s.else_region, all_definitions,
                                                 branch_visible);
                               }
                           },
                           [&](const CoreGotoStmt &s) {
                               if (s.target.value >= state_count) {
                                   error(verify::kGotoTargetInvalid,
                                         "goto target state id " + std::to_string(s.target.value) +
                                             " is out of range in flow '" + flow.agent_name + "'",
                                         stmt.source_range);
                               }
                               terminated = true;
                           },
                           [&](const CoreReturnStmt &s) {
                               if (s.has_value) {
                                   use_value(s.value, stmt.source_range);
                               }
                               terminated = true;
                           },
                       },
                       stmt.node);
        }
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
