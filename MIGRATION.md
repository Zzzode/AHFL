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
