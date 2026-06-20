#include "tooling/cli/provider/provider_step_generic.hpp"

namespace ahfl::cli {

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_write_retry_decision_for_cli,
                            ProviderWriteRetryDecision,
                            SdkMockAdapterExecution,
                            build_provider_write_retry_decision,
                            decision)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_write_commit_receipt_for_cli,
                            ProviderWriteCommitReceipt,
                            WriteRetryDecision,
                            build_provider_write_commit_receipt,
                            receipt)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_write_commit_review_for_cli,
                            ProviderWriteCommitReview,
                            WriteCommitReceipt,
                            build_provider_write_commit_review,
                            review)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_write_recovery_checkpoint_for_cli,
                            ProviderWriteRecoveryCheckpoint,
                            WriteCommitReceipt,
                            build_provider_write_recovery_checkpoint,
                            checkpoint)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_write_recovery_plan_for_cli,
                            ProviderWriteRecoveryPlan,
                            WriteRecoveryCheckpoint,
                            build_provider_write_recovery_plan,
                            plan)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_write_recovery_review_for_cli,
                            ProviderWriteRecoveryReview,
                            WriteRecoveryPlan,
                            build_provider_write_recovery_review,
                            review)

AHFL_DEFINE_PROVIDER_STEP_2(build_provider_failure_taxonomy_report_for_cli,
                            ProviderFailureTaxonomyReport,
                            SdkMockAdapterExecution,
                            WriteRecoveryPlan,
                            build_provider_failure_taxonomy_report,
                            report)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_failure_taxonomy_review_for_cli,
                            ProviderFailureTaxonomyReview,
                            FailureTaxonomyReport,
                            build_provider_failure_taxonomy_review,
                            review)

} // namespace ahfl::cli
