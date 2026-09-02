# Core-IR KR6.5: WASM Orchestration Codegen -- Design

> Status: **DRAFT rev 2** (RFC 0026 P5 / KR6.5).
> This document defines the E1 executable vertical spine; the E2 capability
> boundary, E3 workflow DAG, and the B2-C capability-bearing workflow emitter
> (`4224a52f`, exec-manifest + node-event buffer + capability-status scheduling /
> PENDING / latch) have since landed as FOUNDATION slices. It does not claim all
> of P5 is complete: the production host + fresh-instance durable resume (B2-D),
> exact evidence (B2-E), and expression/control-flow computation (KR6.6 / P6)
> remain; the current verification environment SKIPs the Wasmtime execution lanes.
> B2 and KR6.5 stay false.

## 0. Baseline and problem

At the original E1 design baseline, the legacy `wasm` backend was not an
execution backend. It projected
`ir::AgentDecl` into `WasmAgentConfig`, emitted textual WAT, inferred final states
from missing outgoing edges, assumed state zero is initial, and left `run`
and `step` as stubs. It did not consume `CoreProgram` or `CoreLayoutTable`.
For more than one agent, the CLI concatenated multiple textual modules; that was
not one loadable WebAssembly artifact.

RFC 0026 fixes the layer boundary:

```text
checked AHFL-IR -> lower_ahfl_to_core -> verified CoreProgram
                -> compute_core_layouts -> verified CoreLayoutTable(wasm32)
                -> Core WASM codegen -> deterministic wasm binary
```

KR6.5 is the orchestration layer: agent transition, flow, workflow, and the
`ahfl_cap` boundary. KR6.6 owns expression arithmetic, value construction,
structured `if`/`match`, and closures. E1 must therefore prove that the new
tower emits and executes real wasm without quietly implementing P6 or falling
back to the old AHFL-IR projection.

## 1. E1 goal and explicit non-goals

### 1.1 Goal: one real agent path with opaque identity output

E1 compiles one explicitly selected Core agent and its flow into a wasm32
binary. The accepted executable subset is deliberately small:

1. exactly one target `CoreAgentDecl` and exactly one `CoreFlowDecl` targeting
   it;
2. every non-final state handler contains exactly one `CoreGotoStmt`;
3. every final state handler contains exactly the canonical ANF identity form
   `CoreLetStmt(CorePathExpr{root=Input, projection=[]})` followed by a
   `CoreReturnStmt` of that same SSA value;
4. the deterministic goto graph is total and every state reaches a declared
   final state;
5. agent input and output declaration types are identical, and the identity
   path/result/return logical `CoreValueTypeId` values are exactly equal;
6. no workflow, capability-call statement, store, non-identity let/expression,
   non-canonical return, yield, trap, `if`, or `match` is accepted in E1.

The source probe is a real frontend program whose initial handler only
`goto Done` and whose final handler is `return input;`. Native execution plus
the separately classified wasm execution/structural evidence establish:

- the same declaration-order final `CoreStateId`;
- one state transition;
- the identity output contract described below;
- no capability invocation.

The canonical identity is not evaluated as a Core expression. The validator
uses it only as proof that `run` may return its input pointer without reading,
writing, projecting, coercing, parsing, copying, or reconstructing the value.
This is opaque orchestration frame routing, not computed value semantics, and
therefore stays on the P5 side of the P5/P6 seam.

### 1.2 Non-goals

- No `CoreExprNode` is emitted as wasm instructions in E1. The single canonical
  input identity path is structurally recognized and erased into opaque pointer
  passthrough; it performs no Core value evaluation. Literals, member
  projections, construction, coercion, and every other expression remain P6,
  rather than creating a misleading "simple expression" second engine.
- `CoreIfStmt` and `CoreMatchStmt` are P6 structured computation/control flow,
  not E1 orchestration.
- Capability calls, status/error/pending, argument/result frame marshalling,
  and resume are later KR6.5 slices.
- Workflow DAG scheduling and general opaque input/output frame routing are
  later KR6.5 slices. E1 permits only its explicitly constrained identity
  alias; that exception is not a general frame-routing precedent.
- E1 does not parse or produce `value_json`: the accepted flow is proven to
  return its input unchanged. The stable ABI exports remain present, and E1's
  `run` returns the input pointer itself.
- No browser host, JS glue, WASI resource mapping, optimization, debug info, or
  source map is introduced.
