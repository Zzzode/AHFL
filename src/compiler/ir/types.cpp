#include "ahfl/compiler/ir/types.hpp"

namespace ahfl::ir {

TypeRef clone_type_ref(const TypeRef &type) {
    TypeRef clone;
    clone.kind = type.kind;
    clone.display_name = type.display_name;
    clone.canonical_name = type.canonical_name;
    clone.variant_name = type.variant_name;
    clone.int_bounds = type.int_bounds;
    clone.string_bounds = type.string_bounds;
    clone.decimal_scale = type.decimal_scale;
    clone.collection_capacity = type.collection_capacity;
    clone.nominal_ref = type.nominal_ref;
    clone.source_range = type.source_range;
    clone.first = clone_type_ref(type.first.get());
    clone.second = clone_type_ref(type.second.get());
    clone.params.reserve(type.params.size());
    for (const auto &param : type.params) {
        clone.params.push_back(clone_type_ref(param.get()));
    }
    return clone;
}

TypeRefPtr clone_type_ref(const TypeRef *type) {
    if (type == nullptr) {
        return nullptr;
    }
    return make_owned<TypeRef>(clone_type_ref(*type));
}

namespace {

// Two nominal SymbolRefs denote the same resolved symbol iff kind + canonical
// name agree. Canonical name is the stable identity of a resolved symbol; kind
// guards a name that coincides across namespaces. (Matches ir_equal.cpp's and
// core_lower.cpp's symbol_ref_equal so the SSOT does not diverge from existing
// SymbolRef comparisons.)
[[nodiscard]] bool nominal_ref_equal(const SymbolRef &a, const SymbolRef &b) {
    return a.kind == b.kind && a.canonical_name == b.canonical_name;
}

} // namespace

bool type_refs_equal(const TypeRef &a, const TypeRef &b) {
    if (a.kind != b.kind || a.canonical_name != b.canonical_name ||
        a.display_name != b.display_name || a.variant_name != b.variant_name ||
        a.int_bounds != b.int_bounds || a.string_bounds != b.string_bounds ||
        a.decimal_scale != b.decimal_scale || a.collection_capacity != b.collection_capacity ||
        !nominal_ref_equal(a.nominal_ref, b.nominal_ref)) {
        return false;
    }
    if (!type_refs_equal(a.first.get(), b.first.get()) ||
        !type_refs_equal(a.second.get(), b.second.get())) {
        return false;
    }
    if (a.params.size() != b.params.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.params.size(); ++i) {
        if (!type_refs_equal(a.params[i].get(), b.params[i].get())) {
            return false;
        }
    }
    return true;
}

bool type_refs_equal(const TypeRef *a, const TypeRef *b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    return type_refs_equal(*a, *b);
}

} // namespace ahfl::ir
