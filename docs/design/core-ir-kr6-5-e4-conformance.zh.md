# Core-IR KR6.5 E4-A: Real-Wasmtime Conformance Closure -- Design

> Status: **DRAFT rev 1** (RFC 0026 P5 / KR6.5).
>
> Scope: turn the optional E1/E2/E3 Wasmtime checks into reproducible,
> non-skipped CI/release evidence and add one persistent embedded-Wasmtime
> differential over the already implemented P5 subset. This slice changes no
> Core node, accepted codegen shape, production WASM ABI, or encoder byte.
>
> Owner decision pending: accept the pinned test-only Wasmtime 28.0.0 CLI plus
> official `wasmtime-py==28.0.0` Linux wheel described in section 3. Until that
> decision is recorded, this document authorizes no dependency or CI change.

## 0. Why E4 is split before implementation

E1, E2, and E3 now emit executable wasm for three P5 orchestration slices:

1. an agent state machine with an opaque identity return;
2. one terminal `ahfl_cap` call with `run2` status/ownership propagation; and
3. one deterministic identity-only workflow DAG packaged with multiple agent
   instances.

Their always-on structural tests and Node embedded-host tests are green. The
four tests that are explicitly named as real-Wasmtime evidence still return
CTest SKIP 77 on development and current CI machines because no Wasmtime tool
is installed. A skipped test is useful provenance, but it is not execution
evidence and cannot close KR6.5.

The earlier E1/E2 design also listed five final P5 conformance dimensions:

- state sequence;
- capability sequence;
- pending/resume;
- workflow node order; and
- output frames.

The implemented E1-E3 subset can prove only part of that matrix without new
semantics. In particular, E2 propagates PENDING and traps replay through a
latch but has no resume export, and E3 exposes only completed/transition
counters rather than an exact runtime node event stream. Claiming all five
dimensions now would be false.

E4 therefore has two separately reviewed parts:

- **E4-A (this document)** closes the environment and evidence gap for exactly
  the already implemented E1-E3 subset. It turns real Wasmtime from an
  incidental optional tool into one pinned, required CI/release conformance
  lane while preserving optional local behavior.
- **E4-B (later design)** widens P5 semantics: canonical opaque capability
  chains, ownership-safe capability results between workflow nodes, durable
  `(node_id, cap_id, ordinal)` replay/resume, and an exact workflow runtime
  order observation. E4-B must resolve the resume-record and wire-type
  validation boundary before changing codegen.

After E4-A, it is valid to say **the implemented E1-E3 subset has non-skipped
real-Wasmtime evidence**. It is still invalid to say **KR6.5/P5 is
execution-proven or complete** until E4-B and its full five-dimension matrix
are green in the same required evidence lane.

## 1. Baseline audit and non-negotiable boundaries

### 1.1 Existing evidence has three distinct strengths

The repository currently has:

1. always-on binary/structural probes, which inspect exact imports, exports,
   function bodies, call order, status normalization, and deterministic bytes;
2. Node/V8 embedded execution, which can keep one module instance alive,
   access its private memory, implement a conforming capability host, and read
   mutable globals; and
3. optional Wasmtime CLI tests, which use separate CLI processes and therefore
   cannot observe several values from one persistent instance.

Those classes remain separate. E4-A does not relabel structural or Node
evidence as Wasmtime evidence. It adds an official Wasmtime embedded host so
the strongest observations can be made under Wasmtime itself.

### 1.2 The CLI alone cannot close E2 or E3

The Wasmtime CLI is sufficient for:

- E1 `step` invocation;
- E2 import linkage plus ERROR/PENDING results through `--preload`; and
- E3 legacy `run` pointer passthrough.

It is insufficient for:

- E2 conforming OK output bytes, because a separately preloaded provider cannot
  allocate and write the target module's private memory; and
- E3 persistent counters, because separate `--invoke` processes instantiate
  separate modules.

No CLI output parser, input-pointer alias shortcut, or provider-owned foreign
memory may be used to pretend otherwise.

### 1.3 P4-D and the P5/P6 seam do not move

E4-A consumes the exact binaries already emitted by E1-E3. It does not:

- calculate size, alignment, offset, stride, or backing layout;
- decode a Core value or implement a second `value_json` serializer;
- lower a literal, projection, construct, coercion, `if`, or `match`;
- add a Core interpreter; or
- mutate `CoreProgram`, `CoreLayoutTable`, or the production WASM ABI.

The embedded host treats frames as opaque bytes except when comparing the
fixture's expected byte sequence. P4-D remains the only internal layout
authority, and wire bytes remain RFC 0019/0021 `AHFL_WIRE_VALUE_JSON`, not a
Core object layout.

## 2. E4-A acceptance matrix

### 2.1 State sequence: exact persistent execution

