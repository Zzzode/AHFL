#pragma once

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace ahfl::lsp {

struct LspAnalysisSnapshot;
struct LspSourceSnapshot;

// ⚠️ ORDER CONTRACT (mirrors sort key in hover_index.cpp:HoverTargetIndex::sort).
//
// HoverTargetIndex::sort() orders equal-range targets by
//   (priority ASC, enum-ordinal ASC).
//
// Therefore the ORDER OF VALUES IN THIS ENUM IS MEANINGFUL: within any
// priority group, lower ordinal = higher tie-break precedence. If you add a
// new value, place it with siblings that share its `default_priority()` (see
// hover_index.cpp). Keep the two rules below in sync.
//
// Rule 1 — Priority-0 group (Diagnostic → Construct family): ordinals 22..27.
// Rule 2 — Priority-1 group (ModuleName..LocalBinding, non-contiguous): the
//          explicit ordinals here DO NOT need to be contiguous because they
//          are at distinct priority levels; their relative order inside the
//          priority-1 group IS the tie-break order.
//
// Sentinel below is used by two static_asserts:
//   (a) cardinality exactly N public values
//   (b) ordinal sequence without gaps
enum class HoverTargetKind {
    ModuleName,                    // ordinal  0, priority 1
    ImportPath,                    // ordinal  1, priority 1
    ImportAlias,                   // ordinal  2, priority 1
    DeclarationName,               // ordinal  3, priority 1
    TypeReference,                 // ordinal  4, priority 1
    ConstReference,                // ordinal  5, priority 1
    CallableReference,             // ordinal  6, priority 1
    StructField,                   // ordinal  7, priority 1
    EnumVariant,                   // ordinal  8, priority 1
    CapabilityParam,               // ordinal  9, priority 1
    PredicateParam,                // ordinal 10, priority 1
    AgentSchemaLabel,              // ordinal 11, priority 1
    AgentState,                    // ordinal 12, priority 2
    AgentTransition,               // ordinal 13, priority 1
    FlowState,                     // ordinal 14, priority 1
    WorkflowSchemaLabel,           // ordinal 15, priority 1
    WorkflowTemporalClause,        // ordinal 16, priority 1
    WorkflowNode,                  // ordinal 17, priority 1
    WorkflowDependency,            // ordinal 18, priority 1
    Expression,                    // ordinal 19, priority 20 (lowest)
    MemberAccess,                  // ordinal 20, priority 2
    LocalBinding,                  // ordinal 21, priority 1
    Diagnostic,                    // ordinal 22, priority 0  — highest precedence group
    StructLiteral,                 // ordinal 23, priority 0
    // Wave-20 Lane 1: Construct Hover 4 子类（Construct family 扩展）。
    // 每个新 kind 的 priority 一律为 0（与 StructLiteral / Diagnostic 同最高级）避免 tie。
    EnumLiteral,                   // ordinal 24, priority 0 — E::V 枚举实例构造位置（Priority::High pattern）
    ConstEval,                     // ordinal 25, priority 0 — 常量引用 site：显示 "K = <value>"
    ContractInstantiation,         // ordinal 26, priority 0 — contract C for Target 的 for 右值 Target hover
    CapabilityInstantiation,       // ordinal 27, priority 0 — agent capabilities: [C1, C2] 中命名 capability 的引用位置

    // Internal sentinel — keep at the END. Do not add values after this line
    // without updating both static_asserts below.
    Sentinel_ForStaticAssert,      // ordinal 28
};

// Contract (a): exactly 28 public values — Sentinel sits at ordinal 28.
static_assert(std::to_underlying(HoverTargetKind::Sentinel_ForStaticAssert) == 28,
              "HoverTargetKind cardinality drift. Update this static_assert AND "
              "keep default_priority() in hover_index.cpp aligned. The enum "
              "order also controls tie-break precedence — see note above enum.");

// Contract (b): priority-0 group values (22..27) occupy exactly 6 contiguous
// slots, in the same order that the Construct-family tie-break expects.
static_assert(std::to_underlying(HoverTargetKind::Diagnostic) == 22 &&
                  std::to_underlying(HoverTargetKind::StructLiteral) == 23 &&
                  std::to_underlying(HoverTargetKind::EnumLiteral) == 24 &&
                  std::to_underlying(HoverTargetKind::ConstEval) == 25 &&
                  std::to_underlying(HoverTargetKind::ContractInstantiation) == 26 &&
                  std::to_underlying(HoverTargetKind::CapabilityInstantiation) == 27,
              "HoverTargetKind priority-0 group must be contiguous ordinals 22..27 "
              "(Diagnostic, StructLiteral, EnumLiteral, ConstEval, "
              "ContractInstantiation, CapabilityInstantiation). Tie-breaking in "
              "HoverTargetIndex::sort() relies on this ordinal order.");

struct HoverTarget {
    HoverTargetKind kind{HoverTargetKind::Expression};
    SourceRange token_range;
    std::optional<SourceId> source_id{};
    std::optional<SymbolId> symbol_id{};
    std::optional<SymbolId> owner_symbol_id{};
    std::optional<std::uint32_t> typed_expr_index{};
    std::optional<std::uint32_t> typed_decl_index{};
    std::string local_name{};
    std::string role{};
    std::string declared_spelling{};
    std::string source_label{};
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
