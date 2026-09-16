#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/arena.hpp"
#include "ahfl/compiler/ir/decl.hpp"
#include "ahfl/compiler/ir/tower.hpp"

namespace ahfl::ir {

// ----------------------------------------------------------------------------
// Derived Analyses
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
// Top-Level IR Structures
// ----------------------------------------------------------------------------

/// Declaration (variant, including all top-level declaration types)
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
                          WorkflowDecl,
                          FnDecl,
                          TraitDecl,
                          ImplDecl,
                          InstanceDecl>;

// RFC 0027 P8 IR SSOT compile-time cardinality gate. Adding or removing an
// alternative without updating every exhaustive Decl visitor MUST fail the
// build; keep this pin adjacent to the declaration.
static_assert(std::variant_size_v<Decl> == 16,
              "ahfl::ir::Decl cardinality drift (RFC 0027 P8 IR SSOT): "
              "update every exhaustive visitor and this pin together with "
              "the alternative list.");

enum class ProgramPhase {
    Lowered,
    Analyzed,
    Optimized,
};

/// IR program — complete IR representation of a compilation unit
struct Program {
    std::string format_version{std::string(kFormatVersion)};
    ProgramPhase phase{ProgramPhase::Lowered};
    std::uint64_t analysis_revision{0};
    std::vector<Decl> declarations; // All top-level declarations
    AnalysisBundle analyses;        // Recomputable derived analyses
    ExprArena expr_arena;           // Flat expression store (E-1)

    /// Return a span over all arena-registered expressions for O(1) traversal.
    [[nodiscard]] std::span<Expr *const> all_exprs() noexcept {
        return expr_arena.span();
    }
    [[nodiscard]] std::span<const Expr *const> all_exprs() const noexcept {
        return expr_arena.span();
    }
};

// ----------------------------------------------------------------------------
// AHFL-IR layer boundary (RFC 0026, slice P2 / KR6.3)
// ----------------------------------------------------------------------------
//
// `AhflIr` names the verification / orchestration LAYER of the RFC 0026 IR
// tower (`tower::Layer::AhflIr`). This is the layer consumed by the
// verification backends (SMV / SMT-BMC) and the view backends (K8s / OpenAPI /
// Terraform / WASM-config): agent / flow / workflow / contract / effect /
// temporal / decreases are first-class here and generics are NOT yet
// monomorphized (that happens when this layer lowers to `CoreIr`).
//
// Today this layer is *structurally identical* to `Program` — the alias is an
// alias, not a distinct type, so re-pointing a backend entry point to
// `const AhflIr &` is a zero-behavior-change documentation of the layer
// boundary in the type surface. A later slice purifies the node set (drops the
// execution-only constructs and adds verification-only ones), at which point
// this becomes its own struct rather than an alias. Consumers that want the
// compile-time layer identity can pair it with `tower::AhflIrTag`.
using AhflIr = Program;

} // namespace ahfl::ir
