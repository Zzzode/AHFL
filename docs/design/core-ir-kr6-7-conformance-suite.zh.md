# Core-IR KR6.7: Engine-Agnostic Conformance Case Contract -- Design

> Status: **implemented (foundation slice)** (RFC 0026 P7 / KR6.7).
>
> Scope: define the engine-neutral conformance case manifest format
> (`ahfl.conformance-case.v1`), the committed `tests/conformance/cases/`
> catalogue, and the fail-closed schema validator that every later conformance
> runner consumes. This slice touches NO engine code: it neither lowers
> expressions nor executes a program. It is buildable and fully tested
> independently of the KR6.6 computation lane. The existing E4 design note
> (`core-ir-kr6-5-e4-conformance.zh.md`) covers only the real-Wasmtime evidence
> gate for the E1-E3 subset; this note covers the case contract that evidence
> is compared against.

## 1. Why a neutral case contract

KR6.7 must compare the observable behaviour of structurally different engines
over the same source fixture:

1. the tree-walking native evaluator / `WorkflowRuntime`;
2. the P5 orchestration wasm lane (state machines + workflow DAG scheduling,
   opaque capability frames); and
3. the future KR6.6 computation wasm lane (in-wasm expression and
   control-flow evaluation).

Without a single shared manifest, each engine harness would re-declare the
entry target, the input frame, the mocked capability outcomes, and the expected
observations in its own dialect. A case change (for example renaming a field,
or marking a case pending-resume) would then have to be replicated three ways
and could silently diverge. The manifest is the SSOT: an engine runner takes
an already-validated `ConformanceCase` and maps it onto its own invocation
mechanism.

Following the accepted versioned-sidecar precedent of
`ahfl.capability-mocks.v0.6` (parsed in
`src/pipeline/execution/dry_run/runner.hpp`), a case is a JSON document with
an explicit `format_version`. Unknown schema revisions and unknown fields are
rejected rather than ignored, so a case cannot overclaim semantics the
validator does not understand.

## 2. Layout

```text
tests/conformance/
  conformance_case.hpp          # schema data model + strict parser/validator
  cases/
    *.case.json                 # one sidecar per conformance fixture
tests/unit/runtime/conformance/
  conformance_case_test.cpp     # parser/validator battery (no engine linked)
```

A sidecar is paired with, but never nested under, the fixture it describes:
the committed catalogue references the existing `tests/golden/wasm/` and
`tests/golden/runtime/` sources by repo-relative `source` path, reusing the
already-frozen fixtures rather than duplicating them. `load_conformance_case`
resolves that path against an explicit repository root and fails closed when
the referenced `.ahfl` file is absent.

## 3. Manifest schema (`ahfl.conformance-case.v1`)

```json
{
  "format_version": "ahfl.conformance-case.v1",
  "source": "tests/golden/wasm/e1_identity_agent.ahfl",
  "kind": "agent",
  "entry": "wasm::e1_identity::IdentityAgent",
  "input": {"_type": "wasm::e1_identity::Frame", "value": "identity"},
  "capabilities": [
    {"name": "wasm::e2_capability::Echo", "status": "ok",
     "result_json": {"_type": "wasm::e2_capability::OutputFrame", "value": "echo"}}
  ],
  "expect": {
    "run_status": "completed",
    "state_sequence": ["Start", "Done"],
    "capability_sequence": ["wasm::e2_capability::Echo"],
    "output_json": {"_type": "wasm::e2_capability::OutputFrame", "value": "echo"}
  },
  "engines": {
    "evaluator": true,
    "wasm": {"eligible": "orchestration", "reason": "E2 single capability call"}
  }
}
```

### 3.1 Identity is name-only

Consistent with CLAUDE.md Principle 2, a case never carries numeric
engine-internal ids. Everything an engine observes cross-boundary is a
source-level name:

* `entry` is the canonical target (`module::Agent` / `module::Workflow`);
* `state_sequence` is declaration-order state names for an agent case;
* `capability_sequence` and `capabilities[].name` are canonical capability
  names;
* workflow node order is intentionally NOT encoded here (section 5).

Numeric `SymbolId` / `CoreCapabilityId` / ordinal identity is an engine-
internal matter and stays in the runner adapters.

### 3.2 Fields

| Field | Required | Meaning |
| --- | --- | --- |
| `format_version` | yes | Must equal `ahfl.conformance-case.v1`. |
| `source` | yes | Repo-relative POSIX path to an existing `.ahfl` file; absolute paths and `..` components are rejected. |
| `kind` | yes | `agent` or `workflow`. |
| `entry` | yes | Non-empty canonical entry target. |
| `input` | yes | Canonical wire JSON fed to the run (the `_type` / `_enum` dialect already consumed by `ahflc run --input`). |
| `capabilities` | yes | Array (possibly empty) of mocked outcomes keyed by canonical capability name; duplicate names are rejected. |
| `capabilities[].status` | yes | `ok`, `error`, or `pending`. |
| `capabilities[].result_json` | no | Canonical wire result for `ok`/`error`; forbidden for `pending`. |
| `expect` | yes | Observation contract (section 4). |
| `engines` | yes | Engine eligibility matrix (section 6). |

Every field is mandatory and the object readers are strict: an unknown
top-level or nested field is a hard error. The parser also relies on the JSON
DOM parser's existing duplicate-key rejection, so `{"a":1,"a":2}` cannot pass.

### 3.3 Canonical wire JSON fragments

The `input`, `expect.output_json`, and `capabilities[].result_json` fragments
must be CANONICAL compact wire JSON. The validator re-serializes the parsed
DOM with the byte conventions of runtime `value_to_json` and requires byte
equality against the exact source span:

