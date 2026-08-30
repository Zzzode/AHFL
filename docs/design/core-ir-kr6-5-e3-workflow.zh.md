# Core-IR KR6.5 E3: Workflow DAG and Multi-Agent Packaging -- Design

> Status: **DRAFT rev 1** (RFC 0026 P5 / KR6.5).
>
> Scope: one explicitly selected workflow, deterministic DAG scheduling, and
> borrowed opaque identity-frame routing through multiple packaged agent
> instances. This slice does not lower Core expressions, serialize values,
> compose capability results, or add pending/resume.

## 1. Objective and boundary

E1 proved one selected agent could become a deterministic executable wasm
module without lowering expressions. E2 added the exact `ahfl_cap` import and
the append-only `run2` status/length boundary for one terminal capability call.
E3 adds the next orchestration-only vertical slice:

1. select one entry by typed Core identity rather than choosing the first
   declaration;
2. package every agent instance reachable from one workflow DAG in one wasm
   module;
3. compute a deterministic topological schedule from `CoreWorkflowNodeId`
   edges;
4. execute every node once in that order; and
5. route one opaque `(ptr,len)` frame through canonical identity-only workflow
   regions and identity-only agent runners.

The real frontend positive probe has two distinct agent declarations and one
workflow:

```ahfl
workflow IdentityPipeline {
    input: Frame;
    output: Frame;

    node first: FirstAgent(input);
    node second: SecondAgent(first) after [first];
    return: second;
}
```

Both agents may have a deterministic acyclic goto prefix, but each final
handler must be the E1 canonical identity return. The wasm `run2` executes
`first` and `second`, reports two completed workflow nodes, and returns the
original input pointer and length unchanged.

This is a real workflow scheduler and a real multi-agent module, but it is not
value computation. The module never loads, stores, parses, copies, projects,
constructs, coerces, serializes, or deserializes the frame.

### 1.1 Accepted workflow region shape

Every node `input_region` and the workflow `return_region` must contain exactly
two canonical ANF statements:

1. `CoreLetStmt` whose expression is one `CorePathExpr`; and
2. `CoreYieldStmt` of the same SSA value.

The path must have all of the following properties:

- root is `WorkflowInput` or `WorkflowNodeOutput`;
- `root_type` is the exact declared source nominal type;
- `members` and resolved projection are empty;
- `projection_resolved == true`;
- no local/identifier fallback is present;
- expression result type and the workflow SSA `value_types` slot are the same
  exact materialized `CoreValueTypeId`; and
- the yielded value is the let result and has one definition and one canonical
  use in the accepted region.

For a node input, the source logical type must equal the target
`CoreAgentInstance::input_type` nominal value type exactly. A node-output root
must identify an ancestor already proven legal by the standalone Core
verifier. For the return region, the source logical type must equal the
workflow output nominal value type exactly.

No member projection is accepted even if it would be representation preserving.
No constructor is accepted even if every field came from the same frame. Those
operations require P6 expression/value lowering or a separately reviewed wire
serializer.

### 1.2 Accepted agent shape

Each reachable workflow node resolves through
`CoreWorkflowNode::target_instance` to a concrete `CoreAgentInstance`. E3
packages one internal runner per distinct reachable instance, ordered by
`CoreInstanceId`.

The instance's base agent and unique target flow must pass the E1 deterministic
agent-plan rules:

- every non-final handler is exactly one legal `CoreGotoStmt`;
- every state reaches a declared final state;
- every final handler is the exact E1 identity form;
- input and output logical value types are exactly equal; and
- no capability action or other Core statement/expression is present.

E3 deliberately does not compose E2 terminal capability agents inside a
workflow. Such composition introduces ownership of intermediate
callee-allocated frames, fan-out lifetime, and durable pending coordinates.
Those are E4 concerns. A reachable capability action therefore fails with
`wasm.UNSUPPORTED_WORKFLOW_FRAME`, without emitting an import or partial
module.

Unreachable declarations do not expand authority. Agents, flows,
capabilities, instances, and workflows outside the selected entry's transitive
instance set are verified as part of the Core program but are not packaged.

### 1.3 P5/P6 seam and non-goals

E3 does not:

- emit any `CoreExprNode` as a wasm value operation;
- lower literal, construct, projection, coercion, `if`, or `match` semantics;
- treat P4-D raw object layout as the RFC 0021 JSON wire format;
- invoke a capability from a workflow region (Core already rejects capability
  calls outside Flow);
- compose a capability-bearing agent, free an intermediate capability result,
  resume `PENDING`, or add retry/checkpoint policy;
