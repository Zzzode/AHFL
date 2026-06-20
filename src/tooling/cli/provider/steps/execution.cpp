#include "tooling/cli/provider/provider_step_generic.hpp"

namespace ahfl::cli {

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_host_execution_for_cli,
                            ProviderHostExecutionPlan,
                            SdkEnvelope,
                            build_provider_host_execution_plan,
                            plan)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_host_execution_readiness_for_cli,
                            ProviderHostExecutionReadinessReview,
                            HostExecution,
                            build_provider_host_execution_readiness_review,
                            review)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_local_host_execution_receipt_for_cli,
                            ProviderLocalHostExecutionReceipt,
                            HostExecution,
                            build_provider_local_host_execution_receipt,
                            receipt)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_local_host_execution_receipt_review_for_cli,
                            ProviderLocalHostExecutionReceiptReview,
                            LocalHostExecutionReceipt,
                            build_provider_local_host_execution_receipt_review,
                            review)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_local_host_harness_request_for_cli,
                            ProviderLocalHostHarnessExecutionRequest,
                            SecretPolicyReview,
                            build_provider_local_host_harness_execution_request,
                            request)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_local_host_harness_record_for_cli,
                            ProviderLocalHostHarnessExecutionRecord,
                            LocalHostHarnessRequest,
                            run_provider_local_host_test_harness,
                            record)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_local_host_harness_review_for_cli,
                            ProviderLocalHostHarnessReview,
                            LocalHostHarnessRecord,
                            build_provider_local_host_harness_review,
                            review)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_local_filesystem_alpha_plan_for_cli,
                            ProviderLocalFilesystemAlphaPlan,
                            SdkMockAdapterReadiness,
                            build_provider_local_filesystem_alpha_plan,
                            plan)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_local_filesystem_alpha_result_for_cli,
                            ProviderLocalFilesystemAlphaResult,
                            LocalFilesystemAlphaPlan,
                            run_provider_local_filesystem_alpha,
                            result)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_local_filesystem_alpha_readiness_for_cli,
                            ProviderLocalFilesystemAlphaReadiness,
                            LocalFilesystemAlphaResult,
                            build_provider_local_filesystem_alpha_readiness,
                            readiness)

} // namespace ahfl::cli
