#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#include "ahfl/compiler/ir/arena.hpp"
#include "ahfl/compiler/ir/decl.hpp"
#include "ahfl/compiler/ir/node_tags.hpp"
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

namespace node_detail {

#define HANDLE_DECL_NODE(Name, Wire) node_tag<Name>{},
inline constexpr auto kDeclNodeTags = std::tuple{
#include "ahfl/compiler/ir/decl_nodes.def"
};
#undef HANDLE_DECL_NODE

} // namespace node_detail

/// Declaration variant (KR6.13-F): the alternative list is generated from the
/// single X-macro node list decl_nodes.def, so declaration order there IS
/// alternative order and every exhaustive visitor / backend dispatch derived
/// from it stays in lockstep with the node set.
using Decl = node_detail::variant_from_tags_t<
    std::remove_cvref_t<decltype(node_detail::kDeclNodeTags)>>;

// RFC 0027 P8 IR SSOT compile-time cardinality gate. The count itself now
// derives from decl_nodes.def; this pin turns "a node was added to the .def"
// into a deliberate review event across every exhaustive Decl visitor.
static_assert(std::variant_size_v<Decl> == 16,
              "ahfl::ir::Decl cardinality drift (RFC 0027 P8 IR SSOT): "
              "update every exhaustive visitor and this pin together with "
              "decl_nodes.def.");

namespace decl_node_detail {

/// Ordered strong index of each Decl alternative (RFC 0027 Q1 pattern:
/// index-based identity, never strings).
enum class DeclNodeIndex : std::size_t {
#define HANDLE_DECL_NODE(Name, Wire) Name,
#include "ahfl/compiler/ir/decl_nodes.def"
};

/// JSON wire name of each Decl alternative, indexed by `Decl::index()`. The ONE
/// table the ir_json writer and reader resolve the `"kind"` spelling from.
inline constexpr std::array<std::string_view,
                            std::tuple_size_v<std::remove_cvref_t<decltype(
                                node_detail::kDeclNodeTags)>>>
    kDeclWireNames = {
#define HANDLE_DECL_NODE(Name, Wire) Wire,
#include "ahfl/compiler/ir/decl_nodes.def"
};

} // namespace decl_node_detail

/// JSON wire name of the Decl alternative at `variant_index` (a `Decl::index()`
/// value, therefore always in bounds).
[[nodiscard]] inline constexpr std::string_view
decl_node_wire_name(std::size_t variant_index) noexcept {
    return decl_node_detail::kDeclWireNames[variant_index];
}

/// JSON wire name of a declaration node (e.g. `"type_alias"`).
[[nodiscard]] inline std::string_view decl_node_wire_name(const Decl &node) noexcept {
    return decl_node_wire_name(node.index());
}

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
