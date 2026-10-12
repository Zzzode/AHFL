#pragma once

#include <string_view>
#include <optional>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"

namespace ahfl::ir {

[[nodiscard]] bool has_resolved_symbol(const SymbolRef &ref) noexcept;
[[nodiscard]] bool has_resolved_type(const TypeRef &ref) noexcept;

[[nodiscard]] std::string_view symbol_canonical_name(const SymbolRef &ref,
                                                     std::string_view fallback = {}) noexcept;
[[nodiscard]] std::string_view symbol_display_name(const SymbolRef &ref,
                                                   std::string_view fallback = {}) noexcept;
[[nodiscard]] std::string_view type_canonical_name(const TypeRef &ref,
                                                   std::string_view fallback = {}) noexcept;
[[nodiscard]] std::string_view type_display_name(const TypeRef &ref,
                                                 std::string_view fallback = {}) noexcept;

// Contract ordinals are cumulative per resolved Agent across all contract blocks.
// The flat index is rebuilt from the current IR order, including decreases clauses.
class ContractClauseIndex {
  public:
    struct ClauseLocation {
        const ContractDecl *contract;
        std::size_t local_index;
    };

    void rebuild(const Program &program);
    [[nodiscard]] std::size_t clause_begin(const ContractDecl &contract) const;
    [[nodiscard]] bool same_agent(const ContractDecl &left, const ContractDecl &right) const;
    [[nodiscard]] std::optional<ClauseLocation>
    find_clause(std::string_view owner, std::size_t ordinal) const;

  private:
    struct Entry {
        const ContractDecl *contract;
        std::size_t begin;
        std::size_t agent_scope;
    };
    std::vector<Entry> entries_;
};

// Canonical lowercase spelling of a contract clause kind
// (requires/ensures/invariant/forbid/decreases), for diagnostics and backend
// comments. Single source of truth shared by the SMV backend and the formal
// verifier so the two never drift.
[[nodiscard]] std::string_view contract_clause_kind_name(ContractClauseKind kind) noexcept;

} // namespace ahfl::ir
