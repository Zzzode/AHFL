#include "ahfl/compiler/ir/identity.hpp"

#include <unordered_map>
#include <limits>

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

void ContractClauseIndex::rebuild(const Program &program) {
    entries_.clear();
    std::unordered_map<std::size_t, std::size_t> clause_counts;
    for (std::size_t declaration_index = 0; declaration_index < program.declarations.size();
         ++declaration_index) {
        const auto *contract = std::get_if<ContractDecl>(&program.declarations[declaration_index]);
        if (contract == nullptr) {
            continue;
        }
        auto target_id = contract->target_ref.id;
        // IR constructed by embedding callers may omit symbol IDs. Resolve their
        // source-level reference at the boundary to a numeric declaration index.
        if (!target_id.has_value()) {
            for (std::size_t index = 0; index < program.declarations.size(); ++index) {
                const auto *agent = std::get_if<AgentDecl>(&program.declarations[index]);
                if (agent != nullptr && symbol_canonical_name(agent->symbol_ref, agent->name) ==
                                            symbol_canonical_name(contract->target_ref)) {
                    target_id = agent->symbol_ref.id.value_or(
                        std::numeric_limits<std::size_t>::max() - index);
                    break;
                }
            }
        }
        if (!target_id.has_value()) {
            // An unresolved target has no valid Agent scope; never alias it to
            // another unresolved declaration in this derived index.
            target_id = std::numeric_limits<std::size_t>::max() - declaration_index;
        }
        auto &count = clause_counts[*target_id];
        entries_.push_back(Entry{.contract = contract, .begin = count, .agent_scope = *target_id});
        count += contract->clauses.size();
    }
}

std::size_t ContractClauseIndex::clause_begin(const ContractDecl &contract) const {
    for (const auto &entry : entries_) {
        if (entry.contract == &contract) {
            return entry.begin;
        }
    }
    return 0;
}

bool ContractClauseIndex::same_agent(const ContractDecl &left, const ContractDecl &right) const {
    const Entry *left_entry = nullptr;
    const Entry *right_entry = nullptr;
    for (const auto &entry : entries_) {
        if (entry.contract == &left) left_entry = &entry;
        if (entry.contract == &right) right_entry = &entry;
    }
    return left_entry != nullptr && right_entry != nullptr &&
           left_entry->agent_scope == right_entry->agent_scope;
}

std::optional<ContractClauseIndex::ClauseLocation>
ContractClauseIndex::find_clause(std::string_view owner, std::size_t ordinal) const {
    for (const auto &entry : entries_) {
        if (symbol_canonical_name(entry.contract->target_ref) == owner &&
            ordinal >= entry.begin && ordinal - entry.begin < entry.contract->clauses.size()) {
            return ClauseLocation{.contract = entry.contract, .local_index = ordinal - entry.begin};
        }
    }
    return std::nullopt;
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
