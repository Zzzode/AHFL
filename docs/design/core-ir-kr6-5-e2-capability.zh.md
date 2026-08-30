# Core-IR KR6.5 E2: Capability Boundary and `run2` -- Design

> Status: **DRAFT rev 1** (RFC 0026 P5 / KR6.5).
> E1 already proves the deterministic Core-IR orchestration spine. E2 adds one
> terminal capability call and an append-only value-return ABI. It does not add
> expression lowering, general frame serialization, workflow scheduling, or
> durable resume.

## 0. Baseline and problems that must be fixed first

E1 emits a real deterministic wasm binary, but its only value boundary is a
validated identity alias. It deliberately rejects `CoreCapabilityCallStmt` and
keeps the v1 `run(i32, i32) -> i32` export, whose pointer-only result cannot
report output length or capability status.

The existing Core capability model also has two standalone-proof holes that
must not be papered over inside codegen:

1. `CoreCapabilityDecl` stores AHFL `TypeRef` values. `CoreCapabilityCallStmt`
   operands and results, however, are typed by `CoreValueTypeId`. The Core
   verifier checks only arity, so a hand-built call can pass the wrong logical
   argument or result type.
2. `CoreAgentDecl` does not retain its capability whitelist. A standalone Core
   consumer cannot prove that a call is authorized for the target agent.

E2 closes those holes at the Core layer before emitting any import. Codegen
must not implement a second signature resolver or trust source names.

There is a third boundary that must remain explicit. The published `ahfl_cap`
frame is `AHFL_WIRE_VALUE_JSON`, not the P4-D in-memory object representation.
Treating a `CoreLayout` as a wire frame would silently change RFC 0019/0021.
E2 therefore routes one already-serialized input frame to the host and routes
the host-produced result frame back to the caller without decoding either.

## 1. Goal and accepted executable subset

### 1.1 One opaque terminal capability action

E2 extends the E1 state action plan with a final action variant:

```cpp
enum class E2FinalActionKind { Identity, Capability };

struct E2CapabilityAction {
    CoreCapabilityId capability;
    CoreValueId input_value;
    CoreValueId result_value;
};

struct E2StateAction {
    // Non-final states still carry exactly one CoreGotoStmt.
    std::optional<CoreStateId> goto_target;
    E2FinalActionKind final_kind;
    std::optional<E2CapabilityAction> capability;
};
```

This is encoder-local projection data, not another IR. Numeric Core ids remain
the canonical identity; display names are used only in diagnostics.

Every non-final state keeps the E1 rule: exactly one legal `CoreGotoStmt`.
Every final state must have exactly one of these canonical bodies:

1. the E1 two-statement identity body; or
2. this E2 three-statement terminal capability body:

```text
%arg = CorePathExpr(root=Input, projection=[])
%result = CoreCapabilityCallStmt(capability=C, args=[%arg])
return %result
```

The capability form is accepted only when all of the following hold:

- the input path satisfies every E1 canonical-input check: `Input` root,
  exact agent input root, no display members, no canonical projection, resolved,
  no local, exact expression/value `CoreValueTypeId`, and no hidden/orphan node;
- the call has exactly one argument and it is `%arg`;
- the referenced capability is in the target agent's persisted whitelist;
- the capability's sole parameter `CoreValueTypeId` exactly equals `%arg`'s
  recorded value type and is the nominal value type for the agent input;
- the call result's recorded value type exactly equals the capability return
  `CoreValueTypeId` and is the nominal value type for the agent output;
- the return uses exactly `%result`;
- there is exactly one capability call in that final body.

Unlike E1 identity, agent input and output nominal types may differ: all
transformation happens in the host capability. Multiple declared final states
may select different whitelisted capabilities, but each final body still has
one canonical terminal call. Reachable imports are deduplicated by
`CoreCapabilityId` and emitted in ascending id order.

### 1.2 Explicit non-goals

