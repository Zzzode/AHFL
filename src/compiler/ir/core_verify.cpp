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
    void verify_types() {
        for (std::uint32_t i = 0; i < program_.types.size(); ++i) {
            const CoreTypeDecl &t = program_.types[i];
            // field_types is parallel to fields; each valid entry must be an
            // in-range type id (it need not be a struct — an enum field type is
            // legal, though it cannot be projected THROUGH; that is enforced at
            // the projection site, not here).
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
        const auto check_state = [&](CoreStateId s, const char *what) {
            if (s.value >= state_count) {
                error(verify::kStateIdOutOfRange,
                      "agent '" + agent.name + "' " + what + " state id " +
                          std::to_string(s.value) + " is out of range (" +
                          std::to_string(state_count) + " states)",
                      std::nullopt);
            }
        };
        if (state_count != 0) {
            check_state(agent.initial, "initial");
        }
        for (const CoreStateId f : agent.finals) {
            check_state(f, "final");
        }
        for (const CoreTransition &tr : agent.transitions) {
            check_state(tr.from, "transition-from");
            check_state(tr.to, "transition-to");
        }
        // Typed shell: a set (non-kInvalid) input/context/output must name a real
        // struct — a projection roots on these, so a non-struct here is a bug.
        const auto check_shell = [&](CoreTypeId t, const char *what) {
            if (t.value != CoreTypeId::kInvalid && !is_struct(t)) {
                error(verify::kTypedShellInvalid,
                      "agent '" + agent.name + "' " + what +
                          " type id is set but does not name a struct type",
                      std::nullopt);
            }
        };
        check_shell(agent.input_type, "input");
        check_shell(agent.context_type, "context");
        check_shell(agent.output_type, "output");
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
    // executable program has none), and checks typed identity + projection of
    // path/construct/qualified nodes. Value-use ORDER is checked separately in
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
        for (const CoreExpr &expr : flow.exprs) {
            std::visit(Overloaded{
                           [&](const CoreLiteralExpr &) {},
                           [&](const CoreValueRefExpr &) {}, // value bounds in stmt walk
                           [&](const CorePathExpr &p) {
                               verify_projection(p.root_type, p.projection, p.projection_resolved,
                                                 p.root_name, expr.source_range);
                           },
                           [&](const CoreQualifiedExpr &q) { verify_qualified(q, expr.source_range); },
                           [&](const CoreUnaryExpr &u) { check_expr_id(u.operand, expr.source_range); },
                           [&](const CoreBinaryExpr &b) {
                               check_expr_id(b.lhs, expr.source_range);
                               check_expr_id(b.rhs, expr.source_range);
                           },
                           [&](const CoreConstructExpr &c) { verify_construct(c, expr.source_range); },
                           [&](const CoreUnsupportedExpr &u) {
                               error(verify::kUnsupportedExpr,
                                     "executable program contains an unlowered '" + u.source_kind +
                                         "' expression",
                                     u.source_range);
                           },
                       },
                       expr.node);
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
        if (c.is_enum_variant) {
            if (c.variant.value >= type.variants.size()) {
                error(verify::kConstructVariantInvalid,
                      "constructor '" + c.type_name + "::" + c.variant_name + "' variant id " +
                          std::to_string(c.variant.value) + " is out of range",
                      range);
            }
            // Enum-variant payload args are positional slots; the type table does
            // not record payload arity, so only duplicate slots are checked below.
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
        for (const CoreFlowState &state : flow.states) {
            if (state.state.value >= state_count) {
                error(verify::kStateIdOutOfRange,
                      "flow '" + flow.agent_name + "' handler state id " +
                          std::to_string(state.state.value) + " is out of range for agent '" +
                          agent.name + "'",
                      std::nullopt);
                continue;
            }
            std::unordered_set<std::uint32_t> defined; // fresh per state (bodies are independent)
            verify_region(flow, state_count, state.body, defined);
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

    // Verify one region's statements in order: def-before-use (branch-scoped),
    // value-id bounds, capability arity, store-place projection, and the
    // no-statement-after-terminator rule.
    void verify_region(const CoreFlowDecl &flow, std::uint32_t state_count, const CoreRegion &region,
                       std::unordered_set<std::uint32_t> &defined) {
        const auto use_value = [&](CoreValueId v, SourceRangeOpt range) {
            if (v.value >= flow.value_count) {
                error(verify::kValueIdOutOfRange,
                      "value id " + std::to_string(v.value) + " is out of range in flow '" +
                          flow.agent_name + "'",
                      range);
                return;
            }
            if (defined.find(v.value) == defined.end()) {
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
            if (!defined.insert(v.value).second) {
                error(verify::kValueRedefined,
                      "value id " + std::to_string(v.value) + " is defined more than once", range);
            }
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
                               if (s.then_region) {
                                   auto branch = defined; // branch-local defs must not escape
                                   verify_region(flow, state_count, *s.then_region, branch);
                               }
                               if (s.else_region) {
                                   auto branch = defined;
                                   verify_region(flow, state_count, *s.else_region, branch);
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
