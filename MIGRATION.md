# AHFL Migration Notes

## V0.18 to V0.19

V0.19 extends the V0.18 durable receipt persistence request boundary with a
local, deterministic durable receipt persistence response boundary. It does not
introduce a production durable store adapter, persistence executor, object
storage writer, persistence id allocator, crash recovery runtime, or provider
connector.

### New Artifact Boundary

The V0.19 artifact chain is:

```text
ExecutionPlan
-> RuntimeSession
-> ExecutionJournal
-> ReplayView
-> SchedulerSnapshot
-> CheckpointRecord
-> CheckpointPersistenceDescriptor
-> PersistenceExportManifest
-> StoreImportDescriptor
-> Request
-> Decision
-> Receipt
-> PersistenceRequest
-> Response
-> ResponseReviewSummary
```

The machine-facing response format is:

```text
ahfl.durable-store-import-decision-receipt-persistence-response.v1
```

Future durable store adapter consumers should treat `Response` as the first
machine-facing source of response state. `ResponseReviewSummary` is only a
reviewer-facing projection.

### New CLI Commands

Use the new V0.19 commands when you need the response boundary after a V0.18
persistence request:

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-receipt-persistence-response \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-receipt-persistence-response-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

### Response Outcomes

V0.19 validates the response model across these outcome paths:

- `accepted`
- `blocked`
- `deferred`
- `rejected`

The CLI golden fixtures cover accepted and rejected response output through the
normal single-file, project, workspace, and `--package` emission paths. Blocked
and deferred response semantics are fixed by direct model/bootstrap regression
because the current CLI fixtures do not expose separate natural source fixtures
for those response outcomes.

## V0.19 to V0.20

V0.20 extends the V0.19 durable receipt persistence response boundary with a
production-adjacent local fake durable store adapter execution contract. It
keeps consuming the V0.19 `Response` artifact as the first machine-facing source
of adapter response state and does not introduce a real provider SDK,
credential, object storage writer, database writer, recovery daemon, or operator
console.

### New Artifact Boundary

The V0.20 artifact chain is:

```text
Response
-> AdapterExecutionReceipt
-> RecoveryCommandPreview
```

The machine-facing adapter execution format is:

```text
ahfl.durable-store-import-adapter-execution.v1
```

The reviewer-facing recovery preview format is:

```text
ahfl.durable-store-import-recovery-command-preview.v1
```

The local fake durable store contract version is:

```text
ahfl.local-fake-durable-store.v1
```

### New CLI Commands

Use the new V0.20 commands when you need the production-adjacent fake store
execution boundary after a V0.19 response:

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-adapter-execution \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-recovery-preview \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

### Adapter Execution Outcomes

V0.20 validates these adapter execution paths:

- `accepted`: generates mutating `PersistReceipt`, `persisted` mutation status,
  and deterministic `fake-persistence::<package>::<session>::accepted`
  persistence id.
- `blocked`: does not mutate the fake store and preserves source response
  blocker attribution.
- `deferred`: does not mutate the fake store and preserves required capability
  attribution.
- `rejected`: does not mutate the fake store and preserves failure attribution.

The V0.20 plan and completed backlog are documented in:

- `docs/plan/roadmap-v0.20.zh.md`
- `docs/plan/issue-backlog-v0.20.zh.md`

V0.20 adapter execution work must not bypass V0.19 compatibility. New durable
store adapter execution artifacts must not infer state from review summaries,
CLI text, traces, host logs, provider payloads, object paths, database keys, or
private scripts.

### Compatibility Rules

Consumers may depend on:

- the response `format_version`
- response identity and source version chain
- response outcome
- response blocker and capability gap fields
- the boundary distinction between `Response` and `ResponseReviewSummary`
- adapter execution identity, mutation intent, mutation status, deterministic
  fake persistence id, and failure attribution
- recovery preview next action and next-step recommendation

Consumers must not infer durable adapter state from review summaries, compiler
text, traces, host logs, provider payloads, object paths, database keys, or
private scripts.

Breaking changes to stable response fields require a format version bump and
updates to the V0.19 compatibility references and golden fixtures.

