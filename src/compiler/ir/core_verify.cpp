// ---------------------------------------------------------------------------
// Core-IR structural verifier (RFC 0026 P3 / KR6.4)
// ---------------------------------------------------------------------------
//
// See core_verify.hpp for the invariant catalogue. The verifier is pure and
// fail-closed: it never throws and never mutates the program; every violation
// becomes an ERROR diagnostic with a stable `core.verify.*` code and (where a
// node carries one) a source range.

#include "ahfl/compiler/ir/core_recursion.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"

#include "ahfl/base/support/overloaded.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

namespace ahfl::ir::core {

namespace {

// RFC 0027 P6/P7/P8 (KR6.13-P7): every handler group over a Core-IR node
// family in this file is generated from that family's production X-macro .def
// list (core_expr_nodes.def / core_pattern_nodes.def / core_stmt_nodes.def), with
// ONE handler per alternative. Genuinely-empty alternatives (childless /
// child-free nodes) are bound to a name-mangled no-op INSIDE each group (e.g.
// CORE_EXPR_ACYCLIC_CoreLiteralExpr), so the empty semantics stay visible in one
// place and a NEW node is a compile error instead of a silently skipped case
// (CLAUDE.md Principle 5).

// RFC 0026 P4-B (P1): the type a value-yielding region's yields must have.
// Threaded THROUGH nested `if` branches so `if { yield W } else { yield W }`
// inside a guard / expression arm is type-checked, not just the top-level yield.
// `Bool` (a guard result) and `Exact` (an expression match arm ==
// value_types[match.result]) are both HARD: an unexpected type fails closed even
// when the value-type pool lacks a Bool node (a missing expected node must not
// silently disable the rule). `None` = no per-yield type constraint (statement
// arm / workflow region — those are gated by arity elsewhere).
struct ExpectedYield {
    enum class Kind { None, Bool, Exact } kind{Kind::None};
    CoreValueTypeId exact{}; // meaningful only when kind == Exact
};

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
        verify_capabilities();
        for (const CoreFlowDecl &flow : program_.flows) {
            verify_flow(flow);
        }
        for (std::uint32_t i = 0; i < program_.workflows.size(); ++i) {
            verify_workflow(program_.workflows[i], i);
        }
        verify_value_types();
        verify_instances();
        verify_fns();
        verify_closures();
        verify_fn_effect_authorization();
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
                if (t.field_nominal_types.size() != t.fields.size()) {
                    shape_error("field_nominal_types size (" +
                                std::to_string(t.field_nominal_types.size()) +
                                ") != fields size (" + std::to_string(t.fields.size()) + ")");
                }
                if (t.field_type_template_roots.size() != t.fields.size()) {
                    shape_error("field_type_template_roots size (" +
                                std::to_string(t.field_type_template_roots.size()) +
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
                if (!t.fields.empty() || !t.field_nominal_types.empty() ||
                    !t.field_has_default.empty() || !t.field_type_template_roots.empty()) {
                    shape_error("an enum must not carry struct field metadata");
                }
                // Each variant payload's kind must agree with its vectors.
                for (std::uint32_t v = 0; v < t.variant_payloads.size(); ++v) {
                    const auto &p = t.variant_payloads[v];
                    using PK = CoreTypeDecl::VariantPayload::Kind;
                    const bool legal_kind =
                        p.kind == PK::Unit || p.kind == PK::Tuple || p.kind == PK::Struct;
                    if (!legal_kind) {
                        shape_error("variant #" + std::to_string(v) +
                                    " has an illegal payload kind");
                    } else if (p.kind == PK::Unit) {
                        if (!p.slot_type_template_roots.empty() || !p.field_names.empty()) {
                            shape_error("variant #" + std::to_string(v) +
                                        " is Unit but carries payload slots");
                        }
                    } else if (p.kind == PK::Tuple) {
                        if (!p.field_names.empty()) {
                            shape_error("variant #" + std::to_string(v) +
                                        " is Tuple but carries field names");
                        }
                    } else { // Struct payload
                        if (p.field_names.size() != p.slot_type_template_roots.size()) {
                            shape_error("variant #" + std::to_string(v) +
                                        " struct payload field_names/template-roots size mismatch");
                        }
                    }
                }
            }
            verify_member_type_templates(t, shape_error);
            // Each valid field_nominal_types entry must be an in-range type id (it need
            // not be a struct — an enum field type is legal, though it cannot be
            // projected THROUGH; that is enforced at the projection site).
            for (std::uint32_t f = 0; f < t.field_nominal_types.size(); ++f) {
                const CoreTypeId ft = t.field_nominal_types[f];
                if (ft.value != CoreTypeId::kInvalid && ft.value >= program_.types.size()) {
                    error(verify::kTypeIdOutOfRange,
                          "type '" + t.name + "' field #" + std::to_string(f) +
                              " references out-of-range type id " + std::to_string(ft.value),
                          std::nullopt);
                }
            }
            // Role <-> kind + known-arity consistency (RFC 0026 P4): the std
            // nominal roles pin both the Struct/Enum kind and the generic arity, so
            // a hand-built `role=List, kind=Enum` (which would let a bogus capacity
            // slip through the value-type verifier) is rejected here at the SSOT.
            verify_nominal_role(t, shape_error);
        }
    }

    void verify_member_type_templates(const CoreTypeDecl &t,
                                      const std::function<void(std::string)> &shape_error) {
        const auto &nodes = t.member_type_templates;
        std::vector<CoreMemberTypeTemplateNodeId> roots = t.field_type_template_roots;
        for (const auto &payload : t.variant_payloads) {
            roots.insert(roots.end(),
                         payload.slot_type_template_roots.begin(),
                         payload.slot_type_template_roots.end());
        }
        std::vector<bool> reachable(nodes.size(), false);
        const auto mark = [&](auto &&self, CoreMemberTypeTemplateNodeId id) -> void {
            if (id.value >= nodes.size()) {
                shape_error("member template root/child id " + std::to_string(id.value) +
                            " is out of range");
                return;
            }
            if (reachable[id.value]) {
                return;
            }
            reachable[id.value] = true;
            const auto &node = nodes[id.value];
            for (const auto child : node.children) {
                if (child.value >= id.value) {
                    shape_error("member template child must precede parent #" +
                                std::to_string(id.value));
                    continue;
                }
                self(self, child);
            }
            if (node.kind == CoreMemberTypeTemplateKind::Fn) {
                if (node.fn_return.value >= id.value) {
                    shape_error("member template Fn return must precede parent #" +
                                std::to_string(id.value));
                } else {
                    self(self, node.fn_return);
                }
            }
        };
        for (const auto root : roots) {
            mark(mark, root);
        }
        for (std::size_t i = 0; i < reachable.size(); ++i) {
            if (!reachable[i]) {
                shape_error("member template arena contains orphan node #" + std::to_string(i));
            }
        }

        for (std::uint32_t i = 0; i < nodes.size(); ++i) {
            const auto &node = nodes[i];
            using K = CoreMemberTypeTemplateKind;
            const bool legal_kind = node.kind == K::Concrete || node.kind == K::Param ||
                                    node.kind == K::Nominal || node.kind == K::Fn;
            if (!legal_kind) {
                shape_error("member template node #" + std::to_string(i) + " has an illegal kind");
                continue;
            }
            const bool has_concrete = node.concrete.value != CoreValueTypeId::kInvalid;
            const bool has_nominal = node.nominal.value != CoreTypeId::kInvalid;
            const bool has_return = node.fn_return.value != CoreMemberTypeTemplateNodeId::kInvalid;
            if (node.kind == K::Concrete) {
                if (!has_concrete || has_nominal || node.param_index != 0 ||
                    node.capacity.has_value() || !node.children.empty() || has_return) {
                    shape_error("Concrete member template node #" + std::to_string(i) +
                                " has invalid fields");
                    continue;
                }
                if (node.concrete.value >= program_.value_types.size() ||
                    std::holds_alternative<CoreVtNever>(
                        program_.value_types[node.concrete.value].node)) {
                    shape_error("Concrete member template node #" + std::to_string(i) +
                                " references an invalid materialized value type");
                }
            } else if (node.kind == K::Param) {
                if (has_concrete || has_nominal || node.capacity.has_value() ||
                    !node.children.empty() || has_return ||
                    node.param_index >= t.type_param_count) {
                    shape_error("Param member template node #" + std::to_string(i) +
                                " has invalid fields/index");
                }
            } else if (node.kind == K::Nominal) {
                if (has_concrete || !has_nominal || node.param_index != 0 || has_return ||
                    node.nominal.value >= program_.types.size()) {
                    shape_error("Nominal member template node #" + std::to_string(i) +
                                " has invalid fields/base");
                    continue;
                }
                const CoreTypeDecl &base = program_.types[node.nominal.value];
                if (node.children.size() != base.type_param_count) {
                    shape_error("Nominal member template node #" + std::to_string(i) +
                                " has wrong type-argument arity");
                }
                if (node.capacity.has_value() && !capacity_allowed(base.role)) {
                    shape_error("Nominal member template node #" + std::to_string(i) +
                                " carries an illegal capacity");
                }
            } else {
                if (has_concrete || has_nominal || node.param_index != 0 ||
                    node.capacity.has_value() || !has_return) {
                    shape_error("Fn member template node #" + std::to_string(i) +
                                " has invalid fields/return");
                }
            }
        }
    }

    void verify_nominal_role(const CoreTypeDecl &t,
                             const std::function<void(std::string)> &shape_error) {
        using K = CoreTypeDecl::Kind;
        // The single builtin nominal SSOT drives BIDIRECTIONAL consistency (Codex
        // P1-2): a non-Ordinary role must exactly match its unique descriptor
        // (canonical + kind + arity), AND a canonical that hits a descriptor must
        // carry that descriptor's role/kind/arity. This blocks both
        // `name=evil::Foo, role=List` (a user type faking a collection to smuggle a
        // capacity) and `name=std::collections::List, role=Ordinary`.
        const BuiltinNominalDescriptor *by_role = nullptr;
        const BuiltinNominalDescriptor *by_name = nullptr;
        for (const auto &d : builtin_nominal_table()) {
            if (d.role == t.role && t.role != CoreNominalRole::Ordinary) {
                by_role = &d;
            }
            if (t.name == d.canonical_name) {
                by_name = &d;
            }
        }
        const auto require_matches = [&](const BuiltinNominalDescriptor &d, const char *why) {
            if (t.name != d.canonical_name) {
                shape_error(std::string(why) + ": canonical must be '" +
                            std::string(d.canonical_name) + "'");
            }
            if (t.kind != d.kind) {
                shape_error(std::string(why) + ": kind must be " +
                            std::string(d.kind == K::Struct ? "struct" : "enum"));
            }
            if (t.type_param_count != d.type_param_count) {
                shape_error(std::string(why) + ": arity must be " +
                            std::to_string(d.type_param_count));
            }
            if (t.variances != d.variances) {
                shape_error(std::string(why) +
                            ": per-parameter variance must match the builtin descriptor");
            }
            if (t.role != d.role) {
                shape_error(std::string(why) +
                            ": role must match the builtin descriptor for this canonical");
            }
            if (t.kind == K::Enum) {
                if (t.variant_payloads.size() != d.variants.size() ||
                    t.variants.size() != d.variants.size()) {
                    shape_error(std::string(why) + ": variants must match the builtin descriptor");
                } else {
                    for (std::size_t v = 0; v < d.variants.size(); ++v) {
                        const auto &expected = d.variants[v];
                        const auto &payload = t.variant_payloads[v];
                        if (t.variants[v] != expected.name ||
                            payload.kind != expected.payload_kind ||
                            payload.slot_type_template_roots.size() !=
                                expected.payload_type_params.size()) {
                            shape_error(std::string(why) + ": variant #" + std::to_string(v) +
                                        " template shape must match the builtin descriptor");
                            continue;
                        }
                        for (std::size_t s = 0; s < expected.payload_type_params.size(); ++s) {
                            const auto root = payload.slot_type_template_roots[s];
                            if (root.value >= t.member_type_templates.size()) {
                                continue; // the general template verifier reports the bound
                            }
                            const auto &node = t.member_type_templates[root.value];
                            if (node.kind != CoreMemberTypeTemplateKind::Param ||
                                node.param_index != expected.payload_type_params[s]) {
                                shape_error(std::string(why) + ": variant #" + std::to_string(v) +
                                            " slot #" + std::to_string(s) +
                                            " must reference the descriptor parameter");
                            }
                        }
                    }
                }
            } else if (!t.member_type_templates.empty() || !t.field_type_template_roots.empty()) {
                shape_error(std::string(why) +
                            ": builtin collection must not carry member templates");
            }
        };
        // RFC 0026 P4 (coercion): every nominal's variance vector must be parallel
        // to its declared arity (the coercion verifier indexes variances by
        // type-arg position). Empty is legal ONLY for a non-generic nominal.
        if (t.variances.size() != t.type_param_count) {
            shape_error("nominal variance vector length " + std::to_string(t.variances.size()) +
                        " does not equal type_param_count " + std::to_string(t.type_param_count));
        }
        // Every entry must be a legal CoreVariance enumerator (Codex P1-1): a
        // deserialized / hand-built decl could carry an out-of-range underlying
        // value that later switch coverage would silently mishandle.
        for (std::size_t index = 0; index < t.variances.size(); ++index) {
            const auto v = t.variances[index];
            const bool legal = v == CoreVariance::Invariant || v == CoreVariance::Covariant ||
                               v == CoreVariance::Contravariant;
            if (!legal) {
                shape_error(
                    "nominal variance[" + std::to_string(index) +
                    "] is an illegal CoreVariance enumerator value " +
                    std::to_string(static_cast<std::underlying_type_t<CoreVariance>>(v)));
            }
        }
        if (by_role != nullptr) {
            require_matches(*by_role, "nominal role");
        }
        if (by_name != nullptr) {
            require_matches(*by_name, "builtin canonical");
        }
        // symbol_ref provenance: when present it must be a Type ref whose canonical
        // agrees with the decl name; a name-only synthetic base may omit the id.
        const auto &sym = t.symbol_ref;
        const bool sym_present = sym.kind != ir::SymbolRefKind::Unknown ||
                                 !sym.canonical_name.empty() || sym.id.has_value();
        if (sym_present) {
            if (sym.kind != ir::SymbolRefKind::Type) {
                shape_error("nominal symbol_ref is present but not a Type symbol");
            }
            if (!sym.canonical_name.empty() && sym.canonical_name != t.name) {
                shape_error("nominal symbol_ref canonical '" + sym.canonical_name +
                            "' drifts from type name '" + t.name + "'");
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
                  "agent '" + agent.name + "' declares no states", agent.source_range);
        }
        const auto check_state = [&](CoreStateId s, const char *what) {
            if (s.value >= state_count) {
                error(verify::kStateIdOutOfRange,
                      "agent '" + agent.name + "' " + what + " state id " +
                          std::to_string(s.value) + " is out of range (" +
                          std::to_string(state_count) + " states)",
                      agent.source_range);
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
                      agent.source_range);
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
                      agent.source_range);
        }

