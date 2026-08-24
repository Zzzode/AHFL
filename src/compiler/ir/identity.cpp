#include "ahfl/compiler/ir/identity.hpp"

namespace ahfl::ir {

bool has_resolved_symbol(const SymbolRef &ref) noexcept {
    return ref.kind != SymbolRefKind::Unknown && !ref.canonical_name.empty();
}

bool has_resolved_type(const TypeRef &ref) noexcept {
    return ref.kind != TypeRefKind::Unresolved &&
           (!ref.canonical_name.empty() || !ref.display_name.empty());
}

std::string_view symbol_canonical_name(const SymbolRef &ref, std::string_view fallback) noexcept {
    return ref.canonical_name.empty() ? fallback : std::string_view(ref.canonical_name);
}

std::string_view symbol_display_name(const SymbolRef &ref, std::string_view fallback) noexcept {
    if (!ref.local_name.empty()) {
        return ref.local_name;
    }
    return symbol_canonical_name(ref, fallback);
}

std::string_view type_canonical_name(const TypeRef &ref, std::string_view fallback) noexcept {
    if (!ref.canonical_name.empty()) {
        return ref.canonical_name;
    }
    if (!ref.display_name.empty()) {
        return ref.display_name;
    }
    return fallback;
}

std::string_view type_display_name(const TypeRef &ref, std::string_view fallback) noexcept {
    if (!ref.display_name.empty()) {
        return ref.display_name;
    }
    return type_canonical_name(ref, fallback);
}

std::string_view contract_clause_kind_name(ContractClauseKind kind) noexcept {
    switch (kind) {
    case ContractClauseKind::Requires:
        return "requires";
    case ContractClauseKind::Ensures:
        return "ensures";
    case ContractClauseKind::Invariant:
        return "invariant";
    case ContractClauseKind::Forbid:
        return "forbid";
    case ContractClauseKind::Decreases:
        return "decreases";
    }
    return "invalid";
}

} // namespace ahfl::ir