### Regression Commands

Use these commands to validate the V0.19 migration surface:

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.19
ctest --preset test-dev --output-on-failure -L 'v0.19-durable-store-import-receipt-persistence-response-(model|bootstrap|review-model|emission|golden)'
ctest --preset test-dev --output-on-failure -L ahfl-v0.20
```

For full local confidence:

```sh
ctest --preset test-dev --output-on-failure
cmake --preset asan
cmake --build --preset build-asan
ctest --preset test-asan --output-on-failure
```

### Reference Docs

- `docs/plan/roadmap-v0.19.zh.md`
- `docs/plan/issue-backlog-v0.19.zh.md`
- `docs/plan/roadmap-v0.20.zh.md`
- `docs/plan/issue-backlog-v0.20.zh.md`
- `docs/design/native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md`
- `docs/design/native-durable-store-adapter-execution-prototype-bootstrap-v0.20.zh.md`
- `docs/reference/durable-store-adapter-receipt-persistence-response-prototype-compatibility-v0.19.zh.md`
- `docs/reference/durable-store-adapter-execution-prototype-compatibility-v0.20.zh.md`
- `docs/reference/native-consumer-matrix-v0.19.zh.md`
- `docs/reference/native-consumer-matrix-v0.20.zh.md`
- `docs/reference/contributor-guide-v0.19.zh.md`
- `docs/reference/contributor-guide-v0.20.zh.md`

## V0.20 to V0.21

V0.21 extends the V0.20 local fake durable store adapter execution boundary with
a provider-neutral adapter planning boundary. It keeps consuming the V0.20
`AdapterExecutionReceipt` as the first machine-facing source of adapter
execution state and does not introduce a real provider SDK, credential, object
storage writer, database writer, recovery daemon, retry token, or resume token.

### New Artifact Boundary

The V0.21 artifact chain is:

```text
AdapterExecutionReceipt
-> ProviderWriteAttemptPreview
-> ProviderRecoveryHandoffPreview
```

The machine-facing provider write attempt format is:

```text
ahfl.durable-store-import-provider-write-attempt-preview.v1
```

The reviewer-facing provider recovery handoff format is:

```text
ahfl.durable-store-import-provider-recovery-handoff-preview.v1
```

### New CLI Commands

Use the new V0.21 commands when you need the provider-neutral planning boundary
after a V0.20 adapter execution receipt:

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-write-attempt \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-recovery-handoff \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

### Provider Planning Outcomes

V0.21 validates these provider-neutral paths:

- `planned`: accepted and persisted adapter execution generates mutating
  `ProviderPersistReceipt`, deterministic `provider-persistence::<package>::<session>::accepted`
  provider persistence id, and retry / resume placeholder identities.
- `not_planned`: blocked, deferred, rejected, or capability-gap paths stay
  non-mutating and preserve provider planning failure attribution.

V0.21 artifacts must not infer state from `RecoveryCommandPreview`, review
summaries, CLI text, traces, host logs, provider payloads, object paths,
database keys, credentials, or private scripts.

### Regression Commands

Use these commands to validate the V0.21 migration surface:

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.21
ctest --preset test-dev --output-on-failure -L 'v0.21-durable-store-import-provider-adapter-(emission|golden)'
```

### Reference Docs

- `docs/plan/roadmap-v0.21.zh.md`
- `docs/plan/issue-backlog-v0.21.zh.md`
- `docs/design/native-durable-store-provider-adapter-prototype-bootstrap-v0.21.zh.md`
- `docs/reference/durable-store-provider-adapter-prototype-compatibility-v0.21.zh.md`
- `docs/reference/native-consumer-matrix-v0.21.zh.md`
- `docs/reference/contributor-guide-v0.21.zh.md`

## V0.21 to V0.22

V0.22 extends the V0.21 provider-neutral durable store adapter planning boundary
with a provider driver binding boundary. It keeps consuming the V0.21
`ProviderWriteAttemptPreview` as the first machine-facing source of provider
write planning state and does not introduce a real provider SDK, credential,
endpoint URI, object storage writer, database writer, recovery daemon, retry
token, or resume token.