- No literal, projection, construction, coercion, arithmetic, `if`, `match`,
  store, or other Core expression is evaluated. Those remain KR6.6/P6.
- No zero-argument or multi-argument frame is synthesized. Encoding `{}` or
  `{"args":[...]}` from internal values requires a reviewed wire serializer.
- No capability result is decoded into a Core in-memory value.
- No chained or nested capability calls are accepted. They need intermediate
  result ownership and replay checkpoints.
- No capability call is accepted in a non-final handler or workflow region.
- No general input/output frame router, workflow scheduler, retry policy,
  timeout policy, compensation, or WASI projection is added.
- `AHFL_CAP_PENDING` is propagated and latched, but E2 does not add a resume
  entry point or a durable resume record. That remains RFC 0022 plus E4.

Any shape outside this subset fails `wasm.UNSUPPORTED_CAPABILITY_FRAME` before
binary publication. It is never silently skipped and never falls back to the
legacy WAT backend or native evaluator.

## 2. Core-IR signature and authorization foundation

### 2.1 One logical signature SSOT

`CoreCapabilityDecl` becomes fully Core-native:

```cpp
struct CoreCapabilityDecl {
    std::string name;                 // display only
    ir::SymbolRef symbol_ref;         // canonical SymbolId is required
    ir::CapabilityEffectKind effect_kind;
    std::vector<CoreValueTypeId> param_types;
    CoreValueTypeId return_type;
    SourceRangeOpt source_range;
};
```

The old `vector<TypeRef>` / `return_type_ref` fields are removed rather than
kept as a second signature SSOT. Core lowering materializes every capability
parameter and return `TypeRef` through the same program-global
`ValueTypeArena` used by flow/workflow SSA values. An unresolved,
unmaterializable, or `Never` signature fails Core lowering with a source-ranged
diagnostic. Unit remains a valid logical return type; its wire representation
is still non-empty JSON and is not special-cased by E2.

`CoreAgentDecl` gains:

```cpp
std::vector<CoreCapabilityId> capabilities; // declaration-order whitelist
SourceRangeOpt source_range;
```

Core lowering resolves `AgentDecl::capability_refs` through the already-built
`CapabilityIndex`, persists only ids, and fails closed on an unresolved or
duplicate whitelist entry. `CoreCapabilityId` is the in-program identity.
`CoreCapabilityDecl::symbol_ref.id` is the published host ABI identity; the two
must not be conflated.

The lowering order is fixed:

1. register all nominals and finalize P4-C member templates;
2. create the single program-global `ValueTypeArena`;
3. lower all capability declarations in source order, materializing their
   signatures into that arena and building `CapabilityIndex`;
4. lower agents in source order and resolve their whitelist to
   `CoreCapabilityId` values;
5. lower bodies against the same arena and ids.

### 2.2 Standalone Core verifier rules

The Core verifier, not codegen, proves:

- every capability parameter and return id is valid, in range, materialized,
  and not `Never`;
- each capability has a present `SymbolRef::id` of kind `Capability`, and no two
  capability declarations share that SymbolId;
- every agent whitelist id is in range and unique;
- every `CoreCapabilityCallStmt` capability is in the target agent whitelist;
- call arity equals declaration arity;
- each argument SSA type exactly equals the corresponding declaration
  parameter type;
- the result SSA type exactly equals the declaration return type;
- all existing use-before-def, single-definition, and region rules still hold.

The same call-signature checks apply in workflow regions even though E2 codegen
still rejects workflows. This keeps Core's standalone contract complete and
prevents a later consumer from inheriting the old hole.

Stable Core diagnostics distinguish invalid signature ids, argument type
mismatch, result type mismatch, unresolved whitelist identity, duplicate
whitelist entries, and unauthorized calls. All diagnostics use the statement
or declaration source range when present.

## 3. The `ahfl_cap` byte contract is unchanged

For every reachable capability, E2 emits exactly:

