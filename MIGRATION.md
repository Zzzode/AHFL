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

V0.20 starts the production-adjacent durable store adapter planning track. It is
expected to keep consuming the V0.19 `Response` artifact as the first
machine-facing source of adapter response state.

The initial V0.20 plan is documented in:

- `docs/plan/roadmap-v0.20.zh.md`
- `docs/plan/issue-backlog-v0.20.zh.md`

V0.20 planning work should not bypass V0.19 compatibility. New durable store
adapter execution artifacts must not infer state from review summaries, CLI
text, traces, host logs, provider payloads, object paths, database keys, or
private scripts.

### Compatibility Rules

Consumers may depend on:

- the response `format_version`
- response identity and source version chain
- response outcome
- response blocker and capability gap fields
- the boundary distinction between `Response` and `ResponseReviewSummary`

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
- `docs/design/native-durable-store-adapter-receipt-persistence-response-prototype-bootstrap-v0.19.zh.md`
- `docs/reference/durable-store-adapter-receipt-persistence-response-prototype-compatibility-v0.19.zh.md`
- `docs/reference/native-consumer-matrix-v0.19.zh.md`
- `docs/reference/contributor-guide-v0.19.zh.md`