The E1 producer is extended to print one canonical machine-readable native
observation containing the declaration-order state sequence, transition
count, and identity-output fact. The embedded Wasmtime host:

1. emits the binary from the same checked frontend fixture;
2. instantiates it once;
3. reads `current_state`;
4. invokes `step` until the native final state is reached, with a checked fuel
   bound derived from the native sequence length;
5. records every returned state id and the exported `transition_count`; and
6. compares the complete sequence and count with the native observation.

It then allocates one input frame through the module, writes sentinel bytes,
invokes identity `run`, and proves exact pointer alias, unchanged bytes, and
one outer-host deallocation call.

This is execution evidence, not a binary inference. A malformed return tuple,
extra state, repeated state, trap, timeout, or counter mismatch is a hard
failure once the binding has been explicitly configured.

### 2.2 Capability sequence and output frame: exact embedded host

The E2 producer continues to fork from one checked frontend fixture and emits
the exact SymbolId-based import. A fresh Wasmtime instance is used for each
status scenario.

For conforming OK:

1. the host calls the target module's exported `alloc` for input and result
   frames before `run2`;
2. the imported callback, using Wasmtime caller access, reads the exact input
   bytes from the target memory, writes the deterministic result bytes into the
   target-allocated result frame, and returns `(OK, ptr, len)`;
3. the harness records the exact imported field/SymbolId sequence;
4. `run2` must return the same result pointer and length;
5. result bytes must equal the native capability mock's `value_json` bytes; and
6. the outer caller invokes target `dealloc` exactly once for the returned
   result and once for its still-owned input.

Preallocating the result through the target module is a test-host scheduling
detail, not an ownership shortcut: the result buffer is obtained from the
callee/module allocator and resides in the target module's memory. Returning
the argument pointer or uninitialized memory is forbidden.

Fresh instances separately prove ERROR, PENDING, unknown-status
normalization, and PENDING-latch replay trapping. The host records that a
second same-instance call after PENDING traps before a second import. The
existing real-Wasmtime CLI preload test remains the independent byte-level
import-linkage check for ERROR/PENDING.

This closes capability sequence and output-frame evidence only for the E2
single-call subset. It does not claim a multi-call sequence or resume.

### 2.3 Workflow output and counters: persistent execution

The E3 producer's native observation remains the source of truth for the
identity workflow schedule, completed-node count, aggregate transition count,
empty capability sequence, and identity output.

The embedded Wasmtime host instantiates the no-import workflow once and:

1. verifies `workflow_node_count` before execution;
2. calls `run2` with target-allocated input bytes;
3. checks OK plus exact pointer/length alias and unchanged bytes;
4. checks `workflow_completed_count` and `transition_count`;
5. proves `step` and `current_state` trap without changing counters;
6. calls `run2` again and proves counters reset rather than accumulate;
7. invokes legacy `run` and checks the same identity result; and
8. deallocates the outer input exactly once.

The exact node order is still composite evidence in E4-A: native
`WorkflowRuntime` supplies the semantic schedule, the always-on binary parser
supplies the emitted fixed call order, and Wasmtime supplies completion and
transition execution. Counters are not renamed into an event log. Exact
runtime node-order observation remains an explicit E4-B gate.

### 2.4 Honest status after E4-A

| Dimension | E4-A result | Can close full P5? |
| --- | --- | --- |
| state sequence | exact native vs persistent Wasmtime | yes for E1 subset |
| capability sequence | exact one-call SymbolId/status sequence | no multi-call yet |
| pending/resume | PENDING + no-replay latch only | **no resume** |
| workflow node order | native + structural order + executed counters | **no exact runtime event sequence** |
| output frames | E1/E3 identity + E2 conforming OK bytes/ownership | yes for current subset |

The evidence manifest and RFC wording must reproduce this table's boundary.

## 3. Toolchain and supply-chain decision gate

### 3.1 Recommended owner decision

The recommended required evidence lane is one dedicated `ubuntu-24.04`
GitHub Actions job with two official Bytecode Alliance distributions:

1. Wasmtime CLI `28.0.0`, asset
   `wasmtime-v28.0.0-x86_64-linux.tar.xz`, downloaded only from the official
   GitHub release URL and checked against SHA-256
   `677ce3ba138fff0ed6a03564ca7ac43a255d947973ac76e57a67a993d41246c8`;
2. official Python binding `wasmtime==28.0.0`, installed into a job-local venv
   with `pip --require-hashes --only-binary=:all:` and the Linux x86_64 wheel
   SHA-256
   `e2656429c13f408f8cb42b60dc49b96103525ca5e746e948c2cbd6052a86b047`.
   Its required `importlib_resources` dependency is also exact-pinned
   (`7.1.0`, universal-wheel SHA-256
   `1bd7b48b4088eddb2cd16382150bb515af0bd2c70128194392725f82ad2c96a1`);
   an open-ended transitive requirement is not an acceptable lock.