```wat
(import "ahfl_cap" "cap_<SymbolId>"
  (func (param i32 i32) (result i32 i32 i32)))
```

The two parameters are `(args_ptr,args_len)`. The three results are
`(status,result_ptr,result_len)`. Status values are exactly the fixed-width
`ahfl_cap_status` values from `ahfl_host.h`:

```text
0 = AHFL_CAP_OK
1 = AHFL_CAP_ERROR
2 = AHFL_CAP_PENDING
unknown = treated as AHFL_CAP_ERROR
```

The field name uses the decimal `SymbolRef::id`, never a source/canonical name
and never `CoreCapabilityId`. Missing or duplicate identities are rejected by
the Core verifier; an id greater than `UINT32_MAX` or an encoder-side ABI table
invariant failure yields `wasm.INVALID_CAPABILITY_ABI`. The module name, field
spelling, function type, status values, and pointer/length order are not changed
by E2.

Imports are the reachable subset of the target flow, sorted by
`CoreCapabilityId`. This is deterministic and least-privilege. The final field
name remains SymbolId-based even when its table position differs.

### 3.1 Wire frame is not Core layout

The input buffer supplied to `run2` is already one connection-negotiated
`AHFL_WIRE_VALUE_JSON` frame. For the accepted single-Struct-argument form, it
is byte-for-byte the same frame that native
`serialize_args_for_wire_json([struct])` sends. E2 forwards its pointer and
length unchanged to the import.

On success the host returns a separately allocated result frame in the same
wire format. E2 forwards its pointer and length unchanged to the outer host. It
does not parse field names, walk a `CoreLayout`, or reinterpret JSON bytes as a
Core object.

This is deliberate. P4-D remains the only authority for compiled in-memory
values, but it does not define the public JSON wire encoding. When a later
slice serializes an internal Core value, that serializer must read all physical
size/offset/alignment facts from `CoreLayoutTable` and must separately implement
the RFC 0021 wire format. Codegen may not create either a second layout engine
or a raw-layout wire protocol.

E2 still requires a fully verified `CoreLayoutTable`, and validates that every
accepted input/parameter/result/output logical type has a non-pending layout
entry. It performs no layout arithmetic and mutates neither input artifact.

## 4. Append-only `run2` decision

E2 adds one export without changing or removing any E1 export:

```wat
(export "run2"
  (func (param i32 i32) (result i32 i32 i32)))
```

Its inputs are `(input_ptr,input_len)` and its outputs are
`(status,output_ptr,output_len)`. The tuple intentionally mirrors `ahfl_cap`, so
status and length are never hidden. This is an append-only symbol addition;
the existing ABI version stays 1 and the existing `run(i32,i32)->i32`
signature is never mutated.

For an E1 identity final, `run2` returns
`(AHFL_CAP_OK,input_ptr,input_len)` and preserves the E1 alias ownership rule.
For an E2 capability final, it invokes the selected import exactly once and
normalizes its results as follows:

- `OK` with nonzero pointer and nonzero length: return the host result pointer
  and length unchanged;
- `OK` with an empty/invalid result frame: return `(ERROR,0,0)`;
- `ERROR`: return `(ERROR,0,0)`;
- `PENDING` with a null result pointer: latch suspension and return
  `(PENDING,0,0)`;
- `PENDING` with a non-null result pointer, or any unknown status: fail closed
  as `(ERROR,0,0)`.

No branch fabricates a result length. The module does not free a capability
result that it returns. On `OK`, ownership transfers to the outer caller, which
must read it and call the same module's `dealloc` exactly once. On `ERROR`, the
caller frees nothing. On `PENDING`, ownership of the argument frame transfers
to the capability host for the suspension lifetime, as specified by
`ahfl_host.h`; the outer caller must not free it as a completed-run input.

### 4.1 The old `run` cannot lose status or length

For an identity-only E1 plan, `run` keeps its current alias behavior. If an E2
plan contains any terminal capability action, `run` traps before calling any
capability. It must not call `run2` and discard status/length, return a pointer
whose length is unknowable, or silently use the E1 identity exception.