- The legacy `generate_wasm(WasmAgentConfig)` WAT helper is not reused by Core
  codegen and is not an execution semantics source.

## 2. Input contract and ownership

The codegen entry is pure and target-explicit:

```cpp
struct CoreWasmTarget {
    ir::core::CoreAgentId agent;
    WasmProfileKind profile{WasmProfileKind::Wasi};
};

struct CoreWasmArtifact {
    std::vector<std::uint8_t> bytes;
    ir::core::CoreAgentId agent;
    std::vector<std::string> exports;
    std::vector<std::string> imports;
};

struct CoreWasmCodegenResult {
    std::optional<CoreWasmArtifact> artifact;
    std::vector<CoreWasmDiagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept;
};

CoreWasmCodegenResult emit_core_wasm(
    const ir::core::CoreProgram &program,
    const ir::core::CoreLayoutTable &layouts,
    CoreWasmTarget target);
```

Both inputs are `const`; codegen owns only temporary validation/encoding data.
It never appends to `CoreProgram::value_types`, never materializes member
templates, and never edits the layout side artifact.

The backend boundary validates in this order:

1. `verify_core_program(program)` must be clean;
2. `verify_core_layout_table(program, layouts)` must be clean;
3. `layouts.target` must be the exact supported wasm32 target;
4. `target.agent` and its unique flow must resolve;
5. the E1 subset validator builds a deterministic state dispatch plan;
6. only then may binary sections be published.

Any failure returns no artifact. A temporary byte buffer is moved into the
result only after the complete module is encoded, so a caller never receives a
partial wasm file.

### 2.1 P4-D is the only layout authority

E1 carries no materialized values, so it performs no field offset, size,
alignment, stride, or capacity arithmetic at all. Nevertheless, the verified
`CoreLayoutTable` is a mandatory input and a malformed/mismatched table blocks
emission. This makes the dependency real without inventing unused layout work.

The first later slice that allocates or accesses a Core value must obtain every
size/align/offset/stride/backing fact from `CoreLayoutTable`; adding equivalent
arithmetic in codegen is forbidden. The wasm page size and binary-section byte
lengths are WebAssembly format constants, not Core value-layout facts.

## 3. E1 orchestration semantics

### 3.1 Canonical dispatch plan

The subset validator projects each state in declaration/index order to one of:

```cpp
enum class E1StateActionKind { Final, Goto };
struct E1StateAction {
    E1StateActionKind kind;
    CoreStateId target; // valid only for Goto
};
```

This is encoder-local data, not another IR. It contains no source names and is
discarded after emission. The target of every `Goto` must also appear in the
agent's declared legal transition table. Finality comes only from
`CoreAgentDecl::finals`; it is never inferred from outgoing edges.

For each final state, the validator requires the exact two-statement canonical
identity shape described in section 1.1. The `CorePathExpr` must have `Input`
as its root, `root_type == agent.input_type`, empty display members, empty
canonical projection, and `projection_resolved == true`. Its
`CoreExpr::result_type` must be a materialized nominal whose base is that input
type. The `CoreReturnStmt` must return the precise SSA value defined by that
let. Agent input and output `CoreTypeId` values must be equal, and the
expression result and the flow's SSA value-type entry must be the same exact
`CoreValueTypeId`. Hidden/orphan expressions or SSA values are rejected. This
validation never interprets the input value.

The validator follows the single outgoing action from every state with a
three-color walk. A cycle or a non-final sink is rejected. This is stricter
than general AHFL, but it guarantees that E1 `run` terminates without reviving
the quota semantics intentionally erased from Core-IR. General cyclic agents
remain unsupported until a later P5 runtime-policy design supplies an explicit
fuel/cancellation contract.

### 3.2 State globals and exports

The module has deterministic globals:

- mutable `current_state`, initialized from `CoreAgentDecl::initial` (never
  hard-coded to zero);
- mutable exported `transition_count`, initialized to zero;
- immutable exported `ahfl_abi_version = 1`;
- mutable private `heap_next` for the existing ABI allocator.

The RFC 0019 ABI export set remains:

```text
memory
alloc(i32) -> i32
dealloc(i32, i32) -> ()
run(i32, i32) -> i32
step() -> i32
current_state() -> i32
transition_count : i32 global
ahfl_abi_version : i32 global
```

`step` executes the current state's E1 action. A final state is stable and
returns itself without incrementing the counter. A goto updates the state,
increments the counter exactly once, and returns the new state.