### New Artifact Boundary

The V0.22 artifact chain is:

```text
ProviderWriteAttemptPreview
-> ProviderDriverBindingPlan
-> ProviderDriverReadinessReview
```

The machine-facing provider driver binding format is:

```text
ahfl.durable-store-import-provider-driver-binding-plan.v1
```

The reviewer-facing provider driver readiness format is:

```text
ahfl.durable-store-import-provider-driver-readiness-review.v1
```

### New CLI Commands

Use the new V0.22 commands when you need the provider driver binding boundary
after a V0.21 provider write attempt:

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-driver-binding \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-driver-readiness \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

### Driver Binding Outcomes

V0.22 validates these provider driver paths:

- `bound`: planned provider write attempt plus matching secret-free driver
  profile generates `TranslateProviderPersistReceipt`, deterministic
  `provider-driver-operation::<provider-persistence-id>` operation descriptor,
  and `invokes_provider_sdk = false`.
- `not_bound`: source-not-planned, profile mismatch, or driver capability-gap
  paths stay non-SDK and preserve driver binding failure attribution.

V0.22 artifacts must not infer state from readiness reviews, recovery handoff
text, review summaries, CLI text, traces, host logs, provider payloads, object
paths, database tables, credentials, endpoint URI, or private scripts.

### Regression Commands

Use these commands to validate the V0.22 migration surface:

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.22
ctest --preset test-dev --output-on-failure -L 'v0.22-durable-store-import-provider-driver-(emission|golden)'
```

### Reference Docs

- `docs/plan/roadmap-v0.22.zh.md`
- `docs/plan/issue-backlog-v0.22.zh.md`
- `docs/design/native-durable-store-provider-driver-prototype-bootstrap-v0.22.zh.md`
- `docs/reference/durable-store-provider-driver-prototype-compatibility-v0.22.zh.md`
- `docs/reference/native-consumer-matrix-v0.22.zh.md`
- `docs/reference/contributor-guide-v0.22.zh.md`

## V0.22 to V0.23

V0.23 extends the V0.22 provider driver binding boundary with a provider runtime
preflight boundary. It keeps consuming the V0.22 `ProviderDriverBindingPlan` as
the first machine-facing source of provider driver operation state and does not
introduce a real provider SDK, runtime config loader, secret manager, credential,
endpoint URI, object storage writer, database writer, recovery daemon, retry
token, or resume token.

### New Artifact Boundary

The V0.23 artifact chain is:

```text
ProviderDriverBindingPlan
-> ProviderRuntimePreflightPlan
-> ProviderRuntimeReadinessReview
```

The machine-facing provider runtime preflight format is:

```text
ahfl.durable-store-import-provider-runtime-preflight-plan.v1
```

The reviewer-facing provider runtime readiness format is:

```text
ahfl.durable-store-import-provider-runtime-readiness-review.v1
```

### New CLI Commands

Use the new V0.23 commands when you need the runtime / SDK adapter preflight
boundary after a V0.22 provider driver binding:

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-runtime-preflight \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-runtime-readiness \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

### Runtime Preflight Outcomes

V0.23 validates these provider runtime paths:

- `ready`: bound provider driver binding plus matching secret-free runtime
  profile generates `PlanProviderSdkInvocationEnvelope`, deterministic
  `provider-sdk-invocation-envelope::<provider-driver-operation-id>` identity,
  and side-effect flags all set to `false`.
- `blocked`: driver-binding-not-ready, runtime profile mismatch, or runtime
  capability-gap paths stay non-SDK and preserve runtime preflight failure
  attribution.

V0.23 artifacts must not infer state from readiness reviews, CLI text, traces,
host logs, secret manager responses, runtime config loader output, provider
payloads, object paths, database tables, credentials, endpoint URI, or private
scripts.

### Regression Commands

Use these commands to validate the V0.23 migration surface:

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.23
ctest --preset test-dev --output-on-failure -L 'v0.23-durable-store-import-provider-runtime-(emission|golden)'
```

### Reference Docs