This behavior is structural and testable: the capability artifact's v1 `run`
body contains no import call and reaches `unreachable` before any effect.

### 4.2 Pending is propagated but not resumed in E2

E2 adds a private mutable `pending_latched` i32 global. A `PENDING` import result
sets it before returning. A later `run2` call on the same module instance sees
the latch and traps before reading or transferring ownership of the newly
supplied input and before invoking the capability. Returning `PENDING` again
would be ambiguous: it could incorrectly imply that the second input frame had
also transferred to the host. The trap preserves the Core statement's resume-
checkpoint/no-replay invariant without inventing that ownership rule.

E2 intentionally provides no operation that clears this latch. A suspended
instance cannot complete until a later reviewed `resume2`/durable-record slice
lands. Instantiating a fresh module represents a fresh run, not a resume. E2
must not claim durable pending support or exactly-once recovery; it only proves
status propagation and prevents accidental same-instance replay.

The accepted E2 body has one call, so its per-handler ordinal is zero. The host
already knows the SymbolId of the import that returned `PENDING`; E2 does not
invent a second pending-coordinate scheme. E4 must reuse RFC 0022's
`(cap_id,ordinal)` memo model when it adds resume.

## 5. Orchestration and encoder changes

`run2` resets the declared initial state and transition count, follows the same
validated acyclic goto plan as E1, and executes the reached final action. Gotos
increment `transition_count`; a capability call does not. `step()` remains the
E1 state-transition observation API and never invokes a capability because it
has no frame parameters. The only E2 capability execution entry is `run2`.

The in-tree binary encoder adds only standard wasm32 features needed by E2:

- import section before function declarations;
- multi-value function types/results;
- import-aware function index assignment;
- locals for status/result pointer/result length;
- `call`, exact status comparisons, normalization branches, and `return`;
- the private pending latch global;
- append-only `run2` export.

All type/function/global/export indices come from a fixed table derived from
the sorted reachable capability ids. No hash-map iteration controls bytes.
Names/custom sections remain absent. WASI and browser profiles remain
byte-identical in E2 because both use the same named `ahfl_cap` host boundary
and no direct WASI import.

Emission remains transactional: validate Core, validate P4-D, validate the E2
subset and import table, encode into temporary buffers, then publish exactly
one complete artifact. Any error returns no bytes.

## 6. Diagnostics and fail-closed matrix

E2 retains every E1 diagnostic and adds:

| Code | Meaning |
| --- | --- |
| `wasm.INVALID_CAPABILITY_ABI` | out-of-range SymbolId or encoder ABI import-table invariant failure |
| `wasm.UNSUPPORTED_CAPABILITY_FRAME` | valid Core call needs P6 serialization, chaining, or a noncanonical frame source/result |

Runtime `ERROR`/`PENDING` are `run2` status results, not compile diagnostics.
Malformed host results are normalized fail-closed as described in section 4.

The following valid Core shapes are explicitly rejected with no artifact:

- zero or multiple capability arguments;
- any argument other than the exact canonical input value;
- capability result used by anything except the exact terminal return;
- two calls, nested calls, a call in a non-final handler, or a workflow call;
- an argument/result requiring literal, projection, construct, coercion, or
  internal value serialization;
- a capability not in the target agent whitelist;
- a called capability whose verified SymbolId is outside the uint32 ABI domain;
- any signature/layout mismatch;
- any attempt to use v1 `run` as a value-return capability entry.

## 7. Test and evidence plan

### 7.1 Always-on Core and binary tests

1. Real frontend fixture:
   `Init -> Done`, with `Done { return Echo(input); }`, one Struct input,
   one distinct Struct output, and `Echo(Input)->Output` in the agent whitelist.
2. Core lowering asserts materialized capability signature ids, whitelist ids,
   exact call arg/result types, and source ranges.