`run` resets `current_state` to the declared initial state and
`transition_count` to zero, then calls the same step logic until a final state
is reached. Its input `(ptr,len)` is not dereferenced because the E1 validator
proved that the only accepted result is the exact input identity. It returns
`ptr` unchanged. The `run` body performs no load, store, parse, copy, coercion,
or allocation for that frame. The loop carries a compile-time/state-count fuel
guard even though the validator already proved termination; exhausting it
traps rather than hanging.

`current_state` returns the index. State names are display/provenance and do
not enter executable identity.

### 3.3 ABI v1 frame limitation is not hidden

RFC 0019's v1 `run` returns only an output pointer, while RFC 0021's capability
ABI returns both pointer and length. Existing comments also alternate between
"length-prefixed bytes" and a separately supplied length. E1 uses one narrow,
explicit identity exception rather than claiming that this ambiguity is
resolved:

- the returned output pointer aliases the input buffer exactly;
- output length equals the caller-known `in_len` by the validated identity
  invariant;
- ownership remains with the host, which obtained the buffer from the module's
  `alloc` and must deallocate that allocation exactly once with the same
  allocator;
- the module neither reads nor writes the aliased buffer in `run`.

This exception is valid only when input/output declaration types are identical
and the final handler has the canonical identity shape. A projection, literal,
construction, coercion, different output type, or any other value-returning
form fails closed; none may reuse this alias rule.

Before a value-returning agent/workflow slice lands, the ABI must receive a
separate reviewed decision. Because published ABI symbols are append-only, a
likely solution is `run2(...)->(status,ptr,len)` while preserving `run`; E1 does
not pre-approve that signature, does not generalize its identity alias, and
must not silently mutate v1.

## 4. Deterministic wasm binary emission

E1 introduces a small in-tree WebAssembly binary encoder for its MVP subset;
it does not shell out to `wat2wasm`, depend on host paths, or use the textual
WAT generator as an intermediate semantics.

The artifact starts with the standard wasm magic/version and emits sections in
canonical numeric order. Vectors preserve Core declaration order. Function,
global, and export indices are assigned by fixed tables in the design order,
not hash-map iteration. Integer immediates use canonical shortest LEB128.
Names/custom sections are absent in E1, preventing source paths or allocator
addresses from leaking into bytes.

The encoder supports only opcodes required for globals, integer comparisons,
structured block/loop/branch dispatch, calls, loads/stores needed by the ABI
bump allocator, and `unreachable`. Asking it to encode an unknown node/opcode
is a structured internal error, never a skipped instruction.

Two emissions from structurally equal `CoreProgram` + `CoreLayoutTable` +
target must be byte-for-byte equal. WASI and browser profiles are byte-identical
for E1 because the accepted program has no imports; profile starts affecting
bytes only when the capability slice lands.

## 5. CLI migration boundary

E1 changes `ahflc emit wasm` from textual WAT to a binary wasm artifact for the
accepted single-agent subset. The backend:

1. lowers the already checked `ir::Program` with `lower_ahfl_to_core`;
2. rejects any Core lowering diagnostic;
3. computes wasm32 layouts;
4. requires exactly one agent and one flow target in E1;
5. emits that single binary to the provided stream.

It does not call `lower_wasm`, build `WasmAgentConfig`, infer final states, or
concatenate modules. Zero or multiple agents, any workflow, or an unsupported
flow body fails with a stable diagnostic. A later package/entry-selection slice
must define multi-agent/workflow artifact identity before lifting this gate;
silently choosing the first agent is forbidden.

The old WAT generator may remain temporarily for its RFC 0019 contract unit
tests, but it becomes a clearly named legacy helper and is not reachable from
the `emit wasm` execution path. A later cleanup can add an explicit debug WAT
target or delete it; E1 does not make `wasm` mean two formats.

## 6. Diagnostics and fail-closed matrix

Stable codes (E1 identity agents + the E2/E3 capability and workflow slices; the
capability-workflow `wasm.RESOURCE_EXHAUSTED` contract below is defined for the
B2-C emitter):