- add general frame routing, ref-counting, fan-out ownership, or serialization;
- select an entry by declaration position, concatenate per-agent modules, or
  embed source/canonical names in wasm; or
- retire the evaluator or claim all of KR6.5 execution-proven.

## 2. Explicit entry and artifact identity

The direct API replaces the agent-only target with one closed typed variant:

```cpp
using CoreWasmEntry =
    std::variant<ir::core::CoreAgentId, ir::core::CoreWorkflowId>;

struct CoreWasmTarget {
    CoreWasmEntry entry;
    WasmProfileKind profile{WasmProfileKind::Wasi};
};

struct CoreWasmArtifact {
    std::vector<std::uint8_t> bytes;
    CoreWasmEntry entry;
    std::vector<ir::core::CoreInstanceId> packaged_agent_instances;
    std::vector<std::string> exports;
    std::vector<std::string> imports;
};
```

The variant is the only executable entry identity. There is no pair of
optional agent/workflow ids and no display-name lookup inside codegen. A
workflow artifact represents exactly one `CoreWorkflowId` plus its reachable
instance closure. One API call publishes at most one loadable module.

`CoreWasmArtifact::entry` and `packaged_agent_instances` are side metadata for
the caller/package builder. E3 does not put canonical names, package paths, or
an unstable digest in a wasm name/custom section. Package identity remains the
existing handoff manifest's responsibility; executable identity inside the
verified Core program is index based.

### 2.1 CLI entry resolution

`ahflc emit wasm` resolves the target before calling codegen:

1. when `EmitContext::package_metadata.entry_target` exists, its kind and exact
   canonical name must resolve to exactly one Core agent or workflow;
2. no SymbolId/name fallback is used after an explicit canonical entry misses;
3. when package metadata is absent, the E1 compatibility case of exactly one
   agent and no workflows may select `CoreAgentId{0}`;
4. every other metadata-free zero/multi-agent/workflow program fails with
   `wasm.ENTRY_AMBIGUOUS`.

Export targets do not create extra wasm entries, and codegen never selects the
first workflow. An explicit agent entry in a larger program packages only that
agent; an explicit workflow entry packages only its reachable instances.

The existing E1/E2 agent artifact remains byte-for-byte stable when its input
program and explicit agent target are unchanged. Lifting the old whole-program
`agents.size()==1`, `flows.size()==1`, and `workflows.empty()` gates must not
change the selected agent plan or import ordering.

## 3. Deterministic workflow plan

The E3 validator builds a temporary `E3WorkflowPlan` before binary emission.
Nothing is appended to the encoder until the entire plan succeeds.

### 3.1 Topological order

The Core verifier is still the authority for bounds, duplicate/self edges,
acyclicity, target-instance validity, and dependency visibility. Codegen then
projects one deterministic schedule with Kahn's algorithm:

- initial zero-indegree nodes are enqueued in ascending
  `CoreWorkflowNodeId` order;
- successor lists are constructed by scanning nodes in ascending id order, so
  each successor list is ascending;
- the ready set is a FIFO vector/queue, never a hash-map iteration; and
- each node is appended exactly once.

This is the same declaration-order FIFO refinement used by the native
`WorkflowRuntime` plan builder. If the produced order does not contain every
node despite a successful Core verifier, codegen fails `wasm.INVALID_CORE`;
it never silently drops a node.

All declared nodes execute, including a node whose output is not selected by
the workflow return. Dependency edges define readiness, not liveness pruning.

### 3.2 Canonical frame sources

Each canonical region is normalized to one of:

```text
WorkflowInput
WorkflowNodeOutput(CoreWorkflowNodeId)
```

For node `N`, a node-output source must be in `N`'s transitive dependency set.
The standalone verifier already proves this; E3 records and cross-checks it so
the normalized plan is self-contained. The source node must appear earlier in
the computed schedule.

The encoder assigns two wasm i32 locals `(ptr,len)` to each node output in
`CoreWorkflowNodeId` order. At a scheduled node it selects either the `run2`
input pair or an earlier node pair and calls that target instance's internal
identity runner. The runner returns the same pair. E3 stores the pair and
increments `workflow_completed_count` once.

The return source selects the outer input pair or one completed node pair and
returns `(OK,ptr,len)`. It never invents a length.

### 3.3 Borrowing and ownership

The E3 frame is a borrowed opaque wire frame for the duration of the run:

