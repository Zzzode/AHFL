#include "ahfl/compiler/semantics/type_relations.hpp"

namespace ahfl {

std::string_view to_string(SchemaBoundaryKind kind) noexcept {
    switch (kind) {
    case SchemaBoundaryKind::AgentInput:
        return "agent input";
    case SchemaBoundaryKind::AgentOutput:
        return "agent output";
    case SchemaBoundaryKind::AgentContextDefault:
        return "agent context default";
    case SchemaBoundaryKind::WorkflowInput:
        return "workflow input";
    case SchemaBoundaryKind::WorkflowOutput:
        return "workflow output";
    case SchemaBoundaryKind::WorkflowNodeInput:
        return "workflow node input";
    }

    return "schema boundary";
}

bool are_types_equivalent(const Type &lhs, const Type &rhs) {
    // After type interning, equal types share a single canonical instance.
    // Pointer identity therefore short-circuits the structural comparison
    // below in the common case.
    if (&lhs == &rhs) {
        return true;
    }

    if (lhs.payload.index() != rhs.payload.index()) {
        return false;
    }

    return lhs.visit(types::Overloads{
        [](const types::AnyT &) { return true; },
        [](const types::NeverT &) { return true; },
        [](const types::UnitT &) { return true; },
        [](const types::BoolT &) { return true; },
        [](const types::IntT &) { return true; },
        [](const types::FloatT &) { return true; },
        [](const types::StringT &) { return true; },
        [](const types::UUIDT &) { return true; },
        [](const types::TimestampT &) { return true; },
        [](const types::DurationT &) { return true; },
        [&](const types::BoundedStringT &l) {
            const auto *r = rhs.get_if<types::BoundedStringT>();
            return r != nullptr && l.minimum == r->minimum && l.maximum == r->maximum;
        },
        [&](const types::DecimalT &l) {
            const auto *r = rhs.get_if<types::DecimalT>();
            return r != nullptr && l.scale == r->scale;
        },
        [&](const types::StructT &l) {
            const auto *r = rhs.get_if<types::StructT>();
            if (r == nullptr) {
                return false;
            }
            if (l.symbol.has_value() && r->symbol.has_value()) {
                return *l.symbol == *r->symbol;
            }
            return l.canonical_name == r->canonical_name;
        },
        [&](const types::EnumT &l) {
            const auto *r = rhs.get_if<types::EnumT>();
            if (r == nullptr) {
                return false;
            }
            if (l.symbol.has_value() && r->symbol.has_value()) {
                return *l.symbol == *r->symbol;
            }
            return l.canonical_name == r->canonical_name;
        },
        [&](const types::OptionalT &l) {
            const auto *r = rhs.get_if<types::OptionalT>();
            return r != nullptr && l.inner != nullptr && r->inner != nullptr &&
                   are_types_equivalent(*l.inner, *r->inner);
        },
        [&](const types::ListT &l) {
            const auto *r = rhs.get_if<types::ListT>();
            return r != nullptr && l.element != nullptr && r->element != nullptr &&
                   are_types_equivalent(*l.element, *r->element);
        },
        [&](const types::SetT &l) {
            const auto *r = rhs.get_if<types::SetT>();
            return r != nullptr && l.element != nullptr && r->element != nullptr &&
                   are_types_equivalent(*l.element, *r->element);
        },
        [&](const types::MapT &l) {
            const auto *r = rhs.get_if<types::MapT>();
            return r != nullptr && l.key != nullptr && r->key != nullptr && l.value != nullptr &&
                   r->value != nullptr && are_types_equivalent(*l.key, *r->key) &&
                   are_types_equivalent(*l.value, *r->value);
        },
    });
}

bool is_subtype_of(const Type &source, const Type &target) {
    if (are_types_equivalent(source, target)) {
        return true;
    }

    // BoundedString relaxations: BoundedString <: String, and BoundedString
    // is covariant in its bounds (a tighter range refines a wider one).
    const auto *src_bs = source.get_if<types::BoundedStringT>();
    if (src_bs != nullptr && target.holds<types::StringT>()) {
        return true;
    }

    if (src_bs != nullptr) {
        const auto *tgt_bs = target.get_if<types::BoundedStringT>();
        if (tgt_bs != nullptr) {
            return src_bs->minimum >= tgt_bs->minimum && src_bs->maximum <= tgt_bs->maximum;
        }
    }

    return false;
}

bool is_assignable_to(const Type &source, const Type &target) {
    return is_subtype_of(source, target);
}

bool is_exact_schema_match(const Type &source, const Type &target) {
    return are_types_equivalent(source, target);
}

} // namespace ahfl
