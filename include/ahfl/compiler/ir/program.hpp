#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/arena.hpp"
#include "ahfl/compiler/ir/decl.hpp"

namespace ahfl::ir {

// ----------------------------------------------------------------------------
// 派生分析 (Derived Analyses)
// ----------------------------------------------------------------------------

struct StateHandlerSummaryAnalysis {
    std::string flow_target;
    std::string state_name;
    std::size_t handler_index{0};
    StateHandler::Summary summary;
};

struct WorkflowNodeExprSummaryAnalysis {
    std::string workflow_name;
    std::string node_name;
    std::size_t node_index{0};
    WorkflowExprSummary summary;
};

struct WorkflowReturnExprSummaryAnalysis {
    std::string workflow_name;
    WorkflowExprSummary summary;
};

struct AnalysisBundle {
    std::uint64_t source_program_revision{0};
    std::vector<StateHandlerSummaryAnalysis> state_handler_summaries;
    std::vector<WorkflowNodeExprSummaryAnalysis> workflow_node_input_summaries;
    std::vector<WorkflowReturnExprSummaryAnalysis> workflow_return_summaries;
    std::vector<FormalObservation> formal_observations;
};

// ----------------------------------------------------------------------------
// 顶层 IR 结构 (Top-Level IR Structures)
// ----------------------------------------------------------------------------

/// 声明（variant，包含所有顶层声明类型）
using Decl = std::variant<ModuleDecl,
                          ImportDecl,
                          ConstDecl,
                          TypeAliasDecl,
                          StructDecl,
                          EnumDecl,
                          CapabilityDecl,
                          PredicateDecl,
                          AgentDecl,
                          ContractDecl,
                          FlowDecl,
                          WorkflowDecl>;

enum class ProgramPhase {
    Lowered,
    Analyzed,
    Optimized,
};

/// IR 程序 — 编译单元的完整 IR 表示
struct Program {
    std::string format_version{std::string(kFormatVersion)};
    ProgramPhase phase{ProgramPhase::Lowered};
    std::uint64_t analysis_revision{0};
    std::vector<Decl> declarations; // 所有顶层声明
    AnalysisBundle analyses;        // 可重算派生分析
    ExprArena expr_arena;           // Flat expression store (E-1)

    /// Return a span over all arena-registered expressions for O(1) traversal.
    [[nodiscard]] std::span<Expr *const> all_exprs() noexcept {
        return expr_arena.span();
    }
    [[nodiscard]] std::span<const Expr *const> all_exprs() const noexcept {
        return expr_arena.span();
    }
};

} // namespace ahfl::ir
