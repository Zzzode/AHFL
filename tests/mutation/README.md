# tests/mutation — Mutation Testing

AHFL mutation-testing assets. There are two runners plus two CTest gates.

## Runners

| Script | Depends on mull? | Produces a real score? |
|--------|------------------|------------------------|
| `run_fallback_mutation.sh` | No | **Yes** — a genuine killed/survived score from a fixed mutant set |
| `run_mutation.sh` | Yes (`mull-runner` + `mull-ir-frontend`) | Yes when mull is installed; otherwise emits an honest `tool_unavailable` report |

`mull` (an LLVM-based mutation tool) is not installed in CI here, so the
fallback runner is what produces a real mutation score in this environment.
Neither runner ever fabricates a score.

### `run_fallback_mutation.sh` (self-contained)

Applies a fixed, hand-audited set of source mutants to **copies** of a tiny
target translation unit (`fallback/target.cpp`), rebuilds a narrow test
binary (`fallback/target_test.cpp`) against each mutated copy with `$CXX`
(default: `g++`/`clang++`/`c++`), and records whether the test suite catches
(kills) the mutant. The real source tree is never modified — every build runs
inside a scratch `mktemp -d` directory.

The mutant set exercises a range of mutation operator categories a real
mutation tool would hit — relational-operator swaps, arithmetic-operator
swaps, boolean-connective swaps, unary-negation removal, constant replacement,
comparison-boundary swaps, and off-by-one loop bounds. It is deterministic: 9
mutants, 8 expected killed, 1 expected survivor (`scaled()` is intentionally
left untested so the score is an honest 8/9, not a rigged 100%).

| Mutant id | Operator category | Mutation | Expected |
|-----------|-------------------|----------|----------|
| `classify_rel`    | relational        | `<= ` -> `<` in `classify()`      | killed |
| `add_arith`       | arithmetic        | `+` -> `-` in `add()`             | killed |
| `is_valid_rel`    | relational        | `>=` -> `>` in `is_valid()`       | killed |
| `scaled_arith`    | arithmetic        | `*` -> `+` in `scaled()`          | **survived** (untested) |
| `bool_connective` | boolean connective| `&&` -> `\|\|` in `in_range()`     | killed |
| `unary_negation`  | unary negation    | `!a` -> `a` in `negate()`         | killed |
| `const_replace`   | constant          | `return 0` -> `return 1` in `origin()` | killed |
| `eq_boundary`     | comparison boundary| `==` -> `!=` in `equals()`       | killed |
| `loop_bound`      | off-by-one        | `i < n` -> `i <= n` in `count_to()` | killed |

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

## JSON score-report schemas

### `ahfl.mutation.fallback.v1`

```json
{
  "schema": "ahfl.mutation.fallback.v1",
  "runner": "fallback",
  "status": "ok",                 // or "tool_unavailable"
  "compiler": "g++",              // present when status == ok
  "mutants_total": 9,
  "mutants_evaluated": 9,
  "killed": 8,
  "survived": 1,
  "mutation_score": 0.8889,       // killed/evaluated; null when unavailable
  "mutants": [
    {"id": "classify_rel",    "description": "...", "outcome": "killed"},
    {"id": "add_arith",       "description": "...", "outcome": "killed"},
    {"id": "is_valid_rel",    "description": "...", "outcome": "killed"},
    {"id": "scaled_arith",    "description": "...", "outcome": "survived"},
    {"id": "bool_connective", "description": "...", "outcome": "killed"},
    {"id": "unary_negation",  "description": "...", "outcome": "killed"},
    {"id": "const_replace",   "description": "...", "outcome": "killed"},
    {"id": "eq_boundary",     "description": "...", "outcome": "killed"},
    {"id": "loop_bound",      "description": "...", "outcome": "killed"}
  ]
}
```

`status: "tool_unavailable"` (no C++ compiler, or the baseline build/test
failed) carries a non-empty `reason`, `mutation_score: null`, and an empty
`mutants` array. It is an honest "cannot run" contract, not a fabricated
score.

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
| `ahfl.mutation.fallback_score` | Runs `run_fallback_mutation.sh` and validates the JSON score report is produced and structurally well-formed: schema, status, `killed + survived == evaluated`, at least the fixed mutant set was evaluated, each mutant has an `id` and a valid `outcome`. It does **not** gate on an absolute score threshold (that would be flaky). |

Both are labelled `v0.59-mutation` and `v0.59-quality-gates`.

```bash
ctest --preset test-dev -L v0.59-mutation --output-on-failure
```
