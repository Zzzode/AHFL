#pragma once

#include "ahfl/base/support/ownership.hpp"
#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ahfl {

struct ModuleDeclInfo {
    std::string name;
    SourceRange declaration_range;
};

struct ImportDeclInfo {
    std::string target_module;
    std::string alias;
    SourceRange declaration_range;
};

struct ConstDeclInfo {
    std::string canonical_name;
    std::string local_name;
    TypePtr type;
    SourceRange type_range;
    SourceRange value_range;
    SourceRange declaration_range;
};

struct TypeAliasDeclInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::string local_name;
    TypePtr aliased_type;
    SourceRange aliased_type_range;
    SourceRange declaration_range;
};

struct StructFieldInfo {
    std::string name;
    TypePtr type;
    bool has_default{false};
    SourceRange default_value_range;
    SourceRange declaration_range;
};

struct StructTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::vector<StructFieldInfo> fields;
    SourceRange declaration_range;

    [[nodiscard]] MaybeCRef<StructFieldInfo> find_field(std::string_view name) const;
    void rebuild_field_index();

    std::unordered_map<std::string, std::size_t> field_index_;
};

struct EnumVariantInfo {
    std::string name;
    // P1 (ADT, RFC §1.5): positional tuple payload types. Empty for the legacy
    // payload-less `enumDecl: IDENT` form, preserving full backward compatibility.
    // Resolved once by the typecheck pass (see build_enum_types) and consumed by
    // match arm narrowing (binding positions to payload slot types).
    std::vector<TypePtr> payload;
    SourceRange declaration_range;
};

struct EnumTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::vector<EnumVariantInfo> variants;
    SourceRange declaration_range;

    [[nodiscard]] bool has_variant(std::string_view name) const noexcept;
    // P1 (ADT): linear lookup used by the match typecheck pass. The variant
    // count is small (an enum rarely has more than a handful of variants), so
    // no hash index is built — callers iterate at most once per match arm.
    [[nodiscard]] MaybeCRef<EnumVariantInfo> find_variant(std::string_view name) const;
    void rebuild_variant_index();

    std::unordered_set<std::string> variant_set_;
};

struct ParamTypeInfo {
    std::string name;
    TypePtr type;
    SourceRange declaration_range;
};

struct CapabilityEffectTypeInfo {
    bool declared{false};
    int effect_kind{0};
    int receipt_mode{0};
    int retry_mode{0};
    std::string domain;
    std::string idempotency_key;
    std::string timeout;
    std::string compensation;
    std::vector<std::string> policies;
    SourceRange source_range;
};

struct CapabilityTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::vector<ParamTypeInfo> params;
    TypePtr return_type;
    SourceRange declaration_range;
    CapabilityEffectTypeInfo effect;
};

struct PredicateTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    std::vector<ParamTypeInfo> params;
    SourceRange declaration_range;
};

struct AgentTransitionInfo {
    std::string from_state;
    std::string to_state;
};

struct AgentQuotaInfo {
    std::string name;
    std::string value;
};

struct AgentTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    TypePtr input_type;
    TypePtr context_type;
    TypePtr output_type;
    std::vector<SymbolId> capability_symbols;
    SourceRange declaration_range;
    SourceRange input_type_range;
    SourceRange context_type_range;
    SourceRange output_type_range;

    std::vector<std::string> states;
    std::string initial_state;
    std::vector<std::string> final_states;
    std::vector<AgentTransitionInfo> transitions;
    std::vector<AgentQuotaInfo> quota;
};

struct WorkflowNodeInfo {
    std::string name;
    std::string target_name;
    SymbolId target_symbol{0};
    std::vector<std::string> after;
    SourceRange source_range;
    SourceRange input_expr_range;
    SourceRange target_range;
};

struct WorkflowTypeInfo {
    SymbolId symbol;
    std::string canonical_name;
    TypePtr input_type;
    TypePtr output_type;
    SourceRange declaration_range;
    SourceRange input_type_range;
    SourceRange output_type_range;

    std::vector<WorkflowNodeInfo> nodes;
    std::vector<SourceRange> safety_ranges;
    std::vector<SourceRange> liveness_ranges;
    SourceRange return_value_range;
};

enum class StatePolicyKind {
    Retry,
    RetryOn,
    Timeout,
};

struct StatePolicyItemInfo {
    StatePolicyKind kind;
    std::string value;
    std::vector<std::string> retry_on_targets;
};

struct FlowStateHandlerInfo {
    std::string state_name;
    std::vector<StatePolicyItemInfo> policy;
    SourceRange body_range;
    SourceRange source_range;
};

struct FlowTypeInfo {
    SymbolId symbol{0};
    std::string target_name;
    SymbolId target_symbol{0};
    SourceRange target_range;
    std::vector<FlowStateHandlerInfo> state_handlers;
    SourceRange declaration_range;
};

struct ContractClauseInfo {
    int clause_kind{0};
    bool is_temporal{false};
    SourceRange expr_range;
    SourceRange source_range;
};

struct ContractTypeInfo {
    SymbolId symbol{0};
    std::string target_name;
    SymbolId target_symbol{0};
    SourceRange target_range;
    std::vector<ContractClauseInfo> clauses;
    SourceRange declaration_range;
};

} // namespace ahfl
