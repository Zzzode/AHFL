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
#include "ahfl/compiler/ir/core_verify.hpp"

#include <cctype>
#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace ahfl::ir::core {

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
           symbol_ref_equal(a.target_ref, b.target_ref) && a.exprs == b.exprs &&
           a.value_count == b.value_count && a.value_types == b.value_types &&
           a.patterns == b.patterns && a.states == b.states;
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
           a.input_type == b.input_type && a.output_type == b.output_type && a.exprs == b.exprs &&
           a.value_count == b.value_count && a.value_types == b.value_types &&
           a.patterns == b.patterns && a.nodes == b.nodes &&
           region_ptr_eq(a.return_region, b.return_region);
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
    static const std::vector<BuiltinNominalDescriptor> table = {
        {"std::option::Option", "Option", TK::Enum, CoreNominalRole::Option, 1,
         {{"Some", PK::Tuple, 1}, {"None", PK::Unit, 0}}},
        {"std::result::Result", "Result", TK::Enum, CoreNominalRole::Result, 2,
         {{"Ok", PK::Tuple, 1}, {"Err", PK::Tuple, 1}}},
        {"std::collections::List", "", TK::Struct, CoreNominalRole::List, 1, {}},
        {"std::collections::Set", "", TK::Struct, CoreNominalRole::Set, 1, {}},
        {"std::collections::Map", "", TK::Struct, CoreNominalRole::Map, 2, {}},
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
class TypeEnv {
  public:
    explicit TypeEnv(std::vector<CoreTypeDecl> &types) : types_(types) {}

    void add_struct(const StructDecl &decl) {
        CoreTypeDecl t;
        t.kind = CoreTypeDecl::Kind::Struct;
        t.name = decl.symbol_ref.canonical_name.empty() ? decl.name
                                                        : decl.symbol_ref.canonical_name;
        std::vector<std::string> field_type_names;
        for (const FieldDecl &f : decl.fields) {
            t.fields.push_back(f.name);
            t.field_types.push_back(CoreTypeId{}); // resolved in fixup_field_types()
            // A field with an initializer is optional in a struct literal; one
            // without is REQUIRED (the verifier proves completeness).
            t.field_has_default.push_back(f.default_value.ptr != nullptr);
            field_type_names.push_back(nominal_type_name(f.type_ref));
        }
        const auto id = register_type(std::move(t), decl.symbol_ref);
        pending_field_type_names_.emplace(id.value, std::move(field_type_names));
    }
    void add_enum(const EnumDecl &decl) {
        CoreTypeDecl t;
        t.kind = CoreTypeDecl::Kind::Enum;
        t.name = decl.symbol_ref.canonical_name.empty() ? decl.name
                                                       : decl.symbol_ref.canonical_name;
        // Per-variant payload metadata (arity + slot field names), plus the slot
        // type NAMES pending resolution in the fixup pass.
        std::vector<std::vector<std::string>> pending_slot_names;
        for (const EnumVariantDecl &v : decl.variants) {
            t.variants.push_back(v.name);
            CoreTypeDecl::VariantPayload payload;
            std::vector<std::string> slot_names;
            switch (v.payload_kind) {
            case EnumVariantPayloadKind::Unit:
                payload.kind = CoreTypeDecl::VariantPayload::Kind::Unit;
                break;
            case EnumVariantPayloadKind::Tuple:
                payload.kind = CoreTypeDecl::VariantPayload::Kind::Tuple;
                for (const TypeRef &slot : v.payload) {
                    payload.slot_types.push_back(CoreTypeId{}); // resolved in fixup
                    slot_names.push_back(nominal_type_name(slot));
                }
                break;
            case EnumVariantPayloadKind::Struct:
                payload.kind = CoreTypeDecl::VariantPayload::Kind::Struct;
                for (const EnumVariantFieldDecl &field : v.fields) {
                    payload.slot_types.push_back(CoreTypeId{}); // resolved in fixup
                    payload.field_names.push_back(field.name);
                    slot_names.push_back(nominal_type_name(field.type_ref));
                }
                break;
            }
            t.variant_payloads.push_back(std::move(payload));
            pending_slot_names.push_back(std::move(slot_names));
        }
        const auto id = register_type(std::move(t), decl.symbol_ref);
        pending_variant_slot_names_.emplace(id.value, std::move(pending_slot_names));
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
                payload.slot_types.assign(v.payload_arity, CoreTypeId{});
                t.variant_payloads.push_back(std::move(payload));
            }
            // Synthetic std base: a NAME-ONLY resolved-Type ref (no symbol id; the
            // sysroot decl is not inlined). register_type stamps role + arity from
            // the same SSOT via decorate_from_builtin_ssot.
            SymbolRef name_only;
            name_only.kind = SymbolRefKind::Type;
            name_only.canonical_name = canonical;
            (void)register_type(std::move(t), name_only);
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
            types_[type.value].variant_payloads[variant].slot_types.size());
    }

    /// The resolved CoreTypeId of a variant payload slot (kInvalid when OOR or
    /// the slot type is a primitive / non-nominal placeholder). Lets a payload
    /// binding carry its nominal struct/enum type for member projection.
    [[nodiscard]] CoreTypeId variant_slot_type(CoreTypeId type, std::uint32_t variant,
                                               std::uint32_t slot) const {
        if (type.value >= types_.size() || variant >= types_[type.value].variant_payloads.size()) {
            return CoreTypeId{};
        }
        const auto &slots = types_[type.value].variant_payloads[variant].slot_types;
        if (slot >= slots.size()) {
            return CoreTypeId{};
        }
        return slots[slot];
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
                // A valid field_types entry (non-kInvalid) is the field's own
                // struct type; invalid means primitive/collection/P4 (no further
                // projection possible).
                if (i < decl.field_types.size() &&
                    decl.field_types[i].value != CoreTypeId::kInvalid) {
                    step.next_type = decl.field_types[i];
                }
                return step;
            }
        }
        return std::nullopt;
    }

    /// Second pass: resolve each struct field's recorded type NAME to a typed
    /// CoreTypeId, so no canonical-name strings survive for a backend. Run once
    /// after ALL types are registered (forward references resolve).
    void fixup_field_types() {
        for (const auto &[type_index, names] : pending_field_type_names_) {
            CoreTypeDecl &decl = types_[type_index];
            for (std::size_t i = 0; i < names.size() && i < decl.field_types.size(); ++i) {
                if (!names[i].empty()) {
                    if (const auto id = resolve_by_name(names[i])) {
                        decl.field_types[i] = *id;
                    }
                }
            }
        }
        pending_field_type_names_.clear();
        // Resolve each enum variant's payload slot type NAMES to CoreTypeIds.
        for (const auto &[type_index, per_variant] : pending_variant_slot_names_) {
            CoreTypeDecl &decl = types_[type_index];
            for (std::size_t v = 0; v < per_variant.size() && v < decl.variant_payloads.size();
                 ++v) {
                auto &slot_types = decl.variant_payloads[v].slot_types;
                const auto &slot_names = per_variant[v];
                for (std::size_t s = 0; s < slot_names.size() && s < slot_types.size(); ++s) {
                    if (!slot_names[s].empty()) {
                        if (const auto id = resolve_by_name(slot_names[s])) {
                            slot_types[s] = *id;
                        }
                    }
                }
            }
        }
        pending_variant_slot_names_.clear();
    }

  private:
    [[nodiscard]] CoreTypeId register_type(CoreTypeDecl t, const SymbolRef &ref) {
        const auto id = CoreTypeId{static_cast<std::uint32_t>(types_.size())};
        if (ref.id.has_value()) {
            by_id_.emplace(*ref.id, id);
        }
        if (!t.name.empty()) {
            by_name_.emplace(t.name, id);
        }
        t.symbol_ref = ref; // Principle 2: persist resolved-symbol provenance.
        // Decorate role + generic arity from the single builtin nominal SSOT when
        // this real declaration IS a well-known stdlib generic (Codex P0-2): a
        // real std Option/Result/List/Set/Map declaration (include_stdlib / inlined
        // / deserialized) MUST get the same role/arity as the synthetic base, or a
        // legal `Option<Int>` would be rejected as "expects 0 args".
        decorate_from_builtin_ssot(t);
        types_.push_back(std::move(t));
        return id;
    }

    // If `t.name` matches a builtin nominal descriptor, stamp its role +
    // type_param_count from that single SSOT (verifying the Struct/Enum kind
    // agrees — a real decl whose kind disagrees with the SSOT is a structural
    // contradiction the verifier will separately reject). A non-std nominal is
    // left as role=Ordinary / arity=0.
    static void decorate_from_builtin_ssot(CoreTypeDecl &t) {
        for (const BuiltinNominalDescriptor &d : builtin_nominal_table()) {
            if (t.name != d.canonical_name) {
                continue;
            }
            t.role = d.role;
            t.type_param_count = d.type_param_count;
            return;
        }
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
    std::unordered_map<std::size_t, CoreTypeId> by_id_;
    std::unordered_map<std::string, CoreTypeId> by_name_;
    // struct CoreTypeId -> its field type NAMES, pending resolution to typed
    // CoreTypeIds in fixup_field_types() (after all types are registered).
    std::unordered_map<std::uint32_t, std::vector<std::string>> pending_field_type_names_;
    // enum CoreTypeId -> per-variant payload slot type NAMES (parallel to the
    // variant's slot_types), pending resolution in fixup_field_types().
    std::unordered_map<std::uint32_t, std::vector<std::vector<std::string>>>
        pending_variant_slot_names_;
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
        : store_(store), types_(types), resolve_(std::move(resolve)) {
        // Rebuild the hash-cons index from any pre-existing arena entries so
        // repeated lowerings (e.g. one per dispatch type) keep deduplicating
        // against everything already interned — the arena stays canonical.
        for (std::uint32_t i = 0; i < store_.size(); ++i) {
            index_.emplace(store_[i], CoreValueTypeId{i});
        }
    }

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

  private:
    // Hash for the hash-cons map. Children are already interned to ids, so a
    // node's hash mixes only its own scalar fields + child ids.
    struct NodeHash {
        [[nodiscard]] std::size_t operator()(const CoreValueType &vt) const noexcept {
            std::size_t h = vt.node.index();
            const auto mix = [&h](std::size_t v) {
                h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
            };
            std::visit(Overloaded{
                           [&](const CoreVtInt &n) {
                               if (n.bounds) {
                                   mix(static_cast<std::size_t>(n.bounds->first));
                                   mix(static_cast<std::size_t>(n.bounds->second));
                               }
                           },
                           [&](const CoreVtString &n) {
                               if (n.length_bounds) {
                                   mix(static_cast<std::size_t>(n.length_bounds->first));
                                   mix(static_cast<std::size_t>(n.length_bounds->second));
                               }
                           },
                           [&](const CoreVtDecimal &n) { mix(static_cast<std::size_t>(n.scale)); },
                           [&](const CoreVtNominal &n) {
                               mix(n.base.value);
                               for (const auto &a : n.args) {
                                   mix(a.value);
                               }
                               if (n.capacity) {
                                   mix(static_cast<std::size_t>(*n.capacity));
                               }
                           },
                           [&](const CoreVtTuple &n) {
                               for (const auto &e : n.elements) {
                                   mix(e.value);
                               }
                           },
                           [&](const CoreVtFn &n) {
                               for (const auto &p : n.params) {
                                   mix(p.value);
                               }
                               mix(n.ret.value);
                           },
                           [&](const CoreVtClosure &n) {
                               mix(n.signature.value);
                               for (const auto &c : n.captures) {
                                   mix(c.value_type.value);
                                   mix(static_cast<std::size_t>(c.mode));
                               }
                           },
                           [&](const auto &) {},
                       },
                       vt.node);
            return h;
        }
    };

    // Intern a fully-built node (children already interned). Deterministic
    // append-only storage + hash-cons dedup (Principle 3, Codex invariant 4).
    // Fails closed if the arena would exceed the CoreValueTypeId (uint32) space.
    [[nodiscard]] std::optional<CoreValueTypeId> intern(CoreValueTypeNode node,
                                                        std::string *error_reason) {
        CoreValueType vt{std::move(node)};
        if (const auto it = index_.find(vt); it != index_.end()) {
            return it->second;
        }
        if (store_.size() >= CoreValueTypeId::kInvalid) {
            return set_reason(error_reason, "value-type arena exceeded its 32-bit id space");
        }
        const auto id = CoreValueTypeId{static_cast<std::uint32_t>(store_.size())};
        index_.emplace(vt, id);
        store_.push_back(std::move(vt));
        return id;
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
    std::unordered_map<CoreValueType, CoreValueTypeId, NodeHash> index_;
};

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

[[nodiscard]] CoreAgentDecl lower_agent(const AgentDecl &agent, const TypeEnv &types) {
    CoreAgentDecl out;
    out.name = agent.name;
    out.symbol_ref = agent.symbol_ref;
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
    return out;
}

[[nodiscard]] CoreCapabilityDecl lower_capability(const CapabilityDecl &cap) {
    CoreCapabilityDecl out;
    out.name = cap.name;
    out.symbol_ref = cap.symbol_ref;
    out.effect_kind = cap.effect.kind;
    out.param_types.reserve(cap.params.size());
    for (const ParamDecl &param : cap.params) {
        out.param_types.push_back(clone_type_ref(param.type_ref));
    }
    out.return_type_ref = clone_type_ref(cap.return_type_ref);
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

// Non-owning refs to the arenas a lowered body owns (CoreFlowDecl OR CoreWorkflowDecl).
struct CoreBodyStorageRef {
    std::vector<CoreExpr> &exprs;
    std::uint32_t &value_count;
    std::vector<CorePattern> &patterns;
    // RFC 0026 P4-B: the per-body logical value-type table (index == CoreValueId).
    // Grown ATOMICALLY with `value_count` by `fresh_value`, so it stays dense.
    std::vector<CoreValueTypeId> &value_types;
};

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
  private:
    CoreTypeId input_type_;
    CoreTypeId context_type_;
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
  private:
    CoreTypeId input_type_;
    const std::unordered_map<std::string, NodeOutput> *nodes_; // not owned
};

/// Human-readable source expression kind for unsupported-node diagnostics.
[[nodiscard]] std::string expr_kind_name(const ExprNode &node) {
    return std::visit(Overloaded{
                          [](const MethodCallExpr &) { return std::string("MethodCallExpr"); },
                          [](const LambdaExpr &) { return std::string("LambdaExpr"); },
                          [](const MemberAccessExpr &) { return std::string("MemberAccessExpr"); },
                          [](const IndexAccessExpr &) { return std::string("IndexAccessExpr"); },
                          [](const MatchExpr &) { return std::string("MatchExpr"); },
                          [](const UnwrapExpr &) { return std::string("UnwrapExpr"); },
                          [](const QuantifierExpr &) { return std::string("QuantifierExpr"); },
                          [](const CallExpr &) { return std::string("CallExpr"); },
                          [](const auto &) { return std::string("Expr"); },
                      },
                      node);
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
    struct LocalBinding {
        CoreValueId value{};
        CoreValueTypeId value_type{}; // the binding's logical value type
    };

    ExprLowerer(CoreBodyStorageRef storage, const CapabilityIndex &caps, const TypeEnv &types,
                RootPolicy policy, const ValueTypeInterner &interner,
                const std::vector<CoreValueType> &value_type_pool,
                std::vector<CoreLowerDiagnostic> &diags)
        : storage_(storage), caps_(caps), types_(types), policy_(std::move(policy)),
          interner_(interner), value_type_pool_(value_type_pool), diags_(diags) {}

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
                                std::holds_alternative<PathExpr>(expr.ptr->node);
        const CoreValueTypeId result_ty =
            self_typed ? CoreValueTypeId{} : intern_value_type(expr.ptr->resolved_type, range);
        return std::visit(
            Overloaded{
                [&](const CallExpr &call) { return lower_call_value(call, expr, range, region); },
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

    [[nodiscard]] CoreValueId lower_path_value(const PathExpr &e, const ExprRef &expr,
                                               SourceRangeOpt range, CoreRegion &region) {
        const auto rr = resolve_path_root(e.path);
        // A bare local reference lowers directly to its bound value id (its type is
        // already recorded in the body value_types table). It produces NO new value,
        // so nothing to intern here.
        if (rr.is_local && e.path.members.empty()) {
            return rr.local;
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
            // Only `Implies` reaches here. Fail closed either way: an effectful
            // operand is a dropped effect, a pure one is an unexecutable node.
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
        std::visit(Overloaded{
                       [&](const ir::BindingPattern &b) {
                           // By (3)-3b, a bare identifier Sema resolved to a unit
                           // variant is already an ir::VariantPattern here, so a
                           // BindingPattern is always a genuine binding. Its type
                           // is the pattern node's own matched type.
                           if (!b.name.empty()) {
                               const CoreValueTypeId vt =
                                   intern_value_type(pattern.matched_type_ref, pattern.source_range);
                               if (const auto existing = out.find(b.name)) {
                                   // Or-alternative reusing the name: the interned
                                   // type must be IDENTICAL across alternatives.
                                   if (!(out.value_types[*existing] == vt)) {
                                       error(diag::kUnresolvedType,
                                             "match binding '" + b.name +
                                                 "' has inconsistent value types across or-pattern "
                                                 "alternatives",
                                             pattern.source_range);
                                   }
                               } else {
                                   static_cast<void>(out.add(b.name, fresh_value(vt), vt));
                               }
                           }
                           if (b.nested) {
                               // `x @ nested`: the outer name binds the whole
                               // value; the nested pattern carries its own type.
                               collect_arm_bindings(*b.nested, out);
                           }
                       },
                       [&](const ir::VariantPattern &v) {
                           // Each sub-pattern is its own MatchPattern node carrying
                           // its own matched_type_ref (the instantiated payload
                           // type), so no slot-type propagation is needed here.
                           for (const auto &sub : v.subpatterns) {
                               if (sub) {
                                   collect_arm_bindings(*sub, out);
                               }
                           }
                           for (const auto &f : v.fields) {
                               if (f.pattern) {
                                   collect_arm_bindings(*f.pattern, out);
                               }
                           }
                       },
                       [&](const ir::TuplePattern &t) {
                           for (const auto &e : t.elements) {
                               if (e) {
                                   collect_arm_bindings(*e, out);
                               }
                           }
                       },
                       [&](const ir::OrPattern &o) {
                           // Alternatives share bindings BY NAME (find/add dedups);
                           // the identical-type check runs per binding above.
                           for (const auto &alt : o.branches) {
                               if (alt) {
                                   collect_arm_bindings(*alt, out);
                               }
                           }
                       },
                       [&](const auto &) {}, // literal / int-range / wildcard bind nothing
                   },
                   pattern.node);
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
    [[nodiscard]] bool expr_has_capability_call(const ExprRef &expr) const {
        if (expr.ptr == nullptr) {
            return false;
        }
        return std::visit(
            Overloaded{
                [&](const CallExpr &call) {
                    if (call.callee_ref.kind == SymbolRefKind::Capability) {
                        return true;
                    }
                    for (const ExprRef &arg : call.arguments) {
                        if (expr_has_capability_call(arg)) {
                            return true;
                        }
                    }
                    return false;
                },
                [&](const UnaryExpr &e) { return expr_has_capability_call(e.operand); },
                [&](const BinaryExpr &e) {
                    return expr_has_capability_call(e.lhs) || expr_has_capability_call(e.rhs);
                },
                [&](const MemberAccessExpr &e) { return expr_has_capability_call(e.base); },
                [&](const IndexAccessExpr &e) {
                    return expr_has_capability_call(e.base) || expr_has_capability_call(e.index);
                },
                [&](const UnwrapExpr &e) { return expr_has_capability_call(e.operand); },
                [&](const StructLiteralExpr &e) {
                    for (const StructFieldInit &f : e.fields) {
                        if (expr_has_capability_call(f.value)) {
                            return true;
                        }
                    }
                    return false;
                },
                [&](const MethodCallExpr &e) {
                    if (expr_has_capability_call(e.receiver)) {
                        return true;
                    }
                    for (const ExprRef &arg : e.arguments) {
                        if (expr_has_capability_call(arg)) {
                            return true;
                        }
                    }
                    return false;
                },
                [&](const MatchExpr &e) {
                    if (expr_has_capability_call(e.scrutinee)) {
                        return true;
                    }
                    for (const MatchArmExpr &arm : e.arms) {
                        if (expr_has_capability_call(arm.body)) {
                            return true;
                        }
                    }
                    return false;
                },
                [](const auto &) { return false; },
            },
            expr.ptr->node);
    }

  private:
    CoreBodyStorageRef storage_;
    const CapabilityIndex &caps_;
    const TypeEnv &types_;
    RootPolicy policy_;
    const ValueTypeInterner &interner_;
    const std::vector<CoreValueType> &value_type_pool_;
    std::vector<CoreLowerDiagnostic> &diags_;
    std::unordered_map<std::string, LocalBinding> scope_;
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
                std::vector<CoreLowerDiagnostic> &diags)
        : ex_(CoreBodyStorageRef{flow.exprs, flow.value_count, flow.patterns, flow.value_types},
              caps, types, FlowRootPolicy{input_type, context_type}, interner, value_type_pool,
              diags),
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

    // --- region / statement lowering ---
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
        std::visit(Overloaded{
                       [&](const LetStatement &s) { lower_let(s, region); },
                       [&](const AssignStatement &s) { lower_assign(s, stmt.source_range, region); },
                       [&](const ExprStatement &s) {
                           // An expression statement is evaluated for its effect;
                           // the produced value id (if any) is discarded.
                           static_cast<void>(ex_.lower_value(s.expr, region));
                       },
                       [&](const IfStatement &s) { lower_if(s, stmt.source_range, region); },
                       [&](const IfLetStatement &s) { lower_if_let(s, stmt.source_range, region); },
                       [&](const GotoStatement &s) { lower_goto(s, stmt.source_range, region); },
                       [&](const ReturnStatement &s) { lower_return(s, stmt.source_range, region); },
                       // Statements without an execution-layer form in this
                       // slice (if-let / assert / requires / unwrap /
                       // unreachable) are DEFERRED. Because dropping them would
                       // change execution behaviour (e.g. `assert(false)` must
                       // not silently become a no-op) and no Core-IR verifier
                       // exists yet, this is an ERROR that marks the program
                       // non-executable — never a silent Warning drop.
                       [&](const auto &) {
                           ex_.error(diag::kUnloweredStatement,
                                     "statement kind is not yet lowered to Core-IR; the "
                                     "program cannot be executed until this slice lands",
                                     stmt.source_range);
                       },
                   },
                   stmt.node);
    }

    void lower_let(const LetStatement &s, CoreRegion &region) {
        const CoreValueId value = ex_.lower_value(s.initializer, region);
        // A source `let` REUSES the initializer value (does NOT allocate a fresh
        // one), so the local's logical type is the value's already-recorded
        // body-table entry (single source of truth - RFC 0026 P4-B). Both travel
        // as one binding, so branch scoping (see lower_if) snapshots/restores them
        // atomically.
        const CoreValueTypeId init_type = ex_.value_type_of(value);
        // P4-B fail-closed (Codex P0-3): the DECLARED annotation `let x: T = ...`
        // must intern to the SAME logical value type as the initializer value.
        // Sema guarantees this on the normal path; a hand-built / deserialized
        // AHFL-IR whose annotation disagrees (e.g. `let x: Bool = <Int>`) must be
        // rejected here, never silently adopt one side. A fully inferred `let`
        // carries the initializer's own type_ref, so it trivially agrees.
        const CoreValueTypeId declared_type =
            ex_.intern_value_type(s.type_ref, s.initializer.ptr ? s.initializer.ptr->source_range
                                                                : std::nullopt);
        if (!(declared_type == init_type)) {
            ex_.error(diag::kLetTypeMismatch,
                      "let binding '" + s.name +
                          "' declared type does not match its initializer's value type",
                      s.initializer.ptr ? s.initializer.ptr->source_range : std::nullopt);
        }
        ex_.scope()[s.name] = LocalBinding{value, init_type};
    }

    void lower_assign(const AssignStatement &s, SourceRangeOpt range, CoreRegion &region) {
        const CoreValueId value = ex_.lower_value(s.value, region);
        CorePlace place;
        const auto rr = ex_.resolve_path_root(s.target);
        place.root = rr.external_root;
        place.root_name = s.target.root_name;
        place.members = s.target.members;
        // A store into a member PROJECTION (`ctx.field = …`) resolves each
        // member to a typed CoreProjectionStep, same as a read. Set the
        // self-contained root type first; fail closed otherwise (Principle 5).
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
        // Each branch is its own region: mutual exclusion is preserved (the two
        // branches are NOT appended to one flat list). Each branch is lexically
        // scoped: it starts from the SAME outer scope snapshot and its
        // branch-local `let` bindings are discarded afterwards, so a binding in
        // one branch cannot leak into the other branch or past the `if`. The
        // snapshot restores the value id AND its type together (P0-1): a branch
        // that shadows an outer local with a DIFFERENT nominal type must not
        // corrupt the outer local's type after the `if`.
        const auto outer_scope = ex_.scope();
        if (s.then_block) {
            node.then_region = std::make_unique<CoreRegion>(lower_block(*s.then_block));
        } else {
            node.then_region = std::make_unique<CoreRegion>();
        }
        ex_.scope() = outer_scope; // restore before the else branch
        if (s.else_block) {
            node.else_region = std::make_unique<CoreRegion>(lower_block(*s.else_block));
        }
        ex_.scope() = outer_scope; // restore after the if
        region.statements.push_back(CoreStmt{std::move(node), std::move(range)});
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

    void lower_return(const ReturnStatement &s, SourceRangeOpt range, CoreRegion &region) {
        CoreReturnStmt node;
        if (s.value.ptr != nullptr) {
            node.has_value = true;
            node.value = ex_.lower_value(s.value, region);
        }
        region.statements.push_back(CoreStmt{std::move(node), std::move(range)});
    }

    // Statement-position if-let: one pattern arm (yields nothing) with the
    // then-block as body; the fallback is the else block (or a trap when absent —
    // an if-let with no else and a refutable pattern is a non-total match).
    void lower_if_let(const IfLetStatement &s, SourceRangeOpt range, CoreRegion &region) {
        const CoreValueId scrutinee = ex_.lower_value(s.scrutinee, region);
        CoreMatchStmt stmt;
        stmt.scrutinee = scrutinee;
        stmt.has_result = false;
        bool ok = true;
        // The single arm: pattern + then-block, yielding no value (statement match).
        ArmBindings bindings;
        ex_.collect_arm_bindings(s.pattern, bindings);
        CoreMatchArm arm;
        arm.pattern = ex_.lower_pattern(s.pattern, bindings, range, ok);
        arm.body = std::make_unique<CoreRegion>();
        // Both branches are lexically scoped from the SAME outer snapshot, exactly
        // like lower_if: the then-branch sees the arm bindings (lower_block_scoped
        // restores after), and the else-branch sees NEITHER the arm bindings nor
        // the then-branch's locals. A branch-local `let` in either branch must not
        // leak into the other branch or past the if-let (P0: the else previously
        // used a bare lower_block with no restore, leaking its locals downstream).
        const auto outer_scope = ex_.scope();
        if (s.then_block) {
            *arm.body = lower_block_scoped(*s.then_block, bindings);
        }
        ex_.scope() = outer_scope; // restore before the else branch
        ex_.seal_statement_arm(*arm.body, range);
        arm.bindings = std::move(bindings.values);
        stmt.arms.push_back(std::move(arm));
        // Fallback = else block, or a trap when the if-let has no else.
        stmt.fallback_region = std::make_unique<CoreRegion>();
        if (s.else_block) {
            *stmt.fallback_region = lower_block(*s.else_block);
        }
        ex_.scope() = outer_scope; // restore after the if-let
        ex_.seal_statement_arm(*stmt.fallback_region, range);
        region.statements.push_back(CoreStmt{std::move(stmt), std::move(range)});
        static_cast<void>(ok);
    }

    // Lower a block that begins in the CURRENT scope extended with the arm's
    // pattern bindings (so `local`-rooted paths naming a binding resolve to its
    // value id). Restores the scope afterwards.
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

    ExprLowerer<FlowRootPolicy> ex_;
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
                    std::vector<CoreLowerDiagnostic> &diags)
        : wf_(wf), caps_(caps), types_(types), agents_(agents), agent_by_id_(agent_by_id),
          agent_by_name_(agent_by_name), interner_(interner), value_type_pool_(value_type_pool),
          diags_(diags) {}

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
            CoreBodyStorageRef{wf_.exprs, wf_.value_count, wf_.patterns, wf_.value_types}, caps_,
            types_, WorkflowRootPolicy{wf_.input_type, &node_index_}, interner_, value_type_pool_,
            diags_);
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
};

} // namespace

CoreLowerResult lower_ahfl_to_core(const AhflIr &ahfl_ir) {
    CoreLowerResult result;
    CoreProgram &core = result.program;

    // Pass 1: type table (structs/enums + builtin stdlib enums).
    TypeEnv types(core.types);
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
    types.fixup_field_types();

    // Capability table.
    CapabilityIndex cap_index;
    for (const Decl &decl : ahfl_ir.declarations) {
        if (const auto *cap = std::get_if<CapabilityDecl>(&decl)) {
            const auto id = CoreCapabilityId{static_cast<std::uint32_t>(core.capabilities.size())};
            core.capabilities.push_back(lower_capability(*cap));
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
            core.agents.push_back(lower_agent(*agent, types));
        }
    }

    // The SINGLE program-global logical value-type arena (RFC 0026 P4). Hoisted
    // here — after the complete types table is built — and reused for Pass 2/3
    // (body value types) AND Pass 6 (instance dispatch types), so every value +
    // dispatch descriptor interns into ONE canonical `core.value_types` pool
    // (Codex ruling: one long-lived arena, not a per-body/per-pass rebuild). The
    // strict id-first resolver is the same one the public `lower_value_type_into`
    // uses, so the two paths never drift. The interner is a thin fail-closed
    // adapter over `arena.lower`.
    ValueTypeArena shared_arena(core.value_types, core.types, [&core](const SymbolRef &r) {
        return resolve_nominal_strict(core.types, r);
    });
    const ValueTypeInterner intern_value_type =
        [&shared_arena](const TypeRef &type, std::string *reason) {
            return shared_arena.lower(type, reason);
        };

    // Pass 2: flows. Resolve each flow's target agent BY IDENTITY; a missing
    // target or an unknown handler state is a fail-closed Error (never a
    // silent fallback to state 0).
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
                            result.diagnostics);
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
                                intern_value_type, core.value_types, result.diagnostics);
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

    // Pass 6: instances. Consume every AHFL-IR InstanceDecl into the flat
    // CoreProgram.instances table (index == CoreInstanceId, in AHFL declaration
    // order). The mangled `name` is used VERBATIM as instance_key (never
    // re-mangled). kind becomes a structural payload variant; Capability / Agent /
    // Workflow payloads resolve their base BY SYMBOL IDENTITY (no id-0 fallback).
    // A duplicate instance_key is fail-closed (never a silent dedup). NOTE: the
    // budgeted authoritative monomorphization closure (run_monomorphization) is
    // NOT yet wired to this SSOT — we consume emit_instantiated_declarations'
    // InstanceDecls; unifying the two is a later slice.
    std::unordered_set<std::string> seen_instance_keys;
    // Interns each instance's concrete dispatch type into the program's logical
    // value-type arena (RFC 0026 P4), REUSING the one long-lived `shared_arena`
    // hoisted before Pass 2 — so body values and dispatch descriptors intern into
    // the SAME canonical `core.value_types` pool. It uses the SAME strict id-first
    // resolver as the public lower_value_type_into (Codex P0-3): resolves over the
    // now-complete core.types table by symbol id, with a name-only-synthetic
    // canonical fallback and fail-closed id/canonical drift — no path drift.
    for (const Decl &decl : ahfl_ir.declarations) {
        const auto *inst = std::get_if<InstanceDecl>(&decl);
        if (inst == nullptr) {
            continue;
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

} // namespace ahfl::ir::core