- the outer host owns the input allocation;
- identity agent runners do not read, write, retain, allocate, or free it;
- all workflow node pairs may alias that same allocation;
- `run2` returns the selected pair with `OK`; in the accepted slice it is the
  original input pair, so ownership remains with the outer host; and
- the host deallocates its input allocation exactly once with the same module
  allocator.

This rule is safe for DAG fan-out because every internal consumer is proven
read-only identity forwarding. It is not a precedent for sharing a capability
result or a mutable Core object. The first reachable capability action or
non-identity frame producer fails closed before publication.

P4-D remains mandatory and verified. E3 requires finalized layouts for the
workflow shell and every reachable instance shell, but performs zero size,
offset, alignment, stride, or backing arithmetic. The bytes are an RFC 0021
wire frame, not a `CoreLayout` object.

## 4. Wasm module behavior

### 4.1 Internal functions and deterministic indices

Each distinct reachable `CoreInstanceId` gets one private internal runner in
ascending instance-id order. Node dispatch calls these functions by a fixed
index table. Repeated nodes targeting the same instance reuse the same runner.

An internal runner has the E2 `run2` tuple shape:

```wat
(func (param i32 i32) (result i32 i32 i32))
```

It resets its private agent state, executes the validated acyclic goto plan,
adds its transition count to the workflow-wide transition counter, and returns
`(OK,input_ptr,input_len)`. It contains no frame memory operation.

The module imports nothing in E3's accepted workflow subset. Import ordering
for explicit agent entries remains E2's reachable `CoreCapabilityId` order.

### 4.2 Exports

The ABI version remains 1 and E1/E2 agent-entry exports keep their behavior.
For a workflow entry:

- `memory`, `alloc`, `dealloc`, `run`, `run2`, `transition_count`, and
  `ahfl_abi_version` remain exported with their existing signatures;
- `run2` resets workflow counters, executes the complete schedule, and returns
  `(OK,ptr,len)`;
- legacy `run` is safe because this slice cannot produce ERROR/PENDING or a
  new-length result; it executes the same schedule and returns the input
  pointer;
- `transition_count` is the sum of agent goto transitions performed by the
  workflow run;
- `step` and `current_state` trap before any effect, because a multi-agent DAG
  has no single honest agent state and these signatures carry no entry/node
  coordinate; and
- E3 append-only exports immutable `workflow_node_count` and mutable
  `workflow_completed_count`, both i32 globals.

`workflow_completed_count` resets to zero at run entry and increments exactly
once after each internal runner returns. It is execution telemetry, not a
substitute for an event log. E3 does not claim that observing only the final
count proves the complete order; binary/structural evidence separately locks
the unrolled call order. A later embedded workflow host may add a durable trace
API without changing these exports.

The private E2 `pending_latched` global is absent from an E3 workflow artifact,
because capability-bearing agents are rejected. Explicit E2 agent artifacts
retain it unchanged.

### 4.3 Encoder determinism and atomicity

E3 extends the in-tree encoder only with the locals/calls/globals needed above.
It preserves:

- canonical section order and shortest LEB128;
- fixed type/function/global/export index tables;
- declaration/id order rather than map iteration;
- no name or custom sections;
- no wall clock, PID, host path, allocator address, or random value; and
- no partial `CoreWasmArtifact` after any diagnostic.

Two emissions from structurally equal Core program, verified layout table, and
typed entry must be byte-for-byte equal. Neither input artifact is mutated.

## 5. Fail-closed diagnostics

E3 retains all E1/E2 codes and adds:

| Code | Meaning |
| --- | --- |
| `wasm.ENTRY_NOT_FOUND` | explicit typed/package entry cannot be resolved exactly |
| `wasm.UNSUPPORTED_WORKFLOW_FRAME` | workflow/agent frame is not canonical borrowed identity routing, including a reachable capability action |

`wasm.ENTRY_AMBIGUOUS` remains the code for missing metadata where no unique
legacy agent entry is allowed. `wasm.INVALID_CORE` remains the code for a Core
program that fails standalone verification or contradicts a verified DAG
invariant. P6 nodes that do not specifically violate the workflow frame seam
remain `wasm.UNSUPPORTED_ORCHESTRATION`.

All failures publish no bytes. Focused negatives cover:

- explicit missing/wrong-kind/duplicate-canonical entry;
- metadata-free workflow or multi-agent selection;
- invalid target instance or duplicate/missing unique target flow;
- workflow cycle/order contradiction (verifier or plan fail-closed);
- literal, member projection, construct, coercion, `if`, `match`, local, or
  hidden/orphan expression/value in a node input or return region;