The two packages use the same Wasmtime release line. Version 28 is above the
existing P-6A minimum 15.0.0. Neither artifact is linked into AHFL libraries or
installed by the default configure/build/test path.

A design-time feasibility audit (not release evidence) loaded the existing E1,
E2, and E3 binaries with the official 28.0.0 binding. It executed E1 `step`, E3
`run2` plus persistent globals, and an E2 caller-aware import that read/wrote
the target memory and returned a target-allocated OK frame. This only proves
the proposed host API can express the required test; the committed required CI
lane remains the evidence source.

The same audit ran the repository's existing P-6A, E1, E2, and E3 CLI harnesses
against the checksum-pinned 28.0.0 binary; all four completed non-skipped,
including E2 `--preload`. These local audit results likewise do not replace the
required revision-bound CI/release manifest.

This selection is not active merely because it appears in a draft. The owner
must explicitly accept the version and sources. A different decision requires
a design delta with exact version, official source, checksum, supported runner,
and maintenance owner before implementation.

### 3.2 Bootstrap and local behavior

After approval, `scripts/bootstrap-wasmtime.sh` may move its default pin to
28.0.0 and populate only verified platform checksums. It remains explicit,
opt-in, and default-zero-network.

The Python binding has a separate explicit cache/config path, for example
`AHFL_WASMTIME_PYTHON`. E4-A never invokes `pip` from CMake or CTest. Local
behavior is:

- no CLI/binding configured: tests stay registered and return visible SKIP 77;
- incidental PATH CLI unusable: preserve P-6A SKIP provenance;
- explicitly configured CLI or binding missing, wrong-version, or unusable:
  hard FAIL;
- both explicitly configured and valid: every E4-A observation must run; no
  runtime failure may be normalized to SKIP.

The embedded harness verifies
`importlib.metadata.version("wasmtime") == "28.0.0"` before loading an artifact.

### 3.3 Required CI lane, not a best-effort matrix side effect

The existing Linux/macOS build matrix remains dependency-free and may visibly
skip optional Wasmtime tests. A new Linux-only conformance job:

1. checks out the exact revision;
2. selects an explicit Python 3.12 toolchain, then installs the
   checksum-verified CLI and complete hash-locked Python wheel closure into
   job-local directories;
3. configures a distinct build tree with both paths explicitly supplied;
4. builds only the compiler and conformance producers required by the lane;
5. runs preflight plus E1/E2/E3 CLI tests and the E4 embedded differential;
6. emits JUnit plus one structured evidence manifest;
7. runs a gate that requires every named test to be present, passed, and
   non-skipped; and
8. uploads the manifest, JUnit, emitted wasm files, and their SHA-256 digests.

The job is required, not `continue-on-error`, and has a fixed timeout. A GitHub
runner image update cannot silently turn the lane into green SKIPs because the
post-test gate rejects any missing/skipped required test.

## 4. Evidence manifest and claim gate

### 4.1 Generated release evidence

The conformance job writes a generated, uncommitted JSON artifact under
`build/release-evidence/wasm/`, with a versioned schema such as
`ahfl.wasm-conformance.v1`. It contains only deterministic identities and
observations:

- source commit SHA;
- Wasmtime CLI version plus executable SHA-256;
- Python distribution name/version plus installed wheel SHA-256 from the
  checked lock;
- target triple and canonical wasm32 profile;
- required test names and pass/non-skip status;
- SHA-256 of each emitted E1/E2/E3 module;
- native and Wasmtime state sequences;
- capability SymbolId/status sequence and exact output-frame digest;
- workflow counts/output digest plus explicit
  `runtime_node_order_observed: false`; and
- `durable_resume_observed: false`.

Wall clock, PID, temporary path, allocator address, runner hostname, and map
iteration order are excluded. The manifest is evidence about one source
revision, not a compiler input and not a committed golden.

### 4.2 Fail-closed evidence checker

One always-tested checker validates both real and synthetic fixtures. It
rejects:

- a missing required test;
- any `<skipped>` result;
- a pass line without the expected Wasmtime provenance;
- the wrong CLI or binding version;
- a missing/malformed digest;
- inconsistent native/Wasmtime observations;
- `execution_proven: true` while either durable resume or exact runtime
  workflow order is false; and
- unknown manifest fields that would alter claim semantics without a schema
  bump.

The E4-A producer must set `implemented_subset_execution_evidenced: true` and
`kr6_5_execution_proven: false`. Only E4-B may change the latter after all five
dimensions are non-skipped and exact.

## 5. Implementation split

After this design and the owner decision are approved, E4-A lands as two
non-amended commits in one review series:

