#pragma once

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ahfl::lsp {

struct LspAnalysisSnapshot;
struct LspSourceSnapshot;

enum class HoverTargetKind {
    ModuleName,
    ImportPath,
    ImportAlias,
    DeclarationName,
    TypeReference,
    ConstReference,
    CallableReference,
    StructField,
    EnumVariant,
    CapabilityParam,
    PredicateParam,
    AgentSchemaLabel,
    AgentState,
    AgentTransition,
    FlowState,
    WorkflowSchemaLabel,
    WorkflowTemporalClause,
    WorkflowNode,
    WorkflowDependency,
    Expression,
    MemberAccess,
    LocalBinding,
    Diagnostic,
};

struct HoverTarget {
    HoverTargetKind kind{HoverTargetKind::Expression};
    SourceRange token_range;
    std::optional<SourceId> source_id;
    std::optional<SymbolId> symbol_id;
    std::optional<SymbolId> owner_symbol_id;
    std::optional<std::uint32_t> typed_expr_index;
    std::optional<std::uint32_t> typed_decl_index;
    std::string local_name;
    std::string role;
    std::string declared_spelling;
    std::string source_label;
    int priority{10};
};

class HoverTargetIndex {
  public:
    void add(HoverTarget target);
    void sort();

    [[nodiscard]] const HoverTarget *lookup(std::size_t offset) const noexcept;
    [[nodiscard]] const std::vector<HoverTarget> &targets() const noexcept {
        return targets_;
    }

  private:
    std::vector<HoverTarget> targets_;
};

[[nodiscard]] std::size_t hover_index_key(const LspSourceSnapshot &source) noexcept;

void build_hover_indices(LspAnalysisSnapshot &snapshot);

} // namespace ahfl::lsp
