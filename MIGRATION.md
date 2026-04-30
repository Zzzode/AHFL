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