| Code | Meaning |
| --- | --- |
| `wasm.INVALID_CORE` | standalone Core verification failed |
| `wasm.INVALID_LAYOUT` | P4-D table failed verification or is not the program's projection |
| `wasm.UNSUPPORTED_TARGET` | target data layout is not exact wasm32 |
| `wasm.ENTRY_AMBIGUOUS` | E1 has zero/multiple agents or no unique target flow |
| `wasm.UNSUPPORTED_ORCHESTRATION` | a valid Core construct is outside E1 (including every P6 node) |
| `wasm.NONTERMINATING_E1_RUN` | deterministic E1 state action graph cycles/does not reach final |
| `wasm.BINARY_OVERFLOW` | section/index/offset/LEB domain exceeds wasm32 limits |
| `wasm.RESOURCE_EXHAUSTED` | a capability-workflow's node-event region + heap arithmetic is legal but the resulting `heap_base` exceeds the fixed 64 KiB linear-memory page |
| `wasm.INTERNAL_INVALID` | encoder invariant failed; no partial artifact |

Where a rejected Core statement has a source range, the diagnostic carries it.
Program/entry-level errors may be range-less because `CoreAgentDecl` currently
does not retain a declaration range; codegen must not fabricate one.

The capability-workflow event region and heap are sized with a two-phase check,
in this fixed priority. FIRST, every size computation (`8 + node_count * 40`, the
`align_up`, and each section/index/offset/LEB domain) is a checked wasm32-domain
operation; any multiply/add/align overflow or wasm32 limit breach fails closed as
`wasm.BINARY_OVERFLOW`. ONLY after a legal
`heap_base = align_up(1024 + 8 + node_count * 40, 8)` is obtained, a `heap_base`
greater than the fixed single 64 KiB linear-memory page (65536 bytes) fails closed
as `wasm.RESOURCE_EXHAUSTED` (concretely, `node_count = 1612` fits at
`heap_base = 65512` and `node_count = 1613` is rejected at `65552`).
`wasm.RESOURCE_EXHAUSTED` is program/entry-level (a `node_count` capacity
property), so it is range-less like the other entry-level codes, carries no
payload/name/byte echo, and is emitted before any byte publication. It is distinct
from the future B2-D1b host-side `resume.preflight.unbounded` (a D1b TOTAL-preflight
orchestrator code), which is NOT emitted by codegen. The B2-D1a per-Verified-Result
canonical size bound authority (`core_wire_canonical_size`, LANDED `5fbd1a9b`)
returns a bare `MaxCanonicalSizeError` (`Unbounded` / `SizeOverflow`, the latter on
a checked u64 add/mul overflow of a finite schema's bound) and emits NO `resume.*`
diagnostic string. The future B2-D1b host-side TOTAL result-size preflight
owns a SEPARATE runtime diagnostic catalogue — a host `resume.preflight.unbounded`
(a legal schema with no finite canonical upper bound) and a host
`resume.preflight.resource_exhausted` (checked add/mul/align/`size_t` overflow, or
the total worst-case reservation exceeding capacity) — distinct in owner and call
site from this compile-time codegen `wasm.RESOURCE_EXHAUSTED`, and it is the owner
that maps the D1a-4 bound's `MaxCanonicalSizeError` into those host codes; codegen
never emits the host codes and the host preflight never emits `wasm.BINARY_OVERFLOW`
or reuses `wasm.RESOURCE_EXHAUSTED`.

The following all fail before byte publication:

- `CoreLetStmt` containing even a Bool/integer literal;
- any identity candidate with a member projection, a coercion, a literal, a
  constructed value, mismatched input/output types, or a return of a different
  SSA value;
- `CoreIfStmt`, `CoreMatchStmt`, or a non-canonical `CoreReturnStmt`;
- capability call, store, yield, trap, or unsupported expression;
- empty/non-goto non-final handler, empty/non-identity final handler, illegal
  goto;
- cyclic deterministic goto graph;
- any workflow in the E1 CLI program;
- a capability-workflow whose node-event region + heap exceeds the fixed 64 KiB
  page (`wasm.RESOURCE_EXHAUSTED`), or whose size arithmetic overflows the wasm32
  domain (`wasm.BINARY_OVERFLOW`);
- mismatched/tampered layout table;
- more than one candidate agent (no first-agent fallback).

## 7. Conformance and real execution

There is no Core evaluator in the repository, and adding one would create the
second execution engine RFC 0026 is removing. The E1 differential therefore
forks from one checked frontend/AHFL program:

```text
same checked program
  A. existing AgentRuntime/evaluator -> native observation
  B. AHFL->Core->layout->wasm       -> wasmtime observation
```

The source fixture is real frontend input, not a hand-built Core graph. The
native and wasm observations compare declaration-order state id, transition
count, semantic identity output, and empty capability-call sequence. For the
one-hop fixture, `wasmtime run --invoke step module.wasm` returns the final
state index. The harness accepts only one integer result line and treats an
unrecognized output shape as failure; it does not snapshot incidental warning
text.

The CLI invocation cannot observe the aliased output pointer and length in the
same persistent instance. E1 therefore locks the identity `run` body with an
always-on binary/structural probe: it returns local argument zero and contains
no memory access for the frame. This is not execution evidence and does not
replace the real-wasmtime `step` differential. A later embedded-host harness
may exercise `alloc` and `run` in one instance without changing this evidence
classification.

Wasmtime remains an external test/host tool, never a linked production
dependency. Reuse the P-6A discovery provenance and minimum-version policy:

- no explicitly configured or PATH-discovered wasmtime -> registered test
  exits 77 (visible SKIP);
- unusable PATH discovery -> SKIP;
- an explicit path/version/runtime failure -> hard FAIL;
- a usable tool -> compile, instantiate, invoke, and compare observations.

E1 implementation may land while the optional local test is skipped, but KR6.5
must not be reported as execution-proven/closed until CI or release evidence
records a non-skipped real wasmtime pass.

## 8. Test plan

Always-on unit/focused tests:

1. real frontend `Init -> Done`, final `return input;` -> Core + layout +
   binary;
2. wasm magic/version, canonical section order, required export signatures,
   initial state id (including an initial state whose index is not zero), and
   explicit final-state behavior;
3. the canonical identity passes, while member projection, literal, different
   output type, and coercion variants each fail
   `wasm.UNSUPPORTED_ORCHESTRATION` with no bytes;
4. the `run` body structurally returns argument zero and contains no frame
   memory operation; this test is named and documented as binary/structural,
   not execution evidence;
5. double emission is byte-identical; profile pair is byte-identical for the
   no-import slice;
6. `CoreProgram` and `CoreLayoutTable` remain equal to pre-emit snapshots;
7. a tampered layout table fails `wasm.INVALID_LAYOUT`;
8. each P6/effect/workflow node in the fail-closed matrix yields
   `wasm.UNSUPPORTED_ORCHESTRATION` and no bytes;
9. multiple agents yields `wasm.ENTRY_AMBIGUOUS`, never concatenation/first
   selection;
10. direct goto cycle yields `wasm.NONTERMINATING_E1_RUN`;
11. binary boundary overflow/fault injection yields no partial artifact;
12. existing P4-C materializer, P4-D layout, Core lower/verifier, evaluator,
    RFC 0019 ABI catalogue, and non-WASM backend suites remain green.

Optional real-execution conformance:

1. the same source fixture runs natively and through the emitted binary in
   wasmtime;
2. `step` result equals the Core/native final state id;
3. native transition count is one and the module's step result proves the same
   single transition (a later embedded-host harness will read both exports in
   one instance);
4. native output is semantically identical to input; the E1 CLI-only wasmtime
   probe does not claim to observe the aliased output frame;
5. malformed/unsupported wasm or an explicit broken wasmtime is a hard failure,
   never a false skip.

## 9. KR6.5 continuation after E1

E1 is only the executable spine. Later P5 slices (E2 capability boundary, E3
workflow DAG, and the B2-C capability-workflow emitter `4224a52f` have landed as
FOUNDATION; the rest remain), each separately reviewed:

1. **E2 capability boundary — LANDED**: lowered `CoreCapabilityCallStmt`, consumes
   the `ahfl_cap cap_<SymbolId>` `(ptr,len)->(status,ptr,len)` contract, propagates
   ERROR and PENDING, and uses only P4-D layouts for internal values. The v1
   value-returning `run` length problem is resolved by the pointer-only pre-effect
   trap.
2. **E3 workflow orchestration — LANDED**: deterministic DAG scheduler over
   `CoreWorkflowDecl`, explicit entry identity and multi-agent packaging,
   opaque frame routing where no computation is needed, no expression fallback.
3. **E4 P5 conformance expansion**: state sequence, capability sequence,
   pending/resume, workflow node order, and output frames. The B2-C
   capability-workflow emitter module-side portion (manifest + node-event bytes +
   PENDING/latch, `4224a52f`) has landed; pending/resume across a FRESH instance +
   output frames remain B2-D production-host work, exact node-order evidence B2-E.
   Only after these are green is KR6.5/P5 complete.
4. **KR6.6/P6** then lowers expressions, arithmetic, structured `if`/`match`,
   ADT construction/projection, coercion physical effects, and closures. Every
   construct not yet landed continues to fail closed.
