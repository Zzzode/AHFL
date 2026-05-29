#include "tooling/cli/provider/provider_step_generic.hpp"

namespace ahfl::cli {

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_config_load_for_cli,
    ProviderConfigLoadPlan,
    SdkAdapterInterface,
    build_provider_config_load_plan,
    plan)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_config_snapshot_for_cli,
    ProviderConfigSnapshotPlaceholder,
    ConfigLoad,
    build_provider_config_snapshot_placeholder,
    placeholder)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_config_readiness_for_cli,
    ProviderConfigReadinessReview,
    ConfigSnapshot,
    build_provider_config_readiness_review,
    review)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_secret_resolver_request_for_cli,
    ProviderSecretResolverRequestPlan,
    ConfigSnapshot,
    build_provider_secret_resolver_request_plan,
    plan)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_secret_resolver_response_for_cli,
    ProviderSecretResolverResponsePlaceholder,
    SecretResolverRequest,
    build_provider_secret_resolver_response_placeholder,
    placeholder)

AHFL_DEFINE_PROVIDER_STEP_1(
    build_provider_secret_policy_review_for_cli,
    ProviderSecretPolicyReview,
    SecretResolverResponse,
    build_provider_secret_policy_review,
    review)

AHFL_DEFINE_PROVIDER_STEP_2(
    build_provider_config_bundle_validation_report_for_cli,
    ProviderConfigBundleValidationReport,
    SelectionPlan,
    ConfigSnapshot,
    build_provider_config_bundle_validation_report,
    report)

} // namespace ahfl::cli