- `docs/plan/roadmap-v0.23.zh.md`
- `docs/plan/issue-backlog-v0.23.zh.md`
- `docs/design/native-durable-store-provider-runtime-preflight-prototype-bootstrap-v0.23.zh.md`
- `docs/reference/durable-store-provider-runtime-preflight-prototype-compatibility-v0.23.zh.md`
- `docs/reference/native-consumer-matrix-v0.23.zh.md`
- `docs/reference/contributor-guide-v0.23.zh.md`

## V0.23 to V0.24

V0.24 extends the V0.23 provider runtime preflight boundary with a provider SDK
request envelope boundary. It keeps consuming the V0.23
`ProviderRuntimePreflightPlan` as the first machine-facing source of SDK
invocation envelope state and does not introduce a real provider SDK, runtime
config loader, secret manager, credential, endpoint URI, object storage writer,
database writer, host process, network connection, retry token, or resume token.

### New Artifact Boundary

The V0.24 artifact chain is:

```text
ProviderRuntimePreflightPlan
-> ProviderSdkRequestEnvelopePlan
-> ProviderSdkHandoffReadinessReview
```

The machine-facing provider SDK request envelope format is:

```text
ahfl.durable-store-import-provider-sdk-request-envelope-plan.v1
```

The reviewer-facing provider SDK handoff readiness format is:

```text
ahfl.durable-store-import-provider-sdk-handoff-readiness-review.v1
```

### New CLI Commands

Use the new V0.24 commands when you need the SDK request envelope / host
handoff boundary after a V0.23 provider runtime preflight:

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-envelope \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-handoff-readiness \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

### SDK Envelope Outcomes

V0.24 validates these provider SDK envelope paths:

- `ready`: ready runtime preflight plus matching secret-free envelope policy
  generates `PlanProviderSdkRequestEnvelope`, deterministic
  `provider-sdk-request-envelope::<provider-sdk-invocation-envelope-id>`
  identity, deterministic
  `provider-sdk-host-handoff::<provider-sdk-invocation-envelope-id>` identity,
  and side-effect flags all set to `false`.
- `blocked`: runtime-preflight-not-ready, envelope policy mismatch, or envelope
  capability-gap paths stay non-SDK and preserve SDK envelope failure
  attribution.

V0.24 artifacts must not infer state from readiness reviews, CLI text, traces,
host logs, secret manager responses, runtime config loader output, provider
payloads, SDK payloads, object paths, database tables, credentials, endpoint
URI, host commands, network endpoints, or private scripts.

### Regression Commands

Use these commands to validate the V0.24 migration surface:

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.24
ctest --preset test-dev --output-on-failure -L 'v0.24-durable-store-import-provider-sdk-(emission|golden)'
```

### Reference Docs

- `docs/plan/roadmap-v0.24.zh.md`
- `docs/plan/issue-backlog-v0.24.zh.md`
- `docs/design/native-durable-store-provider-sdk-envelope-prototype-bootstrap-v0.24.zh.md`
- `docs/reference/durable-store-provider-sdk-envelope-prototype-compatibility-v0.24.zh.md`
- `docs/reference/native-consumer-matrix-v0.24.zh.md`
- `docs/reference/contributor-guide-v0.24.zh.md`

## V0.24 to V0.25

V0.25 extends the V0.24 provider SDK request envelope boundary with a provider
host execution planning boundary. It keeps consuming the V0.24
`ProviderSdkRequestEnvelopePlan` as the first machine-facing source of host
handoff state and does not introduce a real provider SDK, runtime config loader,
secret manager, credential, endpoint URI, object storage writer, database
writer, host process, host environment read, network connection, filesystem
write, retry token, or resume token.

### New Artifact Boundary

The V0.25 artifact chain is:

```text
ProviderSdkRequestEnvelopePlan
-> ProviderHostExecutionPlan
-> ProviderHostExecutionReadinessReview
```

The machine-facing provider host execution format is:

```text
ahfl.durable-store-import-provider-host-execution-plan.v1
```

The reviewer-facing provider host execution readiness format is:

```text
ahfl.durable-store-import-provider-host-execution-readiness-review.v1
```

### New CLI Commands

Use the new V0.25 commands when you need the host execution planning boundary
after a V0.24 provider SDK request envelope:

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-host-execution \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-host-execution-readiness \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

### Host Execution Outcomes

V0.25 validates these provider host execution paths:

- `ready`: ready SDK request envelope plus matching secret-free, network-free
  host execution policy generates `PlanProviderHostExecution`, deterministic
  `provider-host-execution-descriptor::<provider-sdk-host-handoff-id>`
  identity, deterministic
  `provider-host-receipt-placeholder::<provider-sdk-host-handoff-id>`
  identity, and side-effect flags all set to `false`.
- `blocked`: sdk-handoff-not-ready, host policy mismatch, or host
  capability-gap paths stay non-executing and preserve host execution failure
  attribution.

V0.25 artifacts must not infer state from readiness reviews, CLI text, traces,
host logs, secret manager responses, runtime config loader output, provider
payloads, SDK payloads, object paths, database tables, credentials, endpoint
URI, host commands, host environment, network endpoints, filesystem output, or
private scripts.

### Regression Commands

Use these commands to validate the V0.25 migration surface:

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.25
ctest --preset test-dev --output-on-failure -L 'v0.25-durable-store-import-provider-host-execution-(emission|golden)'
```