1. **E4-A1 toolchain/evidence foundation**: accepted pins, checksum-filled
   explicit bootstrap, hash-locked binding requirement, CMake provenance,
   required CI lane, JUnit/manifest checker, and fail-closed checker tests. No
   codegen or runtime observation changes.
2. **E4-A2 embedded conformance**: one official Wasmtime-Python persistent host,
   richer canonical producer observations, E1/E2/E3 execution matrix, exact
   output ownership checks, generated evidence manifest, and regression tests.

E4-A2 must not edit `core_wasm_codegen.cpp` or change any emitted byte. A byte
digest/golden comparison for the E1/E2 agent paths and E3 workflow path proves
that conformance instrumentation did not alter production artifacts.

## 6. E4-B gates recorded now, not silently deferred

E4-B requires a separate design because each item changes production
semantics or ABI:

1. accept a canonical opaque capability chain without evaluating Core
   expressions, assign per-node ordinals in Core order, and preserve the exact
   RFC 0022 memo key;
2. define one append-only resume entry and a versioned index-based resume
   control record that can be rehydrated into a fresh module instance;
3. cross-check `(node_id, cap_id, ordinal, arg_hash)` without confusing opaque
   wire bytes with P4-D layout or inventing a second `Value` type checker;
4. route owned capability results through a workflow, including last-use
   deallocation, fan-out, ERROR, and PENDING ownership transfer;
5. expose an exact runtime workflow node-order observation without relabeling a
   counter or shipping test-only imports in production artifacts; and
6. prove repeated PENDING/resume never re-invokes memoized capabilities.

The critical unresolved boundary is wire-type validation. RFC 0022 native
resume rejects a memo or injected `Value` whose runtime shape does not match
the declared return type. The current P5 wasm artifact intentionally treats
`value_json` as opaque and cannot make that proof. E4-B must choose and review
where canonical wire decode/type validation lives before durable resume is
enabled. A host-provided type id, layout id, or unchecked byte hash alone is
not sufficient proof.

Until E4-B closes these gates, PENDING remains the E2 propagated/latching
subset and workflow capability composition remains fail-closed.

## 7. Test plan

Always-on tests, requiring no real Wasmtime:

1. explicit-vs-incidental CLI and binding provenance decision tables;
2. pinned-version and checksum lock synchronization;
3. JUnit gate rejects missing, failed, skipped, duplicate, and unknown tests;
4. evidence manifest parser rejects every malformed/overclaimed field listed
   in section 4.2;
5. embedded harness dependency absence returns 77 only when not explicitly
   configured;
6. fake binding/CLI cannot satisfy the real provenance marker;
7. producer observation parsers reject extra lines, malformed ids, and
   inconsistent counts; and
8. existing E1/E2/E3 binary/Node tests stay byte-identical and green.

Required real-Wasmtime CI/release tests after owner approval:

1. P-6A preflight execution;
2. E1 CLI state differential;
3. E2 CLI ERROR/PENDING preload execution;
4. E3 CLI workflow run passthrough;
5. E4 embedded E1 complete state sequence plus identity output;
6. E4 embedded E2 OK/ERROR/PENDING/unknown/latch matrix, exact SymbolId sequence,
   conforming output bytes, and deallocation counts;
7. E4 embedded E3 persistent counts, traps, reset, legacy run, and identity
   output; and
8. final evidence gate proving all seven preceding test identities passed
   without SKIP.

## 8. Alternatives rejected

1. **Treat Node as enough to close KR6.5.** Node is valuable independent engine
   evidence but does not satisfy the already documented real-Wasmtime claim.
2. **Install an unpinned latest Wasmtime in CI.** This makes evidence depend on
   release time and supply-chain state; it violates reproducibility.
3. **Use only the Wasmtime CLI.** It cannot provide conforming E2 OK memory or
   persistent E3 observations.
4. **Link Wasmtime into AHFL core libraries.** The runtime is a test/host tool,
   not a new production dependency; RFC 0019/0021 explicitly reject this.
5. **Mark KR6.5 complete after E4-A.** PENDING resume and exact workflow runtime
   order are still absent, so the five-dimension contract would remain false.
6. **Implement resume in the evidence harness only.** That would test a host
   simulation rather than the emitted artifact and create a second execution
   engine.

## 9. Review checklist

E4-A implementation may start only after reviewers confirm:

- the owner accepted exact tool versions/sources/checksums;
- default builds remain zero-network and dependency-free;
- required CI rejects SKIP instead of merely displaying it;
- official Wasmtime embedded execution is distinct from Node and CLI evidence;
- conforming OK frames are in target module memory and freed exactly once;
- no codegen byte or P5/P6 boundary changes in E4-A;
- the manifest cannot overclaim execution-proven; and
- E4-B's resume/order/wire-validation gates remain explicit.