- a node reading a non-ancestor output;
- node input/target input or return/output type mismatch;
- reachable capability-bearing agent;
- capability call in workflow region (rejected by Core as outside Flow);
- non-identity agent final, capability final, or nonterminating agent graph;
- invalid/unfinalized layout root; and
- local/function/global/index count overflow.

## 6. CLI, packaging, and compatibility

The backend receives `PackageMetadata` through `EmitContext`, resolves one
entry as section 2.1 specifies, lowers once to Core, computes/verifies layouts,
and calls the typed API. It does not call the legacy WAT generator, concatenate
modules, or consult the handoff execution plan as a second scheduler.

The Core workflow DAG is the executable scheduling SSOT. Package metadata
chooses the entry only. The existing execution-plan artifact may describe the
same graph for external handoff, but wasm codegen does not parse its JSON or
reconstruct edges from names.

An E1/E2 explicit agent entry remains supported in a package containing other
declarations. Its bytes, import set, status normalization, pending latch, and
legacy `run` behavior are unchanged. Only the obsolete ambiguity rule is
lifted once the typed entry has already resolved selection.

## 7. Evidence model

### 7.1 Always-on unit and binary evidence

Always-on tests prove:

1. target variant/artifact identity and reachable instance set are index based;
2. deterministic Kahn order, including a branch/join tie broken by node id;
3. every node executes once in the encoded schedule and no node is liveness
   pruned;
4. internal runners are ordered by `CoreInstanceId` and reused;
5. canonical node-input/return normalization and the full fail-closed matrix;
6. workflow `run`/`run2` contain no frame load/store/parse/copy/allocation;
7. `step`/`current_state` trap before effects for workflow entries;
8. completed and transition counters have the stated update points;
9. no imports are emitted for the accepted workflow, and unreachable
   capabilities do not expand authority;
10. double emission is byte identical and Core/layout snapshots are unchanged;
    and
11. existing E1/E2 explicit-agent binaries stay byte identical via a locked
    golden/digest fixture.

The call-order parser/probe is explicitly **binary/structural evidence**, not
execution evidence.

### 7.2 Same-frontend differential

One checked frontend fixture forks after typechecking:

- native `WorkflowRuntime` runs the selected workflow and records its node
  schedule, aggregate agent transitions, capability sequence (empty), and
  output `value_json`;
- AHFL-IR lowers to Core, P4-D layouts are computed, and E3 emits wasm.

The native and E3 expectations are the same declaration-order topological
schedule, completed-node count, transition count, empty capability sequence,
and identity output bytes. No Core interpreter is added.

### 7.3 Real execution and honest classification

An optional embedded Node host instantiates the emitted workflow module in one
persistent instance, allocates an input frame, calls `run2`, and checks:

- `OK` and exact `(ptr,len)` identity;
- `workflow_node_count` and `workflow_completed_count`;
- aggregate `transition_count`;
- `step` and `current_state` trap without changing completed count; and
- deallocation occurs once by the outer owner.

An optional real-wasmtime probe invokes the no-import workflow artifact and
checks successful `run` pointer passthrough. Because separate CLI invocations
create separate instances, that probe does not claim to observe persistent
counters or the complete schedule. Missing PATH-discovered wasmtime returns
SKIP 77; an explicitly configured broken binary is a hard failure.

E3's Node test is real embedded-engine execution, while the wasmtime test is
real wasmtime execution only when non-skipped. Neither substitutes for E2's
still-required conforming-OK wasmtime host evidence. KR6.5 remains not
execution-proven until release/CI records all required non-skipped evidence.

## 8. Implementation split

E3 lands after this design as one review series with two non-amended commits:

1. **E3-C1 typed entry and plan foundation**: `CoreWasmEntry`, artifact reachable
   instance identity, strict CLI/package entry resolution, reusable selected
   agent-plan builder, deterministic workflow plan/validator, and focused
   fail-closed tests. No workflow bytes are enabled until the complete plan is
   validated.
2. **E3-C2 encoder and evidence**: private instance runners, unrolled workflow
   schedule, workflow exports/counters, CLI binary path, same-frontend probe,
   Node execution host, and optional wasmtime evidence.

After E3, E4 separately reviews capability-result ownership across nodes,
durable `(cap_id,ordinal)` pending/resume, general opaque frame routing, and
expanded P5 conformance. P6 remains the only slice allowed to evaluate Core
expressions or construct/project/serialize internal Core values.

E3 does not close KR6.5 or retire the evaluator by itself.
