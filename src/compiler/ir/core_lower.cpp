// ---------------------------------------------------------------------------
// AhflIr -> Core-IR lowering (RFC 0026 P3, KR6.4) — A-normal form
// ---------------------------------------------------------------------------
//
// See `include/ahfl/compiler/ir/core_ir.hpp` for the layer contract. This pass
// lowers agent state machines, capability imports, and flow handler bodies into
// the execution layer. Handler bodies are A-normalized (Rust MIR / Swift SIL
// style): pure computation lands in a per-flow `CoreExpr` arena and EVERY
// capability invocation is an ordered `CoreCapabilityCallStmt`, so eval order,
// data dependency, pending/suspend, resume checkpoints, and no-replay are
// structural facts. Identity is index-based (Principle 2): CoreValueId /
// CoreExprId / CoreCapabilityId / CoreStateId / field & variant indices. The
// pass is fail-closed: unresolved callees and effectful-unsupported shapes emit
// a structured diagnostic (never a throw, a silent drop, or an Unknown node).

#include "ahfl/compiler/ir/core_ir.hpp"

#include "ahfl/base/support/overloaded.hpp"
#include "ahfl/compiler/ir/core_recursion.hpp"
#include "ahfl/compiler/ir/core_verify.hpp"
#include "ahfl/compiler/ir/expr_child_edges.hpp"
#include "ahfl/compiler/ir/mangling.hpp" // mangle_instance (fn-instance guarantee)

#include <cctype>
#include <algorithm>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace ahfl::ir::core {

