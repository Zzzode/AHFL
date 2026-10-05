# tests/mutation — Mutation Testing

AHFL mutation-testing assets. There are two runners plus two CTest gates.

## Runners

| Script | Depends on mull? | Produces a real score? |
|--------|------------------|------------------------|
| `run_fallback_mutation.sh` | No | **Yes** — a genuine killed/survived score from fixed, per-target mutant sets |
| `run_mutation.sh` | Yes (`mull-runner` + `mull-ir-frontend`) | Yes when mull is installed; otherwise emits an honest `tool_unavailable` report |

`mull` (an LLVM-based mutation tool) is not installed in CI here, so the
fallback runner is what produces a real mutation score in this environment.
Neither runner ever fabricates a score.

### `run_fallback_mutation.sh` (self-contained)

Applies a fixed, hand-audited set of source mutants to **copies** of small
target translation units, rebuilds a narrow test binary against each mutated
copy with `$CXX` (default: `g++`/`clang++`/`c++`), and records whether the test
suite catches (kills) the mutant. The real source tree is never modified —
every build runs inside a scratch `mktemp -d` directory.

The runner is **target-agnostic**. It discovers every target under
`fallback/*/` that carries a `<name>.target.json` spec, so adding a
representative target never edits the runner. Each spec pins, next to its
target's source:

- the mutant set as literal `match`/`replace` string pairs (not regexes),
- a human `description` per mutant,
- the **expected** outcome (`expect`: `killed` | `survived`), and
- the committed `score_floor` for the target.

Three representative targets ship:

| Target | What it covers |
|--------|----------------|
| `arithmetic/` | integer arithmetic/predicate operators |
| `structured_writer/` | a canonical JSON-ish writer (field separators, string escaping, object punctuation) |
| `state_machine/` | agent-style state machine (transition table lookup, guard conditions, terminal checks, sentinel returns) |

Each target has exactly **one deliberate, expected survivor** (an intentionally
untested code path), so all three report an honest sub-100% score, never a rigged
100%. `fallback/targets.json` is the signed-off set of targets and each
target's mutant count; it is what makes a deleted target directory fail the
gate instead of silently shrinking it.

Two body patterns are worth knowing:

- The substitution is literal (`index()`/`substr()`), and the patterns are
  passed to `awk` through the **environment**, not `awk -v`: `-v` applies
  escape processing, which silently turns a backslash-bearing pattern (a C
  string escape) into a no-op substitution.
- The runner distinguishes an **environment** reason ("this machine cannot run
  mutation testing" — no compiler, no python3) from **fixture breakage** (a
  spec that no longer matches its source, a baseline that will not build). Only
  the former yields `status: "tool_unavailable"`; the latter fails the run so
  the gate cannot pass on a broken fixture.

```bash
./run_fallback_mutation.sh --report /tmp/fallback-score.json
```

### `run_mutation.sh` (mull path)

Drives `mull-runner` when installed, parses its
mutation-testing-elements JSON, and computes killed/survived/score. When
`mull-runner` is absent it writes a `tool_unavailable` report and exits 0.

```bash
./run_mutation.sh --report /tmp/mull-score.json
```

## Per-target mutant specs (`fallback/<target>/<target>.target.json`)

```json
{
  "schema": "ahfl.mutation.target.v1",
  "target": "arithmetic",
  "source": "arithmetic.cpp",
  "test": "arithmetic_test.cpp",
  "score_floor": 0.88,
  "mutants": [
    {
      "id": "classify_rel",
      "description": "relational operator <= -> < in classify()",
      "match": "x <= 0",
      "replace": "x < 0",
      "expect": "killed"
    }
  ]
}
```

`match` must appear in `source` **exactly once** (the runner verifies this and
fails otherwise). The target's `test` must pass unmutated for it to be a valid
baseline. Order matters: the report lists mutants in spec order, and the gate
pins them by index.

## JSON score-report schemas

### `ahfl.mutation.fallback.v2`

```json
{
  "schema": "ahfl.mutation.fallback.v2",
  "runner": "fallback",
  "status": "ok",                 // or "tool_unavailable"
  "compiler": "g++",              // present when status == ok
  "mutants_total": 24,
  "mutants_evaluated": 24,
  "killed": 21,
  "survived": 3,
  "mutation_score": 0.8750,       // killed/evaluated; null when unavailable
  "targets": [
    {
      "target": "arithmetic",
      "source": "arithmetic.cpp",
      "score_floor": 0.88,
      "mutants_total": 9,
      "mutants_evaluated": 9,
      "killed": 8,
      "survived": 1,
      "mutation_score": 0.8889,
      "mutants": [
        {"id": "classify_rel", "description": "...", "expect": "killed",   "outcome": "killed"},
        {"id": "scaled_arith", "description": "...", "expect": "survived", "outcome": "survived"}
      ]
    }
  ]
}
```

`status: "tool_unavailable"` carries a non-empty `reason` that **starts with
`environment: `** (no C++ compiler, or no python3), `mutation_score: null`, and
an empty `targets` array. It is an honest "cannot run" contract, not a
fabricated score.

### `ahfl.mutation.mull.v1`

```json
{
  "schema": "ahfl.mutation.mull.v1",
  "runner": "mull",
  "status": "ok",                 // or "tool_unavailable"
  "source_report": "<path to mull JSON>",
  "mutants_total": 0,
  "killed": 0,
  "survived": 0,
  "mutation_score": 0.0000        // null when unavailable
}
```

## CTest gates

| Test | What it asserts |
|------|-----------------|
| `ahfl.mutation.config_report` | `mutation_config.json` plumbing is well-formed (budgets on targets/suites/mutators) |
| `ahfl.mutation.fallback_score` | Runs `run_fallback_mutation.sh` and turns the score into a **release-blocking** signal (see below) |

Both are labelled `mutation` and `quality-gates`.

### What `ahfl.mutation.fallback_score` blocks on (KR7.3)

The gate (`RunFallbackMutationGate.cmake`) reads the committed specs directly
and fails when:

1. the report's target set differs from `fallback/targets.json` (a dropped or
   renamed target is a coverage regression, not a silent pass);
2. a target's spec declares a different mutant count than `targets.json`
   (deleting mutants from a spec must be a reviewed manifest edit);
3. any mutant's `outcome` differs from the `expect` pinned in its spec — a
   strengthening test that stopped killing a mutant, **or** a survivor that
   became covered, both fail until the spec is reviewed;
4. a target's `mutation_score` falls below its committed `score_floor`; or
5. the report is malformed, or reports `tool_unavailable` for a non-environment
   reason (i.e. a broken fixture).

It accepts an honest `tool_unavailable` only when the reason carries the
`environment: ` sentinel. Per-mutant expectations are reproducible because the
mutants are fixed literal substitutions and the test binary is rebuilt from
copies in a scratch directory.

```bash
ctest --preset test-dev -L mutation --output-on-failure
```

Flip one mutant's `expect` in a spec (killed -> survived) and the gate fails —
that is the intended "the gate is really gating" self-check.
