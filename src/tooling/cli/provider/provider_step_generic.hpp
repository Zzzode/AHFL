#pragma once

#include "pipeline/persistence/durable_store_import/provider.hpp"
#include "provider_pipeline_cache.hpp"
#include "tooling/cli/pipeline_emit.hpp"

#include <iostream>
#include <optional>
#include <string_view>

namespace ahfl::cli {

// Generates a builder function for a single-dependency provider step.
// The generated function:
//   1. Gets the dependency from cache
//   2. Calls the domain builder
//   3. Renders diagnostics
//   4. Returns the unwrapped result or nullopt
#define AHFL_DEFINE_PROVIDER_STEP_1(fn_name, return_type, dep_kind, domain_fn, result_field)       \
    [[nodiscard]] std::optional<ahfl::durable_store_import::return_type> fn_name(                  \
        const ahfl::ir::Program & /*program*/,                                                     \
        const ahfl::handoff::PackageMetadata & /*metadata*/,                                       \
        const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,                                     \
        const CommandLineOptions & /*options*/,                                                    \
        std::string_view /*command_name*/,                                                         \
        ProviderPipelineCache &cache) {                                                            \
        const auto *dep = cache.get<ProviderArtifactKind::dep_kind>();                             \
        if (dep == nullptr) {                                                                      \
            return std::nullopt;                                                                   \
        }                                                                                          \
        auto step_result = ahfl::durable_store_import::domain_fn(*dep);                            \
        step_result.diagnostics.render(std::cerr);                                                 \
        if (step_result.has_errors() || !step_result.result_field.has_value()) {                   \
            return std::nullopt;                                                                   \
        }                                                                                          \
        return *step_result.result_field;                                                          \
    }

// Generates a builder for a two-dependency provider step.
#define AHFL_DEFINE_PROVIDER_STEP_2(                                                               \
    fn_name, return_type, dep1_kind, dep2_kind, domain_fn, result_field)                           \
    [[nodiscard]] std::optional<ahfl::durable_store_import::return_type> fn_name(                  \
        const ahfl::ir::Program & /*program*/,                                                     \
        const ahfl::handoff::PackageMetadata & /*metadata*/,                                       \
        const ahfl::dry_run::CapabilityMockSet & /*mock_set*/,                                     \
        const CommandLineOptions & /*options*/,                                                    \
        std::string_view /*command_name*/,                                                         \
        ProviderPipelineCache &cache) {                                                            \
        const auto *dep1 = cache.get<ProviderArtifactKind::dep1_kind>();                           \
        if (dep1 == nullptr) {                                                                     \
            return std::nullopt;                                                                   \
        }                                                                                          \
        const auto *dep2 = cache.get<ProviderArtifactKind::dep2_kind>();                           \
        if (dep2 == nullptr) {                                                                     \
            return std::nullopt;                                                                   \
        }                                                                                          \
        auto step_result = ahfl::durable_store_import::domain_fn(*dep1, *dep2);                    \
        step_result.diagnostics.render(std::cerr);                                                 \
        if (step_result.has_errors() || !step_result.result_field.has_value()) {                   \
            return std::nullopt;                                                                   \
        }                                                                                          \
        return *step_result.result_field;                                                          \
    }

} // namespace ahfl::cli
