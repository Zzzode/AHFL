#include "tooling/cli/provider/provider_step_generic.hpp"

namespace ahfl::cli {

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_envelope_for_cli,
                            ProviderSdkRequestEnvelopePlan,
                            RuntimePreflight,
                            build_provider_sdk_request_envelope_plan,
                            plan)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_handoff_readiness_for_cli,
                            ProviderSdkHandoffReadinessReview,
                            SdkEnvelope,
                            build_provider_sdk_handoff_readiness_review,
                            review)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_adapter_request_for_cli,
                            ProviderSdkAdapterRequestPlan,
                            LocalHostExecutionReceipt,
                            build_provider_sdk_adapter_request_plan,
                            plan)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_adapter_response_placeholder_for_cli,
                            ProviderSdkAdapterResponsePlaceholder,
                            SdkAdapterRequest,
                            build_provider_sdk_adapter_response_placeholder,
                            placeholder)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_adapter_readiness_for_cli,
                            ProviderSdkAdapterReadinessReview,
                            SdkAdapterResponsePlaceholder,
                            build_provider_sdk_adapter_readiness_review,
                            review)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_adapter_interface_for_cli,
                            ProviderSdkAdapterInterfacePlan,
                            SdkAdapterRequest,
                            build_provider_sdk_adapter_interface_plan,
                            plan)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_adapter_interface_review_for_cli,
                            ProviderSdkAdapterInterfaceReview,
                            SdkAdapterInterface,
                            build_provider_sdk_adapter_interface_review,
                            review)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_payload_plan_for_cli,
                            ProviderSdkPayloadMaterializationPlan,
                            LocalHostHarnessReview,
                            build_provider_sdk_payload_materialization_plan,
                            plan)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_payload_audit_for_cli,
                            ProviderSdkPayloadAuditSummary,
                            SdkPayloadPlan,
                            build_provider_sdk_payload_audit_summary,
                            audit)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_mock_adapter_contract_for_cli,
                            ProviderSdkMockAdapterContract,
                            SdkPayloadAudit,
                            build_provider_sdk_mock_adapter_contract,
                            contract)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_mock_adapter_execution_for_cli,
                            ProviderSdkMockAdapterExecutionResult,
                            SdkMockAdapterContract,
                            run_provider_sdk_mock_adapter,
                            result)

AHFL_DEFINE_PROVIDER_STEP_1(build_provider_sdk_mock_adapter_readiness_for_cli,
                            ProviderSdkMockAdapterReadiness,
                            SdkMockAdapterExecution,
                            build_provider_sdk_mock_adapter_readiness,
                            readiness)

} // namespace ahfl::cli
