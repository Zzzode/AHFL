#include "tooling/cli/provider/provider_step_generic.hpp"

namespace ahfl::cli {

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_runtime_preflight_for_cli,
    ProviderRuntimePreflightPlan,
    DriverBinding,
    build_provider_runtime_preflight_plan,
    plan)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_runtime_readiness_for_cli,
    ProviderRuntimeReadinessReview,
    RuntimePreflight,
    build_provider_runtime_readiness_review,
    review)

} // namespace ahfl::cli