// ---------------------------------------------------------------------------
// Shared program-global value-type hash-cons (RFC 0026 P9 §6). Declared in
// core_ir.hpp so the AHFL-IR lowerer and the Core JSON reader share ONE
// structural-interning implementation — the reader must rebuild the arena by
// interning (never trust serialized ids), and a second interner would be a
// second notion of structural equality (CLAUDE.md: no parallel SSOT copies).
// ---------------------------------------------------------------------------
std::size_t CoreValueTypeNodeHash::operator()(const CoreValueType &vt) const noexcept {
    std::size_t h = vt.node.index();
    const auto mix = [&h](std::size_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
    const auto hh_int = [&](const CoreVtInt &n) {
        if (n.bounds) {
            mix(static_cast<std::size_t>(n.bounds->first));
            mix(static_cast<std::size_t>(n.bounds->second));
        }
    };
    const auto hh_string = [&](const CoreVtString &n) {
        if (n.length_bounds) {
            mix(static_cast<std::size_t>(n.length_bounds->first));
            mix(static_cast<std::size_t>(n.length_bounds->second));
        }
    };
    const auto hh_decimal = [&](const CoreVtDecimal &n) { mix(static_cast<std::size_t>(n.scale)); };
    const auto hh_nominal = [&](const CoreVtNominal &n) {
        mix(n.base.value);
        for (const auto &a : n.args) {
            mix(a.value);
        }
        if (n.capacity) {
            mix(static_cast<std::size_t>(*n.capacity));
        }
    };
    const auto hh_tuple = [&](const CoreVtTuple &n) {
        for (const auto &e : n.elements) {
            mix(e.value);
        }
    };
    const auto hh_fn = [&](const CoreVtFn &n) {
        for (const auto &p : n.params) {
            mix(p.value);
        }
        mix(n.ret.value);
    };
    const auto hh_closure = [&](const CoreVtClosure &n) {
        mix(n.signature.value);
        for (const auto &c : n.captures) {
            mix(c.value_type.value);
            mix(static_cast<std::size_t>(c.mode));
        }
    };
    // RFC 0027 Q1 (KR6.13-X): one handler per value-type node, generated from
    // core_value_types.def. Leaf scalars with no identity payload share
    // LOWER_HASH_LEAF (a distinct typed no-op lambda per node); structural nodes
    // route to the explicit payload lambdas above. There is NO unnamed
    // catch-all, so a 15th node without a routing macro fails to compile.
#define LOWER_HASH_LEAF(Name, Wire) [](const CoreVt##Name &) {},
#define LOWER_HASH_Unit(Name, Wire) LOWER_HASH_LEAF(Name, Wire)
#define LOWER_HASH_Never(Name, Wire) LOWER_HASH_LEAF(Name, Wire)
#define LOWER_HASH_Bool(Name, Wire) LOWER_HASH_LEAF(Name, Wire)
#define LOWER_HASH_Int(Name, Wire) hh_int,
#define LOWER_HASH_Float(Name, Wire) LOWER_HASH_LEAF(Name, Wire)
#define LOWER_HASH_String(Name, Wire) hh_string,
#define LOWER_HASH_Decimal(Name, Wire) hh_decimal,
#define LOWER_HASH_Duration(Name, Wire) LOWER_HASH_LEAF(Name, Wire)
#define LOWER_HASH_Timestamp(Name, Wire) LOWER_HASH_LEAF(Name, Wire)
#define LOWER_HASH_Uuid(Name, Wire) LOWER_HASH_LEAF(Name, Wire)
#define LOWER_HASH_Nominal(Name, Wire) hh_nominal,
#define LOWER_HASH_Tuple(Name, Wire) hh_tuple,
#define LOWER_HASH_Fn(Name, Wire) hh_fn,
#define LOWER_HASH_Closure(Name, Wire) hh_closure,
#define HANDLE_CORE_VT(Name, Wire) LOWER_HASH_##Name(Name, Wire)
    std::visit(
        Overloaded{
#include "ahfl/compiler/ir/core_value_types.def"
        },
        vt.node);
#undef HANDLE_CORE_VT
#undef LOWER_HASH_Unit
#undef LOWER_HASH_Never
#undef LOWER_HASH_Bool
#undef LOWER_HASH_Int
#undef LOWER_HASH_Float
#undef LOWER_HASH_String
#undef LOWER_HASH_Decimal
#undef LOWER_HASH_Duration
#undef LOWER_HASH_Timestamp
#undef LOWER_HASH_Uuid
#undef LOWER_HASH_Nominal
#undef LOWER_HASH_Tuple
#undef LOWER_HASH_Fn
#undef LOWER_HASH_Closure
#undef LOWER_HASH_LEAF
    return h;
}

std::optional<CoreValueTypeId> CoreValueTypeArena::intern(CoreValueTypeNode node,
                                                          std::string *error_reason) {
    CoreValueType vt{std::move(node)};
    if (const auto it = index_.find(vt); it != index_.end()) {
        return it->second;
    }
    if (store_.size() >= CoreValueTypeId::kInvalid) {
        if (error_reason != nullptr && error_reason->empty()) {
            *error_reason = "value-type arena exceeded its 32-bit id space";
        }
        return std::nullopt;
    }
    const auto id = CoreValueTypeId{static_cast<std::uint32_t>(store_.size())};
    index_.emplace(vt, id);
    store_.push_back(std::move(vt));
    return id;
}

// Out-of-line structural equality for the recursive body types (used by tests).
bool operator==(const CoreIfStmt &a, const CoreIfStmt &b) noexcept {
    if (!(a.condition == b.condition)) {
        return false;
    }
    const auto region_eq = [](const std::unique_ptr<CoreRegion> &x,
                              const std::unique_ptr<CoreRegion> &y) {
        if (!x || !y) {
            return x.get() == y.get();
        }
        return *x == *y;
    };
    return region_eq(a.then_region, b.then_region) &&
           region_eq(a.else_region, b.else_region);
}

bool operator==(const CoreStmt &a, const CoreStmt &b) noexcept {
    return a.node == b.node && a.source_range == b.source_range;
}

bool operator==(const CoreMatchArm &a, const CoreMatchArm &b) noexcept {
    const auto region_eq = [](const std::unique_ptr<CoreRegion> &x,
                              const std::unique_ptr<CoreRegion> &y) {
        if (!x || !y) {
            return x.get() == y.get();
        }
        return *x == *y;
    };
    return a.pattern == b.pattern && a.bindings == b.bindings &&
           region_eq(a.guard_region, b.guard_region) && region_eq(a.body, b.body);
}

bool operator==(const CoreMatchStmt &a, const CoreMatchStmt &b) noexcept {
    const auto region_eq = [](const std::unique_ptr<CoreRegion> &x,
                              const std::unique_ptr<CoreRegion> &y) {
        if (!x || !y) {
            return x.get() == y.get();
        }
        return *x == *y;
    };
    return a.scrutinee == b.scrutinee && a.has_result == b.has_result && a.result == b.result &&
           a.arms == b.arms && region_eq(a.fallback_region, b.fallback_region);
}

bool operator==(const CoreRegion &a, const CoreRegion &b) noexcept {
    return a.statements == b.statements;
}

bool operator==(const CoreFlowState &a, const CoreFlowState &b) noexcept {
    return a.state == b.state && a.state_name == b.state_name &&
           a.policy == b.policy && a.body == b.body;
}

namespace {

[[nodiscard]] bool symbol_ref_equal(const SymbolRef &a, const SymbolRef &b) {
    return a.kind == b.kind && a.canonical_name == b.canonical_name &&
           a.local_name == b.local_name && a.module_name == b.module_name &&
           a.id == b.id;
}

} // namespace

bool operator==(const CoreFlowDecl &a, const CoreFlowDecl &b) noexcept {
    return a.target == b.target && a.agent_name == b.agent_name &&
           symbol_ref_equal(a.target_ref, b.target_ref) && a.storage == b.storage &&
           a.states == b.states;
}

namespace {
[[nodiscard]] bool region_ptr_eq(const std::unique_ptr<CoreRegion> &x,
                                  const std::unique_ptr<CoreRegion> &y) {
    if (!x || !y) {
        return x.get() == y.get();
    }
    return *x == *y;
}
} // namespace

bool operator==(const CoreWorkflowNode &a, const CoreWorkflowNode &b) noexcept {
    return a.id == b.id && a.target_instance == b.target_instance && a.node_name == b.node_name &&
           symbol_ref_equal(a.target_ref, b.target_ref) && a.after == b.after &&
           region_ptr_eq(a.input_region, b.input_region);
}

bool operator==(const CoreWorkflowDecl &a, const CoreWorkflowDecl &b) noexcept {
    return a.id == b.id && a.name == b.name && symbol_ref_equal(a.symbol_ref, b.symbol_ref) &&
           a.input_type == b.input_type && a.output_type == b.output_type &&
           a.storage == b.storage && a.nodes == b.nodes &&
           region_ptr_eq(a.return_region, b.return_region);
}

bool operator==(const CoreFnDecl &a, const CoreFnDecl &b) noexcept {
    return a.id == b.id && a.instance == b.instance &&
           symbol_ref_equal(a.origin, b.origin) && a.params == b.params &&
           a.captures == b.captures && a.env_bindings == b.env_bindings &&
           a.storage == b.storage && a.body == b.body && a.name == b.name;
}

bool operator==(const CoreInstanceDecl &a, const CoreInstanceDecl &b) noexcept {
    if (!(a.id == b.id) || a.instance_key != b.instance_key ||
        !symbol_ref_equal(a.origin, b.origin) || !(a.payload == b.payload)) {
        return false;
    }
    // SAME-OWNER-ARENA equality (RFC 0026 P4): dispatch_types are interned
    // CoreValueTypeIds, so a direct id-vector compare IS structural equality
    // within one program's value_types arena (index eq <=> structural eq). This
    // is NOT a cross-program comparison — see the header contract.
    return a.dispatch_types == b.dispatch_types;
}

// The SINGLE builtin nominal descriptor SSOT (RFC 0026 P4). Every well-known
// stdlib nominal generic is described here ONCE — canonical name, role, kind,
// arity, and (for enums) variant metadata. Everything else (the enum compat
// view, add_builtins registration, the semantics-layer container matcher's sync
// test) projects from or is checked against this table, so there is no
// "IR enum table + IR collection table + name matcher" triple truth.
//
// Variant ORDER for enums MUST MATCH the sysroot declaration order
// (std/option.ahfl: Some(T) then None; std/result.ahfl: Ok(T) then Err(E)),
// guarded by a sync test. Collection arities MUST MATCH std/collections.ahfl
// (List<T>=1, Set<T>=1, Map<K,V>=2), also sync-tested.
const std::vector<BuiltinNominalDescriptor> &builtin_nominal_table() {
    using PK = CoreTypeDecl::VariantPayload::Kind;
    using TK = CoreTypeDecl::Kind;
    using CV = CoreVariance;
    static const std::vector<BuiltinNominalDescriptor> table = {
        {"std::option::Option",
         "Option",
         TK::Enum,
         CoreNominalRole::Option,
         1,
         {{"Some", PK::Tuple, {0}}, {"None", PK::Unit, {}}},
         {CV::Covariant}},
        {"std::result::Result",
         "Result",
         TK::Enum,
         CoreNominalRole::Result,
         2,
         {{"Ok", PK::Tuple, {0}}, {"Err", PK::Tuple, {1}}},
         {CV::Covariant, CV::Covariant}},
        {"std::collections::List", "", TK::Struct, CoreNominalRole::List, 1, {}, {CV::Covariant}},
        {"std::collections::Set", "", TK::Struct, CoreNominalRole::Set, 1, {}, {CV::Covariant}},
        {"std::collections::Map",
         "",
         TK::Struct,
         CoreNominalRole::Map,
         2,
         {},
         {CV::Invariant, CV::Covariant}},
    };
    return table;
}

// COMPATIBILITY VIEW of the SSOT: the enum entries only, keyed by unqualified
// name, in declaration order. Preserved so the existing Option/Result sync test
// keeps working unchanged; it is projected from `builtin_nominal_table()`, not a
// second hand-written order.
const std::vector<BuiltinEnumDescriptor> &builtin_enum_table() {
    static const std::vector<BuiltinEnumDescriptor> table = [] {
        std::vector<BuiltinEnumDescriptor> out;
        for (const BuiltinNominalDescriptor &d : builtin_nominal_table()) {
            if (d.kind != CoreTypeDecl::Kind::Enum) {
                continue;
            }
            out.push_back(BuiltinEnumDescriptor{d.enum_view_name, d.variants});
        }
        return out;
    }();
    return table;
}

namespace {

// ---------------------------------------------------------------------------
// Type environment: name -> typed field / variant indices (Principle 2).
//
// P3 resolves names to typed indices so P4 (physical layout) never re-queries
// AHFL-IR / the type environment. Struct fields and enum variants are keyed by
// DECLARATION-ORDER index. Well-known stdlib nominal generics (Option/Result/
// List/Set/Map), whose declaration lives in the sysroot and may not be inlined in
// the user program, are registered from the builtin nominal descriptor SSOT
// (`builtin_nominal_table()`). Enum variant ORDER MUST MATCH the stdlib
// declaration order (std/option.ahfl: `Some(T)` then `None`), guarded by a sync
// test.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Type environment: nominal name/identity -> CoreTypeId + typed field/variant
// indices (Principle 2). Builds the CoreProgram::types table.
//
// Resolution is FULL-QUALIFIED by canonical name (or symbol id) — we do NOT
// index by unqualified name (which let a user `Option` hijack `std::option`),
// nor blindly strip module paths. Well-known stdlib enums are registered as
// synthetic types so a program using `std::option::Option::Some` resolves even
// when the sysroot EnumDecl is not inlined.
// ---------------------------------------------------------------------------
class ValueTypeArena;

class TypeEnv {
  public:
    explicit TypeEnv(std::vector<CoreTypeDecl> &types, std::vector<CoreLowerDiagnostic> &diags)
        : types_(types), diags_(diags) {}

    void add_struct(const StructDecl &decl) {
        CoreTypeDecl t;
        t.kind = CoreTypeDecl::Kind::Struct;
        t.name = decl.symbol_ref.canonical_name.empty() ? decl.name
                                                        : decl.symbol_ref.canonical_name;
        // RFC 0026 P4 (coercion): carry the declaration's generic arity + variance
        // (numeric metadata from the AHFL-IR bridge). register_type cross-checks /
        // stamps these against the builtin SSOT for a well-known std generic.
        t.type_param_count = decl.type_param_count;
        t.variances = ir_variances_to_core(decl.type_param_variances);
        t.source_range = decl.provenance.source_range;
        std::vector<std::string> field_type_names;
        for (const FieldDecl &f : decl.fields) {
            t.fields.push_back(f.name);
            t.field_nominal_types.push_back(
                CoreTypeId{}); // resolved in fixup_field_nominal_types()
            // A field with an initializer is optional in a struct literal; one
            // without is REQUIRED (the verifier proves completeness).
            t.field_has_default.push_back(f.default_value.ptr != nullptr);
            field_type_names.push_back(nominal_type_name(f.type_ref));
        }
        const auto id = register_type(std::move(t), decl.symbol_ref,
                                      RegistrationOrigin::RealIrDecl,
                                      decl.provenance.source_range);
        pending_field_type_names_.emplace(id.value, std::move(field_type_names));
        pending_member_templates_.push_back(
            PendingMemberTemplateDecl{id, &decl, nullptr, decl.provenance.source_range});
    }
    void add_enum(const EnumDecl &decl) {
        CoreTypeDecl t;
        t.kind = CoreTypeDecl::Kind::Enum;
        t.name = decl.symbol_ref.canonical_name.empty() ? decl.name
                                                       : decl.symbol_ref.canonical_name;
        // RFC 0026 P4 (coercion): see add_struct.
        t.type_param_count = decl.type_param_count;
        t.variances = ir_variances_to_core(decl.type_param_variances);
        t.source_range = decl.provenance.source_range;
        // Per-variant payload metadata. Complete logical slot types are
        // finalized from the declaration-owned template arena after every
        // nominal has a CoreTypeId.
        for (const EnumVariantDecl &v : decl.variants) {
            t.variants.push_back(v.name);
            CoreTypeDecl::VariantPayload payload;
            switch (v.payload_kind) {
            case EnumVariantPayloadKind::Unit:
                payload.kind = CoreTypeDecl::VariantPayload::Kind::Unit;
                break;
            case EnumVariantPayloadKind::Tuple:
                payload.kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
                break;
            case EnumVariantPayloadKind::Struct:
                payload.kind = CoreTypeDecl::VariantPayload::Kind::Struct;
                for (const EnumVariantFieldDecl &field : v.fields) {
                    payload.field_names.push_back(field.name);
                }
                break;
            }
            t.variant_payloads.push_back(std::move(payload));
        }
        const auto id = register_type(std::move(t), decl.symbol_ref,
                                      RegistrationOrigin::RealIrDecl,
                                      decl.provenance.source_range);
        pending_member_templates_.push_back(
            PendingMemberTemplateDecl{id, nullptr, &decl, decl.provenance.source_range});
    }

    /// Register the well-known stdlib nominal generics as synthetic types (only
    /// if a user declaration has not already claimed the canonical name), from the
    /// single builtin nominal descriptor SSOT. Enums carry their variant metadata
    /// (so std Option/Result construct/pattern arity is not a verifier blind
    /// spot); collection structs (List/Set/Map) carry no fields but DO carry their
    /// role + generic arity so a `CoreVtNominal` over them can be arity-checked and
    /// capacity-validated. Generic slot/field types are unknown here (type
    /// parameters), so no slot CoreTypeIds are populated.
    void add_builtins() {
        for (const BuiltinNominalDescriptor &b : builtin_nominal_table()) {
            const std::string canonical(b.canonical_name);
            if (by_name_.count(canonical) != 0) {
                continue; // a real declaration wins
            }
            CoreTypeDecl t;
            t.kind = b.kind;
            t.name = canonical;
            for (const BuiltinEnumDescriptor::Variant &v : b.variants) {
                t.variants.emplace_back(v.name);
                CoreTypeDecl::VariantPayload payload;
                payload.kind = v.payload_kind;
                for (const std::uint32_t param : v.payload_type_params) {
                    const auto node_id = CoreMemberTypeTemplateNodeId{
                        static_cast<std::uint32_t>(t.member_type_templates.size())};
                    CoreMemberTypeTemplateNode node;
                    node.kind = CoreMemberTypeTemplateKind::Param;
                    node.param_index = param;
                    t.member_type_templates.push_back(std::move(node));
                    payload.slot_type_template_roots.push_back(node_id);
                }
                t.variant_payloads.push_back(std::move(payload));
            }
            // Synthetic std base: a NAME-ONLY resolved-Type ref (no symbol id; the
            // sysroot decl is not inlined). register_type stamps role + arity from
            // the same SSOT via decorate_from_builtin_ssot (SyntheticBuiltin origin).
            SymbolRef name_only;
            name_only.kind = SymbolRefKind::Type;
            name_only.canonical_name = canonical;
            (void)register_type(std::move(t), name_only,
                                RegistrationOrigin::SyntheticBuiltin);
        }
    }

    /// Resolve a nominal type by resolved symbol identity, then canonical name.
    [[nodiscard]] std::optional<CoreTypeId> resolve(const SymbolRef &ref) const {
        if (ref.id.has_value()) {
            if (const auto it = by_id_.find(*ref.id); it != by_id_.end()) {
                return it->second;
            }
        }
        if (!ref.canonical_name.empty()) {
            if (const auto it = by_name_.find(ref.canonical_name); it != by_name_.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CoreTypeId> resolve_by_name(const std::string &canonical) const {
        if (const auto it = by_name_.find(canonical); it != by_name_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    /// The nominal ROLE of a declaration id, or `Ordinary` when the id is out of
    /// range. The bounded-collection predicate reads this (never a name), so a
    /// user nominal named `List` is not mistaken for the stdlib collection.
    [[nodiscard]] CoreNominalRole role_of(CoreTypeId id) const {
        if (id.value >= types_.size()) {
            return CoreNominalRole::Ordinary;
        }
        return types_[id.value].role;
    }

    /// Resolve a struct/enum TypeRef to its CoreTypeId (nullopt for primitive /
    /// collection / unresolved types, which do not participate in projection).
    [[nodiscard]] std::optional<CoreTypeId> type_id_of(const TypeRef &type) const {
        const std::string name = nominal_type_name(type);
        return name.empty() ? std::nullopt : resolve_by_name(name);
    }

    /// Variant index within a resolved enum type.
    [[nodiscard]] std::optional<std::uint32_t> variant_index(CoreTypeId type,
                                                             const std::string &variant) const {
        if (type.value >= types_.size()) {
            return std::nullopt;
        }
        const auto &names = types_[type.value].variants;
        for (std::uint32_t i = 0; i < names.size(); ++i) {
            if (names[i] == variant) {
                return i;
            }
        }
        return std::nullopt;
    }

    /// Struct field index within a resolved struct type.
    [[nodiscard]] std::optional<std::uint32_t> field_index(CoreTypeId type,
                                                           const std::string &field) const {
        if (type.value >= types_.size()) {
            return std::nullopt;
        }
        const auto &names = types_[type.value].fields;
        for (std::uint32_t i = 0; i < names.size(); ++i) {
            if (names[i] == field) {
                return i;
            }
        }
        return std::nullopt;
    }

    /// Payload kind of a variant within an enum type (nullopt if OOR).
    [[nodiscard]] std::optional<CoreTypeDecl::VariantPayload::Kind>
    variant_payload_kind(CoreTypeId type, std::uint32_t variant) const {
        if (type.value >= types_.size() || variant >= types_[type.value].variant_payloads.size()) {
            return std::nullopt;
        }
        return types_[type.value].variant_payloads[variant].kind;
    }

    /// Number of payload slots of a variant (0 if OOR / no metadata).
    [[nodiscard]] std::uint32_t variant_payload_arity(CoreTypeId type, std::uint32_t variant) const {
        if (type.value >= types_.size() || variant >= types_[type.value].variant_payloads.size()) {
            return 0;
        }
        return static_cast<std::uint32_t>(
            types_[type.value].variant_payloads[variant].slot_type_template_roots.size());
    }

    /// Declaration slot index of a NAMED field in a struct-payload variant, by
    /// name (nullopt if OOR or no such field). Lets a struct-payload literal
    /// resolve `Open { owner: ... }` to the declared slot, not source order.
    [[nodiscard]] std::optional<std::uint32_t>
    variant_field_slot(CoreTypeId type, std::uint32_t variant, const std::string &field) const {
        if (type.value >= types_.size() || variant >= types_[type.value].variant_payloads.size()) {
            return std::nullopt;
        }
        const auto &names = types_[type.value].variant_payloads[variant].field_names;
        for (std::uint32_t i = 0; i < names.size(); ++i) {
            if (names[i] == field) {
                return i;
            }
        }
        return std::nullopt;
    }

    /// Result of advancing one member step through a struct type.
    struct FieldStep {
        CoreFieldId field{};                 // typed field id within the owning struct
        std::optional<CoreTypeId> next_type; // owning type of the field, if it is a struct
    };

    /// Resolve a member step: `type` must be a struct that declares `field`.
    /// Returns the field's typed id plus the field's own CoreTypeId when the
    /// field is itself a (resolvable) struct, so a chain `a.b.c` can continue.
    /// nullopt when `type` is not a struct or has no such field (fail-closed).
    [[nodiscard]] std::optional<FieldStep> field_step(CoreTypeId type,
                                                      const std::string &field) const {
        if (type.value >= types_.size() ||
            types_[type.value].kind != CoreTypeDecl::Kind::Struct) {
            return std::nullopt;
        }
        const auto &decl = types_[type.value];
        for (std::uint32_t i = 0; i < decl.fields.size(); ++i) {
            if (decl.fields[i] == field) {
                FieldStep step;
                step.field = CoreFieldId{i};
                // A valid field_nominal_types entry (non-kInvalid) is the field's own
                // struct type; invalid means primitive/collection/P4 (no further
                // projection possible).
                if (i < decl.field_nominal_types.size() &&
                    decl.field_nominal_types[i].value != CoreTypeId::kInvalid) {
                    step.next_type = decl.field_nominal_types[i];
                }
                return step;
            }
        }
        return std::nullopt;
    }

    /// Second pass: resolve each struct field's recorded type NAME to a typed
    /// CoreTypeId, so no canonical-name strings survive for a backend. Run once
    /// after ALL types are registered (forward references resolve).
    void fixup_field_nominal_types() {
        for (const auto &[type_index, names] : pending_field_type_names_) {
            CoreTypeDecl &decl = types_[type_index];
            for (std::size_t i = 0; i < names.size() && i < decl.field_nominal_types.size(); ++i) {
                if (!names[i].empty()) {
                    if (const auto id = resolve_by_name(names[i])) {
                        decl.field_nominal_types[i] = *id;
                    }
                }
            }
        }
        pending_field_type_names_.clear();
    }

    void finalize_member_templates(ValueTypeArena &arena);

  private:
    struct PendingMemberTemplateDecl {
        CoreTypeId id{};
        const StructDecl *struct_decl{nullptr};
        const EnumDecl *enum_decl{nullptr};
        SourceRangeOpt source_range;
    };
    // Where a CoreTypeDecl entered `register_type`. This is an EXPLICIT provenance
    // flag, never inferred from the decl's metadata shape (Codex P0-1): a real std
    // decl that happens to arrive with `type_param_count == 0 && variances.empty()`
    // — e.g. a legacy / deserialized / hand-built AHFL-IR StructDecl that predates
    // the variance fields — must NOT be silently mistaken for a synthetic base and
    // stamped past the drift gate. Only `add_builtins` (which fabricates the base
    // from the descriptor SSOT) is SyntheticBuiltin; every real AHFL-IR decl is
    // RealIrDecl and is cross-checked exactly.
    enum class RegistrationOrigin { RealIrDecl, SyntheticBuiltin };

    [[nodiscard]] CoreTypeId register_type(CoreTypeDecl t, const SymbolRef &ref,
                                           RegistrationOrigin origin,
                                           SourceRangeOpt source_range = std::nullopt) {
        const auto id = CoreTypeId{static_cast<std::uint32_t>(types_.size())};
        if (ref.id.has_value()) {
            by_id_.emplace(*ref.id, id);
        }
        if (!t.name.empty()) {
            by_name_.emplace(t.name, id);
        }
        t.symbol_ref = ref; // Principle 2: persist resolved-symbol provenance.
        // Decorate role + generic arity + variance from the single builtin nominal
        // SSOT. Three-way (Codex P0-1): a SYNTHETIC std base (add_builtins, carries
        // no real decl) is stamped from the descriptor; a REAL std decl
        // (include_stdlib / inlined / deserialized, RealIrDecl) is cross-checked
        // EXACTLY against the descriptor and fail-closed on drift; a user nominal
        // keeps its incoming count/variance verbatim.
        decorate_from_builtin_ssot(t, origin, source_range);
        types_.push_back(std::move(t));
        return id;
    }

    // If `t.name` matches a builtin nominal descriptor, reconcile its role /
    // arity / variance against that single SSOT, keyed on the EXPLICIT
    // RegistrationOrigin (Codex P0-1) — never on the decl's metadata shape.
    // A SyntheticBuiltin base is STAMPED from the descriptor; a RealIrDecl std
    // decl must MATCH the descriptor exactly (arity AND variance) or is
    // fail-closed, including the case where it arrives missing both new fields
    // (count 0 / empty variance), which a shape-based guess would have mistaken
    // for a synthetic base. A non-std nominal matches no descriptor and keeps its
    // incoming (user) metadata.
    void decorate_from_builtin_ssot(CoreTypeDecl &t, RegistrationOrigin origin,
                                    SourceRangeOpt source_range) {
        for (const BuiltinNominalDescriptor &d : builtin_nominal_table()) {
            if (t.name != d.canonical_name) {
                continue;
            }
            t.role = d.role;
            const auto descriptor_variances = core_variances_of(d);
            if (origin == RegistrationOrigin::SyntheticBuiltin) {
                // Synthetic base: stamp arity + variance from the descriptor.
                t.type_param_count = d.type_param_count;
                t.variances = descriptor_variances;
            } else if (t.type_param_count != d.type_param_count ||
                       t.variances != descriptor_variances) {
                // Real std decl whose metadata disagrees with the SSOT: fail-closed
                // (a stable core lowering diagnostic; the program is not executable).
                // Principle 5: carry the decl's own source range when it has one.
                diags_.push_back(CoreLowerDiagnostic{
                    CoreDiagnosticSeverity::Error, std::string(diag::kBuiltinMetadataDrift),
                    "builtin nominal '" + t.name +
                        "' declaration metadata (arity/variance) disagrees with the builtin "
                        "descriptor SSOT",
                    source_range});
                // Keep the authoritative descriptor values so downstream arity /
                // variance checks reflect the SSOT, not the corrupt decl.
                t.type_param_count = d.type_param_count;
                t.variances = descriptor_variances;
            }
            return;
        }
    }

    // The descriptor's per-parameter variance as CoreVariance (SSOT -> Core).
    [[nodiscard]] static std::vector<CoreVariance>
    core_variances_of(const BuiltinNominalDescriptor &d) {
        return d.variances;
    }

    // Map AHFL-IR declaration variance to Core variance (identity mapping across
    // the two mirror enums).
    [[nodiscard]] static std::vector<CoreVariance>
    ir_variances_to_core(const std::vector<ir::Variance> &variances) {
        std::vector<CoreVariance> out;
        out.reserve(variances.size());
        for (const auto v : variances) {
            switch (v) {
            case ir::Variance::Invariant:
                out.push_back(CoreVariance::Invariant);
                break;
            case ir::Variance::Covariant:
                out.push_back(CoreVariance::Covariant);
                break;
            case ir::Variance::Contravariant:
                out.push_back(CoreVariance::Contravariant);
                break;
            }
        }
        return out;
    }
    /// Canonical nominal name of a struct/enum TypeRef, else empty (primitive,
    /// collection, fn, unresolved — none of which support field projection here).
    [[nodiscard]] static std::string nominal_type_name(const TypeRef &type) {
        if (type.kind == TypeRefKind::Struct || type.kind == TypeRefKind::Enum) {
            return type.canonical_name.empty() ? type.display_name : type.canonical_name;
        }
        return {};
    }
    std::vector<CoreTypeDecl> &types_;
    std::vector<CoreLowerDiagnostic> &diags_;
    std::unordered_map<std::size_t, CoreTypeId> by_id_;
    std::unordered_map<std::string, CoreTypeId> by_name_;
    // struct CoreTypeId -> its field type NAMES, pending resolution to typed
    // navigation CoreTypeIds after all types are registered.
    std::unordered_map<std::uint32_t, std::vector<std::string>> pending_field_type_names_;
    std::vector<PendingMemberTemplateDecl> pending_member_templates_;
};

// ---------------------------------------------------------------------------
// The SINGLE nominal resolver used by BOTH the production dispatch path and the
// public `lower_value_type_into` (Codex P0-3). STRICT id-first identity:
//   * ref has an id AND a Core type carries that same id -> use it, and require
//     the canonical names agree (a matching id with a drifting canonical is a
//     corrupt identity, fail-closed);
//   * ref has an id but NO Core type has it -> canonical fallback is allowed ONLY
//     to a name-only synthetic base (a CoreTypeDecl whose own symbol_ref has no
//     id, e.g. an un-inlined std base). A canonical candidate that DOES carry a
//     (necessarily different) id is fail-closed — a wrong id must never be
//     silently downgraded to a spelling match;
//   * ref has no id -> plain canonical name-only fallback.
// Returns nullopt on no match OR on a fail-closed contradiction.
[[nodiscard]] inline std::optional<CoreTypeId>
resolve_nominal_strict(const std::vector<CoreTypeDecl> &types, const SymbolRef &ref) {
    if (ref.id.has_value()) {
        for (std::uint32_t i = 0; i < types.size(); ++i) {
            const auto &sym = types[i].symbol_ref;
            if (sym.id.has_value() && *sym.id == *ref.id) {
                // Matching id: canonical must agree (both empty is fine).
                if (!ref.canonical_name.empty() && !types[i].name.empty() &&
                    ref.canonical_name != types[i].name) {
                    return std::nullopt; // id/canonical drift -> fail-closed
                }
                return CoreTypeId{i};
            }
        }
        // Present id with no id-match: only a name-only synthetic base may be
        // reached by canonical, and only if the candidate itself carries no id.
        if (!ref.canonical_name.empty()) {
            for (std::uint32_t i = 0; i < types.size(); ++i) {
                if (types[i].name != ref.canonical_name) {
                    continue;
                }
                if (types[i].symbol_ref.id.has_value()) {
                    return std::nullopt; // candidate has a DIFFERENT id -> reject
                }
                return CoreTypeId{i}; // name-only synthetic base
            }
        }
        return std::nullopt;
    }
    if (!ref.canonical_name.empty()) {
        for (std::uint32_t i = 0; i < types.size(); ++i) {
            if (types[i].name == ref.canonical_name) {
                return CoreTypeId{i};
            }
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Logical value-type arena (RFC 0026 P4). A TRUE hash-cons: `intern` maps a
// structural key to a `CoreValueTypeId`, children are interned FIRST so a node's
// key contains only child ids, and storage is deterministic append-only (so two
// lowerings of the same input produce the same arena order — required for the
// same-owner-arena `CoreInstanceDecl` equality, Codex ruling a). `lower_value_type`
// converts an `ir::TypeRef` to an interned id and is ALSO a fail-closed shape gate
// (Codex invariant 2): it does not assume any prior verifier ran.
// ---------------------------------------------------------------------------
class ValueTypeArena {
  public:
    using NominalResolver = std::function<std::optional<CoreTypeId>(const SymbolRef &)>;

    ValueTypeArena(std::vector<CoreValueType> &store, const std::vector<CoreTypeDecl> &types,
                   NominalResolver resolve)
        : store_(store), types_(types), resolve_(std::move(resolve)), interner_(store) {}

    // Lower an ir::TypeRef into an interned CoreValueTypeId. Returns nullopt (a
    // fail-closed error) on any non-materializable input: Unresolved/Any/Never
    // TypeRefKind, a malformed structural shape, or an unresolved nominal. The
    // caller emits the diagnostic (this keeps the arena free of the diagnostic
    // sink); `*error_reason` is set to a human-readable cause on failure.
    [[nodiscard]] std::optional<CoreValueTypeId> lower(const TypeRef &type,
                                                       std::string *error_reason) {
        const auto fail = [&](std::string reason) -> std::optional<CoreValueTypeId> {
            return set_reason(error_reason, std::move(reason));
        };
        switch (type.kind) {
        case TypeRefKind::Unresolved:
            return fail("unresolved type");
        case TypeRefKind::Any:
            return fail("`Any` cannot be a materialized value type");
        case TypeRefKind::Never:
            // Legal as an arena node in general, but NOT as a materialized value /
            // dispatch type — this consumer (dispatch descriptors) rejects it
            // (Codex invariant 1). lower_value_type in this consumer fails closed.
            return fail("`Never` cannot be a materialized value type");
        case TypeRefKind::Unit:
            return lower_atom(type, CoreVtUnit{}, error_reason);
        case TypeRefKind::Bool:
            return lower_atom(type, CoreVtBool{}, error_reason);
        case TypeRefKind::Float:
            return lower_atom(type, CoreVtFloat{}, error_reason);
        case TypeRefKind::UUID:
            return lower_atom(type, CoreVtUuid{}, error_reason);
        case TypeRefKind::Timestamp:
            return lower_atom(type, CoreVtTimestamp{}, error_reason);
        case TypeRefKind::Duration:
            return lower_atom(type, CoreVtDuration{}, error_reason);
        case TypeRefKind::Int:
            if (!fields_within(type, FieldMask{}, error_reason)) {
                return std::nullopt;
            }
            return intern(CoreVtInt{}, error_reason);
        case TypeRefKind::BoundedInt: {
            if (!fields_within(type, FieldMask{.int_bounds = true}, error_reason)) {
                return std::nullopt;
            }
            if (!type.int_bounds.has_value()) {
                return fail("`BoundedInt` is missing its bounds");
            }
            if (type.int_bounds->first > type.int_bounds->second) {
                return fail("`BoundedInt` has min > max");
            }
            return intern(CoreVtInt{type.int_bounds}, error_reason);
        }
        case TypeRefKind::String:
            if (!fields_within(type, FieldMask{}, error_reason)) {
                return std::nullopt;
            }
            return intern(CoreVtString{}, error_reason);
        case TypeRefKind::BoundedString: {
            if (!fields_within(type, FieldMask{.string_bounds = true}, error_reason)) {
                return std::nullopt;
            }
            if (!type.string_bounds.has_value()) {
                return fail("`BoundedString` is missing its length bounds");
            }
            const auto [lo, hi] = *type.string_bounds;
            if (lo < 0 || hi < 0 || lo > hi) {
                return fail("`BoundedString` has a negative or reversed length bound");
            }
            return intern(CoreVtString{type.string_bounds}, error_reason);
        }
        case TypeRefKind::Decimal:
            if (!fields_within(type, FieldMask{.decimal_scale = true}, error_reason)) {
                return std::nullopt;
            }
            if (!type.decimal_scale.has_value()) {
                return fail("`Decimal` is missing its scale");
            }
            return intern(CoreVtDecimal{*type.decimal_scale}, error_reason);
        case TypeRefKind::Fn:
            return lower_fn(type, error_reason);
        case TypeRefKind::Struct:
        case TypeRefKind::Enum:
            return lower_nominal(type, error_reason);
        }
        return fail("unhandled type kind");
    }

    [[nodiscard]] std::optional<CoreValueTypeId>
    materialize_nominal(CoreTypeId base,
                        std::vector<CoreValueTypeId> args,
                        std::optional<std::uint64_t> capacity,
                        std::string *error_reason) {
        if (base.value >= types_.size()) {
            return set_reason(error_reason, "member template nominal base is out of range");
        }
        const CoreTypeDecl &decl = types_[base.value];
        if (args.size() != decl.type_param_count) {
            return set_reason(error_reason,
                              "member template nominal '" + decl.name + "' expects " +
                                  std::to_string(decl.type_param_count) + " argument(s), got " +
                                  std::to_string(args.size()));
        }
        if (!valid_materialized_children(args, error_reason)) {
            return std::nullopt;
        }
        if (capacity.has_value() && !capacity_allowed(decl.role)) {
            return set_reason(error_reason,
                              "member template nominal '" + decl.name +
                                  "' cannot carry a collection capacity");
        }
        return intern(CoreVtNominal{base, std::move(args), capacity}, error_reason);
    }

    [[nodiscard]] std::optional<CoreValueTypeId> materialize_fn(std::vector<CoreValueTypeId> params,
                                                                CoreValueTypeId ret,
                                                                std::string *error_reason) {
        if (!valid_materialized_children(params, error_reason) ||
            !valid_materialized_id(ret, error_reason)) {
            return std::nullopt;
        }
        return intern(CoreVtFn{std::move(params), ret}, error_reason);
    }

    // Intern a fully-built structural value-type node from OUTSIDE a TypeRef
    // lowering (FB-3a2 uses it to mint the concrete CoreVtFn signature and the
    // CoreVtClosure construction type of a lifted lambda). Children must be
    // interned already; routes through the same shared hash-cons as everything
    // else (one interning decision point).
    [[nodiscard]] std::optional<CoreValueTypeId>
    intern_structural(CoreValueTypeNode node, std::string *error_reason) {
        return intern(std::move(node), error_reason);
    }

    [[nodiscard]] bool valid_materialized_id(CoreValueTypeId id, std::string *error_reason) const {
        if (id.value >= store_.size()) {
            set_reason(error_reason, "member template references an out-of-range value type");
            return false;
        }
        if (std::holds_alternative<CoreVtNever>(store_[id.value].node)) {
            set_reason(error_reason, "member template cannot materialize `Never`");
            return false;
        }
        return true;
    }

  private:
    [[nodiscard]] bool valid_materialized_children(const std::vector<CoreValueTypeId> &ids,
                                                   std::string *error_reason) const {
        return std::all_of(ids.begin(), ids.end(), [&](CoreValueTypeId id) {
            return valid_materialized_id(id, error_reason);
        });
    }
    // Intern a fully-built node (children already interned) through the SHARED
    // program-global hash-cons (CoreValueTypeArena, declared in core_ir.hpp).
    // Deterministic append-only storage + structural dedup (Principle 3); the
    // ONE interning decision point shared with the Core JSON reader (RFC 0026 P9
    // §6.2). Fails closed if the arena would exceed the CoreValueTypeId space.
    [[nodiscard]] std::optional<CoreValueTypeId> intern(CoreValueTypeNode node,
                                                        std::string *error_reason) {
        return interner_.intern(std::move(node), error_reason);
    }

    [[nodiscard]] static bool has_children(const TypeRef &type) {
        return type.first != nullptr || type.second != nullptr || !type.params.empty();
    }

    static std::optional<CoreValueTypeId> set_reason(std::string *error_reason,
                                                     std::string reason) {
        if (error_reason != nullptr && error_reason->empty()) {
            *error_reason = std::move(reason);
        }
        return std::nullopt;
    }

    // Which OPTIONAL/structural fields a given TypeRef kind is allowed to carry.
    // Every field not named here must be empty, or the input is a malformed
    // TypeRef and lowering fails closed (Codex P0-1: no silent field drop, no
    // fail-open normalization). `canonical_name`/`display_name` are display-only
    // and permitted on any kind (they never enter value-type identity); the
    // structural fields below are the ones that would silently vanish.
    struct FieldMask {
        bool int_bounds{false};
        bool string_bounds{false};
        bool decimal_scale{false};
        bool collection_capacity{false};
        bool nominal_ref{false};
        bool variant_name{false};
        bool first{false};
        bool second{false};
        bool params{false};
    };

    // Reject any structural field present but not allowed by `mask`. Returns true
    // if the shape is clean; on violation sets the reason and returns false.
    [[nodiscard]] bool fields_within(const TypeRef &type, const FieldMask &mask,
                                     std::string *error_reason) {
        const char *bad = nullptr;
        if (type.int_bounds.has_value() && !mask.int_bounds) {
            bad = "int_bounds";
        } else if (type.string_bounds.has_value() && !mask.string_bounds) {
            bad = "string_bounds";
        } else if (type.decimal_scale.has_value() && !mask.decimal_scale) {
            bad = "decimal_scale";
        } else if (type.collection_capacity.has_value() && !mask.collection_capacity) {
            bad = "collection_capacity";
        } else if (type.nominal_ref.kind != ir::SymbolRefKind::Unknown && !mask.nominal_ref) {
            bad = "nominal_ref";
        } else if (!type.variant_name.empty() && !mask.variant_name) {
            bad = "variant_name";
        } else if (type.first != nullptr && !mask.first) {
            bad = "first";
        } else if (type.second != nullptr && !mask.second) {
            bad = "second";
        } else if (!type.params.empty() && !mask.params) {
            bad = "params/type_args";
        }
        if (bad != nullptr) {
            set_reason(error_reason,
                       "type of kind " + std::to_string(static_cast<int>(type.kind)) +
                           " carries a stray '" + bad + "' field");
            return false;
        }
        return true;
    }

    // A bare atom (Unit/Bool/Float/UUID/Timestamp/Duration): no field at all.
    template <typename Node>
    [[nodiscard]] std::optional<CoreValueTypeId> lower_atom(const TypeRef &type, Node node,
                                                            std::string *error_reason) {
        if (!fields_within(type, FieldMask{}, error_reason)) {
            return std::nullopt;
        }
        return intern(std::move(node), error_reason);
    }

    // Fn: params are `params`, the RETURN is `first` (typed_hir_lower FnT
    // encoding SSOT), `second` must be empty. Effect is erased by construction.
    [[nodiscard]] std::optional<CoreValueTypeId> lower_fn(const TypeRef &type,
                                                          std::string *error_reason) {
        // Fn carries only its return (`first`) and parameters (`params`); any
        // refinement / nominal / variant field is a malformed shape.
        if (!fields_within(type, FieldMask{.first = true, .params = true}, error_reason)) {
            return std::nullopt;
        }
        if (type.first == nullptr) {
            return set_reason(error_reason, "`Fn` is missing its return type");
        }
        std::vector<CoreValueTypeId> params;
        params.reserve(type.params.size());
        for (const auto &p : type.params) {
            if (p == nullptr) {
                return set_reason(error_reason, "`Fn` has a null parameter type");
            }
            const auto id = lower(*p, error_reason);
            if (!id) {
                return std::nullopt;
            }
            params.push_back(*id);
        }
        const auto ret = lower(*type.first, error_reason);
        if (!ret) {
            return std::nullopt;
        }
        return intern(CoreVtFn{std::move(params), *ret}, error_reason);
    }

    // Struct/Enum -> CoreVtNominal. A nominal carries only its resolved
    // `nominal_ref`, generic args (`params`), an optional `collection_capacity`,
    // and — for the EnumVariant encoding — a `variant_name` that NORMALIZES to the
    // parent enum (the variant name does NOT enter value-type identity). Locks the
    // full nominal identity (Codex P0-1): nominal_ref.kind==Type; tri-canonical
    // agreement (TypeRef canonical == nominal_ref canonical == CoreTypeDecl.name);
    // and Struct<->Struct / Enum<->Enum tag consistency.
    [[nodiscard]] std::optional<CoreValueTypeId> lower_nominal(const TypeRef &type,
                                                               std::string *error_reason) {
        const bool is_variant_encoding =
            type.kind == TypeRefKind::Enum && !type.variant_name.empty();
        FieldMask mask{.collection_capacity = true, .nominal_ref = true, .params = true};
        mask.variant_name = is_variant_encoding; // only an Enum may carry a variant name
        if (!fields_within(type, mask, error_reason)) {
            return std::nullopt;
        }
        if (type.nominal_ref.kind != ir::SymbolRefKind::Type) {
            return set_reason(error_reason,
                              "nominal type's resolved identity is missing or not a Type symbol");
        }
        // Tri-canonical agreement (Codex P1-1): the display canonical, the
        // resolved-symbol canonical, and the resolved CoreTypeDecl name must ALL
        // be non-empty and equal — we do NOT bypass the check when a field is
        // empty (lower must not assume BackendReady ran first).
        if (type.canonical_name.empty() || type.nominal_ref.canonical_name.empty()) {
            return set_reason(error_reason,
                              "nominal type is missing a canonical name on the TypeRef or its "
                              "resolved identity");
        }
        if (type.canonical_name != type.nominal_ref.canonical_name) {
            return set_reason(error_reason, "nominal type canonical '" + type.canonical_name +
                                                "' disagrees with its resolved identity '" +
                                                type.nominal_ref.canonical_name + "'");
        }
        const auto base = resolve_(type.nominal_ref);
        if (!base) {
            return set_reason(error_reason, "nominal type '" + type.nominal_ref.canonical_name +
                                                "' does not resolve to a Core type");
        }
        if (base->value >= types_.size()) {
            return set_reason(error_reason, "resolved nominal base id is out of range");
        }
        const CoreTypeDecl &decl = types_[base->value];
        if (decl.name.empty() || type.nominal_ref.canonical_name != decl.name) {
            return set_reason(error_reason, "nominal identity '" + type.nominal_ref.canonical_name +
                                                "' resolves to a Core type named '" + decl.name +
                                                "'");
        }
        // Struct<->Struct / Enum<->Enum tag consistency (an EnumVariant encoding is
        // still an Enum tag). A Struct TypeRef resolving to an Enum decl (or vice
        // versa) is a malformed cross-kind reference.
        const bool tag_ok = decl.kind == CoreTypeDecl::Kind::Struct
                                ? type.kind == TypeRefKind::Struct
                                : type.kind == TypeRefKind::Enum;
        if (!tag_ok) {
            return set_reason(error_reason, "nominal type tag disagrees with the resolved Core "
                                            "type's Struct/Enum kind for '" +
                                                decl.name + "'");
        }
        // EnumVariant encoding: the variant name must name a real variant of the
        // resolved enum (a `Ghost` variant is a malformed reference, not silently
        // normalized to the parent enum).
        if (is_variant_encoding) {
            if (decl.kind != CoreTypeDecl::Kind::Enum) {
                return set_reason(error_reason,
                                  "variant-qualified nominal '" + decl.name + "' is not an enum");
            }
            const bool variant_found =
                std::find(decl.variants.begin(), decl.variants.end(), type.variant_name) !=
                decl.variants.end();
            if (!variant_found) {
                return set_reason(error_reason, "enum '" + decl.name +
                                                    "' has no variant named '" + type.variant_name +
                                                    "'");
            }
        }
        std::vector<CoreValueTypeId> args;
        args.reserve(type.params.size());
        for (const auto &p : type.params) {
            if (p == nullptr) {
                return set_reason(error_reason, "nominal type has a null type argument");
            }
            const auto id = lower(*p, error_reason);
            if (!id) {
                return std::nullopt;
            }
            args.push_back(*id);
        }
        if (args.size() != decl.type_param_count) {
            return set_reason(error_reason,
                              "nominal type '" + decl.name + "' expects " +
                                  std::to_string(decl.type_param_count) + " type argument(s), got " +
                                  std::to_string(args.size()));
        }
        std::optional<std::uint64_t> capacity = type.collection_capacity;
        if (capacity.has_value() && !capacity_allowed(decl.role)) {
            return set_reason(error_reason, "type '" + decl.name +
                                                "' is not a bounded collection and cannot carry a "
                                                "capacity");
        }
        return intern(CoreVtNominal{*base, std::move(args), capacity}, error_reason);
    }

    std::vector<CoreValueType> &store_;
    const std::vector<CoreTypeDecl> &types_;
    NominalResolver resolve_;
    CoreValueTypeArena interner_;
};

void TypeEnv::finalize_member_templates(ValueTypeArena &arena) {
    const auto builtin_descriptor = [](std::string_view name) -> const BuiltinNominalDescriptor * {
        for (const auto &d : builtin_nominal_table()) {
            if (d.canonical_name == name) {
                return &d;
            }
        }
        return nullptr;
    };

    for (const PendingMemberTemplateDecl &pending : pending_member_templates_) {
        if (pending.id.value >= types_.size()) {
            continue;
        }
        CoreTypeDecl &target = types_[pending.id.value];
        const std::vector<MemberTypeTemplateNode> *source_nodes = nullptr;
        std::vector<std::uint32_t> roots;
        std::vector<std::vector<std::uint32_t>> variant_roots;
        if (pending.struct_decl != nullptr) {
            source_nodes = &pending.struct_decl->member_type_templates;
            roots = pending.struct_decl->field_type_template_roots;
            if (roots.size() != pending.struct_decl->fields.size()) {
                source_nodes = nullptr;
            }
        } else if (pending.enum_decl != nullptr) {
            source_nodes = &pending.enum_decl->member_type_templates;
            variant_roots.reserve(pending.enum_decl->variants.size());
            if (pending.enum_decl->variants.size() != target.variant_payloads.size()) {
                source_nodes = nullptr;
            } else {
                for (const auto &variant : pending.enum_decl->variants) {
                    const std::size_t expected_roots =
                        variant.payload_kind == EnumVariantPayloadKind::Tuple
                            ? variant.payload.size()
                            : (variant.payload_kind == EnumVariantPayloadKind::Struct
                                   ? variant.fields.size()
                                   : 0);
                    if (variant.payload_type_template_roots.size() != expected_roots) {
                        source_nodes = nullptr;
                        break;
                    }
                    variant_roots.push_back(variant.payload_type_template_roots);
                }
            }
        }

        std::string reason;
        bool builtin_template_drift = false;
        std::vector<CoreMemberTypeTemplateNode> converted;
        const auto fail = [&](std::string message) {
            if (reason.empty()) {
                reason = std::move(message);
            }
        };
        if (source_nodes == nullptr) {
            fail("member template roots do not match declaration members");
        }

        std::vector<bool> reachable;
        if (source_nodes != nullptr) {
            reachable.assign(source_nodes->size(), false);
            const auto mark = [&](auto &&self, std::uint32_t id) -> bool {
                if (id >= source_nodes->size()) {
                    fail("member template root/child is out of range");
                    return false;
                }
                if (reachable[id]) {
                    return true;
                }
                reachable[id] = true;
                const auto &node = (*source_nodes)[id];
                for (const std::uint32_t child : node.children) {
                    if (child >= id) {
                        fail("member template children must precede their parent");
                        return false;
                    }
                    if (!self(self, child)) {
                        return false;
                    }
                }
                if (node.kind == MemberTypeTemplateKind::Fn) {
                    if (node.fn_return >= id || !self(self, node.fn_return)) {
                        fail("member template function return must precede its parent");
                        return false;
                    }
                }
                return true;
            };
            for (const std::uint32_t root : roots) {
                (void)mark(mark, root);
            }
            for (const auto &per_variant : variant_roots) {
                for (const std::uint32_t root : per_variant) {
                    (void)mark(mark, root);
                }
            }
            if (reason.empty() &&
                std::any_of(reachable.begin(), reachable.end(), [](bool used) { return !used; })) {
                fail("member template arena contains an orphan node");
            }
        }

        if (source_nodes != nullptr && reason.empty()) {
            converted.reserve(source_nodes->size());
            for (std::uint32_t index = 0; index < source_nodes->size(); ++index) {
                const MemberTypeTemplateNode &source = (*source_nodes)[index];
                CoreMemberTypeTemplateNode node;
                switch (source.kind) {
                case MemberTypeTemplateKind::Concrete: {
                    if (source.param_index != 0 || !source.children.empty() ||
                        source.fn_return != kInvalidMemberTypeTemplateNode) {
                        fail("Concrete member template carries fields for another kind");
                        break;
                    }
                    std::string lower_reason;
                    const auto concrete = arena.lower(source.type_ref, &lower_reason);
                    if (!concrete) {
                        fail("concrete member template is not materializable: " + lower_reason);
                    } else {
                        node.kind = CoreMemberTypeTemplateKind::Concrete;
                        node.concrete = *concrete;
                    }
                    break;
                }
                case MemberTypeTemplateKind::Param:
                    if (!type_refs_equal(source.type_ref, TypeRef{}) || !source.children.empty() ||
                        source.fn_return != kInvalidMemberTypeTemplateNode ||
                        source.param_index >= target.type_param_count) {
                        fail("member template parameter index is out of range");
                    } else {
                        node.kind = CoreMemberTypeTemplateKind::Param;
                        node.param_index = source.param_index;
                    }
                    break;
                case MemberTypeTemplateKind::Nominal: {
                    const TypeRef &type = source.type_ref;
                    if ((type.kind != TypeRefKind::Struct && type.kind != TypeRefKind::Enum) ||
                        source.param_index != 0 ||
                        source.fn_return != kInvalidMemberTypeTemplateNode ||
                        type.nominal_ref.kind != SymbolRefKind::Type ||
                        type.canonical_name.empty() ||
                        type.canonical_name != type.nominal_ref.canonical_name ||
                        type.int_bounds.has_value() || type.string_bounds.has_value() ||
                        type.decimal_scale.has_value() || !type.params.empty() ||
                        type.first != nullptr || type.second != nullptr ||
                        !type.variant_name.empty()) {
                        fail("nominal member template has a malformed base TypeRef");
                        break;
                    }
                    const auto base = resolve_nominal_strict(types_, type.nominal_ref);
                    if (!base || base->value >= types_.size()) {
                        fail("nominal member template base does not resolve");
                        break;
                    }
                    const CoreTypeDecl &base_decl = types_[base->value];
                    const bool kind_matches = (type.kind == TypeRefKind::Struct) ==
                                              (base_decl.kind == CoreTypeDecl::Kind::Struct);
                    if (!kind_matches || source.children.size() != base_decl.type_param_count ||
                        (type.collection_capacity.has_value() &&
                         !capacity_allowed(base_decl.role))) {
                        fail("nominal member template base kind/arity/capacity is invalid");
                        break;
                    }
                    node.kind = CoreMemberTypeTemplateKind::Nominal;
                    node.nominal = *base;
                    node.capacity = type.collection_capacity;
                    for (const std::uint32_t child : source.children) {
                        node.children.push_back(CoreMemberTypeTemplateNodeId{child});
                    }
                    break;
                }
                case MemberTypeTemplateKind::Fn:
                    if (!type_refs_equal(source.type_ref, TypeRef{}) || source.param_index != 0 ||
                        source.fn_return >= index) {
                        fail("function member template has an invalid return node");
                        break;
                    }
                    node.kind = CoreMemberTypeTemplateKind::Fn;
                    for (const std::uint32_t child : source.children) {
                        node.children.push_back(CoreMemberTypeTemplateNodeId{child});
                    }
                    node.fn_return = CoreMemberTypeTemplateNodeId{source.fn_return};
                    break;
                default:
                    fail("member template has an illegal node kind");
                    break;
                }
                if (!reason.empty()) {
                    break;
                }
                converted.push_back(std::move(node));
            }
        }

        std::vector<CoreMemberTypeTemplateNodeId> converted_roots;
        std::vector<std::vector<CoreMemberTypeTemplateNodeId>> converted_variant_roots;
        if (reason.empty()) {
            for (const auto root : roots) {
                converted_roots.push_back(CoreMemberTypeTemplateNodeId{root});
            }
            for (const auto &per_variant : variant_roots) {
                std::vector<CoreMemberTypeTemplateNodeId> out;
                for (const auto root : per_variant) {
                    out.push_back(CoreMemberTypeTemplateNodeId{root});
                }
                converted_variant_roots.push_back(std::move(out));
            }

            if (const auto *builtin = builtin_descriptor(target.name); builtin != nullptr) {
                if (target.kind != builtin->kind ||
                    target.variant_payloads.size() != builtin->variants.size()) {
                    builtin_template_drift = true;
                    fail("builtin member templates disagree with descriptor shape");
                } else if (builtin->kind == CoreTypeDecl::Kind::Struct &&
                           (!target.fields.empty() || !converted.empty() ||
                            !converted_roots.empty())) {
                    builtin_template_drift = true;
                    fail("builtin collection member templates disagree with descriptor shape");
                } else {
                    for (std::size_t v = 0; v < builtin->variants.size() && reason.empty(); ++v) {
                        const auto &expected = builtin->variants[v];
                        const auto &actual_roots = converted_variant_roots[v];
                        if (target.variants[v] != expected.name ||
                            target.variant_payloads[v].kind != expected.payload_kind ||
                            actual_roots.size() != expected.payload_type_params.size()) {
                            builtin_template_drift = true;
                            fail("builtin member templates disagree with descriptor variant");
                            break;
                        }
                        for (std::size_t slot = 0; slot < actual_roots.size(); ++slot) {
                            const auto root = actual_roots[slot];
                            if (root.value >= converted.size() ||
                                converted[root.value].kind != CoreMemberTypeTemplateKind::Param ||
                                converted[root.value].param_index !=
                                    expected.payload_type_params[slot]) {
                                builtin_template_drift = true;
                                fail(
                                    "builtin payload template disagrees with descriptor parameter");
                                break;
                            }
                        }
                    }
                }
            }
        }

        if (!reason.empty()) {
            builtin_template_drift =
                builtin_template_drift || builtin_descriptor(target.name) != nullptr;
            diags_.push_back(CoreLowerDiagnostic{
                CoreDiagnosticSeverity::Error,
                std::string(builtin_template_drift ? diag::kBuiltinMetadataDrift
                                                   : diag::kInvalidMemberTemplate),
                (builtin_template_drift ? "builtin nominal '" : "nominal '") + target.name +
                    (builtin_template_drift
                         ? "' member templates disagree with the builtin descriptor SSOT: "
                         : "' has an invalid member type template: ") +
                    reason,
                pending.source_range});
            continue;
        }

        // Atomic publication: no declaration-owned Core template arena/root is
        // visible until the complete declaration has converted successfully.
        target.member_type_templates = std::move(converted);
        target.field_type_template_roots = std::move(converted_roots);
        for (std::size_t v = 0; v < converted_variant_roots.size(); ++v) {
            target.variant_payloads[v].slot_type_template_roots =
                std::move(converted_variant_roots[v]);
        }
    }
    pending_member_templates_.clear();
}

// ---------------------------------------------------------------------------
// Capability index: canonical identity -> CoreCapabilityId + effect kind.
// ---------------------------------------------------------------------------
struct CapabilityInfo {
    CoreCapabilityId id{};
    CapabilityEffectKind effect_kind{CapabilityEffectKind::Unknown};
};

class CapabilityIndex {
  public:
    void add(const SymbolRef &ref, CoreCapabilityId id, CapabilityEffectKind kind) {
        if (ref.id.has_value()) {
            by_id_.emplace(*ref.id, CapabilityInfo{id, kind});
        }
        if (!ref.canonical_name.empty()) {
            by_name_.emplace(ref.canonical_name, CapabilityInfo{id, kind});
        }
    }
    [[nodiscard]] std::optional<CapabilityInfo> lookup(const SymbolRef &ref) const {
        if (ref.id.has_value()) {
            if (const auto it = by_id_.find(*ref.id); it != by_id_.end()) {
                return it->second;
            }
            // A present SymbolId is canonical. Never let a stale/corrupt id
            // silently downgrade to display spelling and bind another symbol.
            return std::nullopt;
        }
        if (!ref.canonical_name.empty()) {
            if (const auto it = by_name_.find(ref.canonical_name); it != by_name_.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }

  private:
    std::unordered_map<std::size_t, CapabilityInfo> by_id_;
    std::unordered_map<std::string, CapabilityInfo> by_name_;
};

// ---------------------------------------------------------------------------
// Agent state index: state name -> CoreStateId, for goto resolution.
// ---------------------------------------------------------------------------
class StateIndex {
  public:
    explicit StateIndex(const std::vector<std::string> &states) {
        for (std::uint32_t i = 0; i < states.size(); ++i) {
            by_name_.emplace(states[i], CoreStateId{i});
        }
    }
    [[nodiscard]] std::optional<CoreStateId> lookup(const std::string &name) const {
        if (const auto it = by_name_.find(name); it != by_name_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

  private:
    std::unordered_map<std::string, CoreStateId> by_name_;
};

// ---------------------------------------------------------------------------
// Fn-call resolution (CORE-FNBODY-DESIGN §4): identity-first lookup from a
// static callee symbol + the call's concrete type arguments to the unique
// body-bearing Fn CoreInstanceId. Built from the fn-guarantee pass once the
// Core instance table exists; shared by flow, workflow and fn lowerers.
// ---------------------------------------------------------------------------
struct FnCallResolution {
    CoreInstanceId instance{};
    bool has_body{false}; // false: bodyless prototype / @builtin facade
    // FnEffectKind::Pure == 0; a Nondet / Capability callee stays fail-closed.
    ir::FnEffectKind effect{ir::FnEffectKind::Pure};
};

class FnCallResolver {
  public:
    struct Entry {
        FnCallResolution resolution;
    };

    // Register one body-bearing (or known bodyless) Fn instance. `key` is the
    // instance's mangled instance_key (verbatim, never re-derived) — the
    // identifier a generic call's IR callee string carries; `symbol_id` /
    // `canonical` are the origin symbol a non-generic call resolves by.
    void add(std::string key, std::optional<std::size_t> symbol_id,
             std::string canonical, Entry entry) const {
        entries_.push_back(std::move(entry));
        const Entry *stored = &entries_.back();
        by_key_.emplace(std::move(key), stored);
        if (symbol_id.has_value()) {
            by_id_.emplace(*symbol_id, stored);
        }
        if (!canonical.empty()) {
            by_name_.emplace(std::move(canonical), stored);
        }
    }

    // Resolve a free fn call. The IR lowerer rendered generic calls with the
    // mangled instance key and non-generic calls with the origin canonical
    // name; the symbol ref is identity-first. Prefer the explicit key when it
    // names an instance, else resolve the symbol (the canonical non-generic
    // instance — user generics ALWAYS render the mangled key, so an id match
    // here cannot bind a generic body to the wrong instantiation). The
    // symbol/name fallback must resolve to EXACTLY one entry: several concrete
    // instantiations of one generic can share an origin SymbolId / canonical
    // name (notably std-namespaced generics once the sysroot is inlined), and
    // picking the first would silently dispatch an arbitrary instantiation.
    // Such a call must carry the mangled key (the by_key path above); an
    // ambiguous fallback is fail-closed rather than first-entry-wins.
    [[nodiscard]] const Entry *lookup(const SymbolRef &ref,
                                      std::string_view callee_name) const {
        if (!callee_name.empty()) {
            if (auto it = by_key_.find(std::string{callee_name}); it != by_key_.end()) {
                return it->second;
            }
        }
        if (ref.id.has_value()) {
            auto range = by_id_.equal_range(*ref.id);
            const auto count = static_cast<std::size_t>(
                std::distance(range.first, range.second));
            if (count == 1) {
                return range.first->second;
            }
            if (count > 1) {
                return nullptr; // ambiguous: concrete instantiation, needs the key
            }
        }
        if (!ref.canonical_name.empty()) {
            auto range = by_name_.equal_range(ref.canonical_name);
            const auto count = static_cast<std::size_t>(
                std::distance(range.first, range.second));
            if (count == 1) {
                return range.first->second;
            }
            if (count > 1) {
                return nullptr; // ambiguous: concrete instantiation, needs the key
            }
        }
        return nullptr;
    }

  private:
    // Mutable so the lambda-lift driver can register a lifted fn through a const
    // resolver reference while bodies are being lowered (it extends the same
    // fixed point Pass 1.5 published).
    mutable std::deque<Entry> entries_; // stable addresses for the pointer map
    mutable std::unordered_map<std::string, const Entry *> by_key_;
    mutable std::unordered_multimap<std::size_t, const Entry *> by_id_;
    mutable std::unordered_multimap<std::string, const Entry *> by_name_;
};

// ---------------------------------------------------------------------------
// RFC 0026 FB-4 (CORE-FNBODY-DESIGN §5.3): source-level transitive effect
// classification. The published FnCallResolver entry of a fn instance carries
// the fn's EFFECT, used only to reject a Nondet callee on the pure wasm lane;
// the final pure-vs-ordered Core call SHAPE is no longer chosen here — the
// lowerer over-emits ordered CoreCallStmts and reconcile_fn_call_shapes
// downgrades pure callees against the closure-aware structural
// analyze_fn_effects (the only authority that follows call_indirect edges).
// This scan is still a least fixed point over the AHFL-IR fn bodies for the
// Nondet / capability fact: a Capability clause is NOT seeded (it is an upper
// bound, not a forced classification — an fn declaring `effect C` with a pure
// body stays pure), Nondet IS seeded (it has no body-level marker), and a
// LambdaExpr body is treated as a separate lifted node (constructing a
// closure is not an effect, matching the Core closure/lift graph).
// ---------------------------------------------------------------------------
namespace {

// The capability / fn-call references one expression subtree reaches.
struct ExprCallFacts {
    bool capability{false};
    std::vector<const FnDecl *> fn_callees;
};

class FnEffectScanner {
  public:
    explicit FnEffectScanner(const std::unordered_map<std::string, std::size_t> &by_key,
                             const std::unordered_multimap<std::size_t, std::size_t> &by_id,
                             const std::vector<const FnDecl *> &bodies)
        : by_key_(by_key), by_id_(by_id), bodies_(bodies) {}

    // Fixed point over every body. Returns one effect kind per `bodies` entry.
    [[nodiscard]] std::vector<ir::FnEffectKind> run() {
        const std::size_t n = bodies_.size();
        std::vector<bool> has_cap(n, false);
        std::vector<bool> has_nondet(n, false);
        std::vector<std::vector<std::size_t>> callees(n);

        for (std::size_t i = 0; i < n; ++i) {
            const FnDecl &fn = *bodies_[i];
            // Nondet has NO body-level marker: `fn f() -> Int effect Nondet {
            // return 0; }` is a declaration-only promise, so the clause is the
            // only source of the nondet fact (and it propagates through the
            // body call graph below).
            if (fn.effect.kind == ir::FnEffectKind::Nondet) {
                has_nondet[i] = true;
            }
            // A Capability clause is deliberately NOT seeded: an effect clause
            // is an UPPER BOUND, and the structural Core derivation the call
            // shape must agree with classifies an fn ONLY from its body call
            // graph. `fn f() -> Int effect Ping { return n + 1; }` is pure
            // regardless of the clause; seeding the spelling made the lowerer
            // emit an ordered CoreCallStmt the Core verifier then rejected.
            if (fn.has_body && fn.body != nullptr) {
                ExprCallFacts facts = scan_block(*fn.body);
                has_cap[i] = has_cap[i] || facts.capability;
                for (const FnDecl *callee : facts.fn_callees) {
                    if (const auto idx = index_of(callee); idx.has_value()) {
                        callees[i].push_back(*idx);
                    }
                }
            }
            std::sort(callees[i].begin(), callees[i].end());
            callees[i].erase(std::unique(callees[i].begin(), callees[i].end()), callees[i].end());
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t i = 0; i < n; ++i) {
                for (const std::size_t c : callees[i]) {
                    if (has_cap[c] && !has_cap[i]) {
                        has_cap[i] = true;
                        changed = true;
                    }
                    if (has_nondet[c] && !has_nondet[i]) {
                        has_nondet[i] = true;
                        changed = true;
                    }
                }
            }
        }

        std::vector<ir::FnEffectKind> kinds(n, ir::FnEffectKind::Pure);
        for (std::size_t i = 0; i < n; ++i) {
            if (has_cap[i]) {
                kinds[i] = ir::FnEffectKind::Capability;
            } else if (has_nondet[i]) {
                kinds[i] = ir::FnEffectKind::Nondet;
            }
        }
        return kinds;
    }

  private:
    // Resolve one fn-call callee to a BodyFn index: an exact mangled-key match
    // first (concrete generic instantiation), else a UNIQUE origin-symbol match
    // (a non-generic fn). Several concrete instantiations share one origin id;
    // such an ambiguous id fallback resolves to nothing (the call must carry a
    // key), mirroring FnCallResolver::lookup.
    [[nodiscard]] std::optional<std::size_t>
    resolve(const SymbolRef &ref, std::string_view callee_name) const {
        if (!callee_name.empty()) {
            if (const auto it = by_key_.find(std::string{callee_name}); it != by_key_.end()) {
                return it->second;
            }
        }
        if (ref.id.has_value()) {
            auto range = by_id_.equal_range(*ref.id);
            const auto count = static_cast<std::size_t>(std::distance(range.first, range.second));
            if (count == 1) {
                return range.first->second;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> index_of(const FnDecl *fn) const {
        for (std::size_t i = 0; i < bodies_.size(); ++i) {
            if (bodies_[i] == fn) {
                return i;
            }
        }
        return std::nullopt;
    }

    void scan_expr(const Expr &expr, ExprCallFacts &facts) const {
        // A LambdaExpr body is a SEPARATE node after lambda lifting (it becomes
        // an outlined fn reached only through the closure / points-to graph),
        // the exact model the Core derivation uses. Constructing a closure
        // invokes nothing, so merely building an effectful lambda must not make
        // the enclosing fn effectful. Do NOT walk the lambda body edge; the
        // lifted fn's own body is scanned as its own FnDecl when reachable.
        if (std::holds_alternative<LambdaExpr>(expr.node)) {
            return;
        }
        if (const auto *call = std::get_if<CallExpr>(&expr.node)) {
            if (call->callee_ref.kind == SymbolRefKind::Capability) {
                facts.capability = true;
            } else if (call->callee_ref.kind == SymbolRefKind::Function) {
                if (const auto idx = resolve(call->callee_ref, call->callee)) {
                    facts.fn_callees.push_back(bodies_[*idx]);
                }
            }
        } else if (const auto *method = std::get_if<MethodCallExpr>(&expr.node)) {
            // An impl method call resolves by its method symbol the same way a
            // free fn call does (the lowerer's direct-call path treats both as
            // a Fn instance); an effectful method is therefore part of the same
            // transitive fixed point.
            if (method->method_ref.kind == SymbolRefKind::Function) {
                if (const auto idx = resolve(method->method_ref, method->method)) {
                    facts.fn_callees.push_back(bodies_[*idx]);
                }
            }
        }
        // Derived child-edge DFS: every operand subtree is walked, so a
        // capability / fn call nested in an argument, constructor, match arm,
        // etc. is seen. A new ExprNode alternative without declared children is a
        // compile error upstream, so no effect site can be silently skipped.
        auto sink = [&](const Expr &child) {
            scan_expr(child, facts);
            return true;
        };
        (void)expr_child_detail::walk_children(expr, sink);
    }

    void scan_stmt(const Statement &stmt, ExprCallFacts &facts) const {
        std::visit(
            Overloaded{
                [&](const LetStatement &s) {
                    if (s.initializer.ptr != nullptr) {
                        scan_expr(*s.initializer.ptr, facts);
                    }
                },
                [&](const AssignStatement &s) {
                    if (s.value.ptr != nullptr) {
                        scan_expr(*s.value.ptr, facts);
                    }
                },
                [&](const IfStatement &s) {
                    if (s.condition.ptr != nullptr) {
                        scan_expr(*s.condition.ptr, facts);
                    }
                    if (s.then_block != nullptr) {
                        scan_block(*s.then_block, facts);
                    }
                    if (s.else_block != nullptr) {
                        scan_block(*s.else_block, facts);
                    }
                },
                [&](const IfLetStatement &s) {
                    if (s.scrutinee.ptr != nullptr) {
                        scan_expr(*s.scrutinee.ptr, facts);
                    }
                    if (s.then_block != nullptr) {
                        scan_block(*s.then_block, facts);
                    }
                    if (s.else_block != nullptr) {
                        scan_block(*s.else_block, facts);
                    }
                },
                [](const GotoStatement &) {},
                [&](const ReturnStatement &s) {
                    if (s.value.ptr != nullptr) {
                        scan_expr(*s.value.ptr, facts);
                    }
                },
                [&](const AssertStatement &s) {
                    if (s.condition.ptr != nullptr) {
                        scan_expr(*s.condition.ptr, facts);
                    }
                    if (s.message.ptr != nullptr) {
                        scan_expr(*s.message.ptr, facts);
                    }
                },
                [&](const UnwrapStatement &s) {
                    if (s.operand.ptr != nullptr) {
                        scan_expr(*s.operand.ptr, facts);
                    }
                },
                [&](const RequiresStatement &s) {
                    if (s.condition.ptr != nullptr) {
                        scan_expr(*s.condition.ptr, facts);
                    }
                    if (s.message.ptr != nullptr) {
                        scan_expr(*s.message.ptr, facts);
                    }
                },
                [&](const UnreachableStatement &s) {
                    if (s.message.ptr != nullptr) {
                        scan_expr(*s.message.ptr, facts);
                    }
                },
                [&](const ExprStatement &s) {
                    if (s.expr.ptr != nullptr) {
                        scan_expr(*s.expr.ptr, facts);
                    }
                },
            },
            stmt.node);
    }

    [[nodiscard]] ExprCallFacts scan_block(const Block &block) const {
        ExprCallFacts facts;
        scan_block(block, facts);
        return facts;
    }

    void scan_block(const Block &block, ExprCallFacts &facts) const {
        for (const StatementPtr &stmt : block.statements) {
            if (stmt != nullptr) {
                scan_stmt(*stmt, facts);
            }
        }
    }

    const std::unordered_map<std::string, std::size_t> &by_key_;
    const std::unordered_multimap<std::size_t, std::size_t> &by_id_;
    const std::vector<const FnDecl *> &bodies_;
};

} // namespace

// ---------------------------------------------------------------------------
// Agent state-machine lowering (unchanged from the prior increment).
// ---------------------------------------------------------------------------
[[nodiscard]] CoreStateId
intern_state(std::vector<std::string> &names,
             std::unordered_map<std::string, std::uint32_t> &index_of,
             const std::string &name) {
    const auto it = index_of.find(name);
    if (it != index_of.end()) {
        return CoreStateId{it->second};
    }
    const auto id = static_cast<std::uint32_t>(names.size());
    names.push_back(name);
    index_of.emplace(name, id);
    return CoreStateId{id};
}

[[nodiscard]] CoreAgentDecl lower_agent(const AgentDecl &agent, const TypeEnv &types,
                                        const CapabilityIndex &capabilities,
                                        std::vector<CoreLowerDiagnostic> &diagnostics) {
    CoreAgentDecl out;
    out.name = agent.name;
    out.symbol_ref = agent.symbol_ref;
    out.source_range = agent.provenance.source_range;
    // Typed shell (Principle 2): `input`/`ctx`/`output` resolve to these
    // CoreTypeIds, so a member projection through `input.`/`ctx.` walks a real
    // struct type. Sema's schema boundary requires input/output to be Struct;
    // context is either an explicit Struct or the default Unit (stateless
    // agent). Both an omitted context and an explicit `context: Unit;` lower to
    // Unit here. Only a genuine Unit context folds to ContextKind::Unit +
    // kInvalid; ANY other kind (Struct, or an illegal enum/primitive/unresolved
    // context) is recorded as Struct so the verifier can reject a non-struct —
    // a non-struct must never be silently disguised as a valid Unit.
    out.input_type = types.type_id_of(agent.input_type_ref).value_or(CoreTypeId{});
    out.output_type = types.type_id_of(agent.output_type_ref).value_or(CoreTypeId{});
    if (agent.context_type_ref.kind == TypeRefKind::Unit) {
        out.context_kind = CoreAgentDecl::ContextKind::Unit;
        out.context_type = CoreTypeId{};
    } else {
        out.context_kind = CoreAgentDecl::ContextKind::Struct;
        out.context_type = types.type_id_of(agent.context_type_ref).value_or(CoreTypeId{});
    }

    std::unordered_map<std::string, std::uint32_t> index_of;
    out.states.reserve(agent.states.size());
    for (const std::string &state : agent.states) {
        static_cast<void>(intern_state(out.states, index_of, state));
    }
    out.initial = intern_state(out.states, index_of, agent.initial_state);
    out.finals.reserve(agent.final_states.size());
    for (const std::string &fin : agent.final_states) {
        out.finals.push_back(intern_state(out.states, index_of, fin));
    }
    out.transitions.reserve(agent.transitions.size());
    for (const TransitionDecl &t : agent.transitions) {
        out.transitions.push_back(
            CoreTransition{intern_state(out.states, index_of, t.from_state),
                           intern_state(out.states, index_of, t.to_state)});
    }

    // Persist Sema's declaration-order authorization fact as Core ids. The
    // frontend already rejects unresolved/duplicate entries, but lowering is a
    // fail-closed boundary and must not synthesize id 0 or trust display names.
    std::unordered_set<std::uint32_t> seen_capabilities;
    out.capabilities.reserve(agent.capability_refs.size());
    for (const SymbolRef &ref : agent.capability_refs) {
        const auto resolved = capabilities.lookup(ref);
        if (!resolved.has_value()) {
            std::string display = ref.local_name.empty() ? ref.canonical_name : ref.local_name;
            if (display.empty() && ref.id.has_value()) {
                display = "SymbolId " + std::to_string(*ref.id);
            }
            diagnostics.push_back(CoreLowerDiagnostic{
                CoreDiagnosticSeverity::Error,
                std::string(diag::kUnresolvedAgentCapability),
                "agent '" + agent.name + "' capability '" + display +
                    "' could not be resolved to the Core capability table",
                agent.provenance.source_range});
            continue;
        }
        if (!seen_capabilities.insert(resolved->id.value).second) {
            diagnostics.push_back(CoreLowerDiagnostic{
                CoreDiagnosticSeverity::Error,
                std::string(diag::kDuplicateAgentCapability),
                "agent '" + agent.name + "' lists Core capability id " +
                    std::to_string(resolved->id.value) + " more than once",
                agent.provenance.source_range});
            continue;
        }
        out.capabilities.push_back(resolved->id);
    }
    return out;
}

[[nodiscard]] CoreCapabilityDecl
lower_capability(const CapabilityDecl &cap, ValueTypeArena &value_types,
                 std::vector<CoreLowerDiagnostic> &diagnostics) {
    CoreCapabilityDecl out;
    out.name = cap.name;
    out.symbol_ref = cap.symbol_ref;
    out.effect_kind = cap.effect.kind;
    out.source_range = cap.provenance.source_range;

    const auto materialize = [&](const TypeRef &type, std::string position) -> CoreValueTypeId {
        std::string reason;
        const auto id = value_types.lower(type, &reason);
        if (id.has_value()) {
            return *id;
        }
        diagnostics.push_back(CoreLowerDiagnostic{
            CoreDiagnosticSeverity::Error,
            std::string(diag::kUnresolvedCapabilitySignature),
            "capability '" + cap.name + "' " + position +
                " type could not be materialized as a Core value type: " + reason,
            cap.provenance.source_range});
        return CoreValueTypeId{};
    };

    out.param_types.reserve(cap.params.size());
    for (std::size_t index = 0; index < cap.params.size(); ++index) {
        out.param_types.push_back(materialize(
            cap.params[index].type_ref, "parameter #" + std::to_string(index)));
    }
    out.return_type = materialize(cap.return_type_ref, "return");
    return out;
}

// ---------------------------------------------------------------------------
// Flow handler body lowering (A-normal form).
// ---------------------------------------------------------------------------

/// Maps a binary op from AHFL-IR to Core-IR. Returns nullopt for ops that are
/// not part of the pure computation set this slice lowers.
[[nodiscard]] std::optional<CoreBinaryOp> map_binary_op(ExprBinaryOp op) {
    switch (op) {
    case ExprBinaryOp::Add: return CoreBinaryOp::Add;
    case ExprBinaryOp::Subtract: return CoreBinaryOp::Sub;
    case ExprBinaryOp::Multiply: return CoreBinaryOp::Mul;
    case ExprBinaryOp::Divide: return CoreBinaryOp::Div;
    case ExprBinaryOp::Modulo: return CoreBinaryOp::Mod;
    case ExprBinaryOp::Equal: return CoreBinaryOp::Eq;
    case ExprBinaryOp::NotEqual: return CoreBinaryOp::Ne;
    case ExprBinaryOp::Less: return CoreBinaryOp::Lt;
    case ExprBinaryOp::LessEqual: return CoreBinaryOp::Le;
    case ExprBinaryOp::Greater: return CoreBinaryOp::Gt;
    case ExprBinaryOp::GreaterEqual: return CoreBinaryOp::Ge;
    case ExprBinaryOp::And: return CoreBinaryOp::And;
    case ExprBinaryOp::Or: return CoreBinaryOp::Or;
    // `Implies` has no direct execution-layer op in this slice.
    default: return std::nullopt;
    }
}

// Atomically-resolved identity of a NON-LOCAL path root: kind + type + optional
// workflow-node id resolved together, so a root can never mix a kind from one
// rule with a type/id from another.
struct ResolvedExternalRoot {
    CorePathRoot root{CorePathRoot::Identifier};
    CoreTypeId root_type{};
    CoreWorkflowNodeId workflow_node{};
};

// A program-global interner: an `ir::TypeRef` -> interned `CoreValueTypeId`
// (into `CoreProgram::value_types`), fail-closed (nullopt + `*reason`) on any
// non-materializable input. Backed by ONE shared `ValueTypeArena` so every body
// value + every dispatch descriptor interns into the same canonical pool.
using ValueTypeInterner =
    std::function<std::optional<CoreValueTypeId>(const TypeRef &, std::string *)>;

// Non-owning refs to the five ANF assets one body owner holds
// (CoreBodyStorage — shared by CoreFlowDecl / CoreWorkflowDecl / CoreFnDecl,
// CORE-FNBODY-DESIGN §2.2). Aggregate-binding from a `CoreBodyStorage&` is the
// single construction form; field-by-field construction still works for tests.
struct CoreBodyStorageRef {
    std::vector<CoreExpr> &exprs;
    std::uint32_t &value_count;
    std::vector<CorePattern> &patterns;
    // RFC 0026 P4-B: the per-body logical value-type table (index == CoreValueId).
    // Grown ATOMICALLY with `value_count` by `fresh_value`, so it stays dense.
    std::vector<CoreValueTypeId> &value_types;
    std::vector<CoreCoercionPlanNode> &coercion_plans;

    CoreBodyStorageRef(std::vector<CoreExpr> &e, std::uint32_t &vc, std::vector<CorePattern> &p,
                       std::vector<CoreValueTypeId> &vt,
                       std::vector<CoreCoercionPlanNode> &cp)
        : exprs(e), value_count(vc), patterns(p), value_types(vt), coercion_plans(cp) {}
    explicit CoreBodyStorageRef(CoreBodyStorage &s)
        : exprs(s.exprs), value_count(s.value_count), patterns(s.patterns),
          value_types(s.value_types), coercion_plans(s.coercion_plans) {}
};

// A let-bound / arm-bound local in a lowering body: its SSA value id plus the
// value's logical type. Declared before the root policies (the Fn policy's
// frame-capture redirect references it) and reused as the ExprLowerer binding.
struct LocalSsaBinding {
    CoreValueId value{};
    CoreValueTypeId value_type{};
};

// Structural (already-built-node) interner: mints a lifted lambda's concrete
// CoreVtFn signature and its CoreVtClosure construction type, which have no
// source TypeRef (TypeRef lowering never produces a closure). Routes through
// the same program-global value-type hash-cons.
using StructuralInternerFn =
    std::function<std::optional<CoreValueTypeId>(CoreValueTypeNode, std::string *)>;

// Canonical key of a frame-root path (`input.a.b` / `ctx.x`) captured whole by
// a lifted fn (design §3.2 rule 4): the fn sees no agent frame, so the
// projection is evaluated at the construction site and captured by value.
struct CapturedPathKey {
    PathRootKind root{PathRootKind::Identifier};
    std::string root_name;
    std::vector<std::string> members;

    [[nodiscard]] bool operator==(const CapturedPathKey &) const = default;
};
[[nodiscard]] inline std::string captured_path_key_repr(const CapturedPathKey &key) {
    std::string repr = std::to_string(static_cast<int>(key.root)) + ":" + key.root_name;
    for (const std::string &member : key.members) {
        repr += ".";
        repr += member;
    }
    return repr;
}
struct CapturedPathKeyHash {
    [[nodiscard]] std::size_t operator()(const CapturedPathKey &key) const noexcept {
        std::size_t h = std::hash<int>{}(static_cast<int>(key.root));
        h ^= std::hash<std::string>{}(key.root_name) + 0x9e3779b97f4a7c15ULL + (h << 6) +
             (h >> 2);
        for (const std::string &member : key.members) {
            h ^= std::hash<std::string>{}(member) + 0x9e3779b97f4a7c15ULL + (h << 6) +
                 (h >> 2);
        }
        return h;
    }
};
using FrameCaptureTable =
    std::unordered_map<CapturedPathKey, LocalSsaBinding, CapturedPathKeyHash>;

// Flow policy: input/ctx -> agent input/context struct types; others are identifier-like.
class FlowRootPolicy {
  public:
    FlowRootPolicy(CoreTypeId input_type, CoreTypeId context_type)
        : input_type_(input_type), context_type_(context_type) {}
    [[nodiscard]] bool root_may_be_local(PathRootKind kind) const {
        return kind != PathRootKind::Input && kind != PathRootKind::Context;
    }
    [[nodiscard]] ResolvedExternalRoot resolve_external_root(const Path &path) const {
        switch (path.root_kind) {
        case PathRootKind::Input:   return {CorePathRoot::Input, input_type_, {}};
        case PathRootKind::Context: return {CorePathRoot::Context, context_type_, {}};
        default:                    return {CorePathRoot::Identifier, CoreTypeId{}, {}};
        }
    }
    // Frame paths are only redirected to pre-bound env slots INSIDE a lifted fn
    // body (FnRootPolicy); a flow handler is the constructing body, so never.
    [[nodiscard]] std::optional<LocalSsaBinding> captured_frame(const Path &) const {
        return std::nullopt;
    }
  private:
    CoreTypeId input_type_;
    CoreTypeId context_type_;
};

// ---------------------------------------------------------------------------
// RFC 0026 FB-3a2 (CORE-FNBODY-DESIGN §3.1.1 D-LIFT): lambda lifting bridge
// ---------------------------------------------------------------------------
//
// The design places the lift on post-monomorphization typed-HIR. Verified
// against source, that layer is the WRONG earliest point here:
//   * `monomorphization.cpp` has ZERO lambda handling (it clones fn-body blocks
//     keyed by `body_block_index`; a lambda is never an `ir::FnDecl`);
//   * `typed_hir_lower.cpp` only *transcribes* an `ir::LambdaExpr`
//     (`visit_lambda` ~line 2304) — it neither creates an Fn instance nor runs
//     free-variable analysis, and the Core SSA values a capture must dominate do
//     not exist until the Core lowerer A-normalizes the constructing body.
// The lifted fn identity (one fresh monomorphic fn per lambda construction
// site), the deterministic ordered capture list (§3.2), and the dominant
// captured SSA values are therefore ALL only derivable inside the Core lowerer.
// So the equivalent lift runs there: encountering a LambdaExpr declares a
// lifted CoreFnDecl + Fn instance and rewrites the construction site to a
// CoreClosureExpr. The bridge below is the policy-independent interface the
// expression lowerer uses; one concrete driver owns the program fn/instance
// tables and is shared by flow / workflow / fn bodies, so a lambda nested in an
// outlined fn lifts exactly like one in a handler (§3.1.1 rule 1: deterministic
// 1:1 instance per construction site).
class LambdaLiftBridge {
  public:
    // One environment slot resolved at the construction site (design §3.2).
    //  * a captured OUTER LOCAL: `is_frame == false`, `name` is the binding, and
    //    the in-body env slot is pre-bound under that name (a member projection
    //    like `a.x` still captures the whole local `a` and projects inside the
    //    lifted body);
    //  * a captured FRAME-ROOT read (`input.x` / `ctx.y`): `is_frame == true`,
    //    `frame` is the canonical path key, and `outer_value` is the projection
    //    materialized once at the construction site (the fn sees no agent frame,
    //    §2.3/§3.2 rule 4).
    struct ResolvedCapture {
        std::string name;
        bool is_frame{false};
        CapturedPathKey frame{};
        CoreValueId outer_value{}; // constructing body's captured SSA value
        CoreValueTypeId value_type{};
    };

    virtual ~LambdaLiftBridge() = default;

    // Lift one lambda into a new monomorphic CoreFnDecl + Fn instance and lower
    // its body: logical params are pre-bound with the concrete `param_types`
    // (declaration order) and capture slots with `captures` (locals pre-bound
    // under their name, frame paths redirected to their slot). The body
    // completes via a value-bearing return of `return_type`. Returns the new fn
    // table id, or nullopt after recording a fail-closed diagnostic.
    [[nodiscard]] virtual std::optional<CoreFnId>
    lift(const LambdaExpr &lambda, SourceRangeOpt range,
         const std::vector<CoreValueTypeId> &param_types, CoreValueTypeId return_type,
         std::vector<ResolvedCapture> captures) = 0;
};

// Fn policy (CORE-FNBODY-DESIGN §2.3): a fn body sees ONLY pre-bound params
// and locals. There is no agent frame / ctx / workflow input; every non-local
// path root resolves as an Identifier (which, with no same-domain constant
// resolution inside a fn body, stays fail-closed rather than reading a frame).
// When lowering a LIFTED lambda body (FB-3a2), `frame_captures` redirects the
// frame-root paths the closure captured whole (design §3.2 rule 4) to their
// pre-bound env slot SSA value — the projection was evaluated once at the
// construction site, so inside the fn it is just another pre-bound value.
class FnRootPolicy {
  public:
    FnRootPolicy() = default;
    explicit FnRootPolicy(const FrameCaptureTable *frame_captures)
        : frame_captures_(frame_captures) {}

    [[nodiscard]] bool root_may_be_local(PathRootKind) const { return true; }

    // A captured frame-root path (`input.x` / `ctx.y` with members) resolves to
    // the pre-bound env slot; everything else is an unresolved Identifier.
    [[nodiscard]] std::optional<LocalSsaBinding>
    captured_frame(const Path &path) const {
        if (frame_captures_ == nullptr || path.members.empty()) {
            return std::nullopt;
        }
        const auto it = frame_captures_->find(
            CapturedPathKey{path.root_kind, path.root_name, path.members});
        if (it == frame_captures_->end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] ResolvedExternalRoot resolve_external_root(const Path &) const {
        return ResolvedExternalRoot{CorePathRoot::Identifier, CoreTypeId{}, {}};
    }

  private:
    const FrameCaptureTable *frame_captures_{nullptr};
};

// Workflow policy: a path root inside a node input / return region is either the
// workflow input (`input`, typed as the workflow input struct) or an upstream
// node's output (a bare identifier whose name is a declared node — typed as that
// node's target agent output struct, carrying the node's typed id). Every other
// identifier stays identifier-like and is subject to local-scope lookup FIRST, so
// a match-arm binding that shadows a node name wins (Codex-locked precedence).
class WorkflowRootPolicy {
  public:
    struct NodeOutput {
        CoreWorkflowNodeId id{};
        CoreTypeId output_type{}; // target agent output struct (kInvalid if unresolved)
    };
    WorkflowRootPolicy(CoreTypeId input_type,
                       const std::unordered_map<std::string, NodeOutput> *nodes)
        : input_type_(input_type), nodes_(nodes) {}
    // A local (match-arm binding) may shadow ANY identifier-like root — including a
    // node name — but never the `input` keyword. Mirrors FlowRootPolicy.
    [[nodiscard]] bool root_may_be_local(PathRootKind kind) const {
        return kind != PathRootKind::Input;
    }
    [[nodiscard]] ResolvedExternalRoot resolve_external_root(const Path &path) const {
        if (path.root_kind == PathRootKind::Input) {
            return {CorePathRoot::WorkflowInput, input_type_, {}};
        }
        if (path.root_kind == PathRootKind::Identifier && nodes_ != nullptr) {
            if (const auto it = nodes_->find(path.root_name); it != nodes_->end()) {
                return {CorePathRoot::WorkflowNodeOutput, it->second.output_type, it->second.id};
            }
        }
        return {CorePathRoot::Identifier, CoreTypeId{}, {}};
    }
    // Frame paths are only redirected inside a lifted fn body (FnRootPolicy).
    [[nodiscard]] std::optional<LocalSsaBinding> captured_frame(const Path &) const {
        return std::nullopt;
    }
  private:
    CoreTypeId input_type_;
    const std::unordered_map<std::string, NodeOutput> *nodes_; // not owned
};

/// Human-readable source expression kind for unsupported-node diagnostics.
/// RFC 0027 P6/P7/P8 (KR6.13-P7): one handler per ExprNode alternative,
/// generated from expr_nodes.def — the name is the alternative's own type name,
/// so the output can never silently drift from the node set and a NEW node is a
/// COMPILE ERROR here until it is classified (CLAUDE.md Principle 5).
[[nodiscard]] std::string expr_kind_name(const ExprNode &node) {
#define HANDLE_EXPR_NODE(Name, Wire, Edges) [](const Name &) { return std::string(#Name); },
    return std::visit(
        Overloaded{
#include "ahfl/compiler/ir/expr_nodes.def"
        },
        node);
#undef HANDLE_EXPR_NODE
}

/// Reusable expression / pattern / match lowering machinery, parameterized on a
/// `RootPolicy` that classifies path roots (a flow's input/ctx vs a workflow's
/// input/node-output). Owns the lowered body's expr arena, value counter, and
/// pattern arena (via `CoreBodyStorageRef`), plus the local scope. Accumulates
/// diagnostics. A `FlowLowerer` (or a future `WorkflowLowerer`) composes one of
/// these and adds the body's statement/region lowering on top.
template <class RootPolicy> class ExprLowerer {
  public:
    // A let-bound local: its SSA value id AND its logical value type (RFC 0026
    // P4-B). Kept in ONE map so lower_if's snapshot/restore covers both (a
    // branch-local shadow must not leak its value OR its type to the sibling
    // branch or past the `if`). The nominal base for member projection is derived
    // from `value_type` on demand (see `nominal_base_of`).
    // A body-local let / arm binding: SSA value + logical type. Same shape as
    // the standalone LocalSsaBinding the lift bridge uses (one type, no copy).
    using LocalBinding = LocalSsaBinding;

    ExprLowerer(CoreBodyStorageRef storage, const CapabilityIndex &caps, const TypeEnv &types,
                RootPolicy policy, const ValueTypeInterner &interner,
                const std::vector<CoreValueType> &value_type_pool,
                std::vector<CoreLowerDiagnostic> &diags,
                const FnCallResolver *fn_calls = nullptr,
                LambdaLiftBridge *lifter = nullptr,
                const StructuralInternerFn *structural_interner = nullptr)
        : storage_(storage), caps_(caps), types_(types), policy_(std::move(policy)),
          interner_(interner), value_type_pool_(value_type_pool), diags_(diags),
          fn_calls_(fn_calls), lifter_(lifter), structural_interner_(structural_interner) {}

    [[nodiscard]] std::unordered_map<std::string, LocalBinding> &scope() {
        return scope_;
    }
    [[nodiscard]] const TypeEnv &types() const {
        return types_;
    }

    // --- value-type interning (RFC 0026 P4-B) ---
    //
    // Intern an AHFL `ir::TypeRef` into the program-global value-type pool,
    // fail-closed. On failure emit a diagnostic (the program is already Error) and
    // return the kInvalid id so the value_types table stays PARALLEL to value_count
    // on the error path (the verifier only runs on error-free candidates).
    [[nodiscard]] CoreValueTypeId intern_value_type(const TypeRef &type, SourceRangeOpt range) {
        std::string reason;
        const auto id = interner_(type, &reason);
        if (!id) {
            error(diag::kUnresolvedType,
                  "value type could not be lowered to a Core-IR value type: " + reason, range);
            return CoreValueTypeId{}; // kInvalid: keeps value_types parallel on the error path
        }
        return *id;
    }

    // FB-3a2: intern a fully-built structural value-type node (a lifted fn's
    // CoreVtFn signature / CoreVtClosure construction type) through the shared
    // hash-cons. Only valid on a constructing-body lowerer (the lifter passes
    // in the structural interner). kInvalid on failure (a diagnostic is already
    // recorded by the caller-side failure path).
    [[nodiscard]] CoreValueTypeId intern_structural(CoreValueTypeNode node, SourceRangeOpt range) {
        if (structural_interner_ == nullptr) {
            error(diag::kFnBodyUnlowered,
                  "a lifted fn signature cannot be interned without the structural value-type "
                  "interner",
                  range);
            return CoreValueTypeId{};
        }
        std::string reason;
        const auto id = (*structural_interner_)(std::move(node), &reason);
        if (!id) {
            error(diag::kUnresolvedType,
                  "a lifted fn closure type could not be interned: " + reason, range);
            return CoreValueTypeId{};
        }
        return *id;
    }

    // The nominal base CoreTypeId a value type projects through, or kInvalid when
    // the value type is not a nominal (a primitive / tuple / fn has no struct base
    // to walk `.field` through). Reads the interned node from the shared pool.
    [[nodiscard]] CoreTypeId nominal_base_of(CoreValueTypeId ty) const {
        if (ty.value == CoreValueTypeId::kInvalid || ty.value >= value_type_pool_.size()) {
            return CoreTypeId{};
        }
        if (const auto *nom = std::get_if<CoreVtNominal>(&value_type_pool_[ty.value].node)) {
            return nom->base;
        }
        return CoreTypeId{};
    }

    // The nominal base of a local binding's value type, as an optional struct
    // CoreTypeId for member-projection (nullopt when the binding is not a nominal).
    [[nodiscard]] std::optional<CoreTypeId> binding_nominal_base(const LocalBinding &b) const {
        const CoreTypeId base = nominal_base_of(b.value_type);
        if (base.value == CoreTypeId::kInvalid) {
            return std::nullopt;
        }
        return base;
    }

    // --- allocation helpers ---
    [[nodiscard]] CoreValueId fresh_value(CoreValueTypeId ty) {
        const CoreValueId id{storage_.value_count++};
        storage_.value_types.push_back(ty);
        return id;
    }
    // The logical value type recorded for an already-allocated value id (kInvalid
    // if out of range — never on a clean path, since every producer records its
    // type atomically with `fresh_value`).
    [[nodiscard]] CoreValueTypeId value_type_of(CoreValueId v) const {
        return v.value < storage_.value_types.size() ? storage_.value_types[v.value]
                                                      : CoreValueTypeId{};
    }
    [[nodiscard]] CoreExprId push_expr(CoreExprNode node, SourceRangeOpt range,
                                       CoreValueTypeId result_ty) {
        const auto idx = static_cast<std::uint32_t>(storage_.exprs.size());
        storage_.exprs.push_back(CoreExpr{std::move(node), std::move(range), result_ty});
        return CoreExprId{idx};
    }
    void error(std::string_view code, std::string message, SourceRangeOpt range) {
        diags_.push_back(CoreLowerDiagnostic{CoreDiagnosticSeverity::Error, std::string(code),
                                             std::move(message), std::move(range)});
    }

    // --- value (ANF) lowering ---
    //
    // Returns the CoreValueId holding the expression's result. Capability calls
    // are hoisted to a CoreCapabilityCallStmt appended to `region` (in
    // left-to-right eval order); pure expressions are bound via a CoreLetStmt.
    [[nodiscard]] CoreValueId lower_value(const ExprRef &expr, CoreRegion &region) {
        if (expr.ptr == nullptr) {
            // No expr => no source type to intern. Fail-closed on the error path;
            // push a kInvalid-typed value so the arrays stay parallel.
            error(diag::kNullExpr, "null expression in lowered body", std::nullopt);
            return fresh_value(CoreValueTypeId{});
        }
        const SourceRangeOpt range = expr.ptr->source_range;
        // RFC 0026 P4-B: the value's logical result type is the source expr's
        // resolved type, interned once here and threaded to the per-node lowering
        // that produces the bound value. Two node kinds are `self_typed` and manage
        // their OWN result type inside their lowering:
        //   - CallExpr (capability call / constructor) must fail-closed with its
        //     OWN diagnostic FIRST, so interning its (possibly Unresolved) type here
        //     would emit a spurious UNRESOLVED_TYPE ahead of the node's real error;
        //   - PathExpr, because a BARE LOCAL reference produces NO new value (it
        //     echoes an already-typed local's value id), so it must not be forced
        //     to carry a resolved_type; the projection branch interns its own leaf
        //     type inside lower_path_value.
        // Every other node (incl. MatchExpr, whose result value carries the match's
        // resolved type) binds `result_ty` here.
        const bool self_typed = std::holds_alternative<CallExpr>(expr.ptr->node) ||
                                std::holds_alternative<MethodCallExpr>(expr.ptr->node) ||
                                std::holds_alternative<PathExpr>(expr.ptr->node);
        const CoreValueTypeId result_ty =
            self_typed ? CoreValueTypeId{} : intern_value_type(expr.ptr->resolved_type, range);
        return std::visit(
            Overloaded{
                [&](const CallExpr &call) { return lower_call_value(call, expr, range, region); },
                [&](const MethodCallExpr &call) {
                    return lower_method_call_value(call, expr, range, region);
                },
                [&](const PathExpr &e) { return lower_path_value(e, expr, range, region); },
                [&](const BoolLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Bool, e.value ? "true" : "false"},
                                     result_ty, range, region);
                },
                [&](const IntegerLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Integer, e.spelling}, result_ty,
                                     range, region);
                },
                [&](const FloatLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Float, e.spelling}, result_ty,
                                     range, region);
                },
                [&](const DecimalLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Decimal, e.spelling}, result_ty,
                                     range, region);
                },
                [&](const StringLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::String, e.spelling}, result_ty,
                                     range, region);
                },
                [&](const DurationLiteralExpr &e) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Duration, e.spelling}, result_ty,
                                     range, region);
                },
                [&](const UnitLiteralExpr &) {
                    return bind_pure(CoreLiteralExpr{CoreLiteralKind::Unit, ""}, result_ty, range,
                                     region);
                },
                [&](const QualifiedValueExpr &e) {
                    return lower_qualified_value(e, result_ty, range, region);
                },
                [&](const UnaryExpr &e) { return lower_unary_value(e, result_ty, range, region); },
                [&](const BinaryExpr &e) { return lower_binary_value(e, result_ty, range, region); },
                [&](const StructLiteralExpr &e) {
                    return lower_struct_value(e, result_ty, range, region);
                },
                [&](const MatchExpr &e) { return lower_match_value(e, result_ty, range, region); },
                [&](const UnwrapExpr &e) {
                    return lower_unwrap_value(e, expr, result_ty, range, region);
                },
                [&](const LambdaExpr &e) { return lower_lambda_value(e, expr, range, region); },
                // Effectful-or-complex shapes not yet lowered. CRITICAL: if the
                // subtree contains a capability call we MUST fail closed (never
                // hide an effect); otherwise a pure unsupported node is recorded
                // with its enumerated kind + range for the verifier to reject.
                // Routed through the ExprRef so the capability check can recurse.
                [&](const auto &node) {
                    static_cast<void>(node);
                    return lower_unsupported_value(expr, expr_kind_name(expr.ptr->node), result_ty,
                                                   range, region);
                },
            },
            expr.ptr->node);
    }

    [[nodiscard]] CoreValueId bind_pure(CoreExprNode node, CoreValueTypeId result_ty,
                                        SourceRangeOpt range, CoreRegion &region) {
        const CoreExprId expr_id = push_expr(std::move(node), range, result_ty);
        const CoreValueId value = fresh_value(result_ty);
        region.statements.push_back(CoreStmt{CoreLetStmt{value, expr_id}, range});
        return value;
    }

    /// Any/Never cannot name a materialized Core SSA value. This preflight is
    /// intentionally callable before lowering the initializer: FromNever would
    /// otherwise fail while interning the operand and obscure the persisted
    /// adjustment's stable core.INVALID_COERCION boundary diagnostic.
    [[nodiscard]] bool reject_nonmaterializable_adjustment(const AdjustmentPlan &plan,
                                                           SourceRangeOpt range) {
        for (const auto &node : plan.nodes) {
            for (const auto &op : node.ops) {
                if (op.kind == AdjustmentOpKind::ToAny || op.kind == AdjustmentOpKind::FromNever) {
                    error(diag::kInvalidCoercion,
                          "ToAny/FromNever adjustment has no materializable Core value type",
                          range);
                    return true;
                }
            }
        }
        return false;
    }

    /// Apply an annotated-let boundary without ever re-labelling the operand's
    /// existing SSA id. The persisted AHFL adjustment proof is normalized into
    /// a temporary Core arena first; only a complete non-identity plan is
    /// appended and bound as a fresh CoreCoerceExpr value.
    [[nodiscard]] CoreValueId lower_let_boundary(const LetStatement &stmt,
                                                 CoreValueId operand,
                                                 SourceRangeOpt range,
                                                 CoreRegion &region) {
        const CoreValueTypeId source_ty = value_type_of(operand);
        if (source_ty.value == CoreValueTypeId::kInvalid) {
            return operand; // an earlier lowering diagnostic already owns this partial value
        }

        if (!stmt.adjustment.has_value()) {
            const CoreValueTypeId target_ty = intern_value_type(stmt.type_ref, range);
            if (target_ty.value != CoreValueTypeId::kInvalid && !(source_ty == target_ty) &&
                !callable_widens(source_ty, target_ty)) {
                error(diag::kMissingAdjustment,
                      "let boundary changes logical value type without a persisted adjustment",
                      range);
            }
            return operand;
        }

        const auto normalized = normalize_adjustment_plan(*stmt.adjustment, range);
        if (!normalized.has_value()) {
            return operand;
        }

        std::string target_reason;
        const auto declared_target = interner_(stmt.type_ref, &target_reason);
        if (!declared_target.has_value()) {
            error(diag::kInvalidCoercion,
                  "coercion target cannot be represented in Core-IR: " + target_reason,
                  range);
            return operand;
        }
        if (!(normalized->source == source_ty) || !(normalized->result == *declared_target)) {
            error(diag::kInvalidCoercion,
                  "coercion plan boundary does not match the initializer and declared let types",
                  range);
            return operand;
        }

        if (!normalized->root.has_value()) {
            if (!(source_ty == *declared_target)) {
                error(diag::kInvalidCoercion,
                      "coercion normalized to identity but its Core source and target differ",
                      range);
            }
            return operand;
        }

        if (storage_.coercion_plans.size() > CoreCoercionPlanId::kInvalid ||
            normalized->nodes.size() >
                CoreCoercionPlanId::kInvalid - storage_.coercion_plans.size()) {
            error(
                diag::kInvalidCoercion, "coercion-plan arena exceeded its 32-bit id space", range);
            return operand;
        }
        const std::uint32_t base = static_cast<std::uint32_t>(storage_.coercion_plans.size());
        for (auto node : normalized->nodes) {
            for (auto &op : node.ops) {
                if (op.child.value != CoreCoercionPlanId::kInvalid) {
                    op.child.value += base;
                }
            }
            storage_.coercion_plans.push_back(std::move(node));
        }
        const CoreCoercionPlanId root{base + *normalized->root};
        return bind_pure(CoreCoerceExpr{operand, root}, *declared_target, range, region);
    }

    [[nodiscard]] CoreValueId lower_path_value(const PathExpr &e, const ExprRef &expr,
                                               SourceRangeOpt range, CoreRegion &region) {
        // FB-3a2 (§3.2 rule 4): inside a lifted fn body a frame-root path the
        // closure captured whole (`input.x` / `ctx.y`) resolves straight to its
        // pre-bound env slot — the projection was evaluated once at the
        // construction site, so no frame access (and no member chain) remains.
        if (const auto captured = policy_.captured_frame(e.path); captured.has_value()) {
            return captured->value;
        }
        const auto rr = resolve_path_root(e.path);
        // A bare local reference lowers directly to its bound value id (its type is
        // already recorded in the body value_types table). It produces NO new value,
        // so nothing to intern here.
        if (rr.is_local && e.path.members.empty()) {
            return rr.local;
        }
        // Design §3.1.1 rule 6 (a bare top-level fn NAME as a zero-capture
        // first-class value -> CoreClosureExpr{fn, {}}) is intentionally NOT
        // lowered here yet. The frontend rejects a bare fn name in value
        // position (UNKNOWN_VALUE), so no source reaches such a path, and the
        // resolver keys fns by mangled identity rather than the rendered name.
        // It returns when the frontend can produce a first-class bare fn value;
        // until then such a path keeps its ordinary fail-closed lowering below.
        // RFC 0026 P6-5: `xs.length` on a BOUNDED collection local is a length
        // READ of the container's inline `(ptr,len)` header, not a field
        // projection (a collection nominal declares no fields). The container
        // identity is the LOCAL BINDING's interned logical value type, which is
        // exactly what `value_type_is_bounded_collection` decides — never a name,
        // never the `length` spelling alone. A struct that HAPPENS to declare a
        // field named `length` is not a collection, so it keeps the ordinary
        // projection path below; so does an UNBOUNDED collection (no P6 header).
        if (rr.is_local && e.path.members.size() == 1 && e.path.members.front() == "length" &&
            value_type_is_bounded_collection(rr.local)) {
            const CoreValueTypeId result_ty = intern_value_type(expr.ptr->resolved_type, range);
            return bind_pure(CoreCollectionExpr{CoreCollectionOpKind::Len, rr.local, {}, {}},
                             result_ty, range, region);
        }
        // A projection / external-root path DOES produce a fresh value; intern its
        // own resolved type for it (self-typed: not eagerly interned by lower_value).
        const CoreValueTypeId result_ty = intern_value_type(expr.ptr->resolved_type, range);
        CorePathExpr node;
        node.root = rr.is_local ? CorePathRoot::Local : rr.external_root;
        node.root_name = e.path.root_name;
        node.members = e.path.members;
        if (rr.is_local) {
            node.local = rr.local;
            node.has_local = true;
        }
        // Set the self-contained root type, then resolve the member chain into
        // typed projection steps. Fail closed (Principle 5) on any unknown
        // root / field / non-struct intermediate.
        node.root_type = rr.root_type;
        node.workflow_node = rr.workflow_node;
        if (!e.path.members.empty()) {
            const std::optional<CoreTypeId> root_type =
                rr.root_type.value == CoreTypeId::kInvalid ? std::nullopt
                                                           : std::optional<CoreTypeId>{rr.root_type};
            resolve_member_chain(root_type, e.path.members, e.path.root_name, range,
                                 node.projection, node.projection_resolved);
        }
        return bind_pure(std::move(node), result_ty, range, region);
    }

    /// Whether a bound value's recorded logical type is a BOUNDED collection
    /// nominal (List / Set / Map with a static capacity). The single predicate the
    /// collection accessor lowering reads, judged by the declaration ROLE from the
    /// builtin descriptor SSOT — never by a name.
    [[nodiscard]] bool value_type_is_bounded_collection(CoreValueId value) const {
        const CoreValueTypeId ty = value_type_of(value);
        if (ty.value >= value_type_pool_.size()) {
            return false;
        }
        const auto *nominal = std::get_if<CoreVtNominal>(&value_type_pool_[ty.value].node);
        return nominal != nullptr && nominal->capacity.has_value() &&
               capacity_allowed(types_.role_of(nominal->base));
    }

    /// A path root resolved to its Core-IR root kind + type + optional local /
    /// workflow-node identity, ALL AT ONCE. A local shadows an external root only
    /// when the policy allows it (a flow's input/ctx are never local); the
    /// external kind/type/workflow-node come from the policy as one atomic unit,
    /// so a root can never mix a kind from one rule with a type/id from another.
    struct PathRootResolution {
        CorePathRoot external_root{CorePathRoot::Identifier};
        CoreTypeId root_type{};
        CoreWorkflowNodeId workflow_node{};
        bool is_local{false};
        CoreValueId local{};
    };
    [[nodiscard]] PathRootResolution resolve_path_root(const Path &path) {
        PathRootResolution r;
        if (policy_.root_may_be_local(path.root_kind)) {
            if (const auto it = scope_.find(path.root_name); it != scope_.end()) {
                r.is_local = true;
                r.local = it->second.value;
                // Member projection walks the nominal base of the binding's value
                // type (kInvalid when the binding is a non-nominal value).
                r.root_type = nominal_base_of(it->second.value_type);
                r.external_root = CorePathRoot::Identifier;
                return r;
            }
        }
        const ResolvedExternalRoot ext = policy_.resolve_external_root(path);
        r.external_root = ext.root;
        r.root_type = ext.root_type;
        r.workflow_node = ext.workflow_node;
        return r;
    }

    /// Walk `members` from `root_type`, appending a typed `CoreProjectionStep`
    /// per member (owner_type / field / result_type). Enforces the structural
    /// invariants (each step's owner is the previous step's result_type; a step
    /// with no result_type must be last). Sets `resolved=false` + fail-closed
    /// diagnostic on unknown root / unknown field / non-struct intermediate.
    void resolve_member_chain(std::optional<CoreTypeId> root_type,
                              const std::vector<std::string> &members,
                              const std::string &root_name, SourceRangeOpt range,
                              std::vector<CoreProjectionStep> &out_steps, bool &resolved) {
        if (!root_type) {
            resolved = false;
            error(diag::kUnloweredFieldProjection,
                  "cannot resolve field projection on '" + root_name +
                      "': its root type is not a known struct",
                  range);
            return;
        }
        std::optional<CoreTypeId> current = root_type;
        for (std::size_t i = 0; i < members.size(); ++i) {
            if (!current) {
                // Previous member was a primitive/non-struct but more members
                // follow — the chain cannot continue (invariant #2).
                resolved = false;
                error(diag::kUnloweredFieldProjection,
                      "cannot resolve member '" + members[i] + "' of '" + root_name +
                          "': the preceding member is not a struct type",
                      range);
                return;
            }
            const auto step = types_.field_step(*current, members[i]);
            if (!step) {
                resolved = false;
                error(diag::kUnloweredFieldProjection,
                      "type has no field named '" + members[i] + "' (in projection on '" +
                          root_name + "')",
                      range);
                return;
            }
            CoreProjectionStep out;
            out.owner_type = *current;
            out.field = step->field;
            out.result_type = step->next_type.value_or(CoreTypeId{}); // kInvalid if last/non-struct
            out_steps.push_back(out);
            current = step->next_type;
        }
    }

    [[nodiscard]] CoreValueId lower_unary_value(const UnaryExpr &e, CoreValueTypeId result_ty,
                                                SourceRangeOpt range, CoreRegion &region) {
        // Operand lowered first; if it is effectful its call stmt is hoisted. The
        // CoreValueRefExpr echoes the operand value's own logical type.
        const CoreValueId operand_val = lower_value(e.operand, region);
        const CoreExprId operand_ref = push_expr(CoreValueRefExpr{operand_val},
                                                 e.operand.ptr ? e.operand.ptr->source_range
                                                               : std::nullopt,
                                                 value_type_of(operand_val));
        const CoreUnaryOp op = (e.op == ExprUnaryOp::Negate) ? CoreUnaryOp::Neg : CoreUnaryOp::Not;
        return bind_pure(CoreUnaryExpr{op, operand_ref}, result_ty, range, region);
    }

    [[nodiscard]] CoreValueId lower_binary_value(const BinaryExpr &e, CoreValueTypeId result_ty,
                                                 SourceRangeOpt range, CoreRegion &region) {
        const auto op = map_binary_op(e.op);
        if (!op.has_value()) {
            // `Implies` (a => b) is the sole binary op without a direct Core op.
            // Sema defines it as `!a || b`; lower to exactly that shape. Both
            // operands are lowered first (left-to-right), so any effect they
            // carry is hoisted into statements before the pure boolean chain.
            if (e.op == ExprBinaryOp::Implies) {
                const CoreValueId lhs_val = lower_value(e.lhs, region);
                const CoreValueId rhs_val = lower_value(e.rhs, region);
                const CoreExprId lhs_ref = push_expr(
                    CoreValueRefExpr{lhs_val},
                    e.lhs.ptr ? e.lhs.ptr->source_range : std::nullopt, value_type_of(lhs_val));
                const CoreValueTypeId bool_ty = value_type_of(lhs_val);
                const CoreValueId not_lhs =
                    bind_pure(CoreUnaryExpr{CoreUnaryOp::Not, lhs_ref}, bool_ty, range, region);
                const CoreExprId not_lhs_ref =
                    push_expr(CoreValueRefExpr{not_lhs}, range, bool_ty);
                const CoreExprId rhs_ref = push_expr(
                    CoreValueRefExpr{rhs_val},
                    e.rhs.ptr ? e.rhs.ptr->source_range : std::nullopt, value_type_of(rhs_val));
                return bind_pure(CoreBinaryExpr{CoreBinaryOp::Or, not_lhs_ref, rhs_ref}, result_ty,
                                 range, region);
            }
            // Any other unmapped op fails closed: an effectful operand is a
            // dropped effect, a pure one is an unexecutable node.
            if (expr_has_capability_call(e.lhs) || expr_has_capability_call(e.rhs)) {
                error(diag::kEffectfulUnsupported,
                      "binary operator carries a capability effect but is not yet "
                      "lowered to Core-IR",
                      range);
                return fresh_value(result_ty);
            }
            error(diag::kUnloweredExpression,
                  "binary operator is not yet lowered to Core-IR; the program is not "
                  "executable until this slice lands",
                  range);
            const CoreExprId expr_id =
                push_expr(CoreUnsupportedExpr{"BinaryExpr", range}, range, result_ty);
            const CoreValueId value = fresh_value(result_ty);
            region.statements.push_back(CoreStmt{CoreLetStmt{value, expr_id}, range});
            return value;
        }
        // Left-to-right: lhs fully lowered (effects hoisted) before rhs.
        const CoreValueId lhs_val = lower_value(e.lhs, region);
        const CoreValueId rhs_val = lower_value(e.rhs, region);
        const CoreExprId lhs_ref =
            push_expr(CoreValueRefExpr{lhs_val}, e.lhs.ptr ? e.lhs.ptr->source_range : std::nullopt,
                      value_type_of(lhs_val));
        const CoreExprId rhs_ref =
            push_expr(CoreValueRefExpr{rhs_val}, e.rhs.ptr ? e.rhs.ptr->source_range : std::nullopt,
                      value_type_of(rhs_val));
        return bind_pure(CoreBinaryExpr{*op, lhs_ref, rhs_ref}, result_ty, range, region);
    }

    [[nodiscard]] CoreValueId lower_struct_value(const StructLiteralExpr &e,
                                                 CoreValueTypeId result_ty, SourceRangeOpt range,
                                                 CoreRegion &region) {
        // A struct / enum-variant constructor is PURE. Arguments are lowered
        // first (effects hoisted, in source order), then the construct consumes
        // their value ids — but each is tagged with its TYPED field identity so
        // a backend never relies on source WRITE order.
        CoreConstructExpr node;
        node.type_name = e.is_enum_variant ? e.enum_name : e.type_name;
        node.is_enum_variant = e.is_enum_variant;
        node.variant_name = e.variant_name;

        const auto type_id = types_.resolve_by_name(node.type_name);
        if (!type_id) {
            error(diag::kUnresolvedType,
                  "constructed type '" + node.type_name +
                      "' could not be resolved to a Core-IR type id",
                  range);
        } else {
            node.type_id = *type_id;
            if (e.is_enum_variant) {
                if (const auto idx = types_.variant_index(*type_id, e.variant_name)) {
                    node.variant = CoreVariantId{*idx};
                    node.resolved = true;
                } else {
                    error(diag::kUnresolvedEnumVariant,
                          "enum variant '" + node.type_name + "::" + e.variant_name +
                              "' could not be resolved to a declared variant index",
                          range);
                }
            } else {
                node.resolved = true; // a struct type resolved; per-field below
            }
        }

        node.args.reserve(e.fields.size());
        std::uint32_t positional = 0;
        for (const StructFieldInit &field : e.fields) {
            CoreConstructArg arg;
            arg.value = lower_value(field.value, region);
            if (node.is_enum_variant) {
                if (type_id && node.resolved &&
                    types_.variant_payload_kind(*type_id, node.variant.value) ==
                        CoreTypeDecl::VariantPayload::Kind::Struct) {
                    // Struct-payload variant: resolve the WRITTEN field name to
                    // its DECLARATION slot id (not source order), just like a
                    // struct literal. EXPECTED INVARIANT (upheld once the
                    // frontend slice lands): Typed HIR -> AHFL-IR materializes
                    // every omitted defaulted payload field into `e.fields`, so
                    // every declared slot appears here and we only assign
                    // identity. We do NOT re-evaluate a default. An unknown /
                    // missing field is fail-closed (e.g. a hand-built or
                    // deserialized IR that dropped a slot); the verifier's
                    // materialized-complete check is the backstop.
                    if (const auto slot =
                            types_.variant_field_slot(*type_id, node.variant.value, field.name)) {
                        arg.field = CoreFieldId{*slot};
                    } else {
                        node.resolved = false;
                        error(diag::kUnresolvedStructField,
                              "enum variant '" + node.type_name + "::" + node.variant_name +
                                  "' has no payload field named '" + field.name + "'",
                              range);
                    }
                } else {
                    // Tuple (or metadata-less) payload: positional slot identity
                    // (0-based) in source order.
                    arg.field = CoreFieldId{positional++};
                }
            } else if (type_id) {
                // Struct literal: resolve the WRITTEN field name to its typed
                // CoreFieldId so write order is irrelevant. Unresolvable field
                // name is fail-closed.
                if (const auto fidx = types_.field_index(*type_id, field.name)) {
                    arg.field = CoreFieldId{*fidx};
                } else {
                    node.resolved = false;
                    error(diag::kUnresolvedStructField,
                          "struct '" + node.type_name + "' has no field named '" +
                              field.name + "'",
                          range);
                }
            }
            node.args.push_back(arg);
        }
        return bind_pure(std::move(node), result_ty, range, region);
    }

    /// Lower a qualified value — a UNIT enum variant (no payload), e.g.
    /// `Option::None`, `AuditResult::Approve`. Resolves typed identity by the
    /// FULL qualified name (owning enum = everything before the last `::`);
    /// unresolvable is fail-closed.
    [[nodiscard]] CoreValueId lower_qualified_value(const QualifiedValueExpr &e,
                                                    CoreValueTypeId result_ty, SourceRangeOpt range,
                                                    CoreRegion &region) {
        CoreQualifiedExpr node;
        node.name = e.value;
        const auto sep = e.value.rfind("::");
        if (sep != std::string::npos) {
            const std::string enum_name = e.value.substr(0, sep);
            const std::string variant = e.value.substr(sep + 2);
            if (const auto type_id = types_.resolve_by_name(enum_name)) {
                if (const auto idx = types_.variant_index(*type_id, variant)) {
                    node.type_id = *type_id;
                    node.variant = CoreVariantId{*idx};
                    node.resolved = true;
                }
            }
        }
        if (!node.resolved) {
            error(diag::kUnresolvedQualifiedValue,
                  "qualified value '" + e.value +
                      "' could not be resolved to a typed enum variant",
                  range);
        }
        return bind_pure(std::move(node), result_ty, range, region);
    }

    // ------------------------------------------------------------------
    // Match / if-let lowering (RFC 0026 P3 slice (3)-3c)
    // ------------------------------------------------------------------
    //
    // A match arm's bindings are the arm-scoped SSA values its binding patterns
    // name. We pre-scan the arm's pattern to collect binding NAMES in a stable
    // order and allocate ONE CorePatternBindingId (and one fresh CoreValueId) per
    // name; an or-pattern's alternatives that reuse a name share the same
    // binding, so the (3)-2 arm-binding bijection is satisfied by construction.
    struct ArmBindings {
        std::vector<std::string> names;           // index == CorePatternBindingId
        std::vector<CorePatternBinding> values;   // parallel: the fresh SSA value
        std::vector<CoreValueTypeId> value_types; // parallel: the binding's interned value type
        std::unordered_map<std::string, std::uint32_t> index_of;

        // Existing binding index for `name` (nullopt if this is a NEW name).
        [[nodiscard]] std::optional<std::uint32_t> find(const std::string &name) const {
            if (const auto it = index_of.find(name); it != index_of.end()) {
                return it->second;
            }
            return std::nullopt;
        }
        // Register a NEW binding: its arm-scoped fresh SSA value id + interned
        // logical value type (RFC 0026 P4-B — the type table is the SSOT, this
        // struct no longer carries a nominal-only CoreTypeId).
        std::uint32_t add(const std::string &name, CoreValueId value, CoreValueTypeId value_type) {
            const auto id = static_cast<std::uint32_t>(names.size());
            names.push_back(name);
            values.push_back(CorePatternBinding{value});
            value_types.push_back(value_type);
            index_of.emplace(name, id);
            return id;
        }
    };

    // Collect the binding names a pattern introduces, allocating a fresh (typed)
    // value id per NEW name (shared by name across or-alternatives). Runs before
    // pattern lowering so the CorePatternBindingId domain is fixed. Each binding's
    // logical value type is the enclosing pattern node's `matched_type_ref` (the
    // B1 bridge: Sema records the INSTANTIATED type there, e.g. `Some(u)` on
    // `Option<User>` records `u : User`), interned into the program-global pool.
    // An or-alternative reusing a binding NAME must intern to the SAME id (index
    // equality == structural equality); a divergence is fail-closed.
    void collect_arm_bindings(const ir::MatchPattern &pattern, ArmBindings &out) {
        // RFC 0027 P6/P7/P8 (KR6.13-P7): one handler per MatchPatternNode
        // alternative, generated from pattern_nodes.def. Literal / int-range /
        // wildcard patterns bind nothing (COLLECT_BINDINGS_LEAF); the rest route
        // to a real handler. No generic catch-all: a new pattern alternative is a
        // COMPILE ERROR here until classified (CLAUDE.md Principle 5).
#define COLLECT_BINDINGS_LEAF(Name) [](const Name &) {},
#define COLLECT_BINDINGS_BindingPattern(Name)                                                         \
    [&](const ir::BindingPattern &b) {                                                             \
        /* By (3)-3b, a bare identifier Sema resolved to a unit variant is already an */           \
        /* ir::VariantPattern here, so a BindingPattern is always a genuine binding. Its */        \
        /* type is the pattern node's own matched type. */                                         \
        if (!b.name.empty()) {                                                                     \
            const CoreValueTypeId vt =                                                             \
                intern_value_type(pattern.matched_type_ref, pattern.source_range);                 \
            if (const auto existing = out.find(b.name)) {                                          \
                /* Or-alternative reusing the name: the interned type must be IDENTICAL across */  \
                /* alternatives. */                                                                \
                if (!(out.value_types[*existing] == vt)) {                                         \
                    error(diag::kUnresolvedType,                                                   \
                          "match binding '" + b.name +                                             \
                              "' has inconsistent value types across or-pattern "                  \
                              "alternatives",                                                      \
                          pattern.source_range);                                                   \
                }                                                                                  \
            } else {                                                                               \
                static_cast<void>(out.add(b.name, fresh_value(vt), vt));                           \
            }                                                                                      \
        }                                                                                          \
        if (b.nested) {                                                                            \
            /* `x @ nested`: the outer name binds the whole value; the nested pattern carries */   \
            /* its own type. */                                                                    \
            collect_arm_bindings(*b.nested, out);                                                  \
        }                                                                                          \
    },
#define COLLECT_BINDINGS_VariantPattern(Name)                                                         \
    [&](const ir::VariantPattern &v) {                                                             \
        /* Each sub-pattern is its own MatchPattern node carrying its own matched_type_ref (the */ \
        /* instantiated payload type), so no slot-type propagation is needed here. */              \
        for (const auto &sub : v.subpatterns) {                                                    \
            if (sub) {                                                                             \
                collect_arm_bindings(*sub, out);                                                   \
            }                                                                                      \
        }                                                                                          \
        for (const auto &f : v.fields) {                                                           \
            if (f.pattern) {                                                                       \
                collect_arm_bindings(*f.pattern, out);                                             \
            }                                                                                      \
        }                                                                                          \
    },
#define COLLECT_BINDINGS_TuplePattern(Name)                                                           \
    [&](const ir::TuplePattern &t) {                                                               \
        for (const auto &e : t.elements) {                                                         \
            if (e) {                                                                               \
                collect_arm_bindings(*e, out);                                                     \
            }                                                                                      \
        }                                                                                          \
    },
#define COLLECT_BINDINGS_OrPattern(Name)                                                              \
    [&](const ir::OrPattern &o) {                                                                  \
        /* Alternatives share bindings BY NAME (find/add dedups); the identical-type check runs */ \
        /* per binding above. */                                                                   \
        for (const auto &alt : o.branches) {                                                       \
            if (alt) {                                                                             \
                collect_arm_bindings(*alt, out);                                                   \
            }                                                                                      \
        }                                                                                          \
    },
#define COLLECT_BINDINGS_LiteralPattern(Name) COLLECT_BINDINGS_LEAF(Name)
#define COLLECT_BINDINGS_IntRangePattern(Name) COLLECT_BINDINGS_LEAF(Name)
#define COLLECT_BINDINGS_WildcardPattern(Name) COLLECT_BINDINGS_LEAF(Name)
#define HANDLE_PATTERN_NODE(Name, Wire) COLLECT_BINDINGS_##Name(Name)
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/pattern_nodes.def"
            },
            pattern.node);
#undef HANDLE_PATTERN_NODE
#undef COLLECT_BINDINGS_LEAF
#undef COLLECT_BINDINGS_LiteralPattern
#undef COLLECT_BINDINGS_IntRangePattern
#undef COLLECT_BINDINGS_WildcardPattern
#undef COLLECT_BINDINGS_BindingPattern
#undef COLLECT_BINDINGS_VariantPattern
#undef COLLECT_BINDINGS_TuplePattern
#undef COLLECT_BINDINGS_OrPattern
    }

    // Lower one AHFL-IR MatchPattern into the flow's CorePattern arena, returning
    // its id. Variant identity is resolved by the persisted owner_enum SymbolRef
    // ((3)-3b), never by parsing `path`. `ok` is cleared (fail-closed) on any
    // unresolved variant / binding.
    [[nodiscard]] CorePatternId lower_pattern(const ir::MatchPattern &pattern,
                                              const ArmBindings &bindings, SourceRangeOpt range,
                                              bool &ok) {
        CorePatternNode node = std::visit(
            Overloaded{
                [&](const ir::LiteralPattern &lit) -> CorePatternNode {
                    // RFC 0026 (3)-3a/3b: the source literal `none` is NOT a Unit
                    // literal — it is the Option::None variant. Resolve it to a
                    // unit CoreVariantPat by the pattern's matched-enum identity
                    // (never a literal spelling); fail closed if unresolvable.
                    if (lit.spelling == "none") {
                        return lower_none_literal(pattern, range, ok);
                    }
                    return CoreLiteralPat{literal_kind_of(lit.spelling), lit.spelling};
                },
                [&](const ir::IntRangePattern &r) -> CorePatternNode {
                    return CoreIntRangePat{r.start, r.end};
                },
                [&](const ir::WildcardPattern &) -> CorePatternNode { return CoreWildcardPat{}; },
                [&](const ir::BindingPattern &b) -> CorePatternNode {
                    CoreBindingPat out;
                    const auto it = bindings.index_of.find(b.name);
                    if (it == bindings.index_of.end()) {
                        ok = false;
                        error(diag::kUnloweredExpression,
                              "match binding '" + b.name +
                                  "' was not allocated a binding slot (internal)",
                              range);
                    } else {
                        out.binding = CorePatternBindingId{it->second};
                    }
                    if (b.nested) {
                        out.has_nested = true;
                        out.nested = lower_pattern(*b.nested, bindings, range, ok);
                    }
                    return out;
                },
                [&](const ir::VariantPattern &v) -> CorePatternNode {
                    return lower_variant_pattern(v, bindings, range, ok);
                },
                [&](const ir::TuplePattern &t) -> CorePatternNode {
                    CoreTuplePat out;
                    out.elements.reserve(t.elements.size());
                    for (const auto &e : t.elements) {
                        if (e) {
                            out.elements.push_back(lower_pattern(*e, bindings, range, ok));
                        }
                    }
                    return out;
                },
                [&](const ir::OrPattern &o) -> CorePatternNode {
                    CoreOrPat out;
                    out.alternatives.reserve(o.branches.size());
                    for (const auto &alt : o.branches) {
                        if (alt) {
                            out.alternatives.push_back(lower_pattern(*alt, bindings, range, ok));
                        }
                    }
                    return out;
                },
            },
            pattern.node);
        const auto id = static_cast<std::uint32_t>(storage_.patterns.size());
        storage_.patterns.push_back(CorePattern{std::move(node), range});
        return CorePatternId{id};
    }

    // Lower the source literal `none` into the Option::None unit variant pattern
    // by the pattern's matched-enum identity ((3)-3a/3b). Fail closed when the
    // matched enum is unresolved or has no `None` variant — never fall back to a
    // literal `none` a backend cannot match.
    [[nodiscard]] CorePatternNode lower_none_literal(const ir::MatchPattern &pattern,
                                                     SourceRangeOpt range, bool &ok) {
        CoreVariantPat out;
        const auto type_id = types_.resolve(pattern.matched_enum);
        if (!type_id) {
            ok = false;
            error(diag::kUnresolvedEnumVariant,
                  "literal `none` has no resolved matched-enum identity to lower to Option::None",
                  range);
            return out;
        }
        const auto vidx = types_.variant_index(*type_id, "None");
        if (!vidx) {
            ok = false;
            error(diag::kUnresolvedEnumVariant,
                  "literal `none` matched enum '" + pattern.matched_enum.canonical_name +
                      "' has no `None` variant",
                  range);
            return out;
        }
        out.owner_enum = *type_id;
        out.variant = CoreVariantId{*vidx};
        return out; // unit variant: no tuple subpatterns / struct fields
    }

    [[nodiscard]] CorePatternNode lower_variant_pattern(const ir::VariantPattern &v,
                                                        const ArmBindings &bindings,
                                                        SourceRangeOpt range, bool &ok) {
        CoreVariantPat out;
        const auto type_id = types_.resolve(v.owner_enum);
        if (!type_id) {
            ok = false;
            error(diag::kUnresolvedEnumVariant,
                  "match variant pattern owner enum '" + v.owner_enum.canonical_name +
                      "' could not be resolved to a Core-IR type id",
                  range);
            return out;
        }
        out.owner_enum = *type_id;
        const auto vidx = types_.variant_index(*type_id, v.variant_name);
        if (!vidx) {
            ok = false;
            error(diag::kUnresolvedEnumVariant,
                  "match variant pattern '" + v.owner_enum.canonical_name + "::" + v.variant_name +
                      "' could not be resolved to a declared variant index",
                  range);
            return out;
        }
        out.variant = CoreVariantId{*vidx};
        // RFC 0026 (3)-3c P1: the SOURCE variant-pattern shape must agree with
        // the declared payload kind — a mismatched / malformed input (e.g. a Unit
        // source shape carrying tuple subpatterns, or a Struct-kind pattern with
        // positional subpatterns) is fail-closed, never silently normalized into
        // a well-formed Core pattern by ignoring the "other" collection.
        using PK = CoreTypeDecl::VariantPayload::Kind;
        const PK meta_kind = types_.variant_payload_kind(*type_id, *vidx)
                                 .value_or(PK::Unit);
        const bool shape_ok = [&] {
            switch (v.kind) {
            case ir::VariantPatternKind::Unit:
                return meta_kind == PK::Unit && v.subpatterns.empty() && v.fields.empty();
            case ir::VariantPatternKind::Tuple:
                return meta_kind == PK::Tuple && v.fields.empty();
            case ir::VariantPatternKind::Struct:
                return meta_kind == PK::Struct && v.subpatterns.empty();
            }
            return false;
        }();
        if (!shape_ok) {
            ok = false;
            error(diag::kUnresolvedEnumVariant,
                  "match variant pattern '" + v.owner_enum.canonical_name + "::" + v.variant_name +
                      "' source shape disagrees with the declared payload kind",
                  range);
            return out;
        }
        // Struct payload: slot-identified fields (resolve each written name to its
        // declaration slot). Tuple / unit payload: positional subpatterns.
        if (meta_kind == PK::Struct) {
            for (const auto &f : v.fields) {
                if (f.is_rest) {
                    out.has_rest = true;
                    continue;
                }
                const auto slot = types_.variant_field_slot(*type_id, *vidx, f.name);
                if (!slot) {
                    ok = false;
                    error(diag::kUnresolvedStructField,
                          "match variant pattern '" + v.variant_name +
                              "' has no payload field named '" + f.name + "'",
                          range);
                    continue;
                }
                CoreVariantPatField cf;
                cf.slot = CoreFieldId{*slot};
                cf.pattern = f.pattern ? lower_pattern(*f.pattern, bindings, range, ok)
                                       : add_wildcard(range);
                out.struct_fields.push_back(cf);
            }
        } else {
            for (const auto &sub : v.subpatterns) {
                if (sub) {
                    out.tuple_subpatterns.push_back(lower_pattern(*sub, bindings, range, ok));
                }
            }
        }
        return out;
    }

    [[nodiscard]] CorePatternId add_wildcard(SourceRangeOpt range) {
        const auto id = static_cast<std::uint32_t>(storage_.patterns.size());
        storage_.patterns.push_back(CorePattern{CoreWildcardPat{}, range});
        return CorePatternId{id};
    }

    [[nodiscard]] static CoreLiteralKind literal_kind_of(const std::string &spelling) {
        if (spelling == "true" || spelling == "false") {
            return CoreLiteralKind::Bool;
        }
        if (!spelling.empty() && (spelling.front() == '"')) {
            return CoreLiteralKind::String;
        }
        // Integer is the conservative default for a numeric literal pattern; the
        // physical decode is a P4 concern (the spelling is preserved verbatim).
        return CoreLiteralKind::Integer;
    }

    // Expression-position match: each arm body yields the arm value; the match
    // defines a single result value in the parent scope (ANF). A non-exhaustive
    // match's fallback traps.
    [[nodiscard]] CoreValueId lower_match_value(const MatchExpr &m, CoreValueTypeId result_ty,
                                                SourceRangeOpt range, CoreRegion &region) {
        const CoreValueId scrutinee = lower_value(m.scrutinee, region);
        CoreMatchStmt stmt;
        stmt.scrutinee = scrutinee;
        stmt.has_result = true;
        // The match result value carries the match's own resolved type (interned
        // by the caller into `result_ty`).
        stmt.result = fresh_value(result_ty);
        bool ok = true;
        for (const MatchArmExpr &arm : m.arms) {
            stmt.arms.push_back(lower_arm(arm.pattern, arm.guard, arm.body,
                                          /*expression=*/true, range, ok));
        }
        // Non-exhaustive fallback: a trap (structural totality; the verifier
        // trusts the region, not a flag).
        stmt.fallback_region = std::make_unique<CoreRegion>();
        stmt.fallback_region->statements.push_back(
            CoreStmt{CoreTrapStmt{CoreTrapKind::NonExhaustiveMatch}, range});
        const CoreValueId result = stmt.result;
        region.statements.push_back(CoreStmt{std::move(stmt), range});
        static_cast<void>(ok); // per-arm errors already recorded; program marked non-executable
        return result;
    }

    // `unwrap(operand)` (CORE-GAPS 1): no new Core node is introduced. The
    // evaluator semantics are:
    //   * Option::Some(x) / Result::Ok(x) -> x
    //   * Option::None / Result::Err      -> runtime failure (trap)
    //   * anything else                   -> type error (Sema already rejects)
    // Lower to the existing match machinery: an expression match with ONE
    // success variant arm that binds the payload and yields it, plus an
    // UnwrapFailed trap fallback (a wildcard second arm would let an exotic
    // enum value silently yield an unbound slot, so the trap is structural —
    // only the declared success variant completes). The operand's Option /
    // Result identity is read from its interned nominal ROLE (index, never
    // name); the payload value type comes from the instantiated nominal's
    // first type argument (the B1 bridge), which hash-conses to the exact
    // CoreValueTypeId the expression result carries.
    [[nodiscard]] CoreValueId lower_unwrap_value(const UnwrapExpr &u, const ExprRef &expr,
                                                 CoreValueTypeId result_ty,
                                                 SourceRangeOpt range, CoreRegion &region) {
        const CoreValueId operand_val = lower_value(u.operand, region);
        const CoreValueTypeId operand_vt = value_type_of(operand_val);
        const CoreTypeId nominal = nominal_base_of(operand_vt);
        const CoreNominalRole role = types_.role_of(nominal);
        const char *success_name = nullptr;
        if (role == CoreNominalRole::Option) {
            success_name = "Some";
        } else if (role == CoreNominalRole::Result) {
            success_name = "Ok";
        } else {
            return lower_unsupported_value(expr, "UnwrapExpr", result_ty, range, region);
        }
        const auto *nom_node =
            std::get_if<CoreVtNominal>(&value_type_pool_[operand_vt.value].node);
        if (nom_node == nullptr || nom_node->args.empty()) {
            error(diag::kUnloweredExpression,
                  "unwrap operand's nominal type carries no payload type argument", range);
            return fresh_value(result_ty);
        }
        const std::optional<std::uint32_t> variant_idx =
            types_.variant_index(nominal, success_name);
        if (!variant_idx.has_value()) {
            error(diag::kUnresolvedEnumVariant,
                  "unwrap operand enum declares no '" + std::string(success_name) +
                      "' variant (identity table mismatch)",
                  range);
            return fresh_value(result_ty);
        }
        const CoreValueTypeId payload_vt = nom_node->args.front();

        // Build the success pattern `Success(p)` by hand from the SAME identity
        // facts a source match arm would carry (owner enum CoreTypeId + declared
        // variant index + tuple payload), so lower_variant_pattern / codegen see
        // a first-class variant pattern rather than a special case.
        ArmBindings bindings;
        {
            const auto id = bindings.add("__unwrap_payload", fresh_value(payload_vt), payload_vt);
            static_cast<void>(id);
        }
        ir::VariantPattern source_variant;
        source_variant.kind = ir::VariantPatternKind::Tuple;
        source_variant.variant_name = success_name;
        source_variant.owner_enum = u.operand.ptr->resolved_type.nominal_ref;
        {
            auto binding = std::make_unique<ir::MatchPattern>();
            binding->node = ir::BindingPattern{std::string{"__unwrap_payload"}, false, nullptr};
            binding->matched_type_ref = payload_type_ref(u);
            source_variant.subpatterns.push_back(std::move(binding));
        }
        ir::MatchPattern pattern;
        pattern.node = std::move(source_variant);
        pattern.matched_enum = u.operand.ptr->resolved_type.nominal_ref;
        pattern.matched_type_ref = clone_type_ref(u.operand.ptr->resolved_type);

        CoreMatchStmt stmt;
        stmt.scrutinee = operand_val;
        stmt.has_result = true;
        stmt.result = fresh_value(result_ty);
        bool ok = true;
        {
            CoreMatchArm arm;
            arm.pattern = lower_pattern(pattern, bindings, range, ok);
            const auto outer = scope_;
            for (std::uint32_t i = 0; i < bindings.names.size(); ++i) {
                scope_[bindings.names[i]] =
                    LocalBinding{bindings.values[i].value, bindings.value_types[i]};
            }
            arm.body = std::make_unique<CoreRegion>();
            arm.body->statements.push_back(
                CoreStmt{CoreYieldStmt{true, bindings.values.front().value}, range});
            scope_ = outer;
            arm.bindings = std::move(bindings.values);
            stmt.arms.push_back(std::move(arm));
        }
        stmt.fallback_region = std::make_unique<CoreRegion>();
        stmt.fallback_region->statements.push_back(
            CoreStmt{CoreTrapStmt{CoreTrapKind::UnwrapFailed}, range});
        const CoreValueId result = stmt.result;
        region.statements.push_back(CoreStmt{std::move(stmt), range});
        static_cast<void>(ok); // errors already recorded; the candidate is non-executable
        return result;
    }

    // The Sema TypeRef of an unwrap operand's payload slot (the Option::Some /
    // Result::Ok type argument). Used only to stamp the synthetic binding's
    // matched_type_ref exactly the way a source-level `match` arm would.
    TypeRef payload_type_ref(const UnwrapExpr &u) {
        const TypeRef &operand = u.operand.ptr->resolved_type;
        if (!operand.params.empty() && operand.params.front() != nullptr) {
            return clone_type_ref(*operand.params.front());
        }
        return TypeRef{};
    }

    // Build a CoreMatchArm for the expression or statement position. The arm's
    // pattern binding domain is fixed by a pre-scan; the guard (if any) runs in a
    // region that yields the Bool; the body yields the arm value (expression) or
    // nothing (statement). The arm's pattern bindings are visible (by name) in
    // both the guard and the body.
    [[nodiscard]] CoreMatchArm lower_arm(const ir::MatchPattern &pattern, const ExprRef &guard,
                                         const ExprRef &body, bool expression, SourceRangeOpt range,
                                         bool &ok) {
        ArmBindings bindings;
        collect_arm_bindings(pattern, bindings);
        CoreMatchArm arm;
        arm.pattern = lower_pattern(pattern, bindings, range, ok);
        // Extend the scope with the arm bindings for guard + body lowering, then
        // restore (branch-local visibility, mirroring lower_if scoping).
        const auto outer = scope_;
        for (std::uint32_t i = 0; i < bindings.names.size(); ++i) {
            scope_[bindings.names[i]] =
                LocalBinding{bindings.values[i].value, bindings.value_types[i]};
        }
        // Guard: a pure Bool region that yields its value.
        if (guard.ptr != nullptr) {
            arm.guard_region = std::make_unique<CoreRegion>();
            const CoreValueId guard_val = lower_value(guard, *arm.guard_region);
            arm.guard_region->statements.push_back(
                CoreStmt{CoreYieldStmt{true, guard_val}, range});
        }
        // Body: expression arm yields the arm value; statement arm yields nothing.
        arm.body = std::make_unique<CoreRegion>();
        if (expression) {
            const CoreValueId body_val = lower_value(body, *arm.body);
            arm.body->statements.push_back(CoreStmt{CoreYieldStmt{true, body_val}, range});
        } else if (body.ptr != nullptr) {
            static_cast<void>(lower_value(body, *arm.body));
            seal_statement_arm(*arm.body, range);
        } else {
            seal_statement_arm(*arm.body, range);
        }
        scope_ = outer;
        arm.bindings = std::move(bindings.values);
        return arm;
    }

    // A statement-position region must yield unit on every path that falls
    // through (the verifier requires no fallthrough for a statement arm). Append
    // a valueless yield when the region can complete normally. Uses the shared
    // `core_region_may_fallthrough` so the lowerer and verifier agree exactly.
    void seal_statement_arm(CoreRegion &region, SourceRangeOpt range) {
        if (core_region_may_fallthrough(region)) {
            region.statements.push_back(CoreStmt{CoreYieldStmt{false, CoreValueId{}}, range});
        }
    }

    // --- RFC 0026 FB-3a2 (CORE-FNBODY-DESIGN §3.1.1 D-LIFT / §3.2): lambda lift ---

    // Deterministic ordered capture analysis over a lambda body (§3.2).
    //
    //  * explicit capture list `[a, b]`: the named OUTER LOCALS in source order
    //    (the resolver already validated each name);
    //  * implicit: a single syntax-order (left-to-right) DFS collecting each
    //    referenced outer local at its FIRST use, de-duplicated;
    //  * lambda parameters and in-body binders (match arms, quantifiers) shadow
    //    outer names, exactly like the typechecker's child-ValueContext rule;
    //  * frame-root reads `input.x` / `ctx.y` are ALWAYS captured (explicit list
    //    or not): the fn sees no agent frame (§2.3), so the projection is
    //    evaluated once at the construction site (§3.2 rule 4);
    //  * a bare name that is neither a parameter / in-body binder nor a visible
    //    outer binding is fail-closed (covers a self-referential / fixpoint
    //    lambda, which the snapshot-closure evaluator semantics cannot run).
    //
    // Frame expressions are materialized HERE (in the constructing body region)
    // so each slot dominates the closure construction. Returns the resolved,
    // ordered slot list; failure already recorded a diagnostic.
    [[nodiscard]] std::vector<LambdaLiftBridge::ResolvedCapture>
    analyze_lambda_captures(const LambdaExpr &lambda, SourceRangeOpt range, CoreRegion &region) {
        std::vector<LambdaLiftBridge::ResolvedCapture> slots;
        std::unordered_set<std::string> seen_locals;
        std::unordered_set<std::string> seen_frames;

        const auto push_local = [&](const std::string &name) -> bool {
            if (seen_locals.insert(name).second) {
                const auto it = scope_.find(name);
                if (it == scope_.end()) {
                    error(diag::kClosureCaptureInvalid,
                          "lambda captures '" + name +
                              "' which is not a visible outer local at the construction site (a "
                              "self-referential closure or an unresolvable free reference cannot be "
                              "lifted)",
                          range);
                    return false;
                }
                LambdaLiftBridge::ResolvedCapture cap;
                cap.name = name;
                cap.is_frame = false;
                cap.outer_value = it->second.value;
                cap.value_type = it->second.value_type;
                slots.push_back(std::move(cap));
            }
            return true;
        };

        // Explicit list first, in source order (authoritative local ordering).
        for (const std::string &name : lambda.captures) {
            if (scope_.find(name) == scope_.end()) {
                error(diag::kClosureCaptureUnknown,
                      "lambda capture list names '" + name +
                          "' which is not a visible outer local at the construction site",
                      range);
                continue;
            }
            static_cast<void>(push_local(name));
        }
        const bool explicit_list = !lambda.captures.empty();

        std::unordered_set<std::string> bound;
        for (const std::string &p : lambda.params) {
            bound.insert(p);
        }

        bool ok = true;
        // Append a frame-root read slot (dedup by canonical path key). The frame
        // path EXPRESSION is materialized in the constructing body.
        const auto push_frame = [&](const PathExpr &pe, const ExprRef &ref) {
            CapturedPathKey key{pe.path.root_kind, pe.path.root_name, pe.path.members};
            if (!seen_frames.insert(captured_path_key_repr(key)).second) {
                return;
            }
            const CoreValueId value = lower_value(ref, region);
            const CoreValueTypeId ty = value_type_of(value);
            LambdaLiftBridge::ResolvedCapture cap;
            cap.name = captured_path_key_repr(key);
            cap.is_frame = true;
            cap.frame = key;
            cap.outer_value = value;
            cap.value_type = ty;
            slots.push_back(std::move(cap));
        };

        // Recursive DFS. `bound` is the set of lambda params + every binder
        // introduced inside the body (match-arm bindings, quantifier binders,
        // nested-lambda params), with shadowing exactly like the typechecker.
        std::function<void(const ExprRef &)> visit;

        // Collect the binding names a match pattern introduces into `bound`,
        // appending the NEW names to `introduced` so they can be retracted after
        // the arm (binders do not leak across arms or after the match).
        std::function<void(const MatchPattern &, std::vector<std::string> &)> collect_pattern;
        collect_pattern = [&](const MatchPattern &pattern,
                              std::vector<std::string> &introduced) {
            std::visit(
                Overloaded{
                    [](const LiteralPattern &) {},
                    [](const IntRangePattern &) {},
                    [&](const VariantPattern &v) {
                        for (const auto &s : v.subpatterns) {
                            if (s) {
                                collect_pattern(*s, introduced);
                            }
                        }
                        for (const auto &f : v.fields) {
                            if (f.pattern) {
                                collect_pattern(*f.pattern, introduced);
                            }
                        }
                    },
                    [](const WildcardPattern &) {},
                    [&](const BindingPattern &b) {
                        if (!b.name.empty() && bound.insert(b.name).second) {
                            introduced.push_back(b.name);
                        }
                        if (b.nested) {
                            collect_pattern(*b.nested, introduced);
                        }
                    },
                    [&](const TuplePattern &t) {
                        for (const auto &e : t.elements) {
                            if (e) {
                                collect_pattern(*e, introduced);
                            }
                        }
                    },
                    [&](const OrPattern &o) {
                        for (const auto &br : o.branches) {
                            if (br) {
                                collect_pattern(*br, introduced);
                            }
                        }
                    },
                },
                pattern.node);
        };

        // A bare-identifier USE of a name — the identifier root of a PathExpr
        // or the callee slot of a CallExpr whose target is a first-class local
        // rather than a statically resolved fn/capability. A name shadowed by a
        // lambda parameter / in-body binder is a local of the lifted fn; any
        // other visible name is an outer local captured at its first use (or
        // validated against an explicit capture list). Frame-root paths take
        // their own push_frame path and never reach this.
        const auto use_bare_identifier = [&](const std::string &name) {
            if (bound.find(name) != bound.end()) {
                return;
            }
            if (!explicit_list) {
                if (!push_local(name)) {
                    ok = false;
                }
            } else if (std::none_of(lambda.captures.begin(), lambda.captures.end(),
                                    [&](const std::string &c) { return c == name; })) {
                error(diag::kClosureCaptureInvalid,
                      "lambda body references '" + name +
                          "' which is neither a parameter, an in-body binding, nor "
                          "a name in its explicit capture list",
                      range);
                ok = false;
            }
        };

        visit = [&](const ExprRef &ref) {
            if (!ok || ref.ptr == nullptr) {
                return;
            }
            std::visit(
                Overloaded{
                    [&](const PathExpr &pe) {
                        const bool is_frame_root =
                            (pe.path.root_kind == PathRootKind::Input ||
                             pe.path.root_kind == PathRootKind::Context) &&
                            !pe.path.members.empty();
                        if (is_frame_root) {
                            push_frame(pe, ref);
                            return;
                        }
                        // A bare identifier reference: a parameter / in-body
                        // binder (bound) or an outer local to capture. A member
                        // projection on a captured local captures the WHOLE local;
                        // the projection itself is evaluated inside the lifted fn.
                        if (pe.path.root_kind == PathRootKind::Identifier ||
                            pe.path.root_kind == PathRootKind::Local) {
                            use_bare_identifier(pe.path.root_name);
                        }
                    },
                    [&](const LambdaExpr &nested) {
                        // A nested lambda is a scope boundary. Its params bind in
                        // its body (shadowing outer names); free names found there
                        // are captures of THIS fn too (the nested lift threads the
                        // pre-bound slot). Track only the newly-bound names so an
                        // outer shadowed binding is restored verbatim.
                        std::vector<std::string> introduced;
                        for (const std::string &p : nested.params) {
                            if (bound.insert(p).second) {
                                introduced.push_back(p);
                            }
                        }
                        visit(nested.body);
                        for (const std::string &p : introduced) {
                            bound.erase(p);
                        }
                    },
                    [&](const MatchExpr &m) {
                        visit(m.scrutinee);
                        for (const MatchArmExpr &arm : m.arms) {
                            std::vector<std::string> introduced;
                            collect_pattern(arm.pattern, introduced);
                            visit(arm.guard);
                            visit(arm.body);
                            for (const std::string &n : introduced) {
                                bound.erase(n);
                            }
                        }
                    },
                    [&](const QuantifierExpr &q) {
                        visit(q.collection);
                        std::vector<std::string> introduced;
                        if (!q.binder.empty() && bound.insert(q.binder).second) {
                            introduced.push_back(q.binder);
                        }
                        if (!q.value_binder.empty() && bound.insert(q.value_binder).second) {
                            introduced.push_back(q.value_binder);
                        }
                        visit(q.body);
                        for (const std::string &n : introduced) {
                            bound.erase(n);
                        }
                    },
                    [&](const CallExpr &c) {
                        // The callee slot is a first-class local callable (a
                        // FnT parameter, a callable local, or a constructed
                        // closure) precisely when it did not statically resolve
                        // to a top-level fn / capability AND its name names a
                        // binder of the lifted fn (`bound`) or a visible outer
                        // local (`scope_`) — the same scope gate the call
                        // lowering itself applies. A qualified enum-variant
                        // constructor / collection hook / stdlib helper has a
                        // non-Function callee_ref too but names NO local, so it
                        // must not be mistaken for a capture. A local callee is
                        // a USE exactly like the identifier root of a PathExpr,
                        // so it is captured in first-use order; otherwise the
                        // name is unbound in the lifted SSA domain and the call
                        // falls into the generic unsupported arm.
                        if (c.callee_ref.kind != SymbolRefKind::Function &&
                            c.callee_ref.kind != SymbolRefKind::Capability &&
                            (bound.count(c.callee) != 0 || scope_.count(c.callee) != 0)) {
                            use_bare_identifier(c.callee);
                        }
                        for (const ExprRef &a : c.arguments) {
                            visit(a);
                        }
                    },
                    [&](const MethodCallExpr &c) {
                        visit(c.receiver);
                        for (const ExprRef &a : c.arguments) {
                            visit(a);
                        }
                    },
                    [&](const StructLiteralExpr &s) {
                        for (const StructFieldInit &f : s.fields) {
                            visit(f.value);
                        }
                    },
                    [&](const UnaryExpr &u) { visit(u.operand); },
                    [&](const BinaryExpr &b) {
                        visit(b.lhs);
                        visit(b.rhs);
                    },
                    [&](const MemberAccessExpr &m) { visit(m.base); },
                    [&](const IndexAccessExpr &i) {
                        visit(i.base);
                        visit(i.index);
                    },
                    [&](const UnwrapExpr &u) {
                        visit(u.operand);
                        if (u.fallback_none_message) {
                            visit(u.fallback_none_message);
                        }
                    },
                    [](const auto &) {},
                },
                ref.ptr->node);
        };
        visit(lambda.body);
        if (!ok) {
            return {};
        }
        return slots;
    }

    // Lift a lambda construction site into a CoreClosureExpr (design §3.1).
    [[nodiscard]] CoreValueId lower_lambda_value(const LambdaExpr &lambda, const ExprRef &expr,
                                                 SourceRangeOpt range, CoreRegion &region) {
        if (lifter_ == nullptr) {
            return lower_unsupported_value(expr, "LambdaExpr",
                                           intern_value_type(expr.ptr->resolved_type, range), range,
                                           region);
        }
        // Concrete signature: the lambda's resolved FnT type is the typed
        // closure shape; the body's resolved type is its return.
        const CoreValueTypeId signature =
            intern_value_type(expr.ptr->resolved_type, range);
        const auto *sig_fn = signature.value == CoreValueTypeId::kInvalid
                                 ? nullptr
                                 : std::get_if<CoreVtFn>(&value_type_pool_[signature.value].node);
        if (sig_fn == nullptr) {
            error(diag::kFnCallableTypeIncompatible,
                  "lambda expression does not have a materialized Fn signature", range);
            return fresh_value(CoreValueTypeId{});
        }
        const CoreValueTypeId return_type =
            lambda.body.ptr == nullptr
                ? CoreValueTypeId{}
                : intern_value_type(lambda.body.ptr->resolved_type, range);

        // Deterministic ordered captures, with frame reads materialized in the
        // constructing region before the closure is built.
        std::vector<LambdaLiftBridge::ResolvedCapture> captures =
            analyze_lambda_captures(lambda, range, region);
        bool capture_failed = false;
        for (const auto &cap : captures) {
            // CoreValueId has no kInvalid sentinel; a slot is invalid only when
            // its materialized type is kInvalid (value id 0 is a legal first
            // SSA value).
            if (cap.value_type.value == CoreValueTypeId::kInvalid) {
                capture_failed = true;
            }
        }
        if (signature.value == CoreValueTypeId::kInvalid ||
            return_type.value == CoreValueTypeId::kInvalid || capture_failed) {
            return fresh_value(signature);
        }

        const std::optional<CoreFnId> fn =
            lifter_->lift(lambda, range, sig_fn->params, return_type, captures);
        if (!fn.has_value()) {
            return fresh_value(signature);
        }

        // The closure value type: the concrete signature + one ByValue capture
        // per slot, same canonical order.
        std::vector<CoreValueTypeId> capture_types;
        std::vector<CoreValueId> env;
        capture_types.reserve(captures.size());
        env.reserve(captures.size());
        for (const auto &cap : captures) {
            capture_types.push_back(cap.value_type);
            env.push_back(cap.outer_value);
        }
        const CoreValueTypeId closure_ty = intern_structural(
            CoreVtClosure{signature,
                          [&] {
                              std::vector<CoreClosureCapture> cs;
                              cs.reserve(capture_types.size());
                              for (const CoreValueTypeId t : capture_types) {
                                  cs.push_back(CoreClosureCapture{t, CoreCaptureMode::ByValue});
                              }
                              return cs;
                          }()},
            range);
        if (closure_ty.value == CoreValueTypeId::kInvalid) {
            return fresh_value(signature);
        }
        return bind_pure(CoreClosureExpr{*fn, std::move(env)}, closure_ty, range, region);
    }

    // An indirect call through a first-class callable VALUE (a FnT parameter,
    // a callable local, or a constructed closure), design §5.2. Arity / arg /
    // result types are checked by the verifier against the callable signature.
    // Returns an unset value id when `callee_name` is not a callable local, so
    // the caller can try the other resolution paths.
    [[nodiscard]] std::optional<CoreValueId>
    lower_closure_call_value(const std::string &callee_name, std::vector<CoreValueId> args,
                             CoreValueTypeId result_ty, SourceRangeOpt range, CoreRegion &region) {
        const auto it = scope_.find(callee_name);
        if (it == scope_.end() ||
            callable_signature_id(it->second.value_type) == CoreValueTypeId{}) {
            return std::nullopt;
        }
        return bind_pure(CoreCallClosureExpr{it->second.value, std::move(args)}, result_ty, range,
                         region);
    }

    // The interned CoreVtFn signature a callable binding carries, or kInvalid:
    // a bare signature type is itself a CoreVtFn; a constructed closure yields
    // its referenced signature (D-FNREP, §3.1.1).
    [[nodiscard]] CoreValueTypeId callable_signature_id(CoreValueTypeId ty) const {
        if (ty.value == CoreValueTypeId::kInvalid || ty.value >= value_type_pool_.size()) {
            return CoreValueTypeId{};
        }
        const CoreValueTypeNode &node = value_type_pool_[ty.value].node;
        if (std::holds_alternative<CoreVtFn>(node)) {
            return ty;
        }
        if (const auto *closure = std::get_if<CoreVtClosure>(&node)) {
            return closure->signature;
        }
        return CoreValueTypeId{};
    }

    // D-FNREP (§3.1.1): a constructed closure value CoreVtClosure{S, _} widens
    // to the bare signature type S with NO adapter (identical 8-byte layout, the
    // caller ignores the env). Only closure -> its own Fn signature widens;
    // Fn -> closure (narrowing) and any other mismatch do not. The verifier
    // independently re-checks dispatch through callable_signature.
    [[nodiscard]] bool callable_widens(CoreValueTypeId source, CoreValueTypeId target) const {
        if (source == target) {
            return true;
        }
        if (source.value == CoreValueTypeId::kInvalid || source.value >= value_type_pool_.size()) {
            return false;
        }
        const auto *closure =
            std::get_if<CoreVtClosure>(&value_type_pool_[source.value].node);
        return closure != nullptr && closure->signature == target;
    }

    // --- RFC 0026 FB-1 (CORE-FNBODY-DESIGN §4/§5.1): static direct fn calls ---

    // Lower a resolved direct fn/method call (pure OR effectful). `callee_ref`
    // is the resolved Fn symbol, `args` are the already-lowered ANF operands
    // (method calls pass receiver as args[0]), and `result_ty` is the call's
    // interned result type. Returns false (no diagnostic) when the callee is
    // unknown/bodyless so the caller keeps its legacy fail-closed path.
    // RFC 0026 FB-4 (design §5.3): every call is emitted as an ordered
    // CoreCallStmt first; the source scanner cannot see an effect routed
    // through a FnT parameter, and reconcile_fn_call_shapes canonicalizes the
    // pure callees back to CoreCallExpr against the closure-aware structural
    // authority. A Nondet callee stays fail-closed here (no value-level nondet
    // on the wasm computation lane yet).
    [[nodiscard]] bool emit_direct_call(const SymbolRef &callee_ref,
                                        std::string_view callee_name,
                                        std::vector<CoreValueId> args,
                                        CoreValueTypeId result_ty,
                                        SourceRangeOpt range,
                                        CoreRegion &region,
                                        CoreValueId &out_value) {
        if (fn_calls_ == nullptr) {
            return false;
        }
        const FnCallResolver::Entry *entry = fn_calls_->lookup(callee_ref, callee_name);
        if (entry == nullptr || !entry->resolution.has_body) {
            return false;
        }
        if (entry->resolution.effect == ir::FnEffectKind::Nondet) {
            error(diag::kNondetFnValue,
                  "a Nondet fn cannot be called from the pure wasm computation lane",
                  range);
            out_value = fresh_value(result_ty);
            return true;
        }
        // FB-4: EVERY resolvable body fn call is lowered as an ordered
        // CoreCallStmt first. The source-level FnEffectScanner cannot see an
        // effect routed through a FnT parameter / closure value (that fact
        // exists only in the post-lift Core ClosurePointsTo lattice), so an
        // expr-vs-statement choice made here would disagree with the
        // structural authority. A final reconciliation pass
        // (reconcile_call_shapes, after every body incl. lifted lambdas is
        // lowered) downgrades exactly the statements whose callee the
        // closure-aware analyze_fn_effects classifies PURE back to a
        // CoreCallExpr, leaving genuinely effectful callees ordered. The
        // result is a fresh SSA value and the statement follows its
        // already-lowered left-to-right argument statements.
        CoreCallStmt stmt;
        stmt.callee = entry->resolution.instance;
        stmt.args = std::move(args);
        stmt.result = fresh_value(result_ty);
        out_value = stmt.result;
        region.statements.push_back(CoreStmt{std::move(stmt), range});
        return true;
    }

    [[nodiscard]] CoreValueId
    lower_method_call_value(const MethodCallExpr &call, const ExprRef &expr,
                            SourceRangeOpt range, CoreRegion &region) {
        const CoreValueTypeId result_ty = intern_value_type(expr.ptr->resolved_type, range);
        // Receiver-first argument order (design §5.1): exact mirror of the
        // evaluator's MethodCallExpr flattening.
        std::vector<CoreValueId> args;
        args.reserve(call.arguments.size() + 1);
        if (call.receiver.ptr != nullptr) {
            args.push_back(lower_value(call.receiver, region));
        }
        for (const ExprRef &arg : call.arguments) {
            args.push_back(lower_value(arg, region));
        }
        CoreValueId value{};
        if (emit_direct_call(call.method_ref, call.method, std::move(args), result_ty, range, region,
                             value)) {
            return value;
        }
        return lower_unsupported_value(expr, "MethodCallExpr", result_ty, range, region);
    }

    /// The A-normalization core: a capability call becomes an ordered statement.
    [[nodiscard]] CoreValueId lower_call_value(const CallExpr &call, const ExprRef &expr,
                                               SourceRangeOpt range, CoreRegion &region) {
        if (call.callee_ref.kind == SymbolRefKind::Capability) {
            const auto info = caps_.lookup(call.callee_ref);
            if (!info.has_value()) {
                // Fail closed: an unresolved capability call must not produce an
                // Unknown node the backend would treat as a no-op.
                error(diag::kUnresolvedCapabilityCall,
                      "capability call '" + call.callee +
                          "' could not be resolved to a capability declaration",
                      range);
                return fresh_value(CoreValueTypeId{});
            }
            // The call result value carries the call's resolved return type (the
            // CallExpr's resolved_type is the call result). `expr.ptr` is non-null
            // here (lower_value checks it before dispatching). Fail-closed if the
            // resolved type is Unresolved/unmaterializable.
            const CoreValueTypeId result_ty =
                intern_value_type(expr.ptr->resolved_type, range);
            CoreCapabilityCallStmt stmt;
            stmt.result = fresh_value(result_ty);
            stmt.capability = info->id;
            stmt.callee_name = call.callee;
            stmt.args.reserve(call.arguments.size());
            // Arguments lowered left-to-right; a nested capability call among
            // them is itself hoisted to a preceding statement (recursion).
            for (const ExprRef &arg : call.arguments) {
                stmt.args.push_back(lower_value(arg, region));
            }
            const CoreValueId result = stmt.result;
            region.statements.push_back(CoreStmt{std::move(stmt), range});
            return result;
        }
        // A non-capability call may be an INTERNAL COLLECTION ACCESSOR builtin
        // (`list_raw_get` / `list_raw_set` / `list_raw_length` — the frontend's
        // desugar target for `xs[i]` / `xs[i] = v` / `xs.length()`). Those hooks
        // are compiler-internal, so the callee string is matched against the
        // compile-time hook SSOT and mapped to a typed `CoreCollectionOpKind`; the
        // CONTAINER IDENTITY survives as the base operand's interned logical value
        // type, never as this string. A hook call whose container is NOT a bounded
        // collection fails closed.
        if (const auto op = collection_op_of_hook(call.callee)) {
            return lower_collection_builtin(call, *op, expr, range, region);
        }
        // RFC 0026 FB-3a2 (design §4 rule 5 / §5.2): a call whose callee is a
        // first-class callable VALUE — a FnT parameter, a callable local, or a
        // constructed closure — lowers to an indirect CoreCallClosureExpr
        // (call_indirect). A free `fn` call (SymbolRefKind::Function) is NOT such
        // a value and stays on the direct-call path below. Arguments are
        // A-normalized left-to-right first.
        if (call.callee_ref.kind != SymbolRefKind::Capability &&
            call.callee_ref.kind != SymbolRefKind::Function) {
            if (scope_.find(call.callee) != scope_.end()) {
                const CoreValueTypeId result_ty =
                    intern_value_type(expr.ptr->resolved_type, range);
                std::vector<CoreValueId> args;
                args.reserve(call.arguments.size());
                for (const ExprRef &arg : call.arguments) {
                    args.push_back(lower_value(arg, region));
                }
                if (auto value =
                        lower_closure_call_value(call.callee, std::move(args), result_ty, range,
                                                 region)) {
                    return *value;
                }
            }
        }
        // RFC 0026 FB-1 (CORE-FNBODY-DESIGN §4 rule 4): a statically resolved
        // pure fn / monomorphized generic fn / body-bearing method lowers to a
        // direct CoreCallExpr (`call` opcode). Identity-first: the symbol ref is
        // the fn instance, the IR callee name is the mangled key for a generic.
        // Arguments are A-normalized first (a nested call hoists to a preceding
        // let). Unknown / bodyless / effectful callees fall through to the
        // existing fail-closed arm below.
        if (call.callee_ref.kind == SymbolRefKind::Function) {
            const CoreValueTypeId result_ty =
                intern_value_type(expr.ptr->resolved_type, range);
            std::vector<CoreValueId> args;
            args.reserve(call.arguments.size());
            for (const ExprRef &arg : call.arguments) {
                args.push_back(lower_value(arg, region));
            }
            CoreValueId value{};
            if (emit_direct_call(call.callee_ref, call.callee, std::move(args), result_ty, range,
                                 region, value)) {
                return value;
            }
        }
        // A non-capability call may be an ENUM-VARIANT CONSTRUCTOR — the front
        // end lowers `Enum::Variant(payload)` to a CallExpr whose `callee_ref`
        // resolves to the ENUM symbol and whose `callee` string ends in the
        // variant name. We resolve the owning type by SYMBOL IDENTITY (not by
        // string munging), then the variant index within that type. These are
        // PURE constructors: arguments are lowered first (a nested capability
        // call among them is hoisted, A-normal), then a CoreConstructExpr
        // consumes the resulting value ids.
        if (const auto type_id = types_.resolve(call.callee_ref)) {
            const std::string variant = variant_suffix(call.callee);
            if (const auto idx = types_.variant_index(*type_id, variant)) {
                CoreConstructExpr node;
                node.type_name = call.callee_ref.canonical_name;
                node.variant_name = variant;
                node.is_enum_variant = true;
                node.type_id = *type_id;
                node.variant = CoreVariantId{*idx};
                node.resolved = true;
                node.args.reserve(call.arguments.size());
                std::uint32_t positional = 0;
                for (const ExprRef &arg : call.arguments) {
                    // Enum payload: positional slot identity, in source order.
                    node.args.push_back(CoreConstructArg{CoreFieldId{positional++},
                                                         lower_value(arg, region)});
                }
                // The constructed value carries the call's resolved result type.
                const CoreValueTypeId result_ty =
                    intern_value_type(expr.ptr->resolved_type, range);
                return bind_pure(std::move(node), result_ty, range, region);
            }
        }
        // Otherwise: a free-function call (not modelled by this slice) or an
        // unresolved variant. Fail closed if the subtree carries an effect;
        // else record a pure unsupported node.
        const CoreValueTypeId result_ty = intern_value_type(expr.ptr->resolved_type, range);
        return lower_unsupported_value(expr, "CallExpr", result_ty, range, region);
    }

    /// The trailing `::`-separated segment of a qualified callee (the variant
    /// name); the whole string if it has no `::`.
    [[nodiscard]] static std::string variant_suffix(const std::string &callee) {
        const auto last = callee.rfind("::");
        return last == std::string::npos ? callee : callee.substr(last + 2);
    }

    /// The bounded-collection operation an internal builtin hook names, or
    /// nullopt when the hook is not a collection accessor. The hook spelling is
    /// matched against the compile-time `known_builtin_hooks()` SSOT, NOT parsed:
    /// a hook is a compiler-internal identifier, and this is the ONE place that
    /// maps it to a typed `CoreCollectionOpKind`. The container's own identity
    /// (its interned `CoreValueTypeId`) is what the verifier and codegen read —
    /// never this string.
    [[nodiscard]] static std::optional<CoreCollectionOpKind>
    collection_op_of_hook(std::string_view hook) {
        if (hook == "list_raw_get") {
            return CoreCollectionOpKind::ElementGet;
        }
        if (hook == "list_raw_set") {
            return CoreCollectionOpKind::ElementSet;
        }
        if (hook == "list_raw_length") {
            return CoreCollectionOpKind::Len;
        }
        return std::nullopt;
    }

    /// Lower an internal collection-accessor builtin call to a typed
    /// `CoreCollectionExpr`. The base operand's recorded logical value type is
    /// the container identity: it MUST be a BOUNDED collection nominal (List /
    /// Set / Map with a capacity), otherwise the program is not executable on the
    /// P6 bounded-collection lane and this fails closed with an actionable
    /// diagnostic instead of emitting a node whose backing store does not exist.
    [[nodiscard]] CoreValueId
    lower_collection_builtin(const CallExpr &call, CoreCollectionOpKind op,
                             const ExprRef &expr, SourceRangeOpt range, CoreRegion &region) {
        if (call.arguments.empty()) {
            error(diag::kUnloweredExpression,
                  "collection accessor '" + call.callee + "' has no container operand", range);
            return fresh_value(intern_value_type(expr.ptr->resolved_type, range));
        }
        CoreCollectionExpr node;
        node.op = op;
        node.base = lower_value(call.arguments[0], region);
        // An already-invalid operand (an earlier diagnostic owns this partial
        // value) must NOT get a second, misleading boundedness diagnostic; still
        // produce a value of the accessor's own result type so the arrays stay
        // parallel.
        if (value_type_of(node.base).value == CoreValueTypeId::kInvalid) {
            return fresh_value(intern_value_type(expr.ptr->resolved_type, range));
        }
        if (!value_type_is_bounded_collection(node.base)) {
            error(diag::kUnloweredExpression,
                  "collection accessor '" + call.callee +
                      "' requires a BOUNDED collection operand (List/Set/Map with a static "
                      "capacity); an unbounded collection has no wasm32 backing store",
                  range);
            return fresh_value(intern_value_type(expr.ptr->resolved_type, range));
        }
        const std::size_t expected = op == CoreCollectionOpKind::Len ? 1u : 2u;
        if (op == CoreCollectionOpKind::ElementSet) {
            // `list_raw_set(xs, i, x)` returns the updated collection.
            if (call.arguments.size() != 3) {
                error(diag::kUnloweredExpression,
                      "collection accessor '" + call.callee + "' expects 3 operands", range);
                return fresh_value(intern_value_type(expr.ptr->resolved_type, range));
            }
            node.index = lower_value(call.arguments[1], region);
            node.value = lower_value(call.arguments[2], region);
        } else {
            if (call.arguments.size() != expected) {
                error(diag::kUnloweredExpression,
                      "collection accessor '" + call.callee + "' expects " +
                          std::to_string(expected) + " operand(s)",
                      range);
                return fresh_value(intern_value_type(expr.ptr->resolved_type, range));
            }
            if (op == CoreCollectionOpKind::ElementGet) {
                node.index = lower_value(call.arguments[1], region);
            }
        }
        const CoreValueTypeId result_ty = intern_value_type(expr.ptr->resolved_type, range);
        return bind_pure(std::move(node), result_ty, range, region);
    }

    /// Fail-closed handler for a not-yet-lowered expression. Whether it carries
    /// an effect or not, the resulting program is NOT executable: with no
    /// Core-IR verifier yet, a `CoreUnsupportedExpr` the backend cannot execute
    /// must never be reported as executable. So this ALWAYS emits an Error. The
    /// node (with enumerated kind + range) is still recorded so the produced
    /// partial program remains inspectable for diagnostics/tests.
    [[nodiscard]] CoreValueId lower_unsupported_value(const ExprRef &expr, std::string kind,
                                                      CoreValueTypeId result_ty,
                                                      SourceRangeOpt range, CoreRegion &region) {
        if (expr_has_capability_call(expr)) {
            error(diag::kEffectfulUnsupported,
                  "expression kind '" + kind +
                      "' carries a capability effect but is not yet lowered to Core-IR "
                      "(cannot be reduced to A-normal form in this slice)",
                  range);
            return fresh_value(result_ty);
        }
        error(diag::kUnloweredExpression,
              "expression kind '" + kind +
                  "' is not yet lowered to Core-IR; the program is not executable until "
                  "this slice lands",
              range);
        const CoreExprId expr_id =
            push_expr(CoreUnsupportedExpr{std::move(kind), range}, range, result_ty);
        const CoreValueId value = fresh_value(result_ty);
        region.statements.push_back(CoreStmt{CoreLetStmt{value, expr_id}, range});
        return value;
    }

    // --- capability-effect detection over an arbitrary subtree ---
    //
    // RFC 0027 P6/P7/P8 (KR6.13-P7): one handler per ExprNode alternative,
    // generated from expr_nodes.def, so a NEW node is a COMPILE ERROR here until
    // it is classified as effect-carrier or leaf — never a silent `false` that
    // could hide a capability call (CLAUDE.md Principle 5). The leaf set is the
    // nodes that can never contain a capability invocation: literals, paths,
    // qualified values, lambda / quantifier bodies (not yet lowered at all; they
    // fail closed as unsupported shapes upstream), unit / match.
    [[nodiscard]] bool expr_has_capability_call(const ExprRef &expr) const {
        if (expr.ptr == nullptr) {
            return false;
        }
#define CAPABILITY_LEAF(Name) [](const Name &) { return false; },
#define CAPABILITY_CallExpr(Name)                                                                  \
    [&](const Name &call) {                                                                        \
        if (call.callee_ref.kind == SymbolRefKind::Capability) {                                   \
            return true;                                                                           \
        }                                                                                          \
        for (const ExprRef &arg : call.arguments) {                                                \
            if (expr_has_capability_call(arg)) {                                                   \
                return true;                                                                       \
            }                                                                                      \
        }                                                                                          \
        return false;                                                                              \
    },
#define CAPABILITY_MethodCallExpr(Name)                                                            \
    [&](const Name &e) {                                                                           \
        if (expr_has_capability_call(e.receiver)) {                                                \
            return true;                                                                           \
        }                                                                                          \
        for (const ExprRef &arg : e.arguments) {                                                   \
            if (expr_has_capability_call(arg)) {                                                   \
                return true;                                                                       \
            }                                                                                      \
        }                                                                                          \
        return false;                                                                              \
    },
#define CAPABILITY_UnaryExpr(Name) [&](const Name &e) { return expr_has_capability_call(e.operand); },
#define CAPABILITY_BinaryExpr(Name)                                                                \
    [&](const Name &e) {                                                                           \
        return expr_has_capability_call(e.lhs) || expr_has_capability_call(e.rhs);                 \
    },
#define CAPABILITY_MemberAccessExpr(Name)                                                          \
    [&](const Name &e) { return expr_has_capability_call(e.base); },
#define CAPABILITY_IndexAccessExpr(Name)                                                           \
    [&](const Name &e) {                                                                           \
        return expr_has_capability_call(e.base) || expr_has_capability_call(e.index);              \
    },
#define CAPABILITY_UnwrapExpr(Name) [&](const Name &e) { return expr_has_capability_call(e.operand); },
#define CAPABILITY_StructLiteralExpr(Name)                                                         \
    [&](const Name &e) {                                                                           \
        for (const StructFieldInit &f : e.fields) {                                                \
            if (expr_has_capability_call(f.value)) {                                               \
                return true;                                                                       \
            }                                                                                      \
        }                                                                                          \
        return false;                                                                              \
    },
#define CAPABILITY_MatchExpr(Name)                                                                 \
    [&](const Name &e) {                                                                           \
        if (expr_has_capability_call(e.scrutinee)) {                                               \
            return true;                                                                           \
        }                                                                                          \
        for (const MatchArmExpr &arm : e.arms) {                                                   \
            if (expr_has_capability_call(arm.body)) {                                              \
                return true;                                                                       \
            }                                                                                      \
        }                                                                                          \
        return false;                                                                              \
    },
        // Leaves: no capability call can hide in these at this layer.
#define CAPABILITY_BoolLiteralExpr(Name) CAPABILITY_LEAF(Name)
#define CAPABILITY_IntegerLiteralExpr(Name) CAPABILITY_LEAF(Name)
#define CAPABILITY_FloatLiteralExpr(Name) CAPABILITY_LEAF(Name)
#define CAPABILITY_DecimalLiteralExpr(Name) CAPABILITY_LEAF(Name)
#define CAPABILITY_StringLiteralExpr(Name) CAPABILITY_LEAF(Name)
#define CAPABILITY_DurationLiteralExpr(Name) CAPABILITY_LEAF(Name)
#define CAPABILITY_PathExpr(Name) CAPABILITY_LEAF(Name)
#define CAPABILITY_QualifiedValueExpr(Name) CAPABILITY_LEAF(Name)
#define CAPABILITY_LambdaExpr(Name) CAPABILITY_LEAF(Name)
#define CAPABILITY_UnitLiteralExpr(Name) CAPABILITY_LEAF(Name)
#define CAPABILITY_QuantifierExpr(Name) CAPABILITY_LEAF(Name)
#define HANDLE_EXPR_NODE(Name, Wire, Edges) CAPABILITY_##Name(Name)
        return std::visit(
            Overloaded{
#include "ahfl/compiler/ir/expr_nodes.def"
            },
            expr.ptr->node);
#undef HANDLE_EXPR_NODE
#undef CAPABILITY_LEAF
#undef CAPABILITY_CallExpr
#undef CAPABILITY_MethodCallExpr
#undef CAPABILITY_UnaryExpr
#undef CAPABILITY_BinaryExpr
#undef CAPABILITY_MemberAccessExpr
#undef CAPABILITY_IndexAccessExpr
#undef CAPABILITY_UnwrapExpr
#undef CAPABILITY_StructLiteralExpr
#undef CAPABILITY_MatchExpr
#undef CAPABILITY_BoolLiteralExpr
#undef CAPABILITY_IntegerLiteralExpr
#undef CAPABILITY_FloatLiteralExpr
#undef CAPABILITY_DecimalLiteralExpr
#undef CAPABILITY_StringLiteralExpr
#undef CAPABILITY_DurationLiteralExpr
#undef CAPABILITY_PathExpr
#undef CAPABILITY_QualifiedValueExpr
#undef CAPABILITY_LambdaExpr
#undef CAPABILITY_UnitLiteralExpr
#undef CAPABILITY_QuantifierExpr
    }

  private:
    struct NormalizedAdjustment {
        CoreValueTypeId source{};
        CoreValueTypeId result{};
        std::optional<std::uint32_t> root;
        std::vector<CoreCoercionPlanNode> nodes;
    };

    struct NormalizedNode {
        bool ok{false};
        CoreValueTypeId source{};
        CoreValueTypeId result{};
        std::optional<std::uint32_t> id;
    };

    [[nodiscard]] std::optional<NormalizedAdjustment>
    normalize_adjustment_plan(const AdjustmentPlan &plan, SourceRangeOpt range) {
        // Any/Never are not materializable Core value types. Reject their
        // semantic leaf operations BEFORE attempting to intern either endpoint,
        // so the stable diagnostic is core.INVALID_COERCION rather than a lower-
        // level value-type materialization error.
        if (reject_nonmaterializable_adjustment(plan, range)) {
            return std::nullopt;
        }
        if (plan.root >= plan.nodes.size()) {
            error(diag::kInvalidCoercion, "coercion plan root is out of range", range);
            return std::nullopt;
        }

        const auto intern_plan_type =
            [&](const TypeRef &type, std::string_view label) -> std::optional<CoreValueTypeId> {
            std::string reason;
            const auto id = interner_(type, &reason);
            if (!id.has_value()) {
                error(diag::kInvalidCoercion,
                      "coercion " + std::string(label) +
                          " cannot be represented in Core-IR: " + reason,
                      range);
            }
            return id;
        };

        const auto boundary_source = intern_plan_type(plan.source, "source type");
        const auto boundary_result = intern_plan_type(plan.target, "target type");
        if (!boundary_source.has_value() || !boundary_result.has_value()) {
            return std::nullopt;
        }

        NormalizedAdjustment out;
        out.source = *boundary_source;
        out.result = *boundary_result;
        std::vector<unsigned char> color(plan.nodes.size(), 0);
        std::vector<NormalizedNode> memo(plan.nodes.size());

        std::function<NormalizedNode(std::uint32_t)> visit =
            [&](std::uint32_t old_id) -> NormalizedNode {
            if (old_id >= plan.nodes.size()) {
                error(diag::kInvalidCoercion, "coercion child plan id is out of range", range);
                return {};
            }
            if (color[old_id] == 1) {
                error(diag::kInvalidCoercion, "coercion plan contains a cycle", range);
                return {};
            }
            if (color[old_id] == 2) {
                return memo[old_id];
            }
            color[old_id] = 1;
            const AdjustmentNode &input = plan.nodes[old_id];
            const auto source = intern_plan_type(input.source, "node source type");
            const auto result = intern_plan_type(input.target, "node result type");
            if (!source.has_value() || !result.has_value()) {
                color[old_id] = 2;
                return {};
            }

            std::vector<CoreCoercionOp> ops;
            ops.reserve(input.ops.size());
            for (const AdjustmentOp &op : input.ops) {
                if (op.kind == AdjustmentOpKind::VariantToEnum) {
                    if (input.ops.size() != 1 || op.arg_index != 0 || op.child != UINT32_MAX ||
                        !(*source == *result)) {
                        error(diag::kInvalidCoercion,
                              "VariantToEnum can only erase when its Core endpoint types are equal",
                              range);
                        color[old_id] = 2;
                        return {};
                    }
                    continue;
                }

                CoreCoercionOp core_op;
                core_op.arg_index = op.arg_index;
                switch (op.kind) {
                case AdjustmentOpKind::IntWiden:
                    core_op.kind = CoreCoercionOpKind::IntWiden;
                    break;
                case AdjustmentOpKind::StringWiden:
                    core_op.kind = CoreCoercionOpKind::StringWiden;
                    break;
                case AdjustmentOpKind::CapacityWiden:
                    core_op.kind = CoreCoercionOpKind::CapacityWiden;
                    break;
                case AdjustmentOpKind::TypeArg:
                    core_op.kind = CoreCoercionOpKind::TypeArg;
                    break;
                case AdjustmentOpKind::FnParam:
                    core_op.kind = CoreCoercionOpKind::FnParam;
                    break;
                case AdjustmentOpKind::FnReturn:
                    core_op.kind = CoreCoercionOpKind::FnReturn;
                    break;
                case AdjustmentOpKind::VariantToEnum:
                case AdjustmentOpKind::ToAny:
                case AdjustmentOpKind::FromNever:
                    break; // handled before endpoint interning / above
                default:
                    error(diag::kInvalidCoercion,
                          "coercion plan carries an unknown adjustment operation kind",
                          range);
                    color[old_id] = 2;
                    return {};
                }

                const bool projected = op.kind == AdjustmentOpKind::TypeArg ||
                                       op.kind == AdjustmentOpKind::FnParam ||
                                       op.kind == AdjustmentOpKind::FnReturn;
                if (projected) {
                    if (op.child == UINT32_MAX) {
                        error(diag::kInvalidCoercion,
                              "projected coercion operation is missing its child proof",
                              range);
                        color[old_id] = 2;
                        return {};
                    }
                    const NormalizedNode child = visit(op.child);
                    if (!child.ok) {
                        color[old_id] = 2;
                        return {};
                    }
                    if (!child.id.has_value()) {
                        continue; // identity child removes the projected operation
                    }
                    core_op.child = CoreCoercionPlanId{*child.id};
                } else if (op.child != UINT32_MAX) {
                    error(diag::kInvalidCoercion,
                          "leaf coercion operation unexpectedly carries a child proof",
                          range);
                    color[old_id] = 2;
                    return {};
                }
                ops.push_back(core_op);
            }

            NormalizedNode result_node{
                .ok = true,
                .source = *source,
                .result = *result,
                .id = std::nullopt,
            };
            if (ops.empty()) {
                if (!(*source == *result)) {
                    error(diag::kInvalidCoercion,
                          "coercion node normalizes to identity but its Core endpoints differ",
                          range);
                    color[old_id] = 2;
                    return {};
                }
            } else {
                if (out.nodes.size() >= CoreCoercionPlanId::kInvalid) {
                    error(diag::kInvalidCoercion,
                          "temporary coercion-plan arena exceeded its 32-bit id space",
                          range);
                    color[old_id] = 2;
                    return {};
                }
                result_node.id = static_cast<std::uint32_t>(out.nodes.size());
                out.nodes.push_back(CoreCoercionPlanNode{*source, *result, std::move(ops)});
            }
            color[old_id] = 2;
            memo[old_id] = result_node;
            return result_node;
        };

        const NormalizedNode root = visit(plan.root);
        if (!root.ok) {
            return std::nullopt;
        }
        if (!(root.source == out.source) || !(root.result == out.result)) {
            error(diag::kInvalidCoercion,
                  "coercion root endpoints do not match the plan boundary",
                  range);
            return std::nullopt;
        }
        out.root = root.id;
        return out;
    }
    CoreBodyStorageRef storage_;
    const CapabilityIndex &caps_;
    const TypeEnv &types_;
    RootPolicy policy_;
    const ValueTypeInterner &interner_;
    const std::vector<CoreValueType> &value_type_pool_;
    std::vector<CoreLowerDiagnostic> &diags_;
    // RFC 0026 FB-1: when present, a free `fn` / method call whose callee
    // resolves to a body-bearing Fn instance lowers to a direct CoreCallExpr.
    const FnCallResolver *fn_calls_{nullptr};
    // RFC 0026 FB-3a2: when present, a LambdaExpr is lifted through this bridge
    // into a CoreFnDecl and its construction site becomes a CoreClosureExpr.
    LambdaLiftBridge *lifter_{nullptr};
    // Structural node interner (mints the lifted fn's CoreVtFn / CoreVtClosure
    // types without a source TypeRef). Paired with `lifter_`.
    const StructuralInternerFn *structural_interner_{nullptr};
    std::unordered_map<std::string, LocalBinding> scope_;
};

/// Statement / region lowering shared by flow handlers and outlined fn bodies
/// (CORE-FNBODY-DESIGN §2.2 — ONE region lowering implementation, reused by the
/// third body owner). Owns an `ExprLowerer<Policy>&` for expressions / patterns
/// and lowers blocks into `CoreRegion`s. `lower_goto` is supplied by the caller
/// (only flow handlers have states); a fn body never reaches it (a goto there
/// is fail-closed).
template <class Policy, class GotoLowerer> class BodyRegionLowerer {
  public:
    BodyRegionLowerer(ExprLowerer<Policy> &ex, GotoLowerer lower_goto)
        : ex_(ex), lower_goto_(std::move(lower_goto)) {}

    using LocalBinding = typename ExprLowerer<Policy>::LocalBinding;
    using ArmBindings = typename ExprLowerer<Policy>::ArmBindings;

    [[nodiscard]] CoreRegion lower_block(const Block &block) {
        CoreRegion region;
        for (const StatementPtr &stmt : block.statements) {
            if (stmt) {
                lower_statement(*stmt, region);
            }
        }
        return region;
    }

    void lower_statement(const Statement &stmt, CoreRegion &region) {
        // RFC 0027 P6/P7/P8 (KR6.13-P7): one handler per StatementNode
        // alternative, generated from stmt_nodes.def. Statements without an
        // execution-layer form in this slice (assert / requires / unwrap /
        // unreachable) name LOWER_STMT_DEFERRED: because dropping them would
        // change execution behaviour (e.g. `assert(false)` must not silently
        // become a no-op), that is an ERROR marking the program non-executable —
        // never a silent Warning drop. No generic catch-all: a new statement
        // alternative is a COMPILE ERROR here until it is routed.
#define LOWER_STMT_DEFERRED(Name)                                                                  \
    [&](const Name &) {                                                                            \
        ex_.error(diag::kUnloweredStatement,                                                       \
                  "statement kind is not yet lowered to Core-IR; the "                             \
                  "program cannot be executed until this slice lands",                             \
                  stmt.source_range);                                                              \
    },
#define LOWER_STMT_Let(Name) [&](const Name &s) { lower_let(s, region); },
#define LOWER_STMT_Assign(Name)                                                                    \
    [&](const Name &s) { lower_assign(s, stmt.source_range, region); },
#define LOWER_STMT_Expr(Name)                                                                      \
    [&](const Name &s) { static_cast<void>(ex_.lower_value(s.expr, region)); },
#define LOWER_STMT_If(Name) [&](const Name &s) { lower_if(s, stmt.source_range, region); },
#define LOWER_STMT_IfLet(Name) [&](const Name &s) { lower_if_let(s, stmt.source_range, region); },
#define LOWER_STMT_Goto(Name)                                                                      \
    [&](const Name &s) { lower_goto_(s, stmt.source_range, region); },
#define LOWER_STMT_Return(Name) [&](const Name &s) { lower_return(s, stmt.source_range, region); },
#define LOWER_STMT_LetStatement(Name) LOWER_STMT_Let(Name)
#define LOWER_STMT_AssignStatement(Name) LOWER_STMT_Assign(Name)
#define LOWER_STMT_ExprStatement(Name) LOWER_STMT_Expr(Name)
#define LOWER_STMT_IfStatement(Name) LOWER_STMT_If(Name)
#define LOWER_STMT_IfLetStatement(Name) LOWER_STMT_IfLet(Name)
#define LOWER_STMT_GotoStatement(Name) LOWER_STMT_Goto(Name)
#define LOWER_STMT_ReturnStatement(Name) LOWER_STMT_Return(Name)
#define LOWER_STMT_AssertStatement(Name) LOWER_STMT_DEFERRED(Name)
#define LOWER_STMT_UnwrapStatement(Name) LOWER_STMT_DEFERRED(Name)
#define LOWER_STMT_RequiresStatement(Name) LOWER_STMT_DEFERRED(Name)
#define LOWER_STMT_UnreachableStatement(Name) LOWER_STMT_DEFERRED(Name)
#define HANDLE_STMT_NODE(Name, Wire) LOWER_STMT_##Name(Name)
        std::visit(
            Overloaded{
#include "ahfl/compiler/ir/stmt_nodes.def"
            },
            stmt.node);
#undef HANDLE_STMT_NODE
#undef LOWER_STMT_Let
#undef LOWER_STMT_Assign
#undef LOWER_STMT_Expr
#undef LOWER_STMT_If
#undef LOWER_STMT_IfLet
#undef LOWER_STMT_Goto
#undef LOWER_STMT_Return
#undef LOWER_STMT_LetStatement
#undef LOWER_STMT_AssignStatement
#undef LOWER_STMT_ExprStatement
#undef LOWER_STMT_IfStatement
#undef LOWER_STMT_IfLetStatement
#undef LOWER_STMT_GotoStatement
#undef LOWER_STMT_ReturnStatement
#undef LOWER_STMT_AssertStatement
#undef LOWER_STMT_UnwrapStatement
#undef LOWER_STMT_RequiresStatement
#undef LOWER_STMT_UnreachableStatement
#undef LOWER_STMT_DEFERRED
    }

    void lower_let(const LetStatement &s, CoreRegion &region) {
        const SourceRangeOpt range =
            s.initializer.get() != nullptr ? s.initializer.get()->source_range : std::nullopt;
        if (s.adjustment.has_value() &&
            ex_.reject_nonmaterializable_adjustment(*s.adjustment, range)) {
            return;
        }
        const CoreValueId initializer = ex_.lower_value(s.initializer, region);
        const CoreValueId value = ex_.lower_let_boundary(s, initializer, range, region);
        ex_.scope()[s.name] = LocalBinding{value, ex_.value_type_of(value)};
    }

    void lower_assign(const AssignStatement &s, SourceRangeOpt range, CoreRegion &region) {
        const CoreValueId value = ex_.lower_value(s.value, region);
        CorePlace place;
        const auto rr = ex_.resolve_path_root(s.target);
        place.root = rr.external_root;
        place.root_name = s.target.root_name;
        place.members = s.target.members;
        place.root_type = rr.root_type;
        if (!s.target.members.empty()) {
            const std::optional<CoreTypeId> root_type =
                rr.root_type.value == CoreTypeId::kInvalid ? std::nullopt
                                                           : std::optional<CoreTypeId>{rr.root_type};
            ex_.resolve_member_chain(root_type, s.target.members, s.target.root_name, range,
                                     place.projection, place.projection_resolved);
        }
        region.statements.push_back(
            CoreStmt{CoreStoreStmt{std::move(place), value}, std::move(range)});
    }

    void lower_if(const IfStatement &s, SourceRangeOpt range, CoreRegion &region) {
        const CoreValueId cond = ex_.lower_value(s.condition, region);
        CoreIfStmt node;
        node.condition = cond;
        const auto outer_scope = ex_.scope();
        if (s.then_block) {
            node.then_region = std::make_unique<CoreRegion>(lower_block(*s.then_block));
        } else {
            node.then_region = std::make_unique<CoreRegion>();
        }
        ex_.scope() = outer_scope;
        if (s.else_block) {
            node.else_region = std::make_unique<CoreRegion>(lower_block(*s.else_block));
        }
        ex_.scope() = outer_scope;
        region.statements.push_back(CoreStmt{std::move(node), std::move(range)});
    }

    void lower_return(const ReturnStatement &s, SourceRangeOpt range, CoreRegion &region) {
        CoreReturnStmt node;
        if (s.value.ptr != nullptr) {
            node.has_value = true;
            node.value = ex_.lower_value(s.value, region);
        }
        region.statements.push_back(CoreStmt{std::move(node), std::move(range)});
    }

    // Statement-position if-let: one pattern arm (yields nothing) with the
    // then-block as body; the fallback is the else block (or a trap when absent).
    void lower_if_let(const IfLetStatement &s, SourceRangeOpt range, CoreRegion &region) {
        const CoreValueId scrutinee = ex_.lower_value(s.scrutinee, region);
        CoreMatchStmt stmt;
        stmt.scrutinee = scrutinee;
        stmt.has_result = false;
        bool ok = true;
        ArmBindings bindings;
        ex_.collect_arm_bindings(s.pattern, bindings);
        CoreMatchArm arm;
        arm.pattern = ex_.lower_pattern(s.pattern, bindings, range, ok);
        arm.body = std::make_unique<CoreRegion>();
        const auto outer_scope = ex_.scope();
        if (s.then_block) {
            *arm.body = lower_block_scoped(*s.then_block, bindings);
        }
        ex_.scope() = outer_scope;
        ex_.seal_statement_arm(*arm.body, range);
        arm.bindings = std::move(bindings.values);
        stmt.arms.push_back(std::move(arm));
        stmt.fallback_region = std::make_unique<CoreRegion>();
        if (s.else_block) {
            *stmt.fallback_region = lower_block(*s.else_block);
        }
        ex_.scope() = outer_scope;
        ex_.seal_statement_arm(*stmt.fallback_region, range);
        region.statements.push_back(CoreStmt{std::move(stmt), std::move(range)});
        static_cast<void>(ok);
    }

    [[nodiscard]] CoreRegion lower_block_scoped(const Block &block, const ArmBindings &bindings) {
        const auto outer = ex_.scope();
        for (std::uint32_t i = 0; i < bindings.names.size(); ++i) {
            ex_.scope()[bindings.names[i]] =
                LocalBinding{bindings.values[i].value, bindings.value_types[i]};
        }
        CoreRegion region = lower_block(block);
        ex_.scope() = outer;
        return region;
    }

  private:
    ExprLowerer<Policy> &ex_;
    GotoLowerer lower_goto_;
};

/// Lowers one flow's handler bodies into ANF. Owns a `CoreFlowDecl` and composes
/// an `ExprLowerer<FlowRootPolicy>` (which owns the per-flow expr / value / pattern
/// arenas and the per-state local scope) for all expression/pattern/match work;
/// this class adds only the flow's statement/region lowering on top and resolves
/// `goto` targets against the agent's state index. Accumulates diagnostics.
class FlowLowerer {
  public:
    FlowLowerer(CoreFlowDecl &flow, const CapabilityIndex &caps, const StateIndex &states,
                const TypeEnv &types, CoreTypeId input_type, CoreTypeId context_type,
                const ValueTypeInterner &interner, const std::vector<CoreValueType> &value_type_pool,
                std::vector<CoreLowerDiagnostic> &diags,
                const FnCallResolver *fn_calls = nullptr,
                LambdaLiftBridge *lifter = nullptr,
                const StructuralInternerFn *structural_interner = nullptr)
        : ex_(CoreBodyStorageRef{flow.storage},
              caps, types, FlowRootPolicy{input_type, context_type}, interner, value_type_pool,
              diags, fn_calls, lifter, structural_interner),
          body_(ex_,
                [this](const GotoStatement &s, SourceRangeOpt range, CoreRegion &region) {
                    lower_goto(s, range, region);
                }),
          flow_(flow), states_(states) {}

    void lower_handler(const StateHandler &handler, CoreStateId state_id) {
        CoreFlowState core_state;
        core_state.state = state_id;
        core_state.state_name = handler.state_name;
        core_state.policy = lower_policy(handler.policy);
        ex_.scope().clear();
        core_state.body = lower_block(handler.body);
        flow_.states.push_back(std::move(core_state));
    }

  private:
    using LocalBinding = ExprLowerer<FlowRootPolicy>::LocalBinding;
    using ArmBindings = ExprLowerer<FlowRootPolicy>::ArmBindings;

    [[nodiscard]] CoreStatePolicy lower_policy(const std::vector<StatePolicyItem> &policy) {
        CoreStatePolicy out;
        for (const StatePolicyItem &item : policy) {
            std::visit(Overloaded{
                           [&](const RetryPolicy &r) { out.retry_limit = r.limit; },
                           [&](const RetryOnPolicy &r) { out.retry_on = r.targets; },
                           [&](const TimeoutPolicy &t) { out.timeout = t.duration; },
                       },
                       item);
        }
        return out;
    }

    // --- region / statement lowering is delegated to BodyRegionLowerer ---
    [[nodiscard]] CoreRegion lower_block(const Block &block) {
        return body_.lower_block(block);
    }

    void lower_goto(const GotoStatement &s, SourceRangeOpt range, CoreRegion &region) {
        CoreGotoStmt node;
        node.target_name = s.target_state;
        if (const auto id = states_.lookup(s.target_state)) {
            node.target = *id;
        } else {
            ex_.error(diag::kUnknownGotoTarget,
                      "goto targets unknown state '" + s.target_state + "'", range);
        }
        region.statements.push_back(CoreStmt{std::move(node), std::move(range)});
    }

    ExprLowerer<FlowRootPolicy> ex_;
    BodyRegionLowerer<FlowRootPolicy,
                     std::function<void(const GotoStatement &, SourceRangeOpt, CoreRegion &)>>
        body_;
    CoreFlowDecl &flow_;
    const StateIndex &states_;
};

// Lowers one AHFL-IR WorkflowDecl into a CoreWorkflowDecl (RFC 0026 KR6.4). A
// workflow is a DAG of agent invocations: each node computes its input from an
// ANF `input_region` that runs once its dependencies are ready, and a separate
// `return_region` computes the workflow output after the DAG completes. Two
// passes (Codex-locked): Pass A assigns each node a typed CoreWorkflowNodeId
// (== declaration index), builds a UNIQUE node-name table, and resolves each
// node's target agent + `after` dependency edges by identity; Pass B lowers the
// node input / return expressions through an ExprLowerer<WorkflowRootPolicy> that
// classifies `input` / upstream-node-output path roots. safety / liveness are
// erased (verification is finished at the AhflIr layer).
class WorkflowLowerer {
  public:
    WorkflowLowerer(CoreWorkflowDecl &wf, const CapabilityIndex &caps, const TypeEnv &types,
                    const std::vector<CoreAgentDecl> &agents,
                    const std::unordered_map<std::size_t, CoreAgentId> &agent_by_id,
                    const std::unordered_map<std::string, CoreAgentId> &agent_by_name,
                    const ValueTypeInterner &interner,
                    const std::vector<CoreValueType> &value_type_pool,
                    std::vector<CoreLowerDiagnostic> &diags,
                    const FnCallResolver *fn_calls = nullptr,
                    LambdaLiftBridge *lifter = nullptr,
                    const StructuralInternerFn *structural_interner = nullptr)
        : wf_(wf), caps_(caps), types_(types), agents_(agents), agent_by_id_(agent_by_id),
          agent_by_name_(agent_by_name), interner_(interner), value_type_pool_(value_type_pool),
          diags_(diags), fn_calls_(fn_calls), lifter_(lifter),
          structural_interner_(structural_interner) {}

    void lower(const WorkflowDecl &decl) {
        wf_.name = decl.name;
        wf_.symbol_ref = decl.symbol_ref;
        wf_.input_type = types_.type_id_of(decl.input_type_ref).value_or(CoreTypeId{});
        wf_.output_type = types_.type_id_of(decl.output_type_ref).value_or(CoreTypeId{});

        // Pass A: assign node ids (== declaration index), build the unique node
        // name table + resolve target/after. A DUPLICATE source node name is a
        // fail-closed lowering error (never a silent map first-wins).
        wf_.nodes.reserve(decl.nodes.size());
        for (std::uint32_t i = 0; i < decl.nodes.size(); ++i) {
            const WorkflowNode &src = decl.nodes[i];
            const CoreWorkflowNodeId id{i};
            CoreWorkflowNode node;
            node.id = id;
            node.node_name = src.name;
            node.target_ref = src.target_ref;
            // target_instance stays kInvalid here — the workflow-invocation LINK
            // pass (in lower_ahfl_to_core, after the instance table is built)
            // resolves each node to its concrete Agent CoreInstanceId. We still
            // resolve the NOMINAL agent now, ONLY to type this node's output for
            // WorkflowRootPolicy (a lowering-only side fact, not a Core field).
            const auto target = resolve_agent(src.target_ref);
            if (!target) {
                error(diag::kUnresolvedWorkflowTarget,
                      "workflow '" + wf_.name + "' node '" + src.name +
                          "' targets agent '" + agent_display(src.target_ref) +
                          "' which could not be resolved to a declared agent",
                      src.source_range);
            }
            // Record the node-name -> {id, target output type} entry BEFORE Pass
            // B, and reject a duplicate name fail-closed.
            const CoreTypeId out_type =
                target ? agents_[target->value].output_type : CoreTypeId{};
            if (!node_index_.emplace(src.name, WorkflowRootPolicy::NodeOutput{id, out_type}).second) {
                error(diag::kUnknownWorkflowDependency,
                      "workflow '" + wf_.name + "' declares more than one node named '" +
                          src.name + "'; node names must be unique",
                      src.source_range);
            }
            wf_.nodes.push_back(std::move(node));
        }
        // Resolve `after` edges by node name -> typed CoreWorkflowNodeId. An
        // unknown dependency name is fail-closed. (Self / cycle / bounds are the
        // verifier's job; here we only resolve identity.)
        for (std::uint32_t i = 0; i < decl.nodes.size(); ++i) {
            const WorkflowNode &src = decl.nodes[i];
            for (const std::string &dep : src.after) {
                const auto it = node_index_.find(dep);
                if (it == node_index_.end()) {
                    error(diag::kUnknownWorkflowDependency,
                          "workflow '" + wf_.name + "' node '" + src.name +
                              "' declares dependency on unknown node '" + dep + "'",
                          src.source_range);
                    continue;
                }
                wf_.nodes[i].after.push_back(it->second.id);
            }
        }

        // Pass B: lower each node's input expression + the return expression.
        // Each region is ANF and ends by yielding its single value.
        for (std::uint32_t i = 0; i < decl.nodes.size(); ++i) {
            wf_.nodes[i].input_region = std::make_unique<CoreRegion>();
            lower_value_region(decl.nodes[i].input, *wf_.nodes[i].input_region,
                               decl.nodes[i].source_range);
        }
        wf_.return_region = std::make_unique<CoreRegion>();
        lower_value_region(decl.return_value, *wf_.return_region, decl.provenance.source_range);
    }

  private:
    void error(std::string_view code, std::string message, SourceRangeOpt range) {
        diags_.push_back(CoreLowerDiagnostic{CoreDiagnosticSeverity::Error, std::string(code),
                                             std::move(message), std::move(range)});
    }

    /// Lower a single workflow value expression (node input / return) into `region`
    /// as ANF, appending a value-yield of the result. Uses a fresh
    /// ExprLowerer<WorkflowRootPolicy> over the workflow's shared arenas.
    void lower_value_region(const ExprRef &expr, CoreRegion &region, SourceRangeOpt range) {
        ExprLowerer<WorkflowRootPolicy> ex(
            CoreBodyStorageRef{wf_.storage}, caps_,
            types_, WorkflowRootPolicy{wf_.input_type, &node_index_}, interner_, value_type_pool_,
            diags_, fn_calls_, lifter_, structural_interner_);
        const CoreValueId value = ex.lower_value(expr, region);
        region.statements.push_back(CoreStmt{CoreYieldStmt{true, value}, range});
    }

    [[nodiscard]] std::optional<CoreAgentId> resolve_agent(const SymbolRef &ref) const {
        if (ref.id.has_value()) {
            if (const auto it = agent_by_id_.find(*ref.id); it != agent_by_id_.end()) {
                return it->second;
            }
        }
        if (!ref.canonical_name.empty()) {
            if (const auto it = agent_by_name_.find(ref.canonical_name); it != agent_by_name_.end()) {
                return it->second;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] static std::string agent_display(const SymbolRef &ref) {
        return ref.local_name.empty() ? ref.canonical_name : ref.local_name;
    }

    CoreWorkflowDecl &wf_;
    const CapabilityIndex &caps_;
    const TypeEnv &types_;
    const std::vector<CoreAgentDecl> &agents_;
    const std::unordered_map<std::size_t, CoreAgentId> &agent_by_id_;
    const std::unordered_map<std::string, CoreAgentId> &agent_by_name_;
    const ValueTypeInterner &interner_;
    const std::vector<CoreValueType> &value_type_pool_;
    std::vector<CoreLowerDiagnostic> &diags_;
    // node name -> {typed id, target agent output type}. Built in Pass A, consumed
    // by Pass B's WorkflowRootPolicy to classify node-output path roots.
    std::unordered_map<std::string, WorkflowRootPolicy::NodeOutput> node_index_;
    const FnCallResolver *fn_calls_{nullptr};
    LambdaLiftBridge *lifter_{nullptr};
    const StructuralInternerFn *structural_interner_{nullptr};
};

/// Lowers one monomorphized pure fn body into a CoreFnDecl
/// (CORE-FNBODY-DESIGN §2.1/§2.3, RFC 0026 FB-1). The body owns a private SSA
/// domain (its own CoreBodyStorage); the parameters are PRE-BOUND into it with
/// their concrete substituted types (the dense value_types table follows the
/// same P4-B rule as flow/workflow bodies). The body completes via return; goto
/// is impossible inside a fn (the region lowerer's goto callback fail-closes).
class FnBodyLowerer {
  public:
    FnBodyLowerer(CoreFnDecl &fn, const FnDecl &source, const CapabilityIndex &caps,
                  const TypeEnv &types, const ValueTypeInterner &interner,
                  const std::vector<CoreValueType> &value_type_pool,
                  const FnCallResolver &fn_calls,
                  std::vector<CoreLowerDiagnostic> &diags,
                  LambdaLiftBridge *lifter = nullptr,
                  const StructuralInternerFn *structural_interner = nullptr,
                  FrameCaptureTable *frame_captures = nullptr)
        : ex_(CoreBodyStorageRef{fn.storage}, caps, types, FnRootPolicy{frame_captures}, interner,
              value_type_pool, diags, &fn_calls, lifter, structural_interner),
          body_(ex_,
                [this](const GotoStatement &, SourceRangeOpt range, CoreRegion &) {
                    ex_.error(diag::kFnBodyUnlowered,
                              "a goto statement is illegal inside a fn body", range);
                }),
          fn_(fn), source_(source) {}

    void lower() {
        if (!source_.has_body || source_.body == nullptr) {
            ex_.error(diag::kFnBodyUnlowered,
                      "fn '" + source_.name + "' instance has no body to lower",
                      source_.provenance.source_range);
            return;
        }
        // Pre-bind the parameters in declaration order (design §2.3).
        fn_.params.reserve(source_.params.size());
        for (const ParamDecl &param : source_.params) {
            const CoreValueTypeId ty =
                ex_.intern_value_type(param.type_ref, param.source_range);
            const CoreValueId value = ex_.fresh_value(ty);
            fn_.params.push_back(value);
            ex_.scope()[param.name] =
                ExprLowerer<FnRootPolicy>::LocalBinding{value, ty};
        }
        fn_.body = body_.lower_block(*source_.body);
    }

    // FB-3a2: lower a lifted lambda BODY (an expression wrapped in one value-
    // bearing return). The caller supplies the FnRootPolicy's frame table (its
    // ADDRESS is already wired into ex_'s policy); this method allocates the
    // dense logical-param + env-slot SSA values, populates BOTH the in-body
    // scope (local captures bound by name) and the frame redirect table (frame
    // captures keyed by path) BEFORE the body expression is lowered, so a frame
    // read inside the body resolves to its slot via the policy.
    void lower_lifted_body(const ExprRef &body_expr, SourceRangeOpt range,
                           const std::vector<std::pair<std::string, CoreValueTypeId>> &param_names,
                           const std::vector<LambdaLiftBridge::ResolvedCapture> &captures,
                           FrameCaptureTable &frame_captures) {
        fn_.params.reserve(param_names.size());
        for (const auto &[name, ty] : param_names) {
            const CoreValueId value = ex_.fresh_value(ty);
            fn_.params.push_back(value);
            ex_.scope()[name] = ExprLowerer<FnRootPolicy>::LocalBinding{value, ty};
        }
        fn_.captures.reserve(captures.size());
        fn_.env_bindings.reserve(captures.size());
        for (const auto &cap : captures) {
            const CoreValueId slot = ex_.fresh_value(cap.value_type);
            fn_.captures.push_back(cap.value_type);
            fn_.env_bindings.push_back(slot);
            ex_.scope()[cap.name] =
                ExprLowerer<FnRootPolicy>::LocalBinding{slot, cap.value_type};
            if (cap.is_frame) {
                frame_captures[cap.frame] = LocalSsaBinding{slot, cap.value_type};
            }
        }
        CoreRegion region;
        if (body_expr.ptr != nullptr) {
            const CoreValueId value = ex_.lower_value(body_expr, region);
            region.statements.push_back(CoreStmt{CoreReturnStmt{true, value}, range});
        } else {
            ex_.error(diag::kFnBodyUnlowered, "a lifted lambda has no body expression", range);
        }
        fn_.body = std::move(region);
    }

  private:
    ExprLowerer<FnRootPolicy> ex_;
    BodyRegionLowerer<FnRootPolicy,
                     std::function<void(const GotoStatement &, SourceRangeOpt, CoreRegion &)>>
        body_;
    CoreFnDecl &fn_;
    const FnDecl &source_;
};

/// RFC 0026 FB-3a2 (CORE-FNBODY-DESIGN §3.1.1 D-LIFT): the concrete lambda-lift
/// driver. ONE instance is shared by every constructing body (flow / workflow /
/// fn), so a lambda — including one nested in an outlined fn body — is lifted by
/// the SAME deterministic path: one monomorphic Fn instance + CoreFnDecl per
/// construction site, its body lowered through the third body owner
/// (FnRootPolicy), and the fn id returned for the CoreClosureExpr. The
/// constructing ExprLowerer owns capture analysis + the construction node; this
/// driver owns the program fn/instance tables and the interners.
class LambdaLifter final : public LambdaLiftBridge {
  public:
    // The structural interner is OWNED BY VALUE, never held by reference. A
    // lifter is constructed inside the Pass-1.5 block but serves Pass 2 (flows)
    // and Pass 3 (workflows) long after that block's locals are gone; a
    // const-reference member bound to a block-local interner would dangle and
    // call a dead std::function while lifting a nested lambda from a handler
    // (stack-use-after-scope / SIGSEGV).
    LambdaLifter(CoreProgram &program, const CapabilityIndex &caps, const TypeEnv &types,
                 const ValueTypeInterner &interner, StructuralInternerFn structural,
                 const FnCallResolver &fn_calls,
                 std::vector<CoreLowerDiagnostic> &diags)
        : program_(program), caps_(caps), types_(types), interner_(interner),
          structural_(std::move(structural)), fn_calls_(fn_calls), diags_(diags) {}

    [[nodiscard]] std::optional<CoreFnId>
    lift(const LambdaExpr &lambda, SourceRangeOpt range,
         const std::vector<CoreValueTypeId> &param_types, CoreValueTypeId return_type,
         std::vector<ResolvedCapture> captures) override {
        const CoreInstanceId instance_id{
            static_cast<std::uint32_t>(program_.instances.size())};
        const CoreFnId fn_id{static_cast<std::uint32_t>(program_.fns.size())};
        const std::string key = lifted_key(fn_id.value);

        CoreInstanceDecl inst;
        inst.id = instance_id;
        inst.instance_key = key;
        inst.origin = ir::SymbolRef{ir::SymbolRefKind::Function, key, key, "", std::nullopt};
        inst.payload = CoreFnInstance{fn_id};
        program_.instances.push_back(std::move(inst));

        CoreFnDecl fn_decl;
        fn_decl.id = fn_id;
        fn_decl.instance = instance_id;
        fn_decl.origin = ir::SymbolRef{ir::SymbolRefKind::Function, key, key, "", std::nullopt};
        fn_decl.name = key;
        fn_decl.source_range = range;
        program_.fns.push_back(std::move(fn_decl));

        // Publish a direct-call resolver entry BEFORE the body is lowered, so a
        // legal by-name / nested reference to the lifted fn resolves. A lifted
        // lambda is Pure (the typechecker enforces PureOnly closures).
        FnCallResolver::Entry entry;
        entry.resolution.instance = instance_id;
        entry.resolution.has_body = true;
        entry.resolution.effect = ir::FnEffectKind::Pure;
        fn_calls_.add(key, std::nullopt, key, entry);

        // The frame table's ADDRESS is wired into the FnRootPolicy now; its
        // contents are populated by lower_lifted_body before the body runs.
        FrameCaptureTable frame_captures;

        // A synthetic, never-read FnDecl anchors the existing lowerer ctor (the
        // lifted body is an expression, so its block/params are never touched).
        FnDecl anchor;
        anchor.name = key;
        FnBodyLowerer lowerer(program_.fns[fn_id.value], anchor, caps_, types_, interner_,
                              program_.value_types, fn_calls_, diags_, this, &structural_,
                              &frame_captures);

        std::vector<std::pair<std::string, CoreValueTypeId>> param_names;
        param_names.reserve(lambda.params.size());
        for (std::size_t i = 0; i < lambda.params.size() && i < param_types.size(); ++i) {
            param_names.emplace_back(lambda.params[i], param_types[i]);
        }
        lowerer.lower_lifted_body(lambda.body, range, param_names, captures, frame_captures);
        static_cast<void>(return_type); // return-type agreement is enforced by the verifier

        return fn_id;
    }

  private:
    // Deterministic synthetic instance key for a lifted construction site
    // (display / dispatch only; identity is the CoreFnId index). The fn-table
    // ordinal makes it unique and stable across lowering order.
    [[nodiscard]] static std::string lifted_key(std::uint32_t ordinal) {
        return "_lambda_" + std::to_string(ordinal);
    }

    CoreProgram &program_;
    const CapabilityIndex &caps_;
    const TypeEnv &types_;
    const ValueTypeInterner &interner_;
    StructuralInternerFn structural_; // owned: outlives the Pass-1.5 construction block
    const FnCallResolver &fn_calls_;
    std::vector<CoreLowerDiagnostic> &diags_;
};

} // namespace

// RFC 0026 FB-4 fix-forward: reconcile the ordered-vs-pure shape of every DIRECT
// fn call with the closure-aware structural authority. Lowering emits every
// resolved direct fn call as an ordered CoreCallStmt (the source scanner
// cannot see an effect routed through a FnT parameter / closure value). This
// pass downgrades exactly the statements whose callee analyze_fn_effects —
// which DOES follow CoreCallClosureExpr points-to edges — classifies PURE back
// to a CoreLetStmt-bound CoreCallExpr, leaving genuinely effectful callees
// ordered. The choice is shape-independent (both nodes are invocation edges),
// so one pass suffices; running it BEFORE verify_core_program guarantees the
// pure-vs-effectful call-site discipline always sees the authoritative shape.
[[nodiscard]] static std::optional<std::uint32_t>
call_stmt_body_fn(const CoreProgram &program, CoreInstanceId instance) {
    if (instance.value == CoreInstanceId::kInvalid ||
        instance.value >= program.instances.size()) {
        return std::nullopt;
    }
    const auto *payload = std::get_if<CoreFnInstance>(&program.instances[instance.value].payload);
    if (payload == nullptr || payload->body.value == CoreFnId::kInvalid ||
        payload->body.value >= program.fns.size()) {
        return std::nullopt;
    }
    return payload->body.value;
}

static void reconcile_region_call_shapes(const CoreProgram &program,
                                         const std::vector<bool> &effectful,
                                         const CoreRegion &region,
                                         CoreBodyStorage &storage) {
    std::vector<CoreRegion *> pending{const_cast<CoreRegion *>(&region)};
    while (!pending.empty()) {
        CoreRegion *r = pending.back();
        pending.pop_back();
        for (CoreStmt &stmt : r->statements) {
            if (const auto *call = std::get_if<CoreCallStmt>(&stmt.node)) {
                const std::optional<std::uint32_t> body =
                    call_stmt_body_fn(program, call->callee);
                const bool is_effectful = body.has_value() && *body < effectful.size() &&
                                          effectful[*body];
                if (!is_effectful) {
                    // Pure callee: lower the ordered statement to a pure
                    // CoreCallExpr bound by the SAME SSA result value.
                    const CoreValueTypeId result_ty =
                        call->result.value < storage.value_types.size()
                            ? storage.value_types[call->result.value]
                            : CoreValueTypeId{};
                    const CoreExprId expr_id{static_cast<std::uint32_t>(storage.exprs.size())};
                    storage.exprs.push_back(
                        CoreExpr{CoreCallExpr{call->callee, call->args},
                                 stmt.source_range, result_ty});
                    stmt.node = CoreLetStmt{call->result, expr_id};
                }
            }
            if (auto *branch = std::get_if<CoreIfStmt>(&stmt.node)) {
                if (branch->then_region != nullptr) {
                    pending.push_back(branch->then_region.get());
                }
                if (branch->else_region != nullptr) {
                    pending.push_back(branch->else_region.get());
                }
            } else if (auto *match = std::get_if<CoreMatchStmt>(&stmt.node)) {
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
}

static void reconcile_fn_call_shapes(CoreProgram &program) {
    if (program.fns.empty()) {
        return;
    }
    const FnEffectAnalysis effects = analyze_fn_effects(program);
    for (CoreFnDecl &fn : program.fns) {
        reconcile_region_call_shapes(program, effects.effectful, fn.body, fn.storage);
    }
    for (CoreFlowDecl &flow : program.flows) {
        for (CoreFlowState &state : flow.states) {
            reconcile_region_call_shapes(program, effects.effectful, state.body, flow.storage);
        }
    }
    for (CoreWorkflowDecl &wf : program.workflows) {
        for (CoreWorkflowNode &node : wf.nodes) {
            if (node.input_region != nullptr) {
                reconcile_region_call_shapes(program, effects.effectful, *node.input_region,
                                             wf.storage);
            }
        }
        if (wf.return_region != nullptr) {
            reconcile_region_call_shapes(program, effects.effectful, *wf.return_region,
                                         wf.storage);
        }
    }
}


// RFC 0026 KR6.5 E4-B0-C2: the shared type-table pass. Registers every
// struct/enum + builtin stdlib nominal (source order), resolves field-nav
// nominal ids, and finalizes the P4-C declaration member templates into the
// supplied `shared_arena`. This is the SINGLE registration/fixup/finalize
// implementation: `lower_ahfl_to_core` runs it in-place for full lowering, and
// `build_core_type_environment` runs it standalone for the wire-schema migration
// projector. It lowers NO capability/agent/flow/workflow body. The returned
// `TypeEnv` binds references to `core.types` / `diags`, which must outlive it.
// `static` (internal linkage): it returns an anonymous-namespace `TypeEnv`, so it
// must not have external linkage.
[[nodiscard]] static TypeEnv populate_core_type_table(const AhflIr &ahfl_ir, CoreProgram &core,
                                                      std::vector<CoreLowerDiagnostic> &diags,
                                                      ValueTypeArena &shared_arena) {
    TypeEnv types(core.types, diags);
    for (const Decl &decl : ahfl_ir.declarations) {
        if (const auto *s = std::get_if<StructDecl>(&decl)) {
            types.add_struct(*s);
        } else if (const auto *e = std::get_if<EnumDecl>(&decl)) {
            types.add_enum(*e);
        }
    }
    types.add_builtins();
    // Now that every struct/enum (and the builtin stdlib enums) has a CoreTypeId,
    // resolve each struct field's recorded type NAME to a typed CoreTypeId so
    // member-projection chains advance struct-to-struct without re-querying
    // AHFL-IR. Must run after ALL types are registered (fields can reference a
    // type declared later in the module).
    types.fixup_field_nominal_types();
    // Finalize declaration member templates in source order before any
    // body/shell consumer can observe the type table.
    types.finalize_member_templates(shared_arena);
    return types;
}

CoreLowerResult lower_ahfl_to_core(const AhflIr &ahfl_ir) {
    CoreLowerResult result;
    CoreProgram &core = result.program;

    // Pass 1: type table (structs/enums + builtin stdlib enums). The one
    // program-global logical value-type arena is built here and REUSED for body
    // lowering below (never a second arena).
    ValueTypeArena shared_arena(core.value_types, core.types, [&core](const SymbolRef &r) {
        return resolve_nominal_strict(core.types, r);
    });
    TypeEnv types = populate_core_type_table(ahfl_ir, core, result.diagnostics, shared_arena);
    const ValueTypeInterner intern_value_type = [&shared_arena](const TypeRef &type,
                                                                std::string *reason) {
        return shared_arena.lower(type, reason);
    };

    // Capability table.
    CapabilityIndex cap_index;
    for (const Decl &decl : ahfl_ir.declarations) {
        if (const auto *cap = std::get_if<CapabilityDecl>(&decl)) {
            const auto id = CoreCapabilityId{static_cast<std::uint32_t>(core.capabilities.size())};
            core.capabilities.push_back(lower_capability(*cap, shared_arena, result.diagnostics));
            cap_index.add(cap->symbol_ref, id, cap->effect.kind);
        }
    }

    // Agent table + identity index (SymbolId / canonical name -> CoreAgentId),
    // so a flow's target resolves by identity rather than a linear string scan.
    std::unordered_map<std::size_t, CoreAgentId> agent_by_id;
    std::unordered_map<std::string, CoreAgentId> agent_by_name;
    for (const Decl &decl : ahfl_ir.declarations) {
        if (const auto *agent = std::get_if<AgentDecl>(&decl)) {
            const auto id = CoreAgentId{static_cast<std::uint32_t>(core.agents.size())};
            if (agent->symbol_ref.id.has_value()) {
                agent_by_id.emplace(*agent->symbol_ref.id, id);
            }
            if (!agent->symbol_ref.canonical_name.empty()) {
                agent_by_name.emplace(agent->symbol_ref.canonical_name, id);
            }
            core.agents.push_back(lower_agent(*agent, types, cap_index, result.diagnostics));
        }
    }

    // RFC 0026 P6-4: an agent's input / context / output struct types are the
    // ADDRESSABLE frame roots of the aggregate-memory convention, so each must be
    // interned into the one logical value-type arena even when no body expression
    // happens to reference it as a value (a `ctx.field = v` store reads the
    // context frame's P4-D layout, and the layout SSOT is keyed by value type).
    // Interning is idempotent (hash-cons), so a type already interned by a body
    // reference keeps its existing id and a program with no aggregate use is
    // unchanged. A Unit context has no struct to intern.
    for (const Decl &decl : ahfl_ir.declarations) {
        const auto *agent = std::get_if<AgentDecl>(&decl);
        if (agent == nullptr) {
            continue;
        }
        static_cast<void>(intern_value_type(agent->input_type_ref, nullptr));
        static_cast<void>(intern_value_type(agent->output_type_ref, nullptr));
        if (agent->context_type_ref.kind != TypeRefKind::Unit) {
            static_cast<void>(intern_value_type(agent->context_type_ref, nullptr));
        }
    }

    // Index every has_body FnDecl by origin symbol (id + canonical). Used by
    // the CORE-FNBODY-DESIGN §2.1 guarantee pass: for each body-bearing fn the
    // Fn-instance table gets EXACTLY one Fn-kind instance (a non-generic fn is
    // guaranteed its canonical empty-type-args instance), and the body table
    // gets one CoreFnDecl. Bodyless @builtin facades / prototypes are absent.
    struct BodyFn {
        const FnDecl *decl;
        bool body_lowered{false};
    };
    // Index every has_body FnDecl. The canonical declaration is indexed by
    // origin symbol (id + canonical); every FnDecl (canonical AND P2d-mangled
    // concrete instantiations) is indexed by its own `name` so an InstanceDecl
    // resolves to the EXACT concrete body — never to the generic TypeVar body.
    std::unordered_multimap<std::size_t, std::size_t> body_fn_by_id;
    std::unordered_multimap<std::string, std::size_t> body_fn_by_name;
    std::unordered_map<std::string, std::size_t> body_fn_by_key;
    std::vector<BodyFn> body_fns;
    for (const Decl &decl : ahfl_ir.declarations) {
        const auto *fn = std::get_if<FnDecl>(&decl);
        if (fn == nullptr || !fn->has_body) {
            continue;
        }
        const std::size_t idx = body_fns.size();
        body_fns.push_back(BodyFn{fn});
        body_fn_by_key.emplace(fn->name, idx);
        // Only the ORIGINAL (non-mangled) declaration carries the symbol index;
        // a mangled concrete instantiation's name is its instance key.
        if (fn->name.rfind("_inst_", 0) != 0) {
            if (fn->symbol_ref.id.has_value()) {
                body_fn_by_id.emplace(*fn->symbol_ref.id, idx);
            }
            if (!fn->symbol_ref.canonical_name.empty()) {
                body_fn_by_name.emplace(fn->symbol_ref.canonical_name, idx);
            }
        }
    }

    // Pass 1.5 (CORE-FNBODY-DESIGN §2.1): fn-instance guarantee + fn table.
    //
    //  * Consume every AHFL-IR Fn-kind InstanceDecl (produced by the typed
    //    lowerer's emit_instantiated_declarations) into CoreProgram.instances,
    //    in AHFL declaration order, exactly like Pass 6 does for the other
    //    kinds. The fn-instance set therefore stays the monomorphization
    //    registration unit.
    //  * GUARANTEE exactly one Fn-kind instance per has_body FnDecl. Non-generic
    //    has_body fns never receive an InstanceDecl from the frontend, so this
    //    pass synthesizes their canonical EMPTY type-args instance with a
    //    mangle_instance-empty-form key (the same rule the typed lowerer uses
    //    for the empty-args shape, never re-derived ad hoc).
    //  * Build CoreProgram.fns 1:1 with the body-bearing Fn instances and lower
    //    every body through the third body owner (FnRootPolicy), then publish
    //    the FnCallResolver flow/workflow bodies use to resolve direct calls.
    //
    // Generic fn bodies in the AHFL-IR carry substituted concrete types ONLY
    // when the P2d typed-body clone ran; the production pipeline runs it before
    // this lowerer, so a generic fn instance's FnDecl body here is already
    // concrete. (A TypeVar-shaped generic body cannot exist in a well-formed
    // input; the verifier rejects its Any parameter types if it ever does.)
    FnCallResolver fn_call_resolver;
    // FB-3a2: the structural interner (mints lifted-fn CoreVtFn / CoreVtClosure
    // types) and the one lambda-lift driver, created lazily right after the fn
    // shells + resolver fixed point exist (still inside Pass 1.5) but used by
    // outlined-fn, flow and workflow body lowering alike. Declared here so all
    // later passes share the same instance.
    std::optional<StructuralInternerFn> intern_structural_holder;
    std::optional<LambdaLifter> lambda_lifter_holder;
    {
        // The FnKind InstanceDecls must land in `core.instances` BEFORE the
        // non-Fn kinds (Pass 6 appends those), preserving AHFL declaration order
        // as much as possible while keeping the resolver usable by Pass 2.
        struct GuaranteedFn {
            const FnDecl *source;
            std::string key;
            ir::SymbolRef origin;
            std::vector<CoreValueTypeId> dispatch_types;
            bool from_ir_instance{false};
            const InstanceDecl *ir_instance{nullptr};
        };
        std::vector<GuaranteedFn> guaranteed;
        std::unordered_map<std::string, std::size_t> guaranteed_by_key;

        // 1) Fn-kind InstanceDecls emitted by the typed lowerer.
        for (const Decl &decl : ahfl_ir.declarations) {
            const auto *inst = std::get_if<InstanceDecl>(&decl);
            if (inst == nullptr || inst->kind != InstanceKind::Fn) {
                continue;
            }
            GuaranteedFn g;
            g.key = inst->name;
            g.origin = inst->symbol_ref;
            g.from_ir_instance = true;
            g.ir_instance = inst;
            g.dispatch_types.reserve(inst->type_args.size());
            for (const TypeRef &t : inst->type_args) {
                std::string reason;
                const auto vt = shared_arena.lower(t, &reason);
                if (!vt) {
                    result.diagnostics.push_back(CoreLowerDiagnostic{
                        CoreDiagnosticSeverity::Error, std::string(diag::kUnresolvedType),
                        "fn instance '" + inst->name +
                            "' has a non-materializable dispatch type: " + reason,
                        inst->provenance.source_range});
                    g.dispatch_types.push_back(CoreValueTypeId{});
                    continue;
                }
                g.dispatch_types.push_back(*vt);
            }
            // Locate its body FnDecl. Identity resolution order (design §4):
            //   1. exact instance_key match against a P2d concrete FnDecl;
            //   2. symbol id (the canonical declaration for a non-generic fn);
            //   3. canonical name.
            const FnDecl *source = nullptr;
            if (auto it = body_fn_by_key.find(inst->name); it != body_fn_by_key.end()) {
                source = body_fns[it->second].decl;
            }
            if (source == nullptr && inst->symbol_ref.id.has_value()) {
                if (auto it = body_fn_by_id.find(*inst->symbol_ref.id);
                    it != body_fn_by_id.end()) {
                    source = body_fns[it->second].decl;
                }
            }
            if (source == nullptr && !inst->symbol_ref.canonical_name.empty()) {
                if (auto it = body_fn_by_name.find(inst->symbol_ref.canonical_name);
                    it != body_fn_by_name.end()) {
                    source = body_fns[it->second].decl;
                }
            }
            g.source = source;
            guaranteed_by_key.emplace(g.key, guaranteed.size());
            guaranteed.push_back(std::move(g));
        }

        // 2) Guarantee a canonical empty-type-args instance for every has_body
        //    fn without one. The empty-type-args mangle key is produced through
        //    the shared mangle SSOT (SymbolId 0 / canonical name resolver), so
        //    it is byte-identical to the shape emit_instantiated_declarations
        //    would have produced.
        for (const BodyFn &bf : body_fns) {
            const FnDecl &fn = *bf.decl;
            // A P2d-mangled concrete instantiation is already emitted as its own
            // FnKind InstanceDecl (step 1) and is NOT a canonical declaration to
            // guarantee — skip it.
            if (fn.name.rfind("_inst_", 0) == 0) {
                continue;
            }
            // A generic DECLARATION (type params present) is not guaranteed an
            // empty-args instance: its body is TypeVar-shaped and only its
            // concrete instantiations (step 1) are callable.
            if (!fn.type_param_names.empty()) {
                continue;
            }
            // Precise check: present iff an existing guarantee with the same
            // symbol id (or canonical) AND empty dispatch types exists.
            bool present = std::any_of(guaranteed.begin(), guaranteed.end(),
                                  [&](const GuaranteedFn &g) {
                                      const bool same_id =
                                          fn.symbol_ref.id.has_value() &&
                                          g.origin.id.has_value() &&
                                          *g.origin.id == *fn.symbol_ref.id &&
                                          g.dispatch_types.empty();
                                      const bool same_name =
                                          !fn.symbol_ref.canonical_name.empty() &&
                                          g.origin.canonical_name ==
                                              fn.symbol_ref.canonical_name &&
                                          g.dispatch_types.empty();
                                      return same_id || same_name;
                                  });
            if (present) {
                continue;
            }
            GuaranteedFn g;
            g.source = &fn;
            g.origin = fn.symbol_ref;
            const std::uint64_t symbol_id =
                fn.symbol_ref.id.has_value() ? *fn.symbol_ref.id : 0ULL;
            const ahfl::mangle::SymbolCanonicalNameFn mangle_resolver =
                [&fn](ahfl::SymbolId) -> std::optional<std::string> {
                return fn.symbol_ref.canonical_name;
            };
            g.key = ahfl::mangle::mangle_instance(
                ahfl::SymbolId{static_cast<std::uint32_t>(symbol_id)},
                std::span<const ahfl::TypePtr>{}, mangle_resolver);
            guaranteed_by_key.emplace(g.key, guaranteed.size());
            guaranteed.push_back(std::move(g));
        }

        // 3) Emit Core Fn instances + CoreFnDecl shells. EVERY resolver entry
        //    is published in this loop BEFORE any body is lowered, so a legal
        //    whole-program forward reference (an fn calling an fn declared
        //    later) and a mutual-recursion group resolve: the fixed point is
        //    visible to every FnBodyLowerer, independent of AHFL declaration
        //    order. Bodies are populated in a second loop (3b) once the whole
        //    fn table + resolver are in place.
        //
        // FB-4: the published resolver entry carries the fn's TRANSITIVE effect
        // (an fn that wraps a capability call is classified effectful even when
        // its own clause says Pure), so an effectful callee lowers to the
        // ordered CoreCallStmt rather than a pure CoreCallExpr. The scanner is
        // keyed on the same body-fn indices the guarantee loop resolves through.
        std::vector<const FnDecl *> scanner_bodies;
        scanner_bodies.reserve(body_fns.size());
        for (const BodyFn &bf : body_fns) {
            scanner_bodies.push_back(bf.decl);
        }
        const std::vector<ir::FnEffectKind> body_effects =
            FnEffectScanner{body_fn_by_key, body_fn_by_id, scanner_bodies}.run();

        std::unordered_map<std::string, std::uint32_t> fn_decl_by_instance_key;
        // Map each guaranteed body's FnDecl* to its transitively-derived effect.
        std::unordered_map<const FnDecl *, ir::FnEffectKind> effect_by_decl;
        effect_by_decl.reserve(scanner_bodies.size());
        for (std::size_t i = 0; i < scanner_bodies.size(); ++i) {
            effect_by_decl.emplace(scanner_bodies[i], body_effects[i]);
        }
        for (GuaranteedFn &g : guaranteed) {
            const CoreInstanceId instance_id{
                static_cast<std::uint32_t>(core.instances.size())};
            CoreInstanceDecl out;
            out.id = instance_id;
            out.instance_key = g.key;
            out.origin = g.origin;
            out.dispatch_types = g.dispatch_types;
            const bool has_body = g.source != nullptr;
            CoreFnId fn_id{CoreFnId::kInvalid};
            if (has_body) {
                fn_id = CoreFnId{static_cast<std::uint32_t>(core.fns.size())};
            }
            out.payload = CoreFnInstance{fn_id};
            core.instances.push_back(std::move(out));
            if (!has_body) {
                // A registered generic instance whose body source was not in
                // this compilation unit (e.g. stdlib facade omitted when
                // include_stdlib=false): no direct call, stays on intrinsic /
                // fail-closed path.
                continue;
            }
            CoreFnDecl fn_decl;
            fn_decl.id = fn_id;
            fn_decl.instance = instance_id;
            fn_decl.origin = g.origin;
            fn_decl.name = g.key;
            fn_decl.source_range = g.source->provenance.source_range;
            core.fns.push_back(std::move(fn_decl));
            fn_decl_by_instance_key.emplace(g.key, fn_id.value);
            FnCallResolver::Entry entry;
            entry.resolution.instance = instance_id;
            entry.resolution.has_body = true;
            // FB-4: the TRANSITIVE effect (fixed point over fn bodies), not the
            // source clause spelling — an fn that only wraps an effectful callee
            // is still effectful and must lower to an ordered CoreCallStmt.
            if (const auto found = effect_by_decl.find(g.source);
                found != effect_by_decl.end()) {
                entry.resolution.effect = found->second;
            } else {
                entry.resolution.effect = g.source->effect.kind;
            }
            fn_call_resolver.add(g.key,
                                 g.origin.id,
                                 g.origin.canonical_name,
                                 entry);
        }

        // 3b) Lower every body against the COMPLETE fixed point. The fn decl
        //     shells and all resolver entries already exist, so a forward
        //     callee and every member of a mutual-recursion group resolve no
        //     matter the declaration order.
        //
        //     FB-3a2 stability: lambda lifting APPENDS a CoreFnDecl + Fn
        //     instance for each construction site WHILE a body is being lowered,
        //     and a body lowerer holds a reference to its own CoreFnDecl. Reserve
        //     headroom now (the guaranteed fn shells are all pushed) so neither
        //     vector reallocates and dangles that reference. One LambdaExpr in
        //     the program is an upper bound on the number of lift sites.
        {
            std::size_t lambda_sites = 0;
            for (const ir::Expr *expr : ahfl_ir.all_exprs()) {
                if (expr != nullptr && std::holds_alternative<ir::LambdaExpr>(expr->node)) {
                    ++lambda_sites;
                }
            }
            core.fns.reserve(core.fns.size() + lambda_sites + 1);
            core.instances.reserve(core.instances.size() + lambda_sites + 1);
        }

        //     RFC 0026 FB-3a2: build the lambda-lift driver BEFORE lowering any
        //     body (a lambda can nest in an outlined fn body as well as a
        //     handler). It appends to the same fn/instance tables and resolver,
        //     so a lifted fn discovered while lowering an outlined body gets a
        //     fixed point like every other fn.
        //
        //     The interner is constructed straight into the OUTER-SCOPE holder
        //     (which Pass 2/3 alias) — never a block-local the holder-held lifter
        //     could dangle on once this block closes. The lifter takes its OWN
        //     by-value copy; both copies forward to the same long-lived
        //     shared_arena and so mint identical interned ids.
        intern_structural_holder.emplace(
            [&shared_arena](CoreValueTypeNode node, std::string *reason) {
                return shared_arena.intern_structural(std::move(node), reason);
            });
        lambda_lifter_holder.emplace(core, cap_index, types, intern_value_type,
                                     *intern_structural_holder, fn_call_resolver,
                                     result.diagnostics);
        LambdaLifter &lambda_lifter = *lambda_lifter_holder;
        for (GuaranteedFn &g : guaranteed) {
            if (g.source == nullptr) {
                continue;
            }
            const auto fn_it = fn_decl_by_instance_key.find(g.key);
            if (fn_it == fn_decl_by_instance_key.end()) {
                result.diagnostics.push_back(CoreLowerDiagnostic{
                    CoreDiagnosticSeverity::Error, std::string(diag::kFnBodyUnlowered),
                    "fn instance '" + g.key + "' lost its body table link during lowering",
                    g.source->provenance.source_range});
                continue;
            }
            FnBodyLowerer lowerer(core.fns[fn_it->second], *g.source, cap_index, types,
                                  intern_value_type, core.value_types, fn_call_resolver,
                                  result.diagnostics, &lambda_lifter,
                                  &*intern_structural_holder);
            lowerer.lower();
        }
    }

    // The lambda-lift driver + structural interner were created in Pass 1.5
    // (they must also serve outlined fn-body lowering there); alias them for the
    // flow / workflow passes below.

    // Pass 2: flows. Resolve each flow's target agent BY IDENTITY; a missing
    // target or an unknown handler state is a fail-closed Error (never a
    // silent fallback to state 0). The FnCallResolver is built BEFORE this loop
    // (Pass 1.5) so a handler's free `fn` call lowers to a direct CoreCallExpr.
    for (const Decl &decl : ahfl_ir.declarations) {
        const auto *flow = std::get_if<FlowDecl>(&decl);
        if (flow == nullptr) {
            continue;
        }
        CoreFlowDecl core_flow;
        core_flow.target_ref = flow->target_ref;
        core_flow.agent_name = flow->target_ref.local_name.empty()
                                   ? flow->target_ref.canonical_name
                                   : flow->target_ref.local_name;

        std::optional<CoreAgentId> target;
        if (flow->target_ref.id.has_value()) {
            if (const auto it = agent_by_id.find(*flow->target_ref.id); it != agent_by_id.end()) {
                target = it->second;
            }
        }
        if (!target && !flow->target_ref.canonical_name.empty()) {
            if (const auto it = agent_by_name.find(flow->target_ref.canonical_name);
                it != agent_by_name.end()) {
                target = it->second;
            }
        }
        if (!target) {
            result.diagnostics.push_back(CoreLowerDiagnostic{
                CoreDiagnosticSeverity::Error, std::string(diag::kUnresolvedFlowTarget),
                "flow target agent '" + core_flow.agent_name +
                    "' could not be resolved to a declared agent",
                flow->provenance.source_range});
            core.flows.push_back(std::move(core_flow));
            continue;
        }
        core_flow.target = *target; // typed target-agent identity (Principle 2)

        const CoreAgentDecl &target_agent = core.agents[target->value];
        StateIndex state_index(target_agent.states);

        // The target agent's input/context struct types are already resolved on
        // its CoreAgentDecl (typed shell), so member projections through
        // `input.` / `ctx.` get typed CoreFieldIds without re-querying AHFL-IR.
        FlowLowerer lowerer(core_flow, cap_index, state_index, types, target_agent.input_type,
                            target_agent.context_type, intern_value_type, core.value_types,
                            result.diagnostics, &fn_call_resolver, &*lambda_lifter_holder,
                            &*intern_structural_holder);
        for (const StateHandler &handler : flow->state_handlers) {
            const auto state_id = state_index.lookup(handler.state_name);
            if (!state_id) {
                result.diagnostics.push_back(CoreLowerDiagnostic{
                    CoreDiagnosticSeverity::Error, std::string(diag::kUnknownHandlerState),
                    "flow handler names state '" + handler.state_name +
                        "' which is not declared by agent '" + core_flow.agent_name + "'",
                    handler.source_range});
                continue; // do NOT lower a handler onto a bogus state 0
            }
            lowerer.lower_handler(handler, *state_id);
        }
        core.flows.push_back(std::move(core_flow));
    }

    // Pass 3: workflows. Each WorkflowDecl lowers to a CoreWorkflowDecl (DAG of
    // agent invocations); node ids are declaration-order indices, node/return
    // input expressions are ANF regions ending in a value-yield, and safety /
    // liveness temporal properties are erased. Node target agents + `after`
    // dependencies resolve by identity; unresolved / duplicate / unknown are
    // fail-closed diagnostics (the verifier then proves the DAG + node-output
    // references on a lowering-clean program).
    for (const Decl &decl : ahfl_ir.declarations) {
        const auto *wf = std::get_if<WorkflowDecl>(&decl);
        if (wf == nullptr) {
            continue;
        }
        CoreWorkflowDecl core_wf;
        core_wf.id = CoreWorkflowId{static_cast<std::uint32_t>(core.workflows.size())};
        WorkflowLowerer lowerer(core_wf, cap_index, types, core.agents, agent_by_id, agent_by_name,
                                intern_value_type, core.value_types, result.diagnostics,
                                &fn_call_resolver, &*lambda_lifter_holder,
                                &*intern_structural_holder);
        lowerer.lower(*wf);
        core.workflows.push_back(std::move(core_wf));
    }

    // Workflow-by-identity index (SymbolId / canonical name -> CoreWorkflowId), so
    // a workflow INSTANCE resolves its base by identity like flows/agents do.
    std::unordered_map<std::size_t, CoreWorkflowId> workflow_by_id;
    std::unordered_map<std::string, CoreWorkflowId> workflow_by_name;
    for (std::uint32_t i = 0; i < core.workflows.size(); ++i) {
        const auto &wf = core.workflows[i];
        if (wf.symbol_ref.id.has_value()) {
            workflow_by_id.emplace(*wf.symbol_ref.id, CoreWorkflowId{i});
        }
        if (!wf.symbol_ref.canonical_name.empty()) {
            workflow_by_name.emplace(wf.symbol_ref.canonical_name, CoreWorkflowId{i});
        }
    }

    // Pass 6: non-Fn instances. Consume every AHFL-IR non-Fn InstanceDecl into
    // the flat CoreProgram.instances table (index == CoreInstanceId, in AHFL
    // declaration order). Fn-kind InstanceDecls were ALREADY consumed in
    // Pass 1.5 (the fn-instance guarantee + fn table), so they are skipped here
    // to keep the 1:1 Fn <-> body link in one place. The mangled `name` is used
    // VERBATIM as instance_key (never re-mangled). Capability / Agent / Workflow
    // payloads resolve their base BY SYMBOL IDENTITY (no id-0 fallback). A
    // duplicate instance_key is fail-closed (never a silent dedup).
    std::unordered_set<std::string> seen_instance_keys;
    for (const CoreInstanceDecl &existing : core.instances) {
        seen_instance_keys.insert(existing.instance_key);
    }
    // Interns each instance's concrete dispatch type into the program's logical
    // value-type arena (RFC 0026 P4), REUSING the one long-lived `shared_arena`
    // hoisted before Pass 2 — so body values and dispatch descriptors intern into
    // the SAME canonical `core.value_types` pool. It uses the SAME strict id-first
    // resolver as the public lower_value_type_into (Codex P0-3): resolves over the
    // now-complete core.types table by symbol id, with a name-only-synthetic
    // canonical fallback and fail-closed id/canonical drift — no path drift.
    for (const Decl &decl : ahfl_ir.declarations) {
        const auto *inst = std::get_if<InstanceDecl>(&decl);
        if (inst == nullptr || inst->kind == InstanceKind::Fn) {
            continue; // Fn instances were consumed + guaranteed in Pass 1.5
        }
        const CoreInstanceId id{static_cast<std::uint32_t>(core.instances.size())};
        CoreInstanceDecl out;
        out.id = id;
        out.instance_key = inst->name; // byte-exact; NEVER re-mangle
        out.origin = inst->symbol_ref;
        out.dispatch_types.reserve(inst->type_args.size());
        for (const TypeRef &t : inst->type_args) {
            std::string reason;
            const auto vt = shared_arena.lower(t, &reason);
            if (!vt) {
                result.diagnostics.push_back(CoreLowerDiagnostic{
                    CoreDiagnosticSeverity::Error, std::string(diag::kUnresolvedType),
                    "instance '" + inst->name + "' has a non-materializable dispatch type: " +
                        reason,
                    inst->provenance.source_range});
                continue;
            }
            out.dispatch_types.push_back(*vt);
        }
        if (!seen_instance_keys.insert(inst->name).second) {
            result.diagnostics.push_back(CoreLowerDiagnostic{
                CoreDiagnosticSeverity::Error, std::string(diag::kDuplicateInstanceKey),
                "instance key '" + inst->name + "' is defined by more than one instance",
                inst->provenance.source_range});
        }
        const auto unresolved_base = [&](const std::string &what) {
            result.diagnostics.push_back(CoreLowerDiagnostic{
                CoreDiagnosticSeverity::Error, std::string(diag::kUnresolvedInstanceBase),
                "instance '" + inst->name + "' " + what, inst->provenance.source_range});
        };
        switch (inst->kind) {
        case InstanceKind::Capability: {
            CoreCapabilityInstance p;
            if (const auto info = cap_index.lookup(inst->symbol_ref)) {
                p.base = info->id;
            } else {
                unresolved_base("could not resolve its base capability by identity");
            }
            out.payload = p;
            break;
        }
        case InstanceKind::Predicate:
            out.payload = CorePredicateInstance{};
            break;
        case InstanceKind::Agent: {
            CoreAgentInstance p;
            std::optional<CoreAgentId> base;
            if (inst->symbol_ref.id.has_value()) {
                if (const auto it = agent_by_id.find(*inst->symbol_ref.id); it != agent_by_id.end()) {
                    base = it->second;
                }
            }
            if (!base && !inst->symbol_ref.canonical_name.empty()) {
                if (const auto it = agent_by_name.find(inst->symbol_ref.canonical_name);
                    it != agent_by_name.end()) {
                    base = it->second;
                }
            }
            if (!base) {
                unresolved_base("could not resolve its base agent by identity");
            } else {
                p.base = *base;
            }
            // The concrete shell this instance applies (resolved to CoreTypeIds).
            p.input_type = types.type_id_of(inst->agent_input_type_ref).value_or(CoreTypeId{});
            p.output_type = types.type_id_of(inst->agent_output_type_ref).value_or(CoreTypeId{});
            if (inst->agent_context_type_ref.kind == TypeRefKind::Unit) {
                p.context_kind = CoreAgentDecl::ContextKind::Unit;
                p.context_type = CoreTypeId{};
            } else {
                p.context_kind = CoreAgentDecl::ContextKind::Struct;
                p.context_type =
                    types.type_id_of(inst->agent_context_type_ref).value_or(CoreTypeId{});
            }
            out.payload = p;
            break;
        }
        case InstanceKind::Workflow: {
            CoreWorkflowInstance p;
            std::optional<CoreWorkflowId> base;
            if (inst->symbol_ref.id.has_value()) {
                if (const auto it = workflow_by_id.find(*inst->symbol_ref.id);
                    it != workflow_by_id.end()) {
                    base = it->second;
                }
            }
            if (!base && !inst->symbol_ref.canonical_name.empty()) {
                if (const auto it = workflow_by_name.find(inst->symbol_ref.canonical_name);
                    it != workflow_by_name.end()) {
                    base = it->second;
                }
            }
            if (!base) {
                unresolved_base("could not resolve its base workflow by identity");
            } else {
                p.base = *base;
            }
            p.input_type = types.type_id_of(inst->workflow_input_type_ref).value_or(CoreTypeId{});
            p.output_type = types.type_id_of(inst->workflow_output_type_ref).value_or(CoreTypeId{});
            out.payload = p;
            break;
        }
        case InstanceKind::Fn:
            out.payload = CoreFnInstance{};
            break;
        case InstanceKind::Unknown:
        default:
            result.diagnostics.push_back(CoreLowerDiagnostic{
                CoreDiagnosticSeverity::Error, std::string(diag::kUnknownInstanceKind),
                "instance '" + inst->name + "' has an unknown kind", inst->provenance.source_range});
            out.payload = CoreFnInstance{}; // placeholder; program already non-executable
            break;
        }
        core.instances.push_back(std::move(out));
    }

    // Pass 7: link workflow invocations. Each workflow node invokes a CONCRETE
    // agent instance — resolve it to a unique CoreInstanceId by the typed tuple
    // (base agent, input, context_kind, context, output) taken from the node's
    // nominal target agent shell. The mangled key is NEVER re-derived here. A node
    // that matches zero or more than one Agent instance is fail-closed.
    {
        // Index: agent-shell tuple -> CoreInstanceId (only Agent-kind instances).
        struct AgentShellKey {
            std::uint32_t base;
            std::uint32_t input;
            int context_kind;
            std::uint32_t context;
            std::uint32_t output;
            [[nodiscard]] bool operator==(const AgentShellKey &o) const noexcept {
                return base == o.base && input == o.input && context_kind == o.context_kind &&
                       context == o.context && output == o.output;
            }
        };
        struct AgentShellHash {
            [[nodiscard]] std::size_t operator()(const AgentShellKey &k) const noexcept {
                std::size_t h = k.base;
                for (std::uint32_t v : {k.input, static_cast<std::uint32_t>(k.context_kind),
                                        k.context, k.output}) {
                    h = h * 1000003u + v;
                }
                return h;
            }
        };
        std::unordered_map<AgentShellKey, std::vector<CoreInstanceId>, AgentShellHash> agent_insts;
        for (const CoreInstanceDecl &inst : core.instances) {
            if (const auto *a = std::get_if<CoreAgentInstance>(&inst.payload)) {
                agent_insts[AgentShellKey{a->base.value, a->input_type.value,
                                          static_cast<int>(a->context_kind), a->context_type.value,
                                          a->output_type.value}]
                    .push_back(inst.id);
            }
        }
        for (CoreWorkflowDecl &wf : core.workflows) {
            for (CoreWorkflowNode &node : wf.nodes) {
                // Recover the node's nominal target agent + shell (the same
                // resolution the lowering-only side table used).
                std::optional<CoreAgentId> base;
                if (node.target_ref.id.has_value()) {
                    if (const auto it = agent_by_id.find(*node.target_ref.id);
                        it != agent_by_id.end()) {
                        base = it->second;
                    }
                }
                if (!base && !node.target_ref.canonical_name.empty()) {
                    if (const auto it = agent_by_name.find(node.target_ref.canonical_name);
                        it != agent_by_name.end()) {
                        base = it->second;
                    }
                }
                if (!base) {
                    // Already reported as kUnresolvedWorkflowTarget in Pass 5.
                    continue;
                }
                const CoreAgentDecl &agent = core.agents[base->value];
                const AgentShellKey key{base->value, agent.input_type.value,
                                        static_cast<int>(agent.context_kind),
                                        agent.context_type.value, agent.output_type.value};
                const auto it = agent_insts.find(key);
                const std::size_t n = it == agent_insts.end() ? 0 : it->second.size();
                if (n != 1) {
                    result.diagnostics.push_back(CoreLowerDiagnostic{
                        CoreDiagnosticSeverity::Error,
                        std::string(diag::kUnresolvedWorkflowInvocation),
                        "workflow '" + wf.name + "' node '" + node.node_name + "' resolves to " +
                            std::to_string(n) + " agent instances (expected exactly 1)",
                        std::nullopt});
                    continue;
                }
                node.target_instance = it->second.front();
            }
        }
    }

    // FB-4 fix-forward: canonicalize direct-call shape against the
    // closure-aware structural effect authority (lowering over-emits ordered
    // statements; this downgrades pure callees) BEFORE the verifier enforces the
    // pure-vs-effectful discipline, so closure-routed effects and pure bodies
    // agree with analyze_fn_effects exactly.
    if (!result.has_errors()) {
        reconcile_fn_call_shapes(core);
    }

    // Auto-verify the candidate program at the lowering boundary. When the
    // lowering itself already produced an Error the program is a PARTIAL
    // artifact (unresolved ids, unlowered nodes) — running the verifier on it
    // would just echo those as structural violations, so we only verify a
    // lowering-clean program. Verifier Errors merge into the same diagnostics
    // channel, and executability requires BOTH passes clean (Codex wiring
    // requirement): is_executable == lowering-clean && verify-clean.
    if (!result.has_errors()) {
        CoreVerifyResult verified = verify_core_program(core);
        for (auto &d : verified.diagnostics) {
            result.diagnostics.push_back(std::move(d));
        }
    }
    result.is_executable = !result.has_errors();
    return result;
}

// Factory access to the private VerifiedCoreTypeEnvironment constructor. Only
// build_core_type_environment (below), after its type-local gate passed, mints
// one — so the immutable handle always carries a verified type environment.
struct CoreTypeEnvironmentFactory {
    [[nodiscard]] static VerifiedCoreTypeEnvironment make(std::vector<CoreTypeDecl> types,
                                                          std::vector<CoreValueType> value_types) {
        return VerifiedCoreTypeEnvironment(
            std::make_shared<const VerifiedCoreTypeEnvironment::Payload>(
                VerifiedCoreTypeEnvironment::Payload{std::move(types), std::move(value_types)}));
    }
};

CoreTypeEnvironmentResult build_core_type_environment(const AhflIr &ahfl_ir) {
    CoreTypeEnvironmentResult result;
    // A throwaway CoreProgram whose type table + value-type arena the shared
    // prelude fills; the verified environment (minted only if the type-local gate
    // passes) owns a copy. No body is lowered, so an unrelated (e.g. not-yet-
    // lowered P6) handler cannot block a wire-schema migration projection.
    CoreProgram scratch;
    ValueTypeArena shared_arena(scratch.value_types, scratch.types, [&scratch](const SymbolRef &r) {
        return resolve_nominal_strict(scratch.types, r);
    });
    // The returned TypeEnv binds references into `scratch`; it is dropped here —
    // only the populated arenas are retained.
    static_cast<void>(populate_core_type_table(ahfl_ir, scratch, result.diagnostics, shared_arena));

    // TYPE-LOCAL gate: `populate_core_type_table` only reports the diagnostics
    // TypeEnv construction happens to raise, which is NOT proof the type table +
    // value-type arena are structurally legal. Run the real structural verifier on
    // the types+value_types-only scratch: with empty agents/capabilities/flows/
    // workflows/instances, `verify_core_program` walks ONLY verify_types() and
    // verify_value_types() — no body — so this is the type-local gate the design
    // requires, reusing the single verifier rather than a second structural pass.
    auto verification = verify_core_program(scratch);
    for (auto &d : verification.diagnostics) {
        result.diagnostics.push_back(std::move(d));
    }
    if (result.has_errors()) {
        return result; // fail closed: no environment minted
    }
    result.environment = CoreTypeEnvironmentFactory::make(std::move(scratch.types),
                                                          std::move(scratch.value_types));
    return result;
}

std::optional<CoreValueTypeId>
lower_value_type_into(CoreProgram &program, const ir::TypeRef &type, std::string *reason) {
    // Strict id-first resolver shared with the production dispatch path (Codex
    // P0-3): a present-but-unmatched id never silently downgrades to a spelling
    // match unless the canonical candidate is a name-only synthetic base.
    auto resolve = [&program](const SymbolRef &ref) {
        return resolve_nominal_strict(program.types, ref);
    };
    ValueTypeArena arena(program.value_types, program.types, std::move(resolve));
    return arena.lower(type, reason);
}

std::optional<CoreValueTypeId>
instantiate_member_template_into(std::vector<CoreValueType> &value_types,
                                 const std::vector<CoreTypeDecl> &types,
                                 CoreTypeId owner,
                                 CoreMemberTypeTemplateNodeId root,
                                 const std::vector<CoreValueTypeId> &owner_args,
                                 std::string *reason) {
    const auto fail = [&](std::string message) -> std::optional<CoreValueTypeId> {
        if (reason != nullptr && reason->empty()) {
            *reason = std::move(message);
        }
        return std::nullopt;
    };
    if (owner.value >= types.size()) {
        return fail("member template owner is out of range");
    }
    const CoreTypeDecl &decl = types[owner.value];
    if (owner_args.size() != decl.type_param_count) {
        return fail("member template owner '" + decl.name + "' expects " +
                    std::to_string(decl.type_param_count) + " argument(s), got " +
                    std::to_string(owner_args.size()));
    }
    if (root.value >= decl.member_type_templates.size()) {
        return fail("member template root is out of range");
    }

    ValueTypeArena arena(value_types, types, [&types](const SymbolRef &ref) {
        return resolve_nominal_strict(types, ref);
    });
    for (const CoreValueTypeId arg : owner_args) {
        if (!arena.valid_materialized_id(arg, reason)) {
            return std::nullopt;
        }
    }

    std::vector<std::optional<CoreValueTypeId>> memo(decl.member_type_templates.size());
    std::vector<std::uint8_t> color(decl.member_type_templates.size(), 0);
    const auto instantiate =
        [&](auto &&self, CoreMemberTypeTemplateNodeId id) -> std::optional<CoreValueTypeId> {
        if (id.value >= decl.member_type_templates.size()) {
            return fail("member template child is out of range");
        }
        if (color[id.value] == 1) {
            return fail("member template graph is cyclic");
        }
        if (color[id.value] == 2) {
            return memo[id.value];
        }
        color[id.value] = 1;
        const CoreMemberTypeTemplateNode &node = decl.member_type_templates[id.value];
        using K = CoreMemberTypeTemplateKind;
        const bool legal_kind = node.kind == K::Concrete || node.kind == K::Param ||
                                node.kind == K::Nominal || node.kind == K::Fn;
        const bool has_concrete = node.concrete.value != CoreValueTypeId::kInvalid;
        const bool has_nominal = node.nominal.value != CoreTypeId::kInvalid;
        const bool has_return = node.fn_return.value != CoreMemberTypeTemplateNodeId::kInvalid;
        if (!legal_kind) {
            return fail("member template node has an illegal kind");
        }
        if ((node.kind == K::Concrete &&
             (!has_concrete || has_nominal || node.param_index != 0 || node.capacity.has_value() ||
              !node.children.empty() || has_return)) ||
            (node.kind == K::Param && (has_concrete || has_nominal || node.capacity.has_value() ||
                                       !node.children.empty() || has_return)) ||
            (node.kind == K::Nominal &&
             (has_concrete || !has_nominal || node.param_index != 0 || has_return)) ||
            (node.kind == K::Fn && (has_concrete || has_nominal || node.param_index != 0 ||
                                    node.capacity.has_value() || !has_return))) {
            return fail("member template node carries fields for another kind");
        }
        for (const auto child : node.children) {
            if (child.value >= id.value) {
                return fail("member template child does not precede its parent");
            }
        }
        if (node.kind == K::Fn && node.fn_return.value >= id.value) {
            return fail("member template function return does not precede its parent");
        }
        std::optional<CoreValueTypeId> value;
        switch (node.kind) {
        case CoreMemberTypeTemplateKind::Concrete:
            if (!arena.valid_materialized_id(node.concrete, reason)) {
                return std::nullopt;
            }
            value = node.concrete;
            break;
        case CoreMemberTypeTemplateKind::Param:
            if (node.param_index >= owner_args.size()) {
                return fail("member template parameter index is out of range");
            }
            value = owner_args[node.param_index];
            break;
        case CoreMemberTypeTemplateKind::Nominal: {
            std::vector<CoreValueTypeId> args;
            args.reserve(node.children.size());
            for (const auto child : node.children) {
                const auto child_value = self(self, child);
                if (!child_value) {
                    return std::nullopt;
                }
                args.push_back(*child_value);
            }
            value = arena.materialize_nominal(node.nominal, std::move(args), node.capacity, reason);
            break;
        }
        case CoreMemberTypeTemplateKind::Fn: {
            std::vector<CoreValueTypeId> params;
            params.reserve(node.children.size());
            for (const auto child : node.children) {
                const auto child_value = self(self, child);
                if (!child_value) {
                    return std::nullopt;
                }
                params.push_back(*child_value);
            }
            const auto ret = self(self, node.fn_return);
            if (!ret) {
                return std::nullopt;
            }
            value = arena.materialize_fn(std::move(params), *ret, reason);
            break;
        }
        }
        if (!value) {
            return std::nullopt;
        }
        color[id.value] = 2;
        memo[id.value] = value;
        return value;
    };
    return instantiate(instantiate, root);
}

std::optional<CoreValueTypeId>
instantiate_member_template(CoreProgram &program,
                            CoreTypeId owner,
                            CoreMemberTypeTemplateNodeId root,
                            const std::vector<CoreValueTypeId> &owner_args,
                            std::string *reason) {
    return instantiate_member_template_into(program.value_types, program.types, owner, root,
                                            owner_args, reason);
}

} // namespace ahfl::ir::core