### Reference Docs

- `docs/plan/roadmap-v0.25.zh.md`
- `docs/plan/issue-backlog-v0.25.zh.md`
- `docs/design/native-durable-store-provider-host-execution-prototype-bootstrap-v0.25.zh.md`
- `docs/reference/durable-store-provider-host-execution-prototype-compatibility-v0.25.zh.md`
- `docs/reference/native-consumer-matrix-v0.25.zh.md`
- `docs/reference/contributor-guide-v0.25.zh.md`

## V0.25 to V0.26

V0.26 extends the V0.25 provider host execution planning boundary with a
provider local host execution receipt boundary. It keeps consuming the V0.25
`ProviderHostExecutionPlan` as the first machine-facing source of host execution
state and does not introduce a real provider SDK, runtime config loader, secret
manager, credential, endpoint URI, object storage writer, database writer, host
process, host environment read, network connection, filesystem write, retry
token, or resume token.

### New Artifact Boundary

The V0.26 artifact chain is:

```text
ProviderHostExecutionPlan
-> ProviderLocalHostExecutionReceipt
-> ProviderLocalHostExecutionReceiptReview
```

The machine-facing provider local host execution receipt format is:

```text
ahfl.durable-store-import-provider-local-host-execution-receipt.v1
```

The reviewer-facing provider local host execution receipt review format is:

```text
ahfl.durable-store-import-provider-local-host-execution-receipt-review.v1
```

### New CLI Commands

Use the new V0.26 commands when you need the simulated local host execution
receipt boundary after a V0.25 provider host execution plan:

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-local-host-execution-receipt \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-local-host-execution-receipt-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

### Local Host Execution Receipt Outcomes

V0.26 validates these provider local host execution receipt paths:

- `simulated_ready`: ready provider host execution plan generates
  `SimulateProviderLocalHostExecutionReceipt`, deterministic
  `provider-local-host-execution-receipt::<provider-host-execution-descriptor-id>`
  identity, and side-effect flags all set to `false`.
- `blocked`: host-execution-not-ready paths stay non-executing and preserve
  failure attribution.

V0.26 artifacts must not infer state from readiness reviews, CLI text, traces,
host logs, secret manager responses, runtime config loader output, provider
payloads, SDK payloads, object paths, database tables, credentials, endpoint
URI, host commands, host environment, network endpoints, filesystem output, or
private scripts.

### Regression Commands