3. Core verifier negatives cover invalid signature id, wrong arg type, wrong
   result type, missing/duplicate whitelist id, unauthorized call, missing/
   duplicate SymbolId, and workflow call type checking.
4. Binary parser asserts exact `ahfl_cap.cap_<SymbolId>` import bytes and exact
   `(i32,i32)->(i32,i32,i32)` type, sorted imports, shifted function indices,
   append-only `run2`, and unchanged legacy exports.
5. Structural `run2` probes cover identity OK forwarding, one terminal import
   call, OK/ERROR/PENDING/unknown normalization, and the pending latch trapping
   before a repeated import call.
6. Capability artifacts' v1 `run` traps before effects; E1 identity artifacts'
   v1 `run` remains byte/behavior compatible.
7. Double emission is byte-identical; profile pair is byte-identical;
   `CoreProgram` and `CoreLayoutTable` snapshots do not change.
8. Every fail-closed shape in section 6 returns the exact diagnostic and no
   partial bytes.
9. Existing Core lower/verifier, P4-C, P4-D, E1, legacy RFC 0019 catalogue,
   AgentRuntime, and native host binding suites remain green.

### 7.2 Same-frontend semantic differential

The source fixture is checked once and forks at the same boundary as E1:

```text
same checked AHFL program
  A. AgentRuntime + native capability mock
  B. AHFL -> Core -> P4-D -> wasm + host capability mock
```

The native observation records final state id, transition count, capability
SymbolId sequence, status, and deterministic `value_json` output. The wasm
observation must match the state/count/call/status sequence and, in an embedded
host capable of reading module memory, the result bytes.

### 7.3 Real-engine evidence is classified honestly

The wasmtime CLI can link a test provider through
`--preload ahfl_cap=<provider.wasm>`. This is sufficient for real execution of
the exact import and for conforming `ERROR` and `PENDING` results, which carry
no result frame. It is not sufficient for a conforming `OK` result frame: a
separately preloaded module cannot allocate/write the target module's private
linear memory, and returning the argument pointer as a result would violate
the callee-allocates rule.

Therefore E2 uses three separately named evidence classes:

1. always-on binary/structural probes for the full OK/result ownership path;
2. optional real-wasmtime CLI execution for import linkage plus ERROR/PENDING
   propagation, using the existing explicit-vs-PATH provenance policy and a
   feature probe for `--preload`;
3. an optional embedded-host execution probe (Node WebAssembly when available,
   or a future Wasmtime embedder) that can call module `alloc`, write/read
   memory, implement an OK import, and verify result bytes plus single free.

Node/V8 evidence is supplemental and must not be labeled wasmtime evidence.
KR6.5 remains not execution-proven until CI/release records a non-skipped real
wasmtime capability execution with a conforming OK result host. A CLI preload
that aliases the input or returns uninitialized memory is forbidden even as a
test shortcut.

Once a wasmtime version and required preload feature are established usable,
an invocation/link/status mismatch is a hard failure. Missing or unusable
PATH-discovered tooling skips; an explicitly configured broken tool fails.

## 8. Implementation split and continuation

E2 lands as one review series with two non-amended commits after this design:

1. **E2-C1 Core foundation**: materialized capability signatures, persisted
   agent whitelist/source ranges, lowering, equality, standalone verifier, and
   focused Core tests. No wasm consumer is enabled in this commit.
2. **E2-C2 codegen and evidence**: E2 plan validator, exact imports, run2,
   status normalization/pending latch, deterministic encoder changes, CLI and
   optional real-engine probes.

After E2:

- E3 adds workflow DAG scheduling and packaging identity;
- E4 adds durable pending/resume, general opaque frame routing, and expanded
  conformance evidence;
- KR6.6/P6 adds expression evaluation plus internal Core value construction,
  projection, control flow, and reviewed Core-value-to-wire serialization.

Neither E2 commit closes KR6.5 or retires the evaluator by itself.