* no insignificant whitespace anywhere inside the fragment (the outer
  manifest remains human-readable);
* struct objects (`_type`) emit the discriminator first, then remaining fields
  in lexicographic order (the runtime `FieldMap` invariant);
* enum objects (`_enum`) emit the fixed wire order `_enum`, `_variant`,
  `_payload`, `_named_payload`;
* other objects (maps) emit lexicographically sorted keys;
* numbers use the existing shortest-round-trip spellings.

This guarantees a case can never pin an engine to a non-canonical encoding and
keeps output comparison a byte equality, matching the E4 evidence rule that
observations are exact wire bytes rather than semantic approximations.

## 4. Observation contract

`expect` declares what a conforming engine run must be compared against:

* `run_status`: `completed`, `suspended` (a pending capability), or `failed`;
* `state_sequence`: for an **agent** case, the non-empty sequence of state
  names the run enters; for a **workflow** case it MUST be empty, because a
  flat list across multiple agents would be ambiguous;
* `capability_sequence`: canonical capability names in invocation order, and
  every name listed must have a matching `capabilities[]` mock entry;
* `output_json`: optional canonical wire JSON of the run output.

The validator enforces only the manifest-internal shape of this contract.
Mapping `WorkflowStatus::Suspended` / capability `Pending` onto the vocabulary
above, observing an exact state sequence, and diffing output bytes belong to
the engine adapters (later slices); no engine symbol is referenced here.

## 5. What is deliberately deferred

The exact *workflow runtime node-order* observation is not part of this
manifest. It is the named E4-B gate: the P5 wasm artifact currently exposes
completion/transition counters, not an exact node event stream, and relabelling
a counter as an order log would repeat the exact mistake the E4 design
forbids. A future schema revision (`v2` or a sibling manifest) will add a
structured node-order contract once an engine can truthfully observe it.

Likewise, this slice defines no runner, no cross-engine differential driver,
and no evidence manifest. Those consume `ConformanceCase` but live in
subsequent slices so the case contract can land and be reviewed on its own.

## 6. Engine eligibility metadata

`engines` lets an adapter decide whether a case applies without hard-coding
fixture knowledge:

* `evaluator`: the native evaluator lane runs the case;
* `wasm.eligible`:
  * `orchestration` -- executable on the P5 orchestration wasm subset
    (state transitions, workflow DAG scheduling, opaque capability frames);
  * `computation` -- requires in-wasm expression / control-flow evaluation, so
    the orchestration adapter SKIPS it until the KR6.6 lane lands;
  * `none` -- permanently host-side semantics;
* `wasm.reason` is ALWAYS required and non-empty. A skip therefore reports a
  structured, case-authored reason instead of an unexplained omission, which is
  the same provenance discipline as the Wasmtime preflight SKIP 77 lane.

The current catalogue marks the four `tests/golden/wasm/` fixtures
`orchestration` (they are the E1-E3 codegen fixtures) and the three
`tests/golden/runtime/` fixtures `computation` (enum/match/if-let projection
and multi-agent capability chains).

## 7. Validation rules and diagnostics

All failures are emitted on a `DiagnosticBag` with the sidecar filename as
source name and, when the offender is a JSON value, a `SourceRange` over the
manifest bytes. The validator rejects at least:

* malformed JSON / non-object root;
* wrong or missing `format_version`;
* missing required fields (`entry`, `expect`, `engines`, ...);
* bad enum spellings (`kind`, capability `status`, `run_status`,
  `wasm.eligible`);
* non-canonical wire fragments;
* a missing `engines.wasm.reason`;
* a `pending` capability carrying `result_json`;
* duplicate capability entries;
* a `capability_sequence` name without a configured mock;
* a workflow case carrying a `state_sequence`, or an agent case with an empty
  one;
* absolute or parent-escaping `source` paths;
* any unknown field at any nesting level;
* and, at load time, a referenced source that does not exist under the
  repository root.

## 8. Test plan

`ahfl_conformance_case_tests` (ctest name `ahfl.conformance_case`) links only
`ahfl_base_json` plus public diagnostics -- deliberately no engine library:

1. all seven committed sidecars load, reference an existing `.ahfl` source,
   and expose the expected kind / entry / capability count / eligibility /
   reason, with byte-exact canonical input and output assertions on the
   richest case;
2. malformed manifests (sections 3-4 and 7) are rejected with a diagnostic
   containing the precise rule text;
3. the canonicality gate accepts a compact ordered fragment and rejects both
   embedded whitespace and a shuffled struct field;
4. `load_conformance_case` fails closed for a missing sidecar and for a
   schema-valid sidecar whose source does not exist.

No engine execution is invoked, so the test is deterministic and independent
of the Wasmtime / KR6.6 state.

## 9. Alternatives rejected

1. **One dialect per engine.** Rejected: the case contract would fork and a
   rename could silently change which behaviour is compared where.
2. **Numeric ids inside cases.** Rejected by Principle 2: ids are
   engine-internal and unstable across lowering; names are the cross-boundary
   identity.
3. **Semantic (parse-and-compare) JSON matching for observations.** Rejected:
   it would accept whitespace/key-order divergence and weaken the exact-byte
   evidence rule; canonical byte equality is the same contract the wire codec
   already enforces.
4. **Encoding workflow node order in `state_sequence`.** Rejected: ambiguous
   across agents and contradicted by the E4-B finding that counters are not an
   event stream.
5. **Permitting an absent `wasm.reason`.** Rejected: a skip without a
   case-authored reason loses the provenance the conformance lane exists to
   provide.