Use these commands to validate the V0.26 migration surface:

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.26
ctest --preset test-dev --output-on-failure -L 'v0.26-durable-store-import-provider-local-host-execution-receipt-(emission|golden)'
```

### Reference Docs

- `docs/plan/roadmap-v0.26.zh.md`
- `docs/plan/issue-backlog-v0.26.zh.md`
- `docs/design/native-durable-store-provider-local-host-execution-prototype-bootstrap-v0.26.zh.md`
- `docs/reference/durable-store-provider-local-host-execution-prototype-compatibility-v0.26.zh.md`
- `docs/reference/native-consumer-matrix-v0.26.zh.md`
- `docs/reference/contributor-guide-v0.26.zh.md`

## V0.26 to V0.27

V0.27 extends the V0.26 provider local host execution receipt boundary with a
provider SDK adapter request / response artifact boundary. It keeps consuming
the V0.26 `ProviderLocalHostExecutionReceipt` as the first machine-facing source
of SDK adapter state and does not introduce a real provider SDK, runtime config
loader, secret manager, credential, endpoint URI, object storage writer,
database writer, host process, host environment read, network connection,
filesystem write, retry token, resume token, SDK payload, or provider response
payload.

### New Artifact Boundary

The V0.27 artifact chain is:

```text
ProviderLocalHostExecutionReceipt
-> ProviderSdkAdapterRequestPlan
-> ProviderSdkAdapterResponsePlaceholder
-> ProviderSdkAdapterReadinessReview
```

The machine-facing provider SDK adapter request format is:

```text
ahfl.durable-store-import-provider-sdk-adapter-request-plan.v1
```

The machine-facing provider SDK adapter response placeholder format is:

```text
ahfl.durable-store-import-provider-sdk-adapter-response-placeholder.v1
```

The reviewer-facing provider SDK adapter readiness format is:

```text
ahfl.durable-store-import-provider-sdk-adapter-readiness-review.v1
```

### New CLI Commands

Use the new V0.27 commands when you need the SDK adapter artifact boundary after
a V0.26 provider local host execution receipt:

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-adapter-request \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-adapter-response-placeholder \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-adapter-readiness \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

### SDK Adapter Outcomes

V0.27 validates these provider SDK adapter paths:

- `ready`: ready provider local host execution receipt generates
  `PlanProviderSdkAdapterRequest`, deterministic
  `provider-sdk-adapter-request::<provider-local-host-execution-receipt-id>`
  identity, deterministic
  `provider-sdk-adapter-response-placeholder::<provider-sdk-adapter-request-id>`
  identity, and side-effect flags all set to `false`.
- `blocked`: local-host-execution-not-ready paths stay non-SDK and preserve
  failure attribution.

V0.27 artifacts must not infer state from readiness reviews, CLI text, traces,
host logs, secret manager responses, runtime config loader output, provider
payloads, SDK payloads, object paths, database tables, credentials, endpoint
URI, host commands, host environment, network endpoints, filesystem output, or
private scripts.

### Regression Commands

Use these commands to validate the V0.27 migration surface:

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.27
ctest --preset test-dev --output-on-failure -L 'v0.27-durable-store-import-provider-sdk-adapter-(emission|golden)'
```

### Reference Docs

- `docs/plan/roadmap-v0.27.zh.md`
- `docs/plan/issue-backlog-v0.27.zh.md`
- `docs/design/native-durable-store-provider-sdk-adapter-prototype-bootstrap-v0.27.zh.md`
- `docs/reference/durable-store-provider-sdk-adapter-prototype-compatibility-v0.27.zh.md`
- `docs/reference/native-consumer-matrix-v0.27.zh.md`
- `docs/reference/contributor-guide-v0.27.zh.md`

## V0.27 to V0.28

V0.28 extends the V0.27 provider SDK adapter request boundary with a provider
SDK adapter interface descriptor boundary. It keeps consuming the V0.27
`ProviderSdkAdapterRequestPlan` as the first machine-facing source of adapter
interface state and does not introduce a real provider SDK, dynamic plugin
loader, registry discovery service, runtime config loader, secret manager,
credential, endpoint URI, network connection, filesystem write, object store
writer, database writer, SDK payload, or provider response payload.

### New Artifact Boundary

The V0.28 artifact chain is:

```text
ProviderSdkAdapterRequestPlan
-> ProviderSdkAdapterInterfacePlan
-> ProviderSdkAdapterInterfaceReview
```

The machine-facing provider SDK adapter interface format is:

```text
ahfl.durable-store-import-provider-sdk-adapter-interface-plan.v1
```

