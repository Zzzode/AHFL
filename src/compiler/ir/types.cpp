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

} // namespace ahfl::ir