        std::unordered_set<std::uint32_t> seen_capabilities;
        for (const CoreCapabilityId capability : agent.capabilities) {
            if (capability.value >= program_.capabilities.size()) {
                error(verify::kCapabilityWhitelistInvalid,
                      "agent '" + agent.name + "' whitelist capability id " +
                          std::to_string(capability.value) + " is out of range",
                      agent.source_range);
            } else if (!seen_capabilities.insert(capability.value).second) {
                error(verify::kCapabilityWhitelistInvalid,
                      "agent '" + agent.name + "' whitelist contains capability id " +
                          std::to_string(capability.value) + " more than once",
                      agent.source_range);
            }
        }
    }

    // --- capability import shell ---
    void verify_capabilities() {
        std::unordered_set<std::size_t> seen_symbol_ids;
        for (const CoreCapabilityDecl &cap : program_.capabilities) {
            if (cap.symbol_ref.kind != ir::SymbolRefKind::Capability ||
                !cap.symbol_ref.id.has_value()) {
                error(verify::kCapabilitySymbolInvalid,
                      "capability '" + cap.name +
                          "' must carry a canonical Capability SymbolId",
                      cap.source_range);
            } else if (!seen_symbol_ids.insert(*cap.symbol_ref.id).second) {
                error(verify::kCapabilitySymbolInvalid,
                      "capability '" + cap.name + "' duplicates SymbolId " +
                          std::to_string(*cap.symbol_ref.id),
                      cap.source_range);
            }

            for (std::uint32_t index = 0; index < cap.param_types.size(); ++index) {
                if (!value_type_slot_ok(cap.param_types[index])) {
                    error(verify::kCapabilitySignatureInvalid,
                          "capability '" + cap.name + "' parameter #" +
                              std::to_string(index) +
                              " is not a valid materialized Core value type",
                          cap.source_range);
                }
            }
            if (!value_type_slot_ok(cap.return_type)) {
                error(verify::kCapabilitySignatureInvalid,
                      "capability '" + cap.name +
                          "' return is not a valid materialized Core value type",
                      cap.source_range);
            }
        }
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
            const CoreTypeId declared = step.field.value < owner.field_nominal_types.size()
                                            ? owner.field_nominal_types[step.field.value]
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
    enum class OwnerKind { Flow, Workflow, Fn };

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
        // RFC 0026 P4-B: the body's per-value logical type table (index ==
        // CoreValueId). Dense (size == value_count) in a well-formed body; used to
        // prove CoreLetStmt / CoreValueRefExpr result-type consistency.
        const std::vector<CoreValueTypeId> *value_types{nullptr};
        const std::vector<CoreCoercionPlanNode> *coercion_plans{nullptr};
        // Flow-only authorization set resolved from the target agent. Workflow
        // bodies deliberately leave this null: capability calls there are
        // rejected as outside the language's Flow-only boundary.
        const std::unordered_set<std::uint32_t> *allowed_capabilities{nullptr};
    };

    // The recorded logical value type of a value id in a body (kInvalid sentinel
    // when the table is missing or the id is out of range — the caller's separate
    // size / bounds checks report those).
    [[nodiscard]] CoreValueTypeId body_value_type(const ArenaView &flow, CoreValueId v) const {
        if (flow.value_types == nullptr || v.value >= flow.value_types->size()) {
            return CoreValueTypeId{};
        }
        return (*flow.value_types)[v.value];
    }

    // Whether a logical value-type id is a materialized value's type: in range AND
    // not `Never` (an uninhabited type cannot be the type of a produced value —
    // the same consumer-context rule dispatch types use). kInvalid is rejected.
    [[nodiscard]] bool value_type_slot_ok(CoreValueTypeId id) const {
        if (id.value == CoreValueTypeId::kInvalid || id.value >= program_.value_types.size()) {
            return false;
        }
        return !std::holds_alternative<CoreVtNever>(program_.value_types[id.value].node);
    }

    // Whether a logical value-type id is the boolean type (RFC 0026 P4-B): used
    // to type-check a condition value and a guard's yielded value.
    [[nodiscard]] bool is_bool_value_type(CoreValueTypeId id) const {
        return id.value != CoreValueTypeId::kInvalid && id.value < program_.value_types.size() &&
               std::holds_alternative<CoreVtBool>(program_.value_types[id.value].node);
    }

    // RFC 0026 P4-B: the body's per-value type table must be DENSE (one entry per
    // allocated value id) and every entry a valid, in-range, non-Never value type.
    void verify_body_value_types(const ArenaView &flow) {
        const auto *table = flow.value_types;
        const auto table_size =
            table != nullptr ? static_cast<std::uint32_t>(table->size()) : 0u;
        if (table_size != flow.value_count) {
            error(verify::kValueTypesSizeMismatch,
                  "body '" + flow.label + "' value_types table size " +
                      std::to_string(table_size) + " does not equal value_count " +
                      std::to_string(flow.value_count),
                  std::nullopt);
        }
        if (table == nullptr) {
            return;
        }
        for (std::uint32_t v = 0; v < table_size; ++v) {
            if (!value_type_slot_ok((*table)[v])) {
                error(verify::kValueTypeSlotInvalid,
                      "body '" + flow.label + "' value id " + std::to_string(v) +
                          " has an invalid logical value type (out-of-range id or a `Never`, which "
                          "cannot be a materialized value type)",
                      std::nullopt);
            }
        }
    }

    // RFC 0026 P4 coercion: verify the normalized, per-body proof-plan arena.
    // This is structural proof checking, not a second subtype solver: each op
    // names one changed dimension and all unnamed dimensions must be identical.
    void verify_coercion_plans(const ArenaView &flow) {
        if (flow.coercion_plans == nullptr) {
            return;
        }
        const auto &plans = *flow.coercion_plans;
        const auto plan_count = static_cast<std::uint32_t>(plans.size());
        const auto plan_error = [&](std::string_view code, std::string message) {
            error(code, "body '" + flow.label + "': " + std::move(message), std::nullopt);
        };
        const auto child_valid = [&](CoreCoercionPlanId id) {
            return id.value != CoreCoercionPlanId::kInvalid && id.value < plan_count;
        };

        // Every plan node must be reachable from some CoreCoerceExpr root. This
        // forbids partially-appended/orphan normalization artifacts.
        std::vector<bool> reachable(plan_count, false);
        std::vector<std::uint32_t> stack;
        for (const CoreExpr &expr : flow.exprs) {
            if (const auto *coerce = std::get_if<CoreCoerceExpr>(&expr.node);
                coerce != nullptr && coerce->plan.value < plan_count) {
                stack.push_back(coerce->plan.value);
            }
        }
        while (!stack.empty()) {
            const std::uint32_t id = stack.back();
            stack.pop_back();
            if (id >= plan_count || reachable[id]) {
                continue;
            }
            reachable[id] = true;
            for (const CoreCoercionOp &op : plans[id].ops) {
                if (child_valid(op.child)) {
                    stack.push_back(op.child.value);
                }
            }
        }
        for (std::uint32_t id = 0; id < plan_count; ++id) {
            if (!reachable[id]) {
                plan_error(verify::kCoercionInvalid,
                           "coercion plan node #" + std::to_string(id) +
                               " is not reachable from a CoreCoerceExpr");
            }
        }

        // Iterative 3-color DFS over child-plan edges.
        enum class Color : std::uint8_t { White, Gray, Black };
        std::vector<Color> color(plan_count, Color::White);
        struct Frame {
            std::uint32_t id;
            std::size_t next;
        };
        for (std::uint32_t root = 0; root < plan_count; ++root) {
            if (color[root] != Color::White) {
                continue;
            }
            std::vector<Frame> dfs{{root, 0}};
            color[root] = Color::Gray;
            while (!dfs.empty()) {
                Frame &frame = dfs.back();
                const auto &ops = plans[frame.id].ops;
                bool descended = false;
                while (frame.next < ops.size()) {
                    const CoreCoercionPlanId child = ops[frame.next++].child;
                    if (!child_valid(child)) {
                        continue;
                    }
                    if (color[child.value] == Color::Gray) {
                        plan_error(verify::kCoercionInvalid,
                                   "coercion plan contains a cycle through node #" +
                                       std::to_string(child.value));
                    } else if (color[child.value] == Color::White) {
                        color[child.value] = Color::Gray;
                        dfs.push_back(Frame{child.value, 0});
                        descended = true;
                        break;
                    }
                }
                if (!descended && frame.next >= ops.size()) {
                    color[frame.id] = Color::Black;
                    dfs.pop_back();
                }
            }
        }

        const auto int_widen = [](const CoreVtInt &source, const CoreVtInt &result) {
            if (!source.bounds.has_value()) {
                return false;
            }
            if (!result.bounds.has_value()) {
                return true;
            }
            return source.bounds->first >= result.bounds->first &&
                   source.bounds->second <= result.bounds->second && source.bounds != result.bounds;
        };
        const auto string_widen = [](const CoreVtString &source, const CoreVtString &result) {
            if (!source.length_bounds.has_value()) {
                return false;
            }
            if (!result.length_bounds.has_value()) {
                return true;
            }
            return source.length_bounds->first >= result.length_bounds->first &&
                   source.length_bounds->second <= result.length_bounds->second &&
                   source.length_bounds != result.length_bounds;
        };
        const auto capacity_widen = [](const CoreVtNominal &source, const CoreVtNominal &result) {
            if (!source.capacity.has_value()) {
                return false;
            }
            if (!result.capacity.has_value()) {
                return true;
            }
            return *source.capacity < *result.capacity;
        };

        for (std::uint32_t id = 0; id < plan_count; ++id) {
            const auto &node = plans[id];
            const auto label = "coercion plan node #" + std::to_string(id);
            if (!value_type_slot_ok(node.source) || !value_type_slot_ok(node.result)) {
                plan_error(verify::kCoercionInvalid,
                           label + " has an invalid or uninhabited endpoint value type");
                continue;
            }
            if (node.ops.empty()) {
                plan_error(verify::kCoercionIdentity,
                           label + " is an identity node retained after normalization");
                continue;
            }

            const auto &source_vt = program_.value_types[node.source.value].node;
            const auto &result_vt = program_.value_types[node.result.value].node;
            const auto *source_nominal = std::get_if<CoreVtNominal>(&source_vt);
            const auto *result_nominal = std::get_if<CoreVtNominal>(&result_vt);
            const auto *source_fn = std::get_if<CoreVtFn>(&source_vt);
            const auto *result_fn = std::get_if<CoreVtFn>(&result_vt);
            std::unordered_set<std::uint32_t> type_args;
            std::unordered_set<std::uint32_t> fn_params;
            bool has_capacity = false;
            bool has_fn_return = false;
            bool has_int = false;
            bool has_string = false;
            std::optional<std::pair<unsigned char, std::uint32_t>> previous_order;

            for (const CoreCoercionOp &op : node.ops) {
                const bool projected = op.kind == CoreCoercionOpKind::TypeArg ||
                                       op.kind == CoreCoercionOpKind::FnParam ||
                                       op.kind == CoreCoercionOpKind::FnReturn;
                const bool indexed = op.kind == CoreCoercionOpKind::TypeArg ||
                                     op.kind == CoreCoercionOpKind::FnParam;
                if (!indexed && op.arg_index != 0) {
                    plan_error(verify::kCoercionKindMismatch,
                               label + " has a non-indexed op with non-zero arg_index");
                }
                if (projected) {
                    if (!child_valid(op.child)) {
                        plan_error(verify::kCoercionInvalid,
                                   label + " has a projected op with an invalid child plan id");
                    }
                } else if (op.child.value != CoreCoercionPlanId::kInvalid) {
                    plan_error(verify::kCoercionKindMismatch,
                               label + " has a leaf op with a child plan id");
                }

                std::optional<std::pair<unsigned char, std::uint32_t>> order;
                bool known_kind = true;
                switch (op.kind) {
                case CoreCoercionOpKind::CapacityWiden:
                case CoreCoercionOpKind::FnParam:
                    order = std::pair{static_cast<unsigned char>(0), op.arg_index};
                    break;
                case CoreCoercionOpKind::TypeArg:
                case CoreCoercionOpKind::FnReturn:
                    order = std::pair{static_cast<unsigned char>(1), op.arg_index};
                    break;
                case CoreCoercionOpKind::IntWiden:
                case CoreCoercionOpKind::StringWiden:
                    break;
                default:
                    known_kind = false;
                    plan_error(verify::kCoercionKindMismatch,
                               label + " carries an unknown coercion operation kind");
                    break;
                }
                if (!known_kind) {
                    continue;
                }
                if (order.has_value()) {
                    if (previous_order.has_value() && *order <= *previous_order) {
                        plan_error(verify::kCoercionKindMismatch,
                                   label + " operations are not in canonical order");
                    }
                    previous_order = order;
                }

                switch (op.kind) {
                case CoreCoercionOpKind::IntWiden: {
                    has_int = true;
                    const auto *source = std::get_if<CoreVtInt>(&source_vt);
                    const auto *result = std::get_if<CoreVtInt>(&result_vt);
                    if (node.ops.size() != 1 || source == nullptr || result == nullptr) {
                        plan_error(verify::kCoercionKindMismatch,
                                   label + " IntWiden does not name a sole Int dimension");
                    } else if (!int_widen(*source, *result)) {
                        plan_error(verify::kCoercionInvalid,
                                   label + " IntWiden endpoints are not a strict bounds widening");
                    }
                    break;
                }
                case CoreCoercionOpKind::StringWiden: {
                    has_string = true;
                    const auto *source = std::get_if<CoreVtString>(&source_vt);
                    const auto *result = std::get_if<CoreVtString>(&result_vt);
                    if (node.ops.size() != 1 || source == nullptr || result == nullptr) {
                        plan_error(verify::kCoercionKindMismatch,
                                   label + " StringWiden does not name a sole String dimension");
                    } else if (!string_widen(*source, *result)) {
                        plan_error(verify::kCoercionInvalid,
                                   label +
                                       " StringWiden endpoints are not a strict bounds widening");
                    }
                    break;
                }
                case CoreCoercionOpKind::CapacityWiden:
                    if (has_capacity) {
                        plan_error(verify::kCoercionKindMismatch,
                                   label + " contains duplicate CapacityWiden operations");
                    }
                    has_capacity = true;
                    if (source_nominal == nullptr || result_nominal == nullptr ||
                        !(source_nominal->base == result_nominal->base) ||
                        source_nominal->base.value >= program_.types.size() ||
                        !capacity_allowed(program_.types[source_nominal->base.value].role)) {
                        plan_error(verify::kCoercionKindMismatch,
                                   label + " CapacityWiden does not name a collection capacity");
                    } else if (!capacity_widen(*source_nominal, *result_nominal)) {
                        plan_error(verify::kCoercionInvalid,
                                   label + " CapacityWiden is not a strict capacity widening");
                    }
                    break;
                case CoreCoercionOpKind::TypeArg: {
                    if (!type_args.insert(op.arg_index).second) {
                        plan_error(verify::kCoercionKindMismatch,
                                   label + " contains duplicate TypeArg positions");
                    }
                    if (source_nominal == nullptr || result_nominal == nullptr ||
                        !(source_nominal->base == result_nominal->base) ||
                        op.arg_index >= source_nominal->args.size() ||
                        op.arg_index >= result_nominal->args.size() ||
                        source_nominal->base.value >= program_.types.size()) {
                        plan_error(verify::kCoercionKindMismatch,
                                   label + " TypeArg position does not match a nominal node");
                        break;
                    }
                    const auto &decl = program_.types[source_nominal->base.value];
                    if (op.arg_index >= decl.variances.size()) {
                        plan_error(verify::kCoercionVarianceInvalid,
                                   label + " TypeArg has no declaration variance metadata");
                        break;
                    }
                    const CoreVariance variance = decl.variances[op.arg_index];
                    if (variance == CoreVariance::Invariant) {
                        plan_error(verify::kCoercionVarianceInvalid,
                                   label + " TypeArg is not legal at an invariant position");
                        break;
                    }
                    if (variance != CoreVariance::Covariant &&
                        variance != CoreVariance::Contravariant) {
                        plan_error(verify::kCoercionVarianceInvalid,
                                   label + " TypeArg declaration variance is invalid");
                        break;
                    }
                    if (!child_valid(op.child)) {
                        break;
                    }
                    const auto &child = plans[op.child.value];
                    const bool direction =
                        variance == CoreVariance::Covariant
                            ? child.source == source_nominal->args[op.arg_index] &&
                                  child.result == result_nominal->args[op.arg_index]
                            : child.source == result_nominal->args[op.arg_index] &&
                                  child.result == source_nominal->args[op.arg_index];
                    if (!direction) {
                        plan_error(verify::kCoercionVarianceInvalid,
                                   label + " TypeArg child has the wrong variance direction");
                    }
                    break;
                }
                case CoreCoercionOpKind::FnParam:
                    if (!fn_params.insert(op.arg_index).second) {
                        plan_error(verify::kCoercionKindMismatch,
                                   label + " contains duplicate FnParam positions");
                    }
                    if (source_fn == nullptr || result_fn == nullptr ||
                        op.arg_index >= source_fn->params.size() ||
                        op.arg_index >= result_fn->params.size()) {
                        plan_error(verify::kCoercionKindMismatch,
                                   label + " FnParam position does not match a function node");
                    } else if (child_valid(op.child)) {
                        const auto &child = plans[op.child.value];
                        if (!(child.source == result_fn->params[op.arg_index]) ||
                            !(child.result == source_fn->params[op.arg_index])) {
                            plan_error(verify::kCoercionVarianceInvalid,
                                       label +
                                           " FnParam child has the wrong contravariant direction");
                        }
                    }
                    break;
                case CoreCoercionOpKind::FnReturn:
                    if (has_fn_return) {
                        plan_error(verify::kCoercionKindMismatch,
                                   label + " contains duplicate FnReturn operations");
                    }
                    has_fn_return = true;
                    if (source_fn == nullptr || result_fn == nullptr) {
                        plan_error(verify::kCoercionKindMismatch,
                                   label + " FnReturn does not match a function node");
                    } else if (child_valid(op.child)) {
                        const auto &child = plans[op.child.value];
                        if (!(child.source == source_fn->ret) ||
                            !(child.result == result_fn->ret)) {
                            plan_error(verify::kCoercionVarianceInvalid,
                                       label + " FnReturn child has the wrong covariant direction");
                        }
                    }
                    break;
                }
            }

            if (source_nominal != nullptr && result_nominal != nullptr) {
                if (!(source_nominal->base == result_nominal->base) ||
                    source_nominal->args.size() != result_nominal->args.size()) {
                    plan_error(verify::kCoercionKindMismatch,
                               label + " changes nominal base or argument arity");
                } else {
                    for (std::uint32_t i = 0; i < source_nominal->args.size(); ++i) {
                        if (!type_args.contains(i) &&
                            !(source_nominal->args[i] == result_nominal->args[i])) {
                            plan_error(verify::kCoercionInvalid,
                                       label + " changes an unnamed nominal type argument");
                        }
                    }
                }
                if (!has_capacity && source_nominal->capacity != result_nominal->capacity) {
                    plan_error(verify::kCoercionInvalid,
                               label + " changes an unnamed nominal capacity");
                }
                if (has_int || has_string || !fn_params.empty() || has_fn_return) {
                    plan_error(verify::kCoercionKindMismatch,
                               label + " mixes nominal and non-nominal operations");
                }
            } else if (source_fn != nullptr && result_fn != nullptr) {
                if (source_fn->params.size() != result_fn->params.size()) {
                    plan_error(verify::kCoercionKindMismatch,
                               label + " changes function parameter arity");
                } else {
                    for (std::uint32_t i = 0; i < source_fn->params.size(); ++i) {
                        if (!fn_params.contains(i) &&
                            !(source_fn->params[i] == result_fn->params[i])) {
                            plan_error(verify::kCoercionInvalid,
                                       label + " changes an unnamed function parameter");
                        }
                    }
                }
                if (!has_fn_return && !(source_fn->ret == result_fn->ret)) {
                    plan_error(verify::kCoercionInvalid,
                               label + " changes an unnamed function return type");
                }
                if (has_int || has_string || has_capacity || !type_args.empty()) {
                    plan_error(verify::kCoercionKindMismatch,
                               label + " mixes function and non-function operations");
                }
            } else if (!has_int && !has_string) {
                plan_error(verify::kCoercionKindMismatch,
                           label + " endpoints do not have a supported coercion shape");
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
    // RFC 0026 FB-1 (CORE-FNBODY-DESIGN §8.1 #3/#6): bounds on a direct fn call
    // embedded in a flow/workflow arena — the full callee-resolution / arity /
    // pure-callee / acyclic-graph rules run in verify_fns over the program's fn
    // table and call graph (they need instance/fn context this body-local walk
    // does not have).
    void verify_call_expr(const ArenaView &body, const CoreCallExpr &c, const CoreExpr &expr) {
        if (c.callee.value == CoreInstanceId::kInvalid ||
            c.callee.value >= program_.instances.size()) {
            error(verify::kFnCallCalleeInvalid,
                  "direct call references an out-of-range fn instance id in body '" + body.label +
                      "'",
                  expr.source_range);
        }
        for (const CoreValueId arg : c.args) {
            if (arg.value >= body.value_count) {
                error(verify::kValueIdOutOfRange,
                      "direct call argument value id " + std::to_string(arg.value) +
                          " is out of range in body '" + body.label + "'",
                      expr.source_range);
            }
        }
    }

    // FB-3a1 (design §8.1 #4): per-arena FIELD-SHAPE bounds for a closure
    // construction — the fn id is a valid CoreFnId and every captured operand is
    // an in-range SSA value of THIS body. The capture-count / per-slot-type /
    // result-signature rules need the fn table and run in the program-wide
    // verify_closures pass (like CoreCallExpr's semantic rules in
    // verify_fn_call_sites), so an out-of-range id here does not cascade.
    void verify_closure_expr(const ArenaView &body, const CoreClosureExpr &c,
                             const CoreExpr &expr) {
        if (c.fn.value == CoreFnId::kInvalid || c.fn.value >= program_.fns.size()) {
            error(verify::kClosureFnInvalid,
                  "closure construction in body '" + body.label +
                      "' references an out-of-range fn id",
                  expr.source_range);
        }
        for (const CoreValueId env : c.env) {
            if (env.value >= body.value_count) {
                error(verify::kValueIdOutOfRange,
                      "closure capture value id " + std::to_string(env.value) +
                          " is out of range in body '" + body.label + "'",
                      expr.source_range);
            }
        }
    }

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
            // RFC 0026 P4-B: CoreExpr.result_type is REQUIRED on every arena node
            // (a lowering-clean program). Check it for EVERY expr here, not only
            // where a Let/ValueRef consistency rule happens to read it — an
            // unreferenced node with an out-of-range / kInvalid / Never result
            // type must fail closed at the consumption boundary too.
            if (!value_type_slot_ok(expr.result_type)) {
                error(verify::kValueTypeSlotInvalid,
                      "expression in '" + flow.label +
                          "' has an invalid result type (out-of-range id, kInvalid, or a `Never`, "
                          "which cannot be a materialized value type)",
                      expr.source_range);
            }
            std::visit(Overloaded{
                           [&](const CoreLiteralExpr &) {},
                           [&](const CoreValueRefExpr &r) {
                               check_value_id(r.value, expr.source_range);
                               // RFC 0026 P4-B: an SSA use echoes the referenced
                               // value's recorded logical type.
                               if (r.value.value < flow.value_count &&
                                   flow.value_types != nullptr &&
                                   r.value.value < flow.value_types->size() &&
                                   !(expr.result_type == (*flow.value_types)[r.value.value])) {
                                   error(verify::kValueTypeMismatch,
                                         "value-ref expression result type does not equal the "
                                         "referenced value's recorded type in '" +
                                             flow.label + "'",
                                         expr.source_range);
                               }
                           },
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
                               // Owner-domain legality (P0-2): flow, workflow and
                               // fn bodies do not share path roots. A workflow
                               // value root in a flow, a flow/workflow root in a
                               // fn, or a flow root / bare identifier in a
                               // workflow, is a malformed IR.
                               if (flow.owner == OwnerKind::Flow) {
                                   if (p.root == CorePathRoot::WorkflowInput ||
                                       p.root == CorePathRoot::WorkflowNodeOutput) {
                                       error(verify::kWorkflowPathRootInvalid,
                                             "flow '" + flow.label +
                                                 "' path expression carries a workflow-only root",
                                             expr.source_range);
                                   }
                               } else if (flow.owner == OwnerKind::Fn) {
                                   // A fn body sees only pre-bound params / locals.
                                   if (p.root != CorePathRoot::Local &&
                                       p.root != CorePathRoot::Identifier) {
                                       error(verify::kWorkflowPathRootInvalid,
                                             "fn '" + flow.label +
                                                 "' path expression carries a flow/workflow-only "
                                                 "root (a fn body sees only parameters and locals)",
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
                           [&](const CoreCoerceExpr &c) {
                               check_value_id(c.operand, expr.source_range);
                               const auto plan_count =
                                   flow.coercion_plans != nullptr
                                       ? static_cast<std::uint32_t>(
                                             flow.coercion_plans->size())
                                       : 0u;
                               if (c.plan.value >= plan_count) {
                                   error(verify::kCoercionInvalid,
                                         "coercion expression plan id is out of range in '" +
                                             flow.label + "'",
                                         expr.source_range);
                                   return;
                               }
                               const auto &root = (*flow.coercion_plans)[c.plan.value];
                               if (!(expr.result_type == root.result)) {
                                   error(verify::kCoercionInvalid,
                                         "coercion expression result type does not equal its plan "
                                         "root result in '" +
                                             flow.label + "'",
                                         expr.source_range);
                               }
                               if (!(body_value_type(flow, c.operand) == root.source)) {
                                   error(verify::kCoercionInvalid,
                                         "coercion operand type does not equal its plan root source "
                                         "in '" +
                                             flow.label + "'",
                                         expr.source_range);
                               }
                           },
                           [&](const CoreCollectionExpr &c) {
                               check_value_id(c.base, expr.source_range);
                               if (c.op != CoreCollectionOpKind::Len) {
                                   check_value_id(c.index, expr.source_range);
                               }
                               if (c.op == CoreCollectionOpKind::ElementSet) {
                                   check_value_id(c.value, expr.source_range);
                               }
                               verify_collection_op(flow, c, expr, expr.source_range);
                           },
                           [&](const CoreUnsupportedExpr &u) {
                               error(verify::kUnsupportedExpr,
                                     "executable program contains an unlowered '" + u.source_kind +
                                         "' expression",
                                     u.source_range);
                           },
                           [&](const CoreCallExpr &c) { verify_call_expr(flow, c, expr); },
                           // FB-3a1: field-shape bounds run with every other expr
                           // (fn-link / arity / signature rules need the fn table
                           // and run in the program-wide verify_closures pass).
                           [&](const CoreClosureExpr &c) { verify_closure_expr(flow, c, expr); },
                           [&](const CoreCallClosureExpr &c) {
                               check_value_id(c.callee, expr.source_range);
                               for (const CoreValueId arg : c.args) {
                                   check_value_id(arg, expr.source_range);
                               }
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
                    // RFC 0027 P6/P7/P8 (KR6.13-P7): one handler per CoreExprNode
                    // alternative, generated from core_expr_nodes.def. Only
                    // CoreUnaryExpr / CoreBinaryExpr carry intra-arena operand
                    // edges, so every other node names CORE_EXPR_ACYCLIC_<Node> (an
                    // explicit, named no-op — the same behavior the unnamed
                    // catch-all had). With no generic catch-all, adding a
                    // CoreExprNode alternative is a COMPILE ERROR here until it is
                    // classified (CLAUDE.md Principle 5).
#define CORE_EXPR_ACYCLIC_CoreLiteralExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_ACYCLIC_CoreValueRefExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_ACYCLIC_CorePathExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_ACYCLIC_CoreQualifiedExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_ACYCLIC_CoreConstructExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_ACYCLIC_CoreCoerceExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_ACYCLIC_CoreCollectionExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_ACYCLIC_CoreUnsupportedExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_ACYCLIC_CoreCallExpr(Name, Wire) [](const Name &) {},
// FB-3a1: closure construction / indirect closure call reference only SSA
// values (never intra-arena CoreExprIds), so they carry no acyclic-graph edge.
#define CORE_EXPR_ACYCLIC_CoreClosureExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_ACYCLIC_CoreCallClosureExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_ACYCLIC_CoreUnaryExpr(Name, Wire)                                                       \
    [&](const Name &u) { push_edge(u.operand); },
#define CORE_EXPR_ACYCLIC_CoreBinaryExpr(Name, Wire)                                                      \
    [&](const Name &b) {                                                                            \
        push_edge(b.lhs);                                                                           \
        push_edge(b.rhs);                                                                           \
    },
#define HANDLE_CORE_EXPR_NODE(Name, Wire) CORE_EXPR_ACYCLIC_##Name(Name, Wire)
                    std::visit(
                        Overloaded{
#include "ahfl/compiler/ir/core_expr_nodes.def"
                        },
                        flow.exprs[id].node);
#undef HANDLE_CORE_EXPR_NODE
#undef CORE_EXPR_ACYCLIC_CoreLiteralExpr
#undef CORE_EXPR_ACYCLIC_CoreValueRefExpr
#undef CORE_EXPR_ACYCLIC_CorePathExpr
#undef CORE_EXPR_ACYCLIC_CoreQualifiedExpr
#undef CORE_EXPR_ACYCLIC_CoreConstructExpr
#undef CORE_EXPR_ACYCLIC_CoreCoerceExpr
#undef CORE_EXPR_ACYCLIC_CoreCollectionExpr
#undef CORE_EXPR_ACYCLIC_CoreUnsupportedExpr
#undef CORE_EXPR_ACYCLIC_CoreCallExpr
#undef CORE_EXPR_ACYCLIC_CoreClosureExpr
#undef CORE_EXPR_ACYCLIC_CoreCallClosureExpr
#undef CORE_EXPR_ACYCLIC_CoreUnaryExpr
#undef CORE_EXPR_ACYCLIC_CoreBinaryExpr
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
            // RFC 0027 P6/P7/P8 (KR6.13-P7): one handler per CorePatternNode
            // alternative, generated from core_pattern_nodes.def. Wildcard /
            // literal patterns carry no shape rule (CORE_PATTERN_SHAPE_LEAF);
            // the rest route to a real per-node handler. No generic catch-all: a
            // new pattern alternative is a COMPILE ERROR here until classified.
#define CORE_PATTERN_SHAPE_LEAF(Name, Wire) [](const Name &) {},
#define CORE_PATTERN_SHAPE_CoreWildcardPat(Name, Wire) CORE_PATTERN_SHAPE_LEAF(Name, Wire)
#define CORE_PATTERN_SHAPE_CoreLiteralPat(Name, Wire) CORE_PATTERN_SHAPE_LEAF(Name, Wire)
#define CORE_PATTERN_SHAPE_CoreIntRangePat(Name, Wire)                                                   \
    [&](const Name &r) {                                                                           \
        /* AHFL `..` is a closed interval [start, end]; a reverse range is empty and never */      \
        /* authored. Sema rejects it, but the standalone verifier guards the JSON / backend */     \
        /* consumption boundary too. */                                                            \
        if (r.start > r.end) {                                                                     \
            error(verify::kPatternShapeInvalid,                                                    \
                  "int-range pattern has start (" + std::to_string(r.start) +                      \
                      ") greater than end (" + std::to_string(r.end) + ")",                        \
                  pat.source_range);                                                               \
        }                                                                                          \
    },
#define CORE_PATTERN_SHAPE_CoreBindingPat(Name, Wire)                                                    \
    [&](const Name &b) {                                                                           \
        if (b.has_nested) {                                                                        \
            check_id(b.nested, pat.source_range);                                                  \
        }                                                                                          \
    },
#define CORE_PATTERN_SHAPE_CoreVariantPat(Name, Wire)                                                    \
    [&](const Name &v) { verify_variant_pattern(v, pat_count, pat.source_range); },
#define CORE_PATTERN_SHAPE_CoreTuplePat(Name, Wire)                                                      \
    [&](const Name &t) {                                                                           \
        for (const CorePatternId e : t.elements) {                                                 \
            check_id(e, pat.source_range);                                                         \
        }                                                                                          \
    },
#define CORE_PATTERN_SHAPE_CoreOrPat(Name, Wire)                                                         \
    [&](const Name &o) {                                                                           \
        if (o.alternatives.size() < 2) {                                                           \
            error(verify::kPatternShapeInvalid,                                                    \
                  "or-pattern must have at least two alternatives", pat.source_range);             \
        }                                                                                          \
        for (const CorePatternId alt : o.alternatives) {                                           \
            check_id(alt, pat.source_range);                                                       \
        }                                                                                          \
    },
#define HANDLE_CORE_PATTERN_NODE(Name, Wire) CORE_PATTERN_SHAPE_##Name(Name, Wire)
            std::visit(
                Overloaded{
#include "ahfl/compiler/ir/core_pattern_nodes.def"
                },
                pat.node);
#undef HANDLE_CORE_PATTERN_NODE
#undef CORE_PATTERN_SHAPE_LEAF
#undef CORE_PATTERN_SHAPE_CoreWildcardPat
#undef CORE_PATTERN_SHAPE_CoreLiteralPat
#undef CORE_PATTERN_SHAPE_CoreIntRangePat
#undef CORE_PATTERN_SHAPE_CoreBindingPat
#undef CORE_PATTERN_SHAPE_CoreVariantPat
#undef CORE_PATTERN_SHAPE_CoreTuplePat
#undef CORE_PATTERN_SHAPE_CoreOrPat
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
        const auto arity = static_cast<std::uint32_t>(payload.slot_type_template_roots.size());
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
                    // RFC 0027 P6/P7/P8 (KR6.13-P7): one handler per
                    // CorePatternNode alternative, generated from
                    // core_pattern_nodes.def. Only CoreBindingPat / CoreVariantPat
                    // / CoreTuplePat / CoreOrPat carry sub-pattern edges, so
                    // those route above; the rest name CORE_PATTERN_ACYCLIC_<Node>
                    // (explicit, named no-op). No generic catch-all: a new
                    // pattern alternative is a COMPILE ERROR here until classified.
#define CORE_PATTERN_ACYCLIC_CoreWildcardPat(Name, Wire) [](const Name &) {},
#define CORE_PATTERN_ACYCLIC_CoreLiteralPat(Name, Wire) [](const Name &) {},
#define CORE_PATTERN_ACYCLIC_CoreIntRangePat(Name, Wire) [](const Name &) {},
#define CORE_PATTERN_ACYCLIC_CoreBindingPat(Name, Wire)                                                  \
    [&](const Name &b) {                                                                           \
        if (b.has_nested) {                                                                        \
            push(b.nested);                                                                        \
        }                                                                                          \
    },
#define CORE_PATTERN_ACYCLIC_CoreVariantPat(Name, Wire)                                                  \
    [&](const Name &v) {                                                                           \
        for (const CorePatternId s : v.tuple_subpatterns) {                                        \
            push(s);                                                                               \
        }                                                                                          \
        for (const CoreVariantPatField &f : v.struct_fields) {                                     \
            push(f.pattern);                                                                       \
        }                                                                                          \
    },
#define CORE_PATTERN_ACYCLIC_CoreTuplePat(Name, Wire)                                                    \
    [&](const Name &t) {                                                                           \
        for (const CorePatternId e : t.elements) {                                                 \
            push(e);                                                                               \
        }                                                                                          \
    },
#define CORE_PATTERN_ACYCLIC_CoreOrPat(Name, Wire)                                                       \
    [&](const Name &o) {                                                                           \
        for (const CorePatternId a : o.alternatives) {                                             \
            push(a);                                                                               \
        }                                                                                          \
    },
#define HANDLE_CORE_PATTERN_NODE(Name, Wire) CORE_PATTERN_ACYCLIC_##Name(Name, Wire)
                    std::visit(
                        Overloaded{
#include "ahfl/compiler/ir/core_pattern_nodes.def"
                        },
                        flow.patterns[id].node);
#undef HANDLE_CORE_PATTERN_NODE
#undef CORE_PATTERN_ACYCLIC_CoreWildcardPat
#undef CORE_PATTERN_ACYCLIC_CoreLiteralPat
#undef CORE_PATTERN_ACYCLIC_CoreIntRangePat
#undef CORE_PATTERN_ACYCLIC_CoreBindingPat
#undef CORE_PATTERN_ACYCLIC_CoreVariantPat
#undef CORE_PATTERN_ACYCLIC_CoreTuplePat
#undef CORE_PATTERN_ACYCLIC_CoreOrPat
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
                const auto arity = payload.slot_type_template_roots.size();
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

    // A bounded-collection operation is legal only on a BOUNDED collection value
    // (a `CoreVtNominal` whose declaration role is List/Set/Map AND whose
    // `capacity` is present — an unbounded collection has no P4-D backing) and
    // only with the operand set + result type its op kind names. `Len` yields the
    // header length (Int), an element read yields the element type (a Map's
    // VALUE type), and an element write yields the collection type itself. The
    // operand NODE KINDS are checked by the caller; this is the typed restatement
    // the layout pass and codegen both rely on, so a backend never receives a
    // collection op it cannot realize.
    void verify_collection_op(const ArenaView &flow, const CoreCollectionExpr &c,
                              const CoreExpr &expr, SourceRangeOpt range) {
        if (c.base.value >= flow.value_count) {
            return; // already reported as an out-of-range value id
        }
        const CoreValueTypeId base_vt = body_value_type(flow, c.base);
        const auto *base_nominal = base_vt.value < program_.value_types.size()
                                       ? std::get_if<CoreVtNominal>(
                                             &program_.value_types[base_vt.value].node)
                                       : nullptr;
        if (base_nominal == nullptr || base_nominal->base.value >= program_.types.size() ||
            !capacity_allowed(program_.types[base_nominal->base.value].role) ||
            !base_nominal->capacity.has_value() || base_nominal->args.empty()) {
            // A bounded collection always carries its element type argument (a
            // Map carries key + value), so an EMPTY arg list is as malformed as a
            // missing capacity. Guarding it here keeps the element-type
            // comparisons below off `args.front()` on a hand-built / deserialized
            // nominal whose arity never matched its declaration.
            error(verify::kCollectionOpInvalid,
                  "collection operation base is not a bounded collection value in '" +
                      flow.label + "'",
                  range);
            return;
        }
        const CoreTypeDecl &decl = program_.types[base_nominal->base.value];
        const bool is_map = decl.role == CoreNominalRole::Map;
        // A Map's element read yields the VALUE type (arg #1); a List / Set yields
        // the single element/key type. Reading an ABSENT Map value would need an
        // Option wrapper this node does not model, so a Map's element op is
        // rejected here — the caller (lowerer) is the one that decides whether a
        // Map surface is in the subset at all.
        if (c.op == CoreCollectionOpKind::ElementGet || c.op == CoreCollectionOpKind::ElementSet) {
            if (is_map) {
                error(verify::kCollectionOpInvalid,
                      "Map element access needs the typed entry accessor, not a bare collection op "
                      "in '" +
                          flow.label + "'",
                      range);
                return;
            }
            if (c.index.value >= flow.value_count ||
                (c.op == CoreCollectionOpKind::ElementSet && c.value.value >= flow.value_count)) {
                return; // out-of-range operand already reported
            }
            // The index must be an Int (the header's length domain).
            const CoreValueTypeId index_vt = body_value_type(flow, c.index);
            const bool index_is_int =
                index_vt.value < program_.value_types.size() &&
                std::holds_alternative<CoreVtInt>(program_.value_types[index_vt.value].node);
            if (!index_is_int) {
                error(verify::kCollectionOpInvalid,
                      "collection element index is not an Int in '" + flow.label + "'", range);
            }
        }
        switch (c.op) {
        case CoreCollectionOpKind::Len: {
            const CoreValueTypeId result_vt = expr.result_type;
            const bool result_is_int =
                result_vt.value < program_.value_types.size() &&
                std::holds_alternative<CoreVtInt>(program_.value_types[result_vt.value].node);
            if (!result_is_int) {
                error(verify::kCollectionOpInvalid,
                      "collection length result is not an Int in '" + flow.label + "'", range);
            }
            return;
        }
        case CoreCollectionOpKind::ElementGet: {
            if (!(expr.result_type == base_nominal->args.front())) {
                error(verify::kCollectionOpInvalid,
                      "collection element read result does not equal the element type in '" +
                          flow.label + "'",
                      range);
            }
            return;
        }
        case CoreCollectionOpKind::ElementSet: {
            if (!(expr.result_type == base_vt)) {
                error(verify::kCollectionOpInvalid,
                      "collection element write result is not the collection type in '" +
                          flow.label + "'",
                      range);
            }
            if (!(body_value_type(flow, c.value) == base_nominal->args.front())) {
                error(verify::kCollectionOpInvalid,
                      "collection element write value does not equal the element type in '" +
                          flow.label + "'",
                      range);
            }
            return;
        }
        }
    }

    // --- flow wiring + per-state statement discipline ---
    void verify_flow(const CoreFlowDecl &flow) {
        ArenaView av{flow.storage.exprs, flow.storage.value_count, flow.storage.patterns, flow.agent_name};
        av.value_types = &flow.storage.value_types;
        av.coercion_plans = &flow.storage.coercion_plans;
        verify_body_value_types(av);
        verify_coercion_plans(av);
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
        std::unordered_set<std::uint32_t> allowed_capabilities;
        for (const CoreCapabilityId capability : agent.capabilities) {
            if (capability.value < program_.capabilities.size()) {
                allowed_capabilities.insert(capability.value);
            }
        }
        av.allowed_capabilities = &allowed_capabilities;
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
                           [&](const CoreCoerceExpr &c) { out.push_back(c.operand); },
                           [&](const CoreCollectionExpr &c) {
                               out.push_back(c.base);
                               if (c.op != CoreCollectionOpKind::Len) {
                                   out.push_back(c.index);
                               }
                               if (c.op == CoreCollectionOpKind::ElementSet) {
                                   out.push_back(c.value);
                               }
                           },
                           [&](const CoreUnsupportedExpr &) {},
                           [&](const CoreCallExpr &c) {
                               for (const CoreValueId arg : c.args) {
                                   out.push_back(arg);
                               }
                           },
                           [&](const CoreClosureExpr &c) {
                               // Captured operands are ordinary SSA uses of the
                               // CONSTRUCTING body (design §3.2).
                               for (const CoreValueId env : c.env) {
                                   out.push_back(env);
                               }
                           },
                           [&](const CoreCallClosureExpr &c) {
                               out.push_back(c.callee);
                               for (const CoreValueId arg : c.args) {
                                   out.push_back(arg);
                               }
                           },
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
    enum class RegionContext { Flow, Guard, MatchArmValue, MatchArmUnit, WorkflowNodeInput, WorkflowReturn, Fn };

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
                                           RegionContext ctx = RegionContext::Flow,
                                           ExpectedYield expected = ExpectedYield{}) {
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
        const auto walk_let = [&](const CoreLetStmt &s) {
            use_expr(s.expr, stmt.source_range);
            define_value(s.result, stmt.source_range);
            // RFC 0026 P4-B: the bound value's recorded logical
            // type must equal the bound expr's result_type
            // (both index the program-global value-type pool).
            if (s.expr.value < flow.exprs.size()) {
                const CoreValueTypeId expr_ty =
                    flow.exprs[s.expr.value].result_type;
                if (!(body_value_type(flow, s.result) == expr_ty)) {
                    error(verify::kValueTypeMismatch,
                          "let-bound value id " + std::to_string(s.result.value) +
                              " recorded type does not equal its expression's "
                              "result type in '" +
                              flow.label + "'",
                          stmt.source_range);
                }
            }
        };
        const auto walk_capabilityCall = [&](const CoreCapabilityCallStmt &s) {
            const CoreCapabilityDecl *decl = nullptr;
            if (s.capability.value >= program_.capabilities.size()) {
                error(verify::kCapabilityIdOutOfRange,
                      "capability call '" + s.callee_name + "' capability id " +
                          std::to_string(s.capability.value) + " is out of range",
                      stmt.source_range);
            } else {
                decl = &program_.capabilities[s.capability.value];
                const auto arity = decl->param_types.size();
                if (s.args.size() != arity) {
                    error(verify::kCapabilityArityMismatch,
                          "capability call '" + s.callee_name + "' passes " +
                              std::to_string(s.args.size()) +
                              " args but the import signature has " +
                              std::to_string(arity),
                          stmt.source_range);
                }
                const auto typed_args = std::min(s.args.size(), arity);
                for (std::size_t index = 0; index < typed_args; ++index) {
                    if (flow.value_types != nullptr &&
                        s.args[index].value < flow.value_types->size() &&
                        !(body_value_type(flow, s.args[index]) ==
                          decl->param_types[index])) {
                        error(verify::kCapabilityArgumentTypeMismatch,
                              "capability call '" + s.callee_name +
                                  "' argument #" + std::to_string(index) +
                                  " type does not match its import signature",
                              stmt.source_range);
                    }
                }
                if (flow.value_types != nullptr &&
                    s.result.value < flow.value_types->size() &&
                    !(body_value_type(flow, s.result) == decl->return_type)) {
                    error(verify::kCapabilityResultTypeMismatch,
                          "capability call '" + s.callee_name +
                              "' result type does not match its import signature",
                          stmt.source_range);
                }
            }

            if (flow.owner == OwnerKind::Workflow) {
                error(verify::kCapabilityOutsideFlow,
                      "capability call '" + s.callee_name +
                          "' appears in a workflow node region (capabilities stay on the "
                          "flow/fn execution lane)",
                      stmt.source_range);
            } else if (flow.owner == OwnerKind::Flow &&
                       (flow.allowed_capabilities == nullptr ||
                        flow.allowed_capabilities->find(s.capability.value) ==
                            flow.allowed_capabilities->end())) {
                error(verify::kCapabilityUnauthorized,
                      "capability call '" + s.callee_name +
                          "' is not in the target agent's whitelist",
                      stmt.source_range);
            }
            // RFC 0026 FB-4: a capability call inside an FN body
            // (OwnerKind::Fn) is legal — that is what makes the fn effectful.
            // It carries no per-fn whitelist (an fn is not bound to one agent);
            // transitive authorization is enforced program-wide in
            // verify_fn_effect_authorization, where an agent's reachable effect
            // graph is checked against that agent's whitelist.
            for (const CoreValueId a : s.args) {
                use_value(a, stmt.source_range);
            }
            define_value(s.result, stmt.source_range);
        };
        const auto walk_store = [&](const CoreStoreStmt &s) {
            verify_projection(s.place.root_type, s.place.projection,
                              s.place.projection_resolved, s.place.root_name,
                              stmt.source_range);
            use_value(s.value, stmt.source_range);
        };
        // RFC 0026 FB-4 (design §5.3): an ordered effectful fn call. Like the
        // capability walk it is a USE (args must be bound) followed by a single
        // DEF (result), so effect order and def-before-use are structural.
        // Callee-resolution / arity / argument-result types / effect-kind are
        // checked in the program-wide verify_fn_call_sites pass (it has the fn
        // table and the effect fixed point); this body-local walk owns the SSA
        // ordering and bounds only.
        const auto walk_callStmt = [&](const CoreCallStmt &s) {
            if (s.callee.value == CoreInstanceId::kInvalid ||
                s.callee.value >= program_.instances.size()) {
                error(verify::kFnCallCalleeInvalid,
                      "ordered effectful call references an out-of-range fn instance id in body '" +
                          flow.label + "'",
                      stmt.source_range);
            } else {
                const CoreInstanceDecl &inst = program_.instances[s.callee.value];
                const auto *payload = std::get_if<CoreFnInstance>(&inst.payload);
                if (payload == nullptr || payload->body.value == CoreFnId::kInvalid) {
                    error(verify::kFnCallCalleeInvalid,
                          "ordered effectful call in body '" + flow.label + "' targets '" +
                              inst.instance_key + "' which has no lowered fn body",
                          stmt.source_range);
                }
            }
            for (const CoreValueId a : s.args) {
                use_value(a, stmt.source_range);
            }
            define_value(s.result, stmt.source_range);
        };
        const auto walk_if = [&](const CoreIfStmt &s) {
            use_value(s.condition, stmt.source_range);
            // RFC 0026 P4-B (P1): the branch condition must be a
            // Bool value (the dense value table now lets the
            // standalone verifier prove this instead of trusting
            // Sema).
            if (s.condition.value < flow.value_count &&
                !is_bool_value_type(body_value_type(flow, s.condition))) {
                error(verify::kValueTypeMismatch,
                      "if condition value must be Bool in '" + flow.label + "'",
                      stmt.source_range);
            }
            // Each branch: own `visible` copy (defs don't
            // escape), shared `all_definitions`, same `ctx`.
            // The else-less branch is an implicit fallthrough.
            RegionExit then_exit;
            RegionExit else_exit;
            else_exit.fallthrough = true;
            if (s.then_region) {
                auto bv = visible;
                then_exit = verify_region(flow, state_count, *s.then_region,
                                          all_definitions, bv, ctx, expected);
            } else {
                then_exit.fallthrough = true;
            }
            if (s.else_region) {
                auto bv = visible;
                else_exit = verify_region(flow, state_count, *s.else_region,
                                          all_definitions, bv, ctx, expected);
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
        };
        const auto walk_goto = [&](const CoreGotoStmt &s) {
            if (flow.owner == OwnerKind::Fn) {
                error(verify::kFnBodyTermination,
                      "fn body '" + flow.label + "' contains a goto (a fn completes via return)",
                      stmt.source_range);
            } else if (is_workflow_ctx(ctx)) {
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
        };
        const auto walk_return = [&](const CoreReturnStmt &s) {
            if (flow.owner == OwnerKind::Fn) {
                // A fn body MUST return a value (its result type); a bare
                // unit return from a value fn is caught by arity on the call.
                if (!s.has_value) {
                    error(verify::kFnBodyTermination,
                          "fn body '" + flow.label + "' returns without a value",
                          stmt.source_range);
                }
            } else if (is_workflow_ctx(ctx)) {
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
        };
        const auto walk_yield = [&](const CoreYieldStmt &s) {
            if (ctx == RegionContext::Flow) {
                error(verify::kYieldOutsideMatchArm,
                      "yield outside a match arm / guard region in flow '" +
                          flow.label + "'",
                      stmt.source_range);
            }
            if (s.has_value) {
                use_value(s.value, stmt.source_range);
                exit.yields_value = true;
                // RFC 0026 P4-B (P1): the yielded value's type
                // must satisfy the region's expected-yield
                // constraint (a guard yields Bool; an expression
                // match arm yields the match result type). HARD:
                // a mismatch (or a Bool expectation that a
                // non-Bool value cannot meet) fails closed even
                // when the pool lacks the expected node.
                if (s.value.value < flow.value_count) {
                    const CoreValueTypeId vt = body_value_type(flow, s.value);
                    if (expected.kind == ExpectedYield::Kind::Bool &&
                        !is_bool_value_type(vt)) {
                        error(verify::kValueTypeMismatch,
                              "guard region must yield a Bool value in '" +
                                  flow.label + "'",
                              stmt.source_range);
                    } else if (expected.kind == ExpectedYield::Kind::Exact &&
                               !(vt == expected.exact)) {
                        error(verify::kValueTypeMismatch,
                              "yielded value type does not match the match "
                              "result type in '" +
                                  flow.label + "'",
                              stmt.source_range);
                    }
                }
            } else {
                exit.yields_unit = true;
            }
            live = false;
        };
        const auto walk_trap = [&](const CoreTrapStmt &/*s*/) {
            exit.diverges_trap = true;
            live = false;
        };
        const auto walk_match = [&](const CoreMatchStmt &s) {
            const RegionExit m =
                verify_match(flow, state_count, s, all_definitions, visible,
                             stmt.source_range);
            // A match consumes its arms' yields; only control /
            // trap divergence propagates to the parent, plus a
            // fallthrough when the match can normally complete.
            exit.diverges_control |= m.diverges_control;
            exit.diverges_trap |= m.diverges_trap;
            live = m.fallthrough;
        };

        // RFC 0027 P6/P7/P8 (KR6.13-P7): one NAMED handler per CoreStmtNode
        // alternative, generated from core_stmt_nodes.def. The per-node bodies live
        // in the named lambdas above (declared in the .def's order); the routing
        // macros below bind each alternative to its lambda. The .def order IS the
        // dispatch order, and a new alternative is a COMPILE ERROR here until it
        // is routed (CLAUDE.md Principle 5).
#define STMT_WALK_CoreLetStmt(Name, Wire) walk_let
#define STMT_WALK_CoreCapabilityCallStmt(Name, Wire) walk_capabilityCall
#define STMT_WALK_CoreStoreStmt(Name, Wire) walk_store
#define STMT_WALK_CoreIfStmt(Name, Wire) walk_if
#define STMT_WALK_CoreGotoStmt(Name, Wire) walk_goto
#define STMT_WALK_CoreReturnStmt(Name, Wire) walk_return
#define STMT_WALK_CoreYieldStmt(Name, Wire) walk_yield
#define STMT_WALK_CoreTrapStmt(Name, Wire) walk_trap
#define STMT_WALK_CoreMatchStmt(Name, Wire) walk_match
#define STMT_WALK_CoreCallStmt(Name, Wire) walk_callStmt
#define HANDLE_CORE_STMT_NODE(Name, Wire) STMT_WALK_##Name(Name, Wire),
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/core_stmt_nodes.def"
            },
            stmt.node);
#undef HANDLE_CORE_STMT_NODE
#undef STMT_WALK_CoreLetStmt
#undef STMT_WALK_CoreCapabilityCallStmt
#undef STMT_WALK_CoreStoreStmt
#undef STMT_WALK_CoreIfStmt
#undef STMT_WALK_CoreGotoStmt
#undef STMT_WALK_CoreReturnStmt
#undef STMT_WALK_CoreYieldStmt
#undef STMT_WALK_CoreTrapStmt
#undef STMT_WALK_CoreMatchStmt
#undef STMT_WALK_CoreCallStmt
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
            // RFC 0026 P4-B (P1): a guard yields Bool; an expression match arm
            // yields the match result's type. Threaded through nested `if`
            // branches so every value-yield path is type-checked, not just the
            // top-level one. A statement arm has no per-yield type constraint.
            ExpectedYield expected;
            if (ctx == RegionContext::Guard) {
                expected.kind = ExpectedYield::Kind::Bool;
            } else if (ctx == RegionContext::MatchArmValue && m.has_result) {
                expected.kind = ExpectedYield::Kind::Exact;
                expected.exact = body_value_type(flow, m.result);
            }
            const RegionExit e =
                verify_region(flow, state_count, region, all_definitions, vis, ctx, expected);
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
    // This region-level rule proves that every path yields a value. Logical SSA
    // types are checked by the shared value-type arena and root/instance rules;
    // physical representation remains the separate verified P4-D layout side
    // artifact and is deliberately not recomputed here.
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
        // allocated once from wf.storage.value_count across ALL node regions + the return
        // region, so a single `all_definitions` set catches any redefinition. The
        // arena pass runs in the Workflow domain, so it rejects flow-only roots,
        // an unresolved identifier, and (over EVERY expr) a bad node-id / mistyped
        // workflow root.
        const auto make_view = [&](std::string lbl) {
            ArenaView av{wf.storage.exprs,           wf.storage.value_count,     wf.storage.patterns, std::move(lbl),
                         OwnerKind::Workflow, &node_output_types, wf.input_type};
            av.value_types = &wf.storage.value_types;
            av.coercion_plans = &wf.storage.coercion_plans;
            return av;
        };
        verify_body_value_types(make_view(wf.name));
        verify_coercion_plans(make_view(wf.name));
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
            // RFC 0027 P6/P7/P8 (KR6.13-P7): one handler per CoreStmtNode
            // alternative, generated from core_stmt_nodes.def. Only Let (its bound
            // expr) and the two nested-region statements walk sub-regions; the
            // rest are leaves. No generic catch-all: a new statement alternative is
            // a COMPILE ERROR here until classified.
#define CORE_REGION_PATHS_LEAF(Name, Wire) [](const Name &) {},
#define CORE_REGION_PATHS_CoreLetStmt(Name, Wire)                                                        \
    [&](const Name &s) { visit_expr_paths(wf, s.expr, stmt.source_range, fn); },
#define CORE_REGION_PATHS_CoreIfStmt(Name, Wire)                                                         \
    [&](const Name &s) {                                                                           \
        if (s.then_region) {                                                                       \
            for_each_region_path_expr(wf, *s.then_region, fn);                                     \
        }                                                                                          \
        if (s.else_region) {                                                                       \
            for_each_region_path_expr(wf, *s.else_region, fn);                                     \
        }                                                                                          \
    },
#define CORE_REGION_PATHS_CoreMatchStmt(Name, Wire)                                                      \
    [&](const Name &s) {                                                                           \
        for (const CoreMatchArm &arm : s.arms) {                                                   \
            if (arm.guard_region) {                                                                \
                for_each_region_path_expr(wf, *arm.guard_region, fn);                              \
            }                                                                                      \
            if (arm.body) {                                                                        \
                for_each_region_path_expr(wf, *arm.body, fn);                                      \
            }                                                                                      \
        }                                                                                          \
        if (s.fallback_region) {                                                                   \
            for_each_region_path_expr(wf, *s.fallback_region, fn);                                 \
        }                                                                                          \
    },
#define CORE_REGION_PATHS_CoreCapabilityCallStmt(Name, Wire) CORE_REGION_PATHS_LEAF(Name, Wire)
#define CORE_REGION_PATHS_CoreStoreStmt(Name, Wire) CORE_REGION_PATHS_LEAF(Name, Wire)
#define CORE_REGION_PATHS_CoreYieldStmt(Name, Wire) CORE_REGION_PATHS_LEAF(Name, Wire)
#define CORE_REGION_PATHS_CoreReturnStmt(Name, Wire) CORE_REGION_PATHS_LEAF(Name, Wire)
#define CORE_REGION_PATHS_CoreGotoStmt(Name, Wire) CORE_REGION_PATHS_LEAF(Name, Wire)
#define CORE_REGION_PATHS_CoreTrapStmt(Name, Wire) CORE_REGION_PATHS_LEAF(Name, Wire)
#define CORE_REGION_PATHS_CoreCallStmt(Name, Wire) CORE_REGION_PATHS_LEAF(Name, Wire)
#define HANDLE_CORE_STMT_NODE(Name, Wire) CORE_REGION_PATHS_##Name(Name, Wire)
            std::visit(
                Overloaded{
#include "ahfl/compiler/ir/core_stmt_nodes.def"
                },
                stmt.node);
#undef HANDLE_CORE_STMT_NODE
#undef CORE_REGION_PATHS_LEAF
#undef CORE_REGION_PATHS_CoreLetStmt
#undef CORE_REGION_PATHS_CoreIfStmt
#undef CORE_REGION_PATHS_CoreMatchStmt
#undef CORE_REGION_PATHS_CoreCapabilityCallStmt
#undef CORE_REGION_PATHS_CoreStoreStmt
#undef CORE_REGION_PATHS_CoreYieldStmt
#undef CORE_REGION_PATHS_CoreReturnStmt
#undef CORE_REGION_PATHS_CoreGotoStmt
#undef CORE_REGION_PATHS_CoreTrapStmt
#undef CORE_REGION_PATHS_CoreCallStmt
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
        const auto expr_count = static_cast<std::uint32_t>(wf.storage.exprs.size());
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
            // RFC 0027 P6/P7/P8 (KR6.13-P7): one handler per CoreExprNode
            // alternative, generated from core_expr_nodes.def. Only CorePathExpr
            // yields a path and only CoreUnaryExpr / CoreBinaryExpr carry
            // intra-arena operand edges, so those route above; every other node
            // names CORE_EXPR_PATHS_<Node> (explicit, named no-op — the same
            // behavior the unnamed catch-all had). No generic catch-all: a new
            // expr alternative is a COMPILE ERROR here until it is classified.
#define CORE_EXPR_PATHS_CorePathExpr(Name, Wire) [&](const Name &p) { fn(p, range); },
#define CORE_EXPR_PATHS_CoreUnaryExpr(Name, Wire) [&](const Name &u) { push(u.operand); },
#define CORE_EXPR_PATHS_CoreBinaryExpr(Name, Wire)                                                       \
    [&](const Name &b) {                                                                           \
        push(b.lhs);                                                                               \
        push(b.rhs);                                                                               \
    },
#define CORE_EXPR_PATHS_CoreLiteralExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_PATHS_CoreValueRefExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_PATHS_CoreQualifiedExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_PATHS_CoreConstructExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_PATHS_CoreCoerceExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_PATHS_CoreCollectionExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_PATHS_CoreUnsupportedExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_PATHS_CoreCallExpr(Name, Wire) [](const Name &) {},
// FB-3a1: closure nodes carry SSA values only — no path and no intra-arena edge.
#define CORE_EXPR_PATHS_CoreClosureExpr(Name, Wire) [](const Name &) {},
#define CORE_EXPR_PATHS_CoreCallClosureExpr(Name, Wire) [](const Name &) {},
#define HANDLE_CORE_EXPR_NODE(Name, Wire) CORE_EXPR_PATHS_##Name(Name, Wire)
            std::visit(
                Overloaded{
#include "ahfl/compiler/ir/core_expr_nodes.def"
                },
                wf.storage.exprs[id].node);
#undef HANDLE_CORE_EXPR_NODE
#undef CORE_EXPR_PATHS_CorePathExpr
#undef CORE_EXPR_PATHS_CoreUnaryExpr
#undef CORE_EXPR_PATHS_CoreBinaryExpr
#undef CORE_EXPR_PATHS_CoreLiteralExpr
#undef CORE_EXPR_PATHS_CoreValueRefExpr
#undef CORE_EXPR_PATHS_CoreQualifiedExpr
#undef CORE_EXPR_PATHS_CoreConstructExpr
#undef CORE_EXPR_PATHS_CoreCoerceExpr
#undef CORE_EXPR_PATHS_CoreCollectionExpr
#undef CORE_EXPR_PATHS_CoreUnsupportedExpr
#undef CORE_EXPR_PATHS_CoreCallExpr
#undef CORE_EXPR_PATHS_CoreClosureExpr
#undef CORE_EXPR_PATHS_CoreCallClosureExpr
        }
    }

    // --- logical value-type arena (RFC 0026 P4) ---
    //
    // Verifies CoreProgram.value_types independently of how it was produced (so a
    // hand-built / deserialized arena cannot bypass the lowerer's invariants):
    //   * every child id is in range and points BACKWARD or to a distinct entry
    //     (the arena is a DAG — an interned, hash-consed acyclic structure);
    //   * the arena is a canonical hash-cons: no two entries are structurally
    //     equal (a duplicate means interning was bypassed);
    //   * a CoreVtNominal's base is an in-range CoreTypeId, its arg count equals
    //     that type's `type_param_count`, and a `capacity` is present ONLY on a
    //     bounded-collection role (the SAME `capacity_allowed` SSOT the lowerer
    //     uses — Codex ruling c);
    //   * refinement bounds are well-formed (min<=max, non-negative lengths).
    // Acyclicity + child-bounds are checked by an iterative 3-color walk (no
    // native recursion). `CoreVtNever` is a legal ARENA node here; a consumer
    // context (dispatch types) separately rejects it.
    void verify_value_types() {
        const auto &arena = program_.value_types;
        // Duplicate-structural-entry detection (canonical interning).
        std::map<std::size_t, std::vector<std::uint32_t>> by_hash; // hash -> entry ids
        // Child-id bounds + collect children for the cycle walk.
        const auto children_of = [](const CoreValueType &vt, std::vector<std::uint32_t> &out) {
            out.clear();
            const auto co_nominal = [&](const CoreVtNominal &n) {
                for (const auto &a : n.args) {
                    out.push_back(a.value);
                }
            };
            const auto co_tuple = [&](const CoreVtTuple &n) {
                for (const auto &e : n.elements) {
                    out.push_back(e.value);
                }
            };
            const auto co_fn = [&](const CoreVtFn &n) {
                for (const auto &p : n.params) {
                    out.push_back(p.value);
                }
                out.push_back(n.ret.value);
            };
            const auto co_closure = [&](const CoreVtClosure &n) {
                out.push_back(n.signature.value);
                for (const auto &c : n.captures) {
                    out.push_back(c.value_type.value);
                }
            };
            // RFC 0027 Q1 (KR6.13-X): one handler per value-type node, generated
            // from core_value_types.def. Childless leaves share VERIFY_CHILD_LEAF
            // (a distinct typed no-op lambda per node); the four structural nodes
            // route to the payload lambdas above. No unnamed catch-all: a 15th
            // node without a VERIFY_CHILD_* routing macro fails to compile.
#define VERIFY_CHILD_LEAF(Name, Wire) [](const CoreVt##Name &) {},
#define VERIFY_CHILD_Unit(Name, Wire) VERIFY_CHILD_LEAF(Name, Wire)
#define VERIFY_CHILD_Never(Name, Wire) VERIFY_CHILD_LEAF(Name, Wire)
#define VERIFY_CHILD_Bool(Name, Wire) VERIFY_CHILD_LEAF(Name, Wire)
#define VERIFY_CHILD_Int(Name, Wire) VERIFY_CHILD_LEAF(Name, Wire)
#define VERIFY_CHILD_Float(Name, Wire) VERIFY_CHILD_LEAF(Name, Wire)
#define VERIFY_CHILD_String(Name, Wire) VERIFY_CHILD_LEAF(Name, Wire)
#define VERIFY_CHILD_Decimal(Name, Wire) VERIFY_CHILD_LEAF(Name, Wire)
#define VERIFY_CHILD_Duration(Name, Wire) VERIFY_CHILD_LEAF(Name, Wire)
#define VERIFY_CHILD_Timestamp(Name, Wire) VERIFY_CHILD_LEAF(Name, Wire)
#define VERIFY_CHILD_Uuid(Name, Wire) VERIFY_CHILD_LEAF(Name, Wire)
#define VERIFY_CHILD_Nominal(Name, Wire) co_nominal,
#define VERIFY_CHILD_Tuple(Name, Wire) co_tuple,
#define VERIFY_CHILD_Fn(Name, Wire) co_fn,
#define VERIFY_CHILD_Closure(Name, Wire) co_closure,
#define HANDLE_CORE_VT(Name, Wire) VERIFY_CHILD_##Name(Name, Wire)
            std::visit(
                Overloaded{
#include "ahfl/compiler/ir/core_value_types.def"
                },
                vt.node);
#undef HANDLE_CORE_VT
#undef VERIFY_CHILD_Unit
#undef VERIFY_CHILD_Never
#undef VERIFY_CHILD_Bool
#undef VERIFY_CHILD_Int
#undef VERIFY_CHILD_Float
#undef VERIFY_CHILD_String
#undef VERIFY_CHILD_Decimal
#undef VERIFY_CHILD_Duration
#undef VERIFY_CHILD_Timestamp
#undef VERIFY_CHILD_Uuid
#undef VERIFY_CHILD_Nominal
#undef VERIFY_CHILD_Tuple
#undef VERIFY_CHILD_Fn
#undef VERIFY_CHILD_Closure
#undef VERIFY_CHILD_LEAF
        };

        std::vector<std::uint32_t> kids;
        for (std::uint32_t i = 0; i < arena.size(); ++i) {
            const CoreValueType &vt = arena[i];
            // Child-id bounds AND topological order: a hash-cons interns children
            // BEFORE their parent, so every child id must be strictly LESS than the
            // parent's index. This makes the arena an acyclic DAG by construction;
            // a forward (or self) reference is a malformed hand-built / deserialized
            // arena and is fail-closed here (not merely by the cycle walk).
            children_of(vt, kids);
            for (std::uint32_t child : kids) {
                if (child == CoreValueTypeId::kInvalid || child >= arena.size()) {
                    error(verify::kValueTypeChildInvalid,
                          "value type #" + std::to_string(i) + " has an out-of-range child id",
                          std::nullopt);
                } else if (child >= i) {
                    error(verify::kValueTypeChildInvalid,
                          "value type #" + std::to_string(i) + " references child #" +
                              std::to_string(child) +
                              " which is not interned earlier (forward/self reference)",
                          std::nullopt);
                }
            }
            // Per-node structural checks.
            verify_value_type_node(i, vt);
        }

        // Duplicate detection: two arena entries that compare equal mean the
        // hash-cons was bypassed. Bucket by a cheap hash then compare within.
        for (std::uint32_t i = 0; i < arena.size(); ++i) {
            by_hash[value_type_hash(arena[i])].push_back(i);
        }
        for (const auto &[h, ids] : by_hash) {
            (void)h;
            for (std::size_t a = 0; a < ids.size(); ++a) {
                for (std::size_t b = a + 1; b < ids.size(); ++b) {
                    if (arena[ids[a]] == arena[ids[b]]) {
                        error(verify::kValueTypeDuplicate,
                              "value types #" + std::to_string(ids[a]) + " and #" +
                                  std::to_string(ids[b]) + " are structurally identical (the arena "
                                  "must be a canonical hash-cons)",
                              std::nullopt);
                    }
                }
            }
        }

        // Acyclicity: iterative 3-color DFS over the child graph.
        verify_value_types_acyclic(children_of);
    }

    void verify_value_type_node(std::uint32_t index, const CoreValueType &vt) {
        const auto vn_int = [&](const CoreVtInt &n) {
            if (n.bounds && n.bounds->first > n.bounds->second) {
                error(verify::kValueTypeRefinementInvalid,
                      "value type #" + std::to_string(index) + " has an Int bound with min > max",
                      std::nullopt);
            }
        };
        const auto vn_string = [&](const CoreVtString &n) {
            if (n.length_bounds && (n.length_bounds->first < 0 || n.length_bounds->second < 0 ||
                                    n.length_bounds->first > n.length_bounds->second)) {
                error(verify::kValueTypeRefinementInvalid,
                      "value type #" + std::to_string(index) +
                          " has a negative or reversed String length bound",
                      std::nullopt);
            }
        };
        const auto vn_nominal = [&](const CoreVtNominal &n) {
            verify_value_type_nominal(index, n);
        };
        const auto vn_fn = [&](const CoreVtFn &n) {
            if (n.ret.value == CoreValueTypeId::kInvalid) {
                error(verify::kValueTypeChildInvalid,
                      "value type #" + std::to_string(index) + " (Fn) has an invalid return id",
                      std::nullopt);
            }
        };
        const auto vn_closure = [&](const CoreVtClosure &n) {
            // A closure's signature MUST resolve to a CoreVtFn (a closure is a
            // function value plus captures).
            const auto sig = n.signature.value;
            const bool sig_ok =
                sig != CoreValueTypeId::kInvalid && sig < program_.value_types.size() &&
                std::holds_alternative<CoreVtFn>(program_.value_types[sig].node);
            if (!sig_ok) {
                error(verify::kValueTypeChildInvalid,
                      "value type #" + std::to_string(index) +
                          " (Closure) signature does not resolve to a Fn value type",
                      std::nullopt);
            }
        };
        // RFC 0027 Q1 (KR6.13-X): one handler per value-type node, generated
        // from core_value_types.def. Nodes with no per-node structural rule share
        // VERIFY_NODE_LEAF (a distinct typed no-op lambda per node); the five
        // checked nodes route to the payload lambdas above. No unnamed catch-all:
        // a 15th node without a VERIFY_NODE_* routing macro fails to compile.
#define VERIFY_NODE_LEAF(Name, Wire) [](const CoreVt##Name &) {},
#define VERIFY_NODE_Unit(Name, Wire) VERIFY_NODE_LEAF(Name, Wire)
#define VERIFY_NODE_Never(Name, Wire) VERIFY_NODE_LEAF(Name, Wire)
#define VERIFY_NODE_Bool(Name, Wire) VERIFY_NODE_LEAF(Name, Wire)
#define VERIFY_NODE_Int(Name, Wire) vn_int,
#define VERIFY_NODE_Float(Name, Wire) VERIFY_NODE_LEAF(Name, Wire)
#define VERIFY_NODE_String(Name, Wire) vn_string,
#define VERIFY_NODE_Decimal(Name, Wire) VERIFY_NODE_LEAF(Name, Wire)
#define VERIFY_NODE_Duration(Name, Wire) VERIFY_NODE_LEAF(Name, Wire)
#define VERIFY_NODE_Timestamp(Name, Wire) VERIFY_NODE_LEAF(Name, Wire)
#define VERIFY_NODE_Uuid(Name, Wire) VERIFY_NODE_LEAF(Name, Wire)
#define VERIFY_NODE_Nominal(Name, Wire) vn_nominal,
#define VERIFY_NODE_Tuple(Name, Wire) VERIFY_NODE_LEAF(Name, Wire)
#define VERIFY_NODE_Fn(Name, Wire) vn_fn,
#define VERIFY_NODE_Closure(Name, Wire) vn_closure,
#define HANDLE_CORE_VT(Name, Wire) VERIFY_NODE_##Name(Name, Wire)
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/core_value_types.def"
            },
            vt.node);
#undef HANDLE_CORE_VT
#undef VERIFY_NODE_Unit
#undef VERIFY_NODE_Never
#undef VERIFY_NODE_Bool
#undef VERIFY_NODE_Int
#undef VERIFY_NODE_Float
#undef VERIFY_NODE_String
#undef VERIFY_NODE_Decimal
#undef VERIFY_NODE_Duration
#undef VERIFY_NODE_Timestamp
#undef VERIFY_NODE_Uuid
#undef VERIFY_NODE_Nominal
#undef VERIFY_NODE_Tuple
#undef VERIFY_NODE_Fn
#undef VERIFY_NODE_Closure
#undef VERIFY_NODE_LEAF
    }

    void verify_value_type_nominal(std::uint32_t index, const CoreVtNominal &n) {
        if (n.base.value == CoreTypeId::kInvalid || n.base.value >= program_.types.size()) {
            error(verify::kValueTypeNominalInvalid,
                  "value type #" + std::to_string(index) + " nominal base id is out of range",
                  std::nullopt);
            return;
        }
        const CoreTypeDecl &decl = program_.types[n.base.value];
        if (n.args.size() != decl.type_param_count) {
            error(verify::kValueTypeArityInvalid,
                  "value type #" + std::to_string(index) + " nominal '" + decl.name + "' expects " +
                      std::to_string(decl.type_param_count) + " arg(s), got " +
                      std::to_string(n.args.size()),
                  std::nullopt);
        }
        if (n.capacity.has_value() && !capacity_allowed(decl.role)) {
            error(verify::kValueTypeCapacityInvalid,
                  "value type #" + std::to_string(index) + " nominal '" + decl.name +
                      "' is not a bounded collection and cannot carry a capacity",
                  std::nullopt);
        }
    }

    // Iterative 3-color DFS: white(0)=unvisited, gray(1)=on stack, black(2)=done.
    // A gray->gray edge is a cycle. Out-of-range children were already reported;
    // skip them here to avoid indexing past the arena.
    void
    verify_value_types_acyclic(const std::function<void(const CoreValueType &,
                                                        std::vector<std::uint32_t> &)> &children_of) {
        const auto &arena = program_.value_types;
        enum Color : std::uint8_t { White, Gray, Black };
        std::vector<Color> color(arena.size(), White);
        std::vector<std::uint32_t> kids;
        bool reported = false;
        for (std::uint32_t root = 0; root < arena.size() && !reported; ++root) {
            if (color[root] != White) {
                continue;
            }
            // Explicit stack of (node, child-cursor); enter=push, exit=color black.
            std::vector<std::pair<std::uint32_t, std::size_t>> stack;
            std::vector<std::vector<std::uint32_t>> child_lists;
            stack.emplace_back(root, 0);
            color[root] = Gray;
            {
                std::vector<std::uint32_t> tmp;
                children_of(arena[root], tmp);
                child_lists.push_back(std::move(tmp));
            }
            while (!stack.empty()) {
                auto &[node, cursor] = stack.back();
                const std::vector<std::uint32_t> &ch = child_lists.back();
                if (cursor >= ch.size()) {
                    color[node] = Black;
                    stack.pop_back();
                    child_lists.pop_back();
                    continue;
                }
                const std::uint32_t child = ch[cursor++];
                if (child >= arena.size()) {
                    continue; // out-of-range already reported
                }
                if (color[child] == Gray) {
                    error(verify::kValueTypeCycle,
                          "value type arena contains a cycle at #" + std::to_string(child),
                          std::nullopt);
                    reported = true;
                    break;
                }
                if (color[child] == White) {
                    color[child] = Gray;
                    stack.emplace_back(child, 0);
                    children_of(arena[child], kids);
                    child_lists.push_back(kids);
                }
            }
        }
    }

    // A cheap structural hash for duplicate bucketing (NOT the interner's hash;
    // just enough to avoid O(n^2) full comparisons across the whole arena).
    [[nodiscard]] static std::size_t value_type_hash(const CoreValueType &vt) noexcept {
        std::size_t h = vt.node.index();
        const auto mix = [&h](std::size_t v) {
            h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        const auto vh_nominal = [&](const CoreVtNominal &n) {
            mix(n.base.value);
            for (const auto &a : n.args) {
                mix(a.value);
            }
        };
        const auto vh_tuple = [&](const CoreVtTuple &n) {
            for (const auto &e : n.elements) {
                mix(e.value);
            }
        };
        const auto vh_fn = [&](const CoreVtFn &n) {
            for (const auto &p : n.params) {
                mix(p.value);
            }
            mix(n.ret.value);
        };
        // RFC 0027 Q1 (KR6.13-X): one handler per value-type node, generated
        // from core_value_types.def. Nodes without child ids share
        // VERIFY_HASH_LEAF (a distinct typed no-op lambda per node); nominal /
        // tuple / fn route to the payload lambdas above. No unnamed catch-all: a
        // 15th node without a VERIFY_HASH_* routing macro fails to compile.
#define VERIFY_HASH_LEAF(Name, Wire) [](const CoreVt##Name &) noexcept {},
#define VERIFY_HASH_Unit(Name, Wire) VERIFY_HASH_LEAF(Name, Wire)
#define VERIFY_HASH_Never(Name, Wire) VERIFY_HASH_LEAF(Name, Wire)
#define VERIFY_HASH_Bool(Name, Wire) VERIFY_HASH_LEAF(Name, Wire)
#define VERIFY_HASH_Int(Name, Wire) VERIFY_HASH_LEAF(Name, Wire)
#define VERIFY_HASH_Float(Name, Wire) VERIFY_HASH_LEAF(Name, Wire)
#define VERIFY_HASH_String(Name, Wire) VERIFY_HASH_LEAF(Name, Wire)
#define VERIFY_HASH_Decimal(Name, Wire) VERIFY_HASH_LEAF(Name, Wire)
#define VERIFY_HASH_Duration(Name, Wire) VERIFY_HASH_LEAF(Name, Wire)
#define VERIFY_HASH_Timestamp(Name, Wire) VERIFY_HASH_LEAF(Name, Wire)
#define VERIFY_HASH_Uuid(Name, Wire) VERIFY_HASH_LEAF(Name, Wire)
#define VERIFY_HASH_Nominal(Name, Wire) vh_nominal,
#define VERIFY_HASH_Tuple(Name, Wire) vh_tuple,
#define VERIFY_HASH_Fn(Name, Wire) vh_fn,
#define VERIFY_HASH_Closure(Name, Wire) VERIFY_HASH_LEAF(Name, Wire)
#define HANDLE_CORE_VT(Name, Wire) VERIFY_HASH_##Name(Name, Wire)
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/core_value_types.def"
            },
            vt.node);
#undef HANDLE_CORE_VT
#undef VERIFY_HASH_Unit
#undef VERIFY_HASH_Never
#undef VERIFY_HASH_Bool
#undef VERIFY_HASH_Int
#undef VERIFY_HASH_Float
#undef VERIFY_HASH_String
#undef VERIFY_HASH_Decimal
#undef VERIFY_HASH_Duration
#undef VERIFY_HASH_Timestamp
#undef VERIFY_HASH_Uuid
#undef VERIFY_HASH_Nominal
#undef VERIFY_HASH_Tuple
#undef VERIFY_HASH_Fn
#undef VERIFY_HASH_Closure
#undef VERIFY_HASH_LEAF
        return h;
    }

    // --- outlined fn bodies (CORE-FNBODY-DESIGN §8.1) ---
    //
    // Verifies the CoreProgram.fns table and its 1:1 link to Fn-kind instances:
    //   #1 fn id == index; instance in range, payload CoreFnInstance, back-link
    //      agrees, no duplicate link (one body per Fn instance);
    //   #2 SSA body checks reuse the shared ArenaView (OwnerKind::Fn): dense
    //      value_types, expr/pattern arena bounds, def-before-use;
    //   #2 every path completes via a value-bearing CoreReturnStmt (termination,
    //      not goto/trap); goto is illegal in a fn;
    //   #3 every direct CoreCallExpr in ANY body (flow/workflow/fn) resolves to
    //      an in-range Fn instance with a valid body, exact arity, argument types
    //      equal to the callee param types, result type equal to the return type,
    //      callee body is pure (no capability call), and the static call graph is
    //      ACYCLIC (FB-1: no recursion until FB-2).
    void verify_fns() {
        std::unordered_set<std::uint32_t> linked_instances;
        for (std::uint32_t fi = 0; fi < program_.fns.size(); ++fi) {
            const CoreFnDecl &fn = program_.fns[fi];
            if (fn.id.value != fi) {
                error(verify::kFnInstanceLinkInvalid,
                      "fn #" + std::to_string(fi) + " id does not equal its table index",
                      fn.source_range);
            }
            // Instance link bounds + payload + back-reference.
            if (fn.instance.value == CoreInstanceId::kInvalid ||
                fn.instance.value >= program_.instances.size()) {
                error(verify::kFnInstanceLinkInvalid,
                      "fn '" + fn.name + "' references an out-of-range instance id",
                      fn.source_range);
                continue;
            }
            const CoreInstanceDecl &inst = program_.instances[fn.instance.value];
            const auto *payload = std::get_if<CoreFnInstance>(&inst.payload);
            if (payload == nullptr) {
                error(verify::kFnInstanceLinkInvalid,
                      "fn '" + fn.name + "' instance '" + inst.instance_key +
                          "' payload is not a CoreFnInstance",
                      fn.source_range);
            } else if (payload->body.value != fi) {
                error(verify::kFnInstanceLinkInvalid,
                      "fn '" + fn.name + "' and its instance disagree on the body link",
                      fn.source_range);
            }
            if (!linked_instances.insert(fn.instance.value).second) {
                error(verify::kFnInstanceLinkInvalid,
                      "fn instance '" + inst.instance_key +
                          "' is linked to more than one fn body",
                      fn.source_range);
            }

            // Body SSA arena (dense value types, expr/pattern arena, coercion
            // plans) reuses the shared flow/workflow checks.
            ArenaView av{fn.storage.exprs,
                         fn.storage.value_count,
                         fn.storage.patterns,
                         "fn '" + fn.name + "'"};
            av.owner = OwnerKind::Fn;
            av.value_types = &fn.storage.value_types;
            av.coercion_plans = &fn.storage.coercion_plans;
            verify_body_value_types(av);
            verify_coercion_plans(av);
            verify_expr_arena(av);
            verify_pattern_arena(av);

            // Parameters are pre-bound into the body SSA domain: each param is
            // a distinct, in-range value with a valid concrete logical type.
            std::unordered_set<std::uint32_t> param_set;
            for (const CoreValueId param : fn.params) {
                if (param.value >= fn.storage.value_count) {
                    error(verify::kFnSignatureArity,
                          "fn '" + fn.name + "' pre-bound param value id " +
                              std::to_string(param.value) + " is out of range",
                          fn.source_range);
                    continue;
                }
                if (!value_type_slot_ok(fn.storage.value_types[param.value])) {
                    error(verify::kFnSignatureArity,
                          "fn '" + fn.name + "' has a parameter without a concrete materialized "
                          "type (generic fn body was not monomorphized?)",
                          fn.source_range);
                }
                if (!param_set.insert(param.value).second) {
                    error(verify::kFnSignatureArity,
                          "fn '" + fn.name + "' binds one SSA value to two parameters",
                          fn.source_range);
                }
            }

            // The declared capture signature is the trust anchor every
            // CoreClosureExpr env check resolves against ("a wire artifact
            // cannot forge a self-consistent capture pair"), so each
            // program-global capture value-type id is validated on the fn
            // declaration itself — not only incidentally via an env-binding
            // cross-check or a construction site that happens to reference it.
            // An unreferenced fn with a kInvalid / out-of-range / `Never`
            // capture slot must fail on its own.
            for (std::size_t i = 0; i < fn.captures.size(); ++i) {
                if (!value_type_slot_ok(fn.captures[i])) {
                    error(verify::kClosureCaptureType,
                          "fn '" + fn.name + "' declared capture slot #" + std::to_string(i) +
                              " does not name a valid, materialized program-global value type "
                              "(out-of-range id, kInvalid, or a `Never`)",
                          fn.source_range);
                }
            }

            // FB-3a2 (§3.1.1 D-LIFT): the env capture slots are ALSO pre-bound
            // body SSA values — one per declared capture, parallel to it, distinct
            // from every logical parameter and from each other. They carry the
            // declared slot type so the lifted body reads captures like any other
            // pre-bound value (the wasm env-pointer / slot-load split is a codegen
            // concern). An ordinary fn has both lists empty.
            if (fn.env_bindings.size() != fn.captures.size()) {
                error(verify::kClosureCaptureArity,
                      "fn '" + fn.name + "' has " + std::to_string(fn.env_bindings.size()) +
                          " pre-bound env slot value(s) but declares " +
                          std::to_string(fn.captures.size()) + " capture slot(s)",
                      fn.source_range);
            }
            for (std::size_t i = 0; i < fn.env_bindings.size() && i < fn.captures.size(); ++i) {
                const CoreValueId slot = fn.env_bindings[i];
                if (slot.value >= fn.storage.value_count) {
                    error(verify::kClosureCaptureArity,
                          "fn '" + fn.name + "' pre-bound env slot #" + std::to_string(i) +
                              " value id " + std::to_string(slot.value) + " is out of range",
                          fn.source_range);
                    continue;
                }
                if (!value_type_slot_ok(fn.storage.value_types[slot.value])) {
                    error(verify::kClosureCaptureType,
                          "fn '" + fn.name + "' env slot #" + std::to_string(i) +
                              " has no concrete materialized value type",
                          fn.source_range);
                } else if (!(fn.storage.value_types[slot.value] == fn.captures[i])) {
                    error(verify::kClosureCaptureType,
                          "fn '" + fn.name + "' env slot #" + std::to_string(i) +
                              " pre-bound value type does not match its declared capture type",
                          fn.source_range);
                }
                if (!param_set.insert(slot.value).second) {
                    error(verify::kClosureCaptureArity,
                          "fn '" + fn.name + "' binds env slot #" + std::to_string(i) +
                              " to an SSA value already used by a parameter or another env slot",
                          fn.source_range);
                }
            }

            // Region walk: params AND env slots are the initial definitions /
            // visible set; the body must complete via return on every path,
            // never goto/yield.
            std::unordered_set<std::uint32_t> all_definitions;
            std::unordered_set<std::uint32_t> visible;
            for (const CoreValueId param : fn.params) {
                all_definitions.insert(param.value);
                visible.insert(param.value);
            }
            for (const CoreValueId slot : fn.env_bindings) {
                all_definitions.insert(slot.value);
                visible.insert(slot.value);
            }
            const RegionExit re = verify_region(av, /*state_count=*/0, fn.body,
                                                all_definitions, visible, RegionContext::Fn);
            verify_fn_termination(fn, re);
            // Every value-bearing return must carry the SAME concrete value
            // type. Disagreement is a malformed body (not the same as an
            // uninhabited fn with no value return); reject it here so the
            // call-site result gate can never silently disappear.
            if (fn_return_summary(fn).status == FnReturnTypeStatus::Disagree) {
                error(verify::kFnBodyTermination,
                      "fn '" + fn.name +
                          "' has value-bearing returns with disagreeing result types; every "
                          "return must carry the fn's single concrete return type",
                      fn.source_range);
            }
        }

        // Every Fn-kind instance with a valid body link must have a fn table
        // entry (reverse direction of the #1 check).
        for (std::uint32_t ii = 0; ii < program_.instances.size(); ++ii) {
            const auto *payload = std::get_if<CoreFnInstance>(&program_.instances[ii].payload);
            if (payload == nullptr) {
                continue;
            }
            if (payload->body.value == CoreFnId::kInvalid) {
                continue; // bodyless facade / prototype: legal
            }
            if (payload->body.value >= program_.fns.size() ||
                program_.fns[payload->body.value].instance.value != ii) {
                error(verify::kFnInstanceLinkInvalid,
                      "fn instance #" + std::to_string(ii) +
                          " body link does not point back to it",
                      std::nullopt);
            }
        }

        verify_fn_call_sites(linked_instances);
    }

    // Fn-body termination: every path must leave through a VALUE-bearing return
    // (CORE-FNBODY-DESIGN §8.1 #2). Fallthrough, goto, or a bare unit return
    // from a value-returning fn are rejected here against the RegionExit.
    void verify_fn_termination(const CoreFnDecl &fn, const RegionExit &re) {
        if (re.fallthrough) {
            error(verify::kFnBodyTermination,
                  "fn '" + fn.name + "' has a path that returns without a return statement",
                  fn.source_range);
        }
        if (re.diverges_control) {
            // RegionExit merges return + goto into diverges_control. The goto
            // legality is separately enforced in walk_goto for OwnerKind::Fn, so
            // here only the fallthrough/return-value shape matters.
        }
        // The concrete return type is taken from the value-bearing returns
        // (checked in walk_return via verify_fn_return_type); a fn with NO
        // value-return path at all (e.g. only trap) is structurally legal but
        // uninhabited — its result type then can't be checked, which the call
        // site catches as a result-type mismatch against its interned type.
    }

    // Walk every body (flow / workflow / fn) and validate each direct fn call —
    // the pure CoreCallExpr (expression position) and the FB-4 ordered
    // CoreCallStmt (statement position) — against the fn table: callee
    // resolution, arity, arg types, result type, and EFFECT-KIND consistency
    // (a pure expr may call only a PURE fn; an effectful callee is legal ONLY
    // through the ordered statement). Then run the recursion lattice over the
    // static graph.
    void verify_fn_call_sites(const std::unordered_set<std::uint32_t> &fn_instances) {
        // FB-4: the structural effect fixed point (shared with the backend), so
        // the pure-vs-effectful callee discipline is decided here, not by a
        // spelling.
        const FnEffectAnalysis effects = analyze_fn_effects(program_);
        struct CallSite {
            CoreInstanceId callee;
            std::vector<CoreValueId> args;
            CoreValueTypeId result_type;
            SourceRangeOpt range;
            const std::vector<CoreValueTypeId> *arg_types; // body value-type table
            std::string owner_label;
            bool statement_position{false}; // true = CoreCallStmt, false = CoreCallExpr
        };
        std::vector<CallSite> sites;
        const auto scan_storage_exprs = [&](const CoreBodyStorage &storage, std::string label,
                                           std::vector<CallSite> &out) {
            for (const CoreExpr &expr : storage.exprs) {
                const auto *call = std::get_if<CoreCallExpr>(&expr.node);
                if (call == nullptr) {
                    continue;
                }
                out.push_back(CallSite{call->callee, call->args, expr.result_type,
                                       expr.source_range, &storage.value_types, std::move(label),
                                       /*statement_position=*/false});
            }
        };
        // CoreCallStmt lives in a region, not the pure expr arena. Walk every
        // region statement of a body (and nested if/match regions).
        const auto scan_region_stmts = [&](const CoreRegion &root, const CoreBodyStorage &storage,
                                           std::string label, std::vector<CallSite> &out) {
            std::vector<const CoreRegion *> pending{&root};
            while (!pending.empty()) {
                const CoreRegion *region = pending.back();
                pending.pop_back();
                for (const CoreStmt &stmt : region->statements) {
                    if (const auto *call = std::get_if<CoreCallStmt>(&stmt.node)) {
                        const CoreValueTypeId result_ty =
                            call->result.value < storage.value_types.size()
                                ? storage.value_types[call->result.value]
                                : CoreValueTypeId{};
                        out.push_back(CallSite{call->callee, call->args, result_ty,
                                               stmt.source_range, &storage.value_types, label,
                                               /*statement_position=*/true});
                    }
                    if (const auto *branch = std::get_if<CoreIfStmt>(&stmt.node)) {
                        if (branch->then_region != nullptr) {
                            pending.push_back(branch->then_region.get());
                        }
                        if (branch->else_region != nullptr) {
                            pending.push_back(branch->else_region.get());
                        }
                    } else if (const auto *match = std::get_if<CoreMatchStmt>(&stmt.node)) {
                        for (const CoreMatchArm &arm : match->arms) {
                            if (arm.guard_region != nullptr) {
                                pending.push_back(arm.guard_region.get());
                            }
                            if (arm.body != nullptr) {
                                pending.push_back(arm.body.get());
                            }
                        }
                        if (match->fallback_region != nullptr) {
                            pending.push_back(match->fallback_region.get());
                        }
                    }
                }
            }
        };
        for (const CoreFlowDecl &flow : program_.flows) {
            scan_storage_exprs(flow.storage, "flow '" + flow.agent_name + "'", sites);
            for (const CoreFlowState &state : flow.states) {
                scan_region_stmts(state.body, flow.storage,
                                  "flow '" + flow.agent_name + "'", sites);
            }
        }
        for (const CoreWorkflowDecl &wf : program_.workflows) {
            scan_storage_exprs(wf.storage, "workflow '" + wf.name + "'", sites);
            // P2 fix-forward: the shared workflow arena's NODE INPUT and
            // RETURN region STATEMENTS are call sites too (the
            // WorkflowRootPolicy ExprLowerer appends a CoreCallStmt there).
            // Without this walk an ordered effectful call in a node region
            // received no callee effect-kind / arity / type check.
            for (const CoreWorkflowNode &node : wf.nodes) {
                if (node.input_region != nullptr) {
                    scan_region_stmts(*node.input_region, wf.storage,
                                      "workflow '" + wf.name + "' node '" + node.node_name + "'",
                                      sites);
                }
            }
            if (wf.return_region != nullptr) {
                scan_region_stmts(*wf.return_region, wf.storage,
                                  "workflow '" + wf.name + "' return", sites);
            }
        }
        for (const CoreFnDecl &fn : program_.fns) {
            scan_storage_exprs(fn.storage, "fn '" + fn.name + "'", sites);
            scan_region_stmts(fn.body, fn.storage, "fn '" + fn.name + "'", sites);
        }

        // Concrete callee signature: the param types are the pre-bound params'
        // body value types; the return type is the value type every return
        // carries (the body must be consistent — checked while scanning).
        for (const CallSite &site : sites) {
            if (site.callee.value == CoreInstanceId::kInvalid ||
                site.callee.value >= program_.instances.size()) {
                error(verify::kFnCallCalleeInvalid,
                      "body '" + site.owner_label + "' calls an out-of-range fn instance",
                      site.range);
                continue;
            }
            const CoreInstanceDecl &inst = program_.instances[site.callee.value];
            const auto *payload = std::get_if<CoreFnInstance>(&inst.payload);
            if (payload == nullptr || payload->body.value == CoreFnId::kInvalid) {
                error(verify::kFnCallCalleeInvalid,
                      "body '" + site.owner_label + "' calls '" + inst.instance_key +
                          "' which has no lowered fn body",
                      site.range);
                continue;
            }
            const std::uint32_t callee_index = payload->body.value;
            const CoreFnDecl &callee = program_.fns[callee_index];
            const bool callee_effectful =
                callee_index < effects.effectful.size() && effects.effectful[callee_index];
            // EFFECT-KIND CONSISTENCY (design §5.3): the ordered statement is the
            // ONLY legal position for an effectful callee; a PURE CoreCallExpr
            // that reaches an effectful fn is a misclassification / dropped-order
            // and fails closed. Conversely an ordered CoreCallStmt to a PURE fn
            // is a needless (but harmless) misclassification rejected so the two
            // call shapes never encode the same callee differently.
            if (!site.statement_position && callee_effectful) {
                error(verify::kFnCallEffectfulCallee,
                      "pure direct call to effectful fn '" + callee.name +
                          "' in body '" + site.owner_label +
                          "': an effectful callee may be invoked only from an ordered call "
                          "statement (CoreCallStmt), never from a pure expression position",
                      site.range);
            }
            if (site.statement_position && !callee_effectful) {
                error(verify::kFnCallEffectKind,
                      "ordered call statement targets pure fn '" + callee.name +
                          "' in body '" + site.owner_label +
                          "': a pure callee is invoked as a CoreCallExpr, not an effect "
                          "statement",
                      site.range);
            }
            if (site.args.size() != callee.params.size()) {
                error(verify::kFnCallArityMismatch,
                      "call to fn '" + callee.name + "' passes " +
                          std::to_string(site.args.size()) + " argument(s) but it declares " +
                          std::to_string(callee.params.size()),
                      site.range);
                continue;
            }
            for (std::size_t i = 0; i < site.args.size(); ++i) {
                const CoreValueId arg = site.args[i];
                if (arg.value >= site.arg_types->size()) {
                    error(verify::kFnCallArgumentTypeMismatch,
                          "call to fn '" + callee.name + "' argument #" + std::to_string(i) +
                              " is out of range in the caller body",
                          site.range);
                    continue;
                }
                const CoreValueTypeId arg_ty = (*site.arg_types)[arg.value];
                const CoreValueTypeId param_ty =
                    callee.storage.value_types[callee.params[i].value];
                // D-FNREP (design §3.1.1): a constructed CoreVtClosure{S,_}
                // passed where the bare signature S is expected is a structural
                // widening (identical layout, env ignored) — accept it, but never
                // the reverse narrowing.
                if (!(arg_ty == param_ty) &&
                    !callable_argument_widens(arg_ty, param_ty)) {
                    error(verify::kFnCallArgumentTypeMismatch,
                          "call to fn '" + callee.name + "' argument #" + std::to_string(i) +
                              " type does not match the callee parameter type",
                          site.range);
                }
            }
            // Result type vs the callee's concrete return type. A `Disagree`
            // callee was already rejected at its definition; an uninhabited
            // callee (no value return) is checked below.
            //
            // D-FNREP (design §3.1.1): the same one-directional widening that
            // applies at callable ARGUMENT slots applies to the call RESULT: a
            // callee whose concrete return is a constructed
            // CoreVtClosure{S,_} (e.g. an fn declared `-> Fn(Int)->Int` whose
            // body returns a captured lambda) is accepted at a call site typed
            // with the bare signature S. The reverse (site typed closure over a
            // bare-Fn return) stays a mismatch.
            const FnReturnTypeSummary ret_summary = fn_return_summary(callee);
            if (ret_summary.status == FnReturnTypeStatus::Ok &&
                !(site.result_type == *ret_summary.type) &&
                !callable_argument_widens(*ret_summary.type, site.result_type)) {
                error(verify::kFnCallResultTypeMismatch,
                      "call to fn '" + callee.name +
                          "' result type does not match the callee return type",
                      site.range);
            }
        }

        // RFC 0026 FB-2 (design §8.1 rule 6): the fn direct-call graph is no
        // longer required ACYCLIC. Recursion is admitted exactly when the
        // compile-time depth lattice seals every recursion group (SCC) with a
        // finite structural bound: one Int rank parameter that progresses by a
        // positive constant on every internal edge, a divergent base-case
        // guard against a bounded expression, and statically bounded rank
        // values on every entry edge. `decreases` is never read (erased before
        // Core). Anything else fails closed — never a silent unbounded call.
        const FnRecursionAnalysis recursion = analyze_fn_recursion(program_);
        for (const FnRecursionIssue &issue : recursion.unbounded_issues) {
            error(verify::kFnRecursionUnbounded,
                  recursion_issue_message(issue), issue.range);
        }
        for (const FnRecursionScc &scc : recursion.overflow_sccs) {
            std::ostringstream msg;
            msg << "recursive fn group (members [";
            for (std::size_t i = 0; i < scc.members.size(); ++i) {
                msg << (i == 0 ? "" : ", ")
                    << program_.fns[scc.members[i]].name;
            }
            msg << "]) has a structural depth bound " << scc.depth_bound
                << " that exceeds the Core-layer recursion ceiling "
                << kFnRecursionDepthCeiling
                << "; the bounded collection driving the recursion is too large "
                   "for the fixed wasm32 resource plan";
            error(verify::kFnRecursionDepth, msg.str(),
                  program_.fns[scc.members.front()].source_range);
        }
        static_cast<void>(fn_instances);
    }

    // RFC 0026 FB-4 (design §5.3): effect-authorization at the AGENT boundary.
    // A direct capability call inside a flow handler is already checked against
    // the target agent's whitelist by walk_capabilityCall. An effect reached
    // TRANSITIVELY through an ordered CoreCallStmt (a handler calls an fn that
    // calls a capability, across any fn depth) must be whitelisted too. This is a
    // program-wide reachability check over the fn call graph (BOTH the ordered
    // CoreCallStmt and the pure CoreCallExpr edges, so authorization follows the
    // effect fixed point the call-site discipline uses), independent of the
    // per-fn whitelist an fn itself carries (an fn is not bound to one agent).
    void verify_fn_effect_authorization() {
        if (program_.fns.empty()) {
            return;
        }
        const FnEffectAnalysis effects = analyze_fn_effects(program_);
        const ClosurePointsTo points_to = ClosurePointsTo::analyze(program_);
        // The SINGLE closure-aware invocation graph (direct CoreCallExpr,
        // ordered CoreCallStmt and indirect CoreCallClosureExpr points-to
        // edges), so authorization follows exactly the effects a body can reach
        // at run time — including an effect routed through a callable
        // parameter / closure value.
        const std::vector<std::vector<std::uint32_t>> graph =
            fn_invocation_graph(program_, points_to);

        // Grow the set of fn bodies reachable from one set of region roots.
        const auto reachable_from = [&](const std::vector<std::uint32_t> &roots) {
            std::unordered_set<std::uint32_t> seen;
            std::vector<std::uint32_t> worklist = roots;
            while (!worklist.empty()) {
                const std::uint32_t fi = worklist.back();
                worklist.pop_back();
                if (!seen.insert(fi).second || fi >= graph.size()) {
                    continue;
                }
                for (const std::uint32_t c : graph[fi]) {
                    if (!seen.contains(c)) {
                        worklist.push_back(c);
                    }
                }
            }
            return seen;
        };
        // Authorize every capability each reachable effectful fn reaches
        // against one agent whitelist.
        const auto authorize_agent = [&](const CoreAgentDecl &agent,
                                         const std::unordered_set<std::uint32_t> &seen,
                                         const std::string &owner_label,
                                         SourceRangeOpt range) {
            std::unordered_set<std::uint32_t> allowed;
            for (const CoreCapabilityId id : agent.capabilities) {
                if (id.value < program_.capabilities.size()) {
                    allowed.insert(id.value);
                }
            }
            for (const std::uint32_t fi : seen) {
                for (const CoreCapabilityId cap : effects.capabilities[fi]) {
                    if (!allowed.contains(cap.value)) {
                        error(verify::kFnEffectCapabilityUnauthorized,
                              "effectful fn '" + program_.fns[fi].name +
                                  "' reachable from " + owner_label + " invokes capability '" +
                                  (cap.value < program_.capabilities.size()
                                       ? program_.capabilities[cap.value].name
                                       : std::to_string(cap.value)) +
                                  "' which is not in target agent '" + agent.name +
                                  "' whitelist",
                              range ? range : program_.fns[fi].source_range);
                    }
                }
            }
        };

        // Flow handler roots: one agent whitelist per flow.
        for (const CoreFlowDecl &flow : program_.flows) {
            if (flow.target.value >= program_.agents.size()) {
                continue;
            }
            std::vector<std::uint32_t> roots;
            for (const CoreFlowState &state : flow.states) {
                append_region_invoked_fns(program_, points_to, state.body, flow.storage, roots);
            }
            authorize_agent(program_.agents[flow.target.value], reachable_from(roots),
                            "flow '" + flow.agent_name + "'", std::nullopt);
        }

        // Workflow node roots: each node invokes a packaged agent instance, so
        // an effectful fn reached from the node input region must be whitelisted
        // on THAT node's target agent. The workflow return region has no
        // invoked agent and only forwards node outputs, so it contributes no
        // authorization root.
        for (const CoreWorkflowDecl &wf : program_.workflows) {
            for (const CoreWorkflowNode &node : wf.nodes) {
                const CoreAgentInstance *instance = agent_instance_of(node.target_instance);
                if (instance == nullptr ||
                    instance->base.value >= program_.agents.size() ||
                    node.input_region == nullptr) {
                    continue;
                }
                std::vector<std::uint32_t> roots;
                append_region_invoked_fns(program_, points_to, *node.input_region,
                                          wf.storage, roots);
                authorize_agent(program_.agents[instance->base.value], reachable_from(roots),
                                "workflow '" + wf.name + "' node '" + node.node_name + "'",
                                std::nullopt);
            }
        }
    }


    //
    // Lambda lifting (FB-3a2) is what makes the lowerer EMIT these nodes, but the
    // model-level rules are enforced now on every artifact — including hand-built
    // partial programs and JSON wire round-trips — so a malformed closure can
    // never cross the consumption boundary. The wasm backend independently fails
    // closed on these nodes until the FB-3 codegen slice, so accepting a
    // well-formed one here never enables execution.
    //
    // A CoreClosureExpr is well-formed iff:
    //   * `fn` resolves to an in-table CoreFnDecl;
    //   * the env operand COUNT equals the callee fn's DECLARED capture
    //     signature (`CoreFnDecl::captures`, env-slot order);
    //   * each env operand's logical type equals the declared slot type;
    //   * the expr's result type is an interned CoreVtClosure whose captures
    //     match the declared list and whose signature is the callee fn's concrete
    //     CoreVtFn (params + return).
    // A CoreCallClosureExpr is well-formed iff its callee value is a callable
    // (CoreVtFn at a binding site or CoreVtClosure at a construction site), the
    // arity / argument types match the resolved signature, and its result type is
    // the signature's return type.
    void verify_closures() {
        struct BodyArena {
            const CoreBodyStorage *storage;
            std::string label;
        };
        std::vector<BodyArena> arenas;
        for (const CoreFlowDecl &flow : program_.flows) {
            arenas.push_back({&flow.storage, "flow '" + flow.agent_name + "'"});
        }
        for (const CoreWorkflowDecl &wf : program_.workflows) {
            arenas.push_back({&wf.storage, "workflow '" + wf.name + "'"});
        }
        for (const CoreFnDecl &fn : program_.fns) {
            arenas.push_back({&fn.storage, "fn '" + fn.name + "'"});
        }

        for (const BodyArena &arena : arenas) {
            const CoreBodyStorage &storage = *arena.storage;
            for (const CoreExpr &expr : storage.exprs) {
                std::visit(
                    Overloaded{
                        [&](const CoreClosureExpr &c) {
                            verify_closure_construction(arena.label, storage, c, expr);
                        },
                        [&](const CoreCallClosureExpr &c) {
                            verify_closure_call(arena.label, storage, c, expr);
                        },
                        [](const auto &) {},
                    },
                    expr.node);
            }
        }
    }

    // The CoreVtFn a callable logical type resolves to, or nullptr. A signature
    // type is itself a CoreVtFn; a constructed closure yields its signature.
    [[nodiscard]] const CoreVtFn *callable_signature(CoreValueTypeId id) const {
        if (id.value == CoreValueTypeId::kInvalid || id.value >= program_.value_types.size()) {
            return nullptr;
        }
        const CoreValueTypeNode &node = program_.value_types[id.value].node;
        if (const auto *fn = std::get_if<CoreVtFn>(&node)) {
            return fn;
        }
        if (const auto *closure = std::get_if<CoreVtClosure>(&node)) {
            const auto sig = closure->signature.value;
            if (sig == CoreValueTypeId::kInvalid || sig >= program_.value_types.size()) {
                return nullptr;
            }
            return std::get_if<CoreVtFn>(&program_.value_types[sig].node);
        }
        return nullptr;
    }

    // D-FNREP (design §3.1.1): a constructed CoreVtClosure{S,_} widens for free
    // to the bare signature S when passed where S is expected (identical 8-byte
    // layout; the caller ignores env). Fn -> closure narrowing and any other
    // mismatch return false.
    [[nodiscard]] bool callable_argument_widens(CoreValueTypeId source,
                                                CoreValueTypeId target) const {
        if (source == target) {
            return true;
        }
        if (source.value == CoreValueTypeId::kInvalid ||
            source.value >= program_.value_types.size()) {
            return false;
        }
        const auto *closure =
            std::get_if<CoreVtClosure>(&program_.value_types[source.value].node);
        return closure != nullptr && closure->signature == target;
    }

    void verify_closure_construction(const std::string &label, const CoreBodyStorage &storage,
                                     const CoreClosureExpr &c, const CoreExpr &expr) {
        // Field-shape pass (verify_closure_expr) already reports an out-of-range
        // fn / env id; do not cascade into table lookups here.
        if (c.fn.value == CoreFnId::kInvalid || c.fn.value >= program_.fns.size()) {
            return;
        }
        const CoreFnDecl &target = program_.fns[c.fn.value];
        if (c.env.size() != target.captures.size()) {
            error(verify::kClosureCaptureArity,
                  "closure construction in body '" + label + "' supplies " +
                      std::to_string(c.env.size()) + " captured value(s) but fn '" + target.name +
                      "' declares " + std::to_string(target.captures.size()) +
                      " environment slot(s)",
                  expr.source_range);
        }
        for (std::size_t i = 0; i < c.env.size() && i < target.captures.size(); ++i) {
            const CoreValueId operand = c.env[i];
            if (operand.value >= storage.value_types.size()) {
                continue; // out-of-range operand reported by the field pass
            }
            if (!(storage.value_types[operand.value] == target.captures[i])) {
                error(verify::kClosureCaptureType,
                      "closure construction in body '" + label + "' capture slot #" +
                          std::to_string(i) + " operand type does not match fn '" + target.name +
                          "' declared capture type",
                      expr.source_range);
            }
        }
        // Bounds-check BEFORE indexing program_.value_types: the field-shape
        // pass (verify_expr_arena) only records a diagnostic, it does not stop
        // this program-wide pass, so a kInvalid / out-of-range result type (e.g.
        // round-tripped from a tampered wire doc) would otherwise dereference
        // off the end of the value-type arena.
        if (expr.result_type.value == CoreValueTypeId::kInvalid ||
            expr.result_type.value >= program_.value_types.size()) {
            error(verify::kClosureResultTypeInvalid,
                  "closure construction in body '" + label +
                      "' result type is not an interned closure value type",
                  expr.source_range);
            return;
        }
        const auto *closure =
            std::get_if<CoreVtClosure>(&program_.value_types[expr.result_type.value].node);
        if (closure == nullptr) {
            error(verify::kClosureResultTypeInvalid,
                  "closure construction in body '" + label +
                      "' result type is not an interned closure value type",
                  expr.source_range);
            return;
        }
        const auto sig_id = closure->signature.value;
        const CoreVtFn *signature = nullptr;
        if (sig_id != CoreValueTypeId::kInvalid && sig_id < program_.value_types.size()) {
            signature = std::get_if<CoreVtFn>(&program_.value_types[sig_id].node);
        }
        if (signature == nullptr) {
            error(verify::kClosureResultTypeInvalid,
                  "closure construction in body '" + label +
                      "' result closure signature does not resolve to a fn value type",
                  expr.source_range);
            return;
        }
        if (closure->captures.size() != target.captures.size()) {
            error(verify::kClosureResultTypeInvalid,
                  "closure construction in body '" + label + "' result closure carries " +
                      std::to_string(closure->captures.size()) +
                      " capture type(s) but fn '" + target.name + "' declares " +
                      std::to_string(target.captures.size()),
                  expr.source_range);
        }
        for (std::size_t i = 0; i < closure->captures.size() && i < target.captures.size(); ++i) {
            if (closure->captures[i].mode != CoreCaptureMode::ByValue ||
                !(closure->captures[i].value_type == target.captures[i])) {
                error(verify::kClosureResultTypeInvalid,
                      "closure construction in body '" + label + "' result closure capture slot #" +
                          std::to_string(i) + " does not match fn '" + target.name +
                          "' declared capture (ByValue) type",
                      expr.source_range);
            }
        }
        // The closure signature is the callee fn's CONCRETE signature: params are
        // the pre-bound params' body types (env is a separate wasm-only first
        // parameter, never a logical parameter — design §3.1.1 D-LIFT), and the
        // return type is the type every value-bearing return carries.
        if (signature->params.size() != target.params.size()) {
            error(verify::kClosureResultTypeInvalid,
                  "closure construction in body '" + label +
                      "' closure signature param count does not match fn '" + target.name + "'",
                  expr.source_range);
        }
        for (std::size_t i = 0; i < signature->params.size() && i < target.params.size(); ++i) {
            const CoreValueId param = target.params[i];
            if (param.value >= target.storage.value_types.size() ||
                !(signature->params[i] == target.storage.value_types[param.value])) {
                error(verify::kClosureResultTypeInvalid,
                      "closure construction in body '" + label + "' signature parameter #" +
                          std::to_string(i) + " type does not match fn '" + target.name +
                          "' concrete parameter type",
                      expr.source_range);
            }
        }
        // D-FNREP (design §3.1.1): the body may RETURN a constructed
        // CoreVtClosure{S2,_} where the lifted fn's declared signature names
        // the bare S2 (a lambda whose body itself constructs a closure) — the
        // same one-directional result widening as a direct call site. The
        // reverse (bare-Fn return at a closure-typed signature) is rejected.
        const FnReturnTypeSummary ret = fn_return_summary(target);
        if (ret.status == FnReturnTypeStatus::Ok && !(signature->ret == *ret.type) &&
            !callable_argument_widens(*ret.type, signature->ret)) {
            error(verify::kClosureResultTypeInvalid,
                  "closure construction in body '" + label +
                      "' signature return type does not match fn '" + target.name +
                      "' concrete return type",
                  expr.source_range);
        }
    }

    void verify_closure_call(const std::string &label, const CoreBodyStorage &storage,
                             const CoreCallClosureExpr &c, const CoreExpr &expr) {
        if (c.callee.value >= storage.value_types.size()) {
            return; // out-of-range callee reported by the field-shape pass
        }
        const CoreValueTypeId callee_type = storage.value_types[c.callee.value];
        const CoreVtFn *signature = callable_signature(callee_type);
        if (signature == nullptr) {
            error(verify::kClosureDispatchCalleeInvalid,
                  "closure call in body '" + label +
                      "' callee value does not have a callable (fn / closure) logical type",
                  expr.source_range);
            return;
        }
        if (c.args.size() != signature->params.size()) {
            error(verify::kClosureDispatchArity,
                  "closure call in body '" + label + "' passes " +
                      std::to_string(c.args.size()) + " argument(s) but its callable signature has " +
                      std::to_string(signature->params.size()),
                  expr.source_range);
        }
        for (std::size_t i = 0; i < c.args.size() && i < signature->params.size(); ++i) {
            const CoreValueId arg = c.args[i];
            if (arg.value >= storage.value_types.size()) {
                continue; // out-of-range arg reported by the field pass
            }
            // D-FNREP (design §3.1.1 / §8.1 #5): exactly as on the direct
            // CoreCallExpr path, a constructed CoreVtClosure{S,_} widens for
            // free to the bare signature S at a callable parameter slot
            // (identical layout, env ignored). Raw-id equality alone would
            // reject a lambda passed to a map-like whose parameter is callable.
            if (!(storage.value_types[arg.value] == signature->params[i]) &&
                !callable_argument_widens(storage.value_types[arg.value], signature->params[i])) {
                error(verify::kClosureDispatchArgumentType,
                      "closure call in body '" + label + "' argument #" + std::to_string(i) +
                          " type does not match the callable signature parameter type",
                      expr.source_range);
            }
        }
        if (!(expr.result_type == signature->ret)) {
            error(verify::kClosureDispatchResultType,
                  "closure call in body '" + label +
                      "' result type does not match the callable signature return type",
                  expr.source_range);
        }
    }

    // The human-readable message for one FB-2 unbounded-recursion finding.
    [[nodiscard]] std::string
    recursion_issue_message(const FnRecursionIssue &issue) const {
        const auto name_of = [&](std::uint32_t id) -> std::string {
            if (id == CoreFnId::kInvalid || id >= program_.fns.size()) {
                return "<fn>";
            }
            return program_.fns[id].name;
        };
        std::ostringstream msg;
        switch (issue.kind) {
        case FnRecursionIssueKind::RankNotInteger:
            msg << "recursive fn '" << name_of(issue.edge_from)
                << "' has no integer rank parameter; FB-2 bounds recursion by an "
                   "Int argument that decreases/increases by a compile-time "
                   "constant on every recursive call";
            break;
        case FnRecursionIssueKind::NoRankProgression:
            msg << "recursive call from '" << name_of(issue.edge_from) << "' to '"
                << name_of(issue.edge_to)
                << "' does not pass a rank parameter progressed by a positive "
                   "constant (rank +/- literal); recursion whose depth is not "
                   "statically countable is rejected";
            break;
        case FnRecursionIssueKind::NoBaseGuard:
            msg << "recursive fn '" << name_of(issue.edge_from)
                << "' has no base-case comparison of its rank parameter against a "
                   "bounded value (a literal or the length of a bounded "
                   "collection); the recursion cannot be proven to terminate "
                   "structurally";
            break;
        case FnRecursionIssueKind::BoundNotStatic:
            msg << "the base-case bound of recursive fn '"
                << name_of(issue.edge_from)
                << "' is not statically derivable; it must be a compile-time "
                   "integer, a bounded-collection length, or an invariant "
                   "parameter bounded by one at every entry call";
            break;
        case FnRecursionIssueKind::EntryRankNotStatic:
            msg << "an entry call to recursive fn '" << name_of(issue.edge_to)
                << "' binds the rank parameter to a value without a static bound "
                   "(not a literal and not a bounded-collection length); the "
                   "initial recursion depth is unknown";
            break;
        case FnRecursionIssueKind::EdgeNotDominatedByGuard:
            msg << "a recursive edge from '" << name_of(issue.edge_from) << "' to '"
                << name_of(issue.edge_to)
                << "' is not dominated by its base-case guard: the call executes "
                   "before the guard or inside the guard's stop branch, so the "
                   "finite depth bound does not gate it; place the recursive call "
                   "on the post-guard continue path (else / after the if)";
            break;
        }
        return msg.str();
    }

    // Classification of an fn body's value-bearing returns.
    enum class FnReturnTypeStatus {
        NoValueReturn, // no value-bearing return on any path (an uninhabited fn)
        Ok,            // every value-bearing return carries ONE value type
        Disagree,      // two value-bearing returns carry DIFFERENT value types
    };
    struct FnReturnTypeSummary {
        FnReturnTypeStatus status{FnReturnTypeStatus::NoValueReturn};
        std::optional<CoreValueTypeId> type;
    };

    // The concrete return type of an fn body: the value type every value-bearing
    // return carries. `Disagree` (rather than a nullopt masquerading as
    // NoValueReturn) marks returns that carry different value types — a
    // malformed body the caller MUST reject with FN_BODY_TERMINATION instead of
    // silently dropping the call-site result-type gate.
    [[nodiscard]] FnReturnTypeSummary fn_return_summary(const CoreFnDecl &fn) const {
        std::optional<CoreValueTypeId> found;
        bool mismatch = false;
        const auto scan = [&](auto &&self, const CoreRegion &region) -> void {
            for (const CoreStmt &stmt : region.statements) {
                if (const auto *ret = std::get_if<CoreReturnStmt>(&stmt.node);
                    ret != nullptr && ret->has_value) {
                    if (ret->value.value >= fn.storage.value_types.size()) {
                        mismatch = true;
                        continue;
                    }
                    const CoreValueTypeId ty = fn.storage.value_types[ret->value.value];
                    if (found.has_value() && !(*found == ty)) {
                        mismatch = true;
                    }
                    found = ty;
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
        scan(scan, fn.body);
        if (mismatch) {
            return FnReturnTypeSummary{FnReturnTypeStatus::Disagree, std::nullopt};
        }
        if (!found.has_value()) {
            return FnReturnTypeSummary{FnReturnTypeStatus::NoValueReturn, std::nullopt};
        }
        return FnReturnTypeSummary{FnReturnTypeStatus::Ok, found};
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
            for (const CoreValueTypeId &t : inst.dispatch_types) {
                if (!dispatch_value_type_ok(t)) {
                    error(verify::kInstanceDispatchTypeInvalid,
                          "instance '" + inst.instance_key +
                              "' has an invalid dispatch type (out-of-range value-type id or a "
                              "`Never`, which cannot be a materialized dispatch type)",
                          std::nullopt);
                }
            }
            // origin.kind must match the payload variant, and (where a Core base
            // table exists) origin must be the SAME nominal symbol as the base.
            const auto require_origin_kind = [&](ir::SymbolRefKind want, const char *what) {
                if (inst.origin.kind != want || (inst.origin.canonical_name.empty() &&
                                                 !inst.origin.id.has_value())) {
                    error(verify::kInstanceOriginInvalid,
                          "instance '" + inst.instance_key + "' origin is not a valid " + what +
                              " symbol",
                          std::nullopt);
                    return false;
                }
                return true;
            };
            const auto require_origin_is = [&](const ir::SymbolRef &base_sym, const char *what) {
                if (!symbol_refs_identify_same(inst.origin, base_sym)) {
                    error(verify::kInstanceOriginInvalid,
                          "instance '" + inst.instance_key + "' origin does not match its base " +
                              what + " symbol",
                          std::nullopt);
                }
            };
            std::visit(Overloaded{
                           [&](const CoreCapabilityInstance &p) {
                               require_origin_kind(ir::SymbolRefKind::Capability, "capability");
                               if (p.base.value >= program_.capabilities.size()) {
                                   error(verify::kInstanceBaseInvalid,
                                         "capability instance '" + inst.instance_key +
                                             "' base id is out of range",
                                         std::nullopt);
                                   return;
                               }
                               require_origin_is(program_.capabilities[p.base.value].symbol_ref,
                                                 "capability");
                           },
                           [&](const CorePredicateInstance &) {
                               require_origin_kind(ir::SymbolRefKind::Predicate, "predicate");
                           },
                           [&](const CoreAgentInstance &p) {
                               require_origin_kind(ir::SymbolRefKind::Agent, "agent");
                               if (p.base.value >= program_.agents.size()) {
                                   error(verify::kInstanceBaseInvalid,
                                         "agent instance '" + inst.instance_key +
                                             "' base id is out of range",
                                         std::nullopt);
                                   return;
                               }
                               const CoreAgentDecl &base = program_.agents[p.base.value];
                               require_origin_is(base.symbol_ref, "agent");
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
                               // The mangle dispatch descriptor for an agent is
                               // exactly [input, context, output] — it must align
                               // with the concrete shell (a descriptor for shell A
                               // with a payload/link for shell B is malformed).
                               check_agent_dispatch_shape(inst, p);
                           },
                           [&](const CoreWorkflowInstance &p) {
                               require_origin_kind(ir::SymbolRefKind::Workflow, "workflow");
                               if (p.base.value >= program_.workflows.size()) {
                                   error(verify::kInstanceBaseInvalid,
                                         "workflow instance '" + inst.instance_key +
                                             "' base id is out of range",
                                         std::nullopt);
                                   return;
                               }
                               const CoreWorkflowDecl &base = program_.workflows[p.base.value];
                               require_origin_is(base.symbol_ref, "workflow");
                               if (!(p.input_type == base.input_type) ||
                                   !(p.output_type == base.output_type)) {
                                   error(verify::kInstanceShellMismatch,
                                         "workflow instance '" + inst.instance_key +
                                             "' shell does not match its nominal workflow '" +
                                             base.name + "'",
                                         std::nullopt);
                               }
                               check_workflow_dispatch_shape(inst, p);
                           },
                           [&](const CoreFnInstance &) {
                               require_origin_kind(ir::SymbolRefKind::Function, "function");
                           },
                       },
                       inst.payload);
        }
    }

    // Two SymbolRefs identify the SAME nominal declaration: id-first (both set +
    // equal), else non-empty canonical name equal.
    [[nodiscard]] static bool symbol_refs_identify_same(const ir::SymbolRef &a,
                                                        const ir::SymbolRef &b) {
        if (a.id.has_value() && b.id.has_value()) {
            return *a.id == *b.id;
        }
        return !a.canonical_name.empty() && a.canonical_name == b.canonical_name;
    }

    // A dispatch value-type id must be in range AND not resolve to a CoreVtNever
    // (uninhabited types cannot be a materialized dispatch descriptor slot — RFC
    // 0026 P4, Codex invariant 1). Concreteness at every depth is guaranteed by
    // construction (lower_value_type fails closed on Unresolved/Any/Never) and
    // re-checked structurally by verify_value_types; here we only add the
    // consumer-context Never rejection.
    [[nodiscard]] bool dispatch_value_type_ok(CoreValueTypeId id) const {
        if (id.value == CoreValueTypeId::kInvalid || id.value >= program_.value_types.size()) {
            return false;
        }
        return !std::holds_alternative<CoreVtNever>(program_.value_types[id.value].node);
    }

    // Whether a dispatch value-type slot is the nominal `shell`. Resolves the
    // slot's interned node: it must be a `CoreVtNominal` whose `base` equals the
    // shell CoreTypeId (Principle 2 — identity by id, not by string).
    [[nodiscard]] bool dispatch_slot_matches_core_type(CoreValueTypeId slot,
                                                       CoreTypeId shell) const {
        if (shell.value == CoreTypeId::kInvalid || shell.value >= program_.types.size()) {
            return false;
        }
        if (slot.value == CoreValueTypeId::kInvalid || slot.value >= program_.value_types.size()) {
            return false;
        }
        const auto *nominal =
            std::get_if<CoreVtNominal>(&program_.value_types[slot.value].node);
        return nominal != nullptr && nominal->base == shell;
    }

    // A Unit context descriptor slot must resolve to a `CoreVtUnit`.
    [[nodiscard]] bool dispatch_slot_is_unit(CoreValueTypeId slot) const {
        if (slot.value == CoreValueTypeId::kInvalid || slot.value >= program_.value_types.size()) {
            return false;
        }
        return std::holds_alternative<CoreVtUnit>(program_.value_types[slot.value].node);
    }

    // Agent dispatch descriptor is exactly [input, context, output]; each slot must
    // STRUCTURALLY match the corresponding shell (kind + canonical name, or a bare
    // Unit for a Unit context). A wrong length or a slot that does not match the
    // payload shell is malformed (the opaque key would describe a different shell
    // than it runs).
    void check_agent_dispatch_shape(const CoreInstanceDecl &inst, const CoreAgentInstance &p) {
        if (inst.dispatch_types.size() != 3) {
            error(verify::kInstanceShellMismatch,
                  "agent instance '" + inst.instance_key + "' dispatch descriptor must be exactly "
                  "[input, context, output] (3 types)",
                  std::nullopt);
            return;
        }
        const bool ctx_ok = p.context_kind == CoreAgentDecl::ContextKind::Unit
                                ? dispatch_slot_is_unit(inst.dispatch_types[1])
                                : dispatch_slot_matches_core_type(inst.dispatch_types[1],
                                                                  p.context_type);
        if (!dispatch_slot_matches_core_type(inst.dispatch_types[0], p.input_type) || !ctx_ok ||
            !dispatch_slot_matches_core_type(inst.dispatch_types[2], p.output_type)) {
            error(verify::kInstanceShellMismatch,
                  "agent instance '" + inst.instance_key +
                      "' dispatch descriptor does not align with its [input, context, output] shell",
                  std::nullopt);
        }
    }

    // Workflow dispatch descriptor is [input, output]; each slot must structurally
    // match the corresponding shell type.
    void check_workflow_dispatch_shape(const CoreInstanceDecl &inst, const CoreWorkflowInstance &p) {
        if (inst.dispatch_types.size() != 2) {
            error(verify::kInstanceShellMismatch,
                  "workflow instance '" + inst.instance_key +
                      "' dispatch descriptor must be exactly [input, output] (2 types)",
                  std::nullopt);
            return;
        }
        if (!dispatch_slot_matches_core_type(inst.dispatch_types[0], p.input_type) ||
            !dispatch_slot_matches_core_type(inst.dispatch_types[1], p.output_type)) {
            error(verify::kInstanceShellMismatch,
                  "workflow instance '" + inst.instance_key +
                      "' dispatch descriptor does not align with its [input, output] shell",
                  std::nullopt);
        }
    }

    const CoreProgram &program_;
    std::vector<CoreLowerDiagnostic> diags_;
};

// RFC 0027 P6/P7/P8 (KR6.13-F): the three handler sets above are exhaustive
// `std::visit` overload groups over CoreExprNode / CorePatternNode with NO
// generic catch-all, so an alternative added to core_ir.hpp without a matching
// arm is rejected by the .def-less variant's own exhaustiveness check — the
// compiler names the unhandled type at the visit, before any build can go
// silently unchecked. The cardinality of each variant is pinned once, in the
// header that declares it (include/ahfl/compiler/ir/core_ir.hpp, "P8 IR SSOT
// compile-time cardinality gate"); it is deliberately NOT re-asserted here,
// because a second copy of the same number would only duplicate the header's
// message without tying it to these handler sets.

} // namespace

CoreVerifyResult verify_core_program(const CoreProgram &program) {
    Verifier verifier(program);
    return CoreVerifyResult{verifier.run()};
}

} // namespace ahfl::ir::core