The reviewer-facing provider SDK adapter interface review format is:

```text
ahfl.durable-store-import-provider-sdk-adapter-interface-review.v1
```

The stable interface ABI and error taxonomy strings are:

```text
ahfl.provider-sdk-adapter-interface.v1
ahfl.provider-sdk-adapter-error-taxonomy.v1
```

### New CLI Commands

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-adapter-interface \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-adapter-interface-review \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

### Interface Outcomes

V0.28 validates these provider SDK adapter interface paths:

- `ready`: ready V0.27 request plan generates deterministic provider registry,
  adapter descriptor, capability descriptor, and interface artifact identities.
- `blocked`: sdk-adapter-request-not-ready paths preserve failure attribution and
  normalize the error to `sdk_adapter_request_not_ready`.

V0.28 artifacts must not infer state from readiness reviews, CLI text, traces,
host logs, secret manager responses, runtime config loader output, provider
payloads, SDK payloads, credentials, endpoint URI, network endpoints,
filesystem output, dynamic plugins, or private scripts.

### Regression Commands

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.28
ctest --preset test-dev --output-on-failure -L 'v0.28-durable-store-import-provider-sdk-adapter-interface-(emission|golden)'
```

### Reference Docs

- `docs/plan/roadmap-v0.28.zh.md`
- `docs/plan/issue-backlog-v0.28.zh.md`
- `docs/design/native-durable-store-provider-sdk-adapter-interface-bootstrap-v0.28.zh.md`
- `docs/reference/durable-store-provider-sdk-adapter-interface-compatibility-v0.28.zh.md`
- `docs/reference/native-consumer-matrix-v0.28.zh.md`
- `docs/reference/contributor-guide-v0.28.zh.md`

## V0.28 to V0.29

V0.29 extends the V0.28 provider SDK adapter interface boundary with a provider
config loader boundary. It keeps consuming the V0.28
`ProviderSdkAdapterInterfacePlan` as the first machine-facing source of config
state and does not introduce secret values, credential material, remote config
service calls, provider endpoint URI, network connections, filesystem writes,
SDK payloads, provider response payloads, or production tenant provisioning.

### New Artifact Boundary

The V0.29 artifact chain is:

```text
ProviderSdkAdapterInterfacePlan
-> ProviderConfigLoadPlan
-> ProviderConfigSnapshotPlaceholder
-> ProviderConfigReadinessReview
```

The machine-facing provider config formats are:

```text
ahfl.durable-store-import-provider-config-load-plan.v1
ahfl.durable-store-import-provider-config-snapshot-placeholder.v1
```

The reviewer-facing provider config readiness format is:

```text
ahfl.durable-store-import-provider-config-readiness-review.v1
```

The stable config schema string is:

```text
ahfl.provider-config-schema.v1
```

### New CLI Commands

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-config-load \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-config-snapshot \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl

./build/dev/src/cli/ahflc emit-durable-store-import-provider-config-readiness \
  --package tests/ir/ok_workflow_value_flow.package.json \
  --capability-mocks tests/dry_run/ok_workflow_value_flow.mocks.json \
  --input-fixture fixture.request.basic \
  --run-id run-001 \
  tests/ir/ok_workflow_value_flow.ahfl
```

### Config Outcomes

V0.29 validates these provider config paths:

- `ready`: ready V0.28 interface plan generates deterministic config load,
  config profile, and snapshot placeholder identities.
- `blocked`: adapter-interface-not-ready paths preserve failure attribution and
  produce blocked config load / snapshot artifacts.

V0.29 artifacts must not infer state from readiness reviews, CLI text, traces,
host logs, secret manager responses, remote config service responses, runtime
config loader output, provider payloads, SDK payloads, credentials, endpoint
URI, network endpoints, filesystem output, dynamic plugins, or private scripts.

### Regression Commands

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L ahfl-v0.29
ctest --preset test-dev --output-on-failure -L 'v0.29-durable-store-import-provider-config-(emission|golden)'
```

### Reference Docs

- `docs/plan/roadmap-v0.29.zh.md`
- `docs/plan/issue-backlog-v0.29.zh.md`
- `docs/design/native-durable-store-provider-config-loader-bootstrap-v0.29.zh.md`
- `docs/reference/durable-store-provider-config-loader-compatibility-v0.29.zh.md`
- `docs/reference/native-consumer-matrix-v0.29.zh.md`
- `docs/reference/contributor-guide-v0.29.zh.md`

## V0.29 to V0.33

V0.30 through V0.33 extend the provider config loader into a secret-free,
test-only provider adapter preparation chain. The sequence is:

```text
ProviderConfigSnapshotPlaceholder
-> ProviderSecretPolicyReview
-> ProviderLocalHostHarnessReview
-> ProviderSdkPayloadAuditSummary
-> ProviderSdkMockAdapterReadiness
```

The new boundaries are intentionally non-production:

- V0.30 defines secret handles and resolver placeholders without secret values.
- V0.31 defines a sandboxed test-only local host harness without network,
  secret injection, host env reads, filesystem writes, or real provider SDK.
- V0.32 defines fake SDK payload materialization with digest and redaction
  summary, but no raw payload persistence.
- V0.33 defines a provider SDK mock adapter covering success, failure,
  timeout, throttle, conflict, and schema mismatch.

### New CLI Commands

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-secret-resolver-request ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-secret-resolver-response ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-secret-policy-review ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-local-host-harness-request ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-local-host-harness-record ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-local-host-harness-review ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-payload-plan ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-payload-audit ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-mock-adapter-contract ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-mock-adapter-execution ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-sdk-mock-adapter-readiness ...
```

### Regression Commands

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L 'ahfl-v0.(30|31|32|33)'
```

### Reference Docs

- `docs/plan/roadmap-v0.30.zh.md`
- `docs/plan/roadmap-v0.31.zh.md`
- `docs/plan/roadmap-v0.32.zh.md`
- `docs/plan/roadmap-v0.33.zh.md`
- `docs/design/native-durable-store-provider-secret-resolver-bootstrap-v0.30.zh.md`
- `docs/design/native-durable-store-provider-local-host-harness-bootstrap-v0.31.zh.md`
- `docs/design/native-durable-store-provider-sdk-payload-materialization-bootstrap-v0.32.zh.md`
- `docs/design/native-durable-store-provider-sdk-mock-adapter-bootstrap-v0.33.zh.md`

## V0.33 to V0.36

V0.34 through V0.36 extend the provider SDK mock adapter into the first
opt-in real provider alpha, then add idempotency / retry and commit receipt
boundaries. The sequence is:

```text
ProviderSdkMockAdapterReadiness
-> ProviderLocalFilesystemAlphaReadiness
-> ProviderWriteRetryDecision
-> ProviderWriteCommitReceipt
-> ProviderWriteCommitReview
```

The new boundaries remain conservative:

- V0.34 introduces `local-filesystem-alpha` as a low-risk real provider alpha,
  but CLI defaults to dry-run and does not write files.
- V0.35 defines idempotency namespace, retry token placeholder, retry
  eligibility, and duplicate detection summary.
- V0.36 defines secret-free commit receipt and reviewer-facing commit review.

### New CLI Commands

```sh
./build/dev/src/cli/ahflc emit-durable-store-import-provider-local-filesystem-alpha-plan ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-local-filesystem-alpha-result ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-local-filesystem-alpha-readiness ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-write-retry-decision ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-write-commit-receipt ...
./build/dev/src/cli/ahflc emit-durable-store-import-provider-write-commit-review ...
```

### Regression Commands

```sh
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -L 'ahfl-v0.(34|35|36)'
```

### Reference Docs

- `docs/plan/roadmap-v0.34.zh.md`
- `docs/plan/roadmap-v0.35.zh.md`
- `docs/plan/roadmap-v0.36.zh.md`
- `docs/design/native-durable-store-provider-local-filesystem-alpha-bootstrap-v0.34.zh.md`
- `docs/design/native-durable-store-provider-idempotency-retry-bootstrap-v0.35.zh.md`
- `docs/design/native-durable-store-provider-write-commit-receipt-bootstrap-v0.36.zh.md`
