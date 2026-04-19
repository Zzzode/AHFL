# AHFL

AHFL is a strongly typed DSL and compiler toolchain for describing agent
state machines, contracts, flows, and workflow orchestration.

The repository currently contains the C++23 implementation of `ahflc`: a
compiler-style CLI that parses AHFL source, checks it, validates project-aware
source graphs, and emits IR / runtime / formal backend outputs.

## Why AHFL Exists

AHFL is meant to make agent behavior auditable before it becomes runtime code:

- model an agent as an explicit state machine
- whitelist capabilities the agent may call
- express preconditions, postconditions, invariants, and forbidden behavior
- type-check flow logic and workflow data movement
- emit stable machine-readable artifacts for downstream tooling

The short version: AHFL tries to move agent orchestration from “prompt plus
glue code” into a checked, inspectable contract.

## Language Sketch

```ahfl
agent RefundAudit {
    input: RefundRequest;
    context: RefundContext;
    output: RefundDecision;
    states: [Init, Auditing, Approved, Rejected, Terminated];
    initial: Init;
    final: [Terminated];
    capabilities: [OrderQuery, AuditDecision, TicketCreate];

    transition Init -> Auditing;
    transition Auditing -> Approved;
    transition Auditing -> Rejected;
    transition Approved -> Terminated;
    transition Rejected -> Terminated;
}

contract for RefundAudit {
    requires: order_exists(input.order_id);
    invariant: always not called(RefundExecute);
    forbid: always not called(UserInfoModify);
}

workflow RefundAuditWorkflow {
    input: RefundRequest;
    output: RefundDecision;

    node audit: RefundAudit(input);

    safety: always not running(audit) or eventually completed(audit);
    liveness: eventually completed(audit, Terminated);

    return: audit;
}
```

Full example: [`examples/refund_audit_core_v0_1.ahfl`](examples/refund_audit_core_v0_1.ahfl)

## Quick Start

Requirements:

- CMake 3.22+
- C++23 compiler
- Ninja, when using the checked-in presets

Build:

```bash
cmake --preset dev
cmake --build --preset build-dev
```

Check the bundled example:

```bash
./build/dev/src/cli/ahflc check examples/refund_audit_core_v0_1.ahfl
```

Inspect compiler output:

```bash
./build/dev/src/cli/ahflc dump-ast examples/refund_audit_core_v0_1.ahfl
./build/dev/src/cli/ahflc dump-types examples/refund_audit_core_v0_1.ahfl
./build/dev/src/cli/ahflc emit-ir-json examples/refund_audit_core_v0_1.ahfl
```

Run the regression suite:

```bash
ctest --preset test-dev
```

## What `ahflc` Does

The current pipeline is:

```text
AHFL source
  -> ANTLR parser
  -> hand-written AST
  -> resolver
  -> type checker
  -> validator
  -> backend emitters
```

Supported frontend concepts include:

- declarations: `struct`, `enum`, `const`, `type`, `capability`, `predicate`
- agent definitions with states, transitions, quotas, and capability allowlists
- `contract for` blocks with `requires`, `ensures`, `invariant`, and `forbid`
- `flow for` blocks with state handlers, calls, assignments, `if`, `goto`, and `return`
- DAG-style `workflow` definitions with safety and liveness formulas
- project-aware loading through search roots, project descriptors, and workspaces

Backend outputs:

| Command | Output |
| --- | --- |
| `emit-ir` | stable textual semantic IR |
| `emit-ir-json` | machine-readable structured IR |
| `emit-summary` | capability-oriented backend summary |
| `emit-native-json` | runtime-facing Native handoff package |
| `emit-execution-plan` | execution plan artifact for planner / dry-run bootstrap |
| `emit-runtime-session` | deterministic runtime session snapshot for local runtime bootstrap |
| `emit-execution-journal` | deterministic event journal for replay / audit bootstrap |
| `emit-replay-view` | deterministic replay projection over plan, session, and journal |
| `emit-scheduler-snapshot` | deterministic scheduler-facing snapshot over plan, session, journal, and replay |
| `emit-checkpoint-record` | deterministic checkpoint-facing record over plan, session, journal, replay, and snapshot |
| `emit-checkpoint-review` | reviewer-facing checkpoint summary over checkpoint record |
| `emit-scheduler-review` | reviewer-facing scheduler decision summary over scheduler snapshot |
| `emit-audit-report` | deterministic audit report across plan, session, journal, and trace |
| `emit-dry-run-trace` | deterministic local dry-run trace with capability mocks |
| `emit-package-review` | package-aware review and planner bootstrap summary |
| `emit-smv` | restricted formal backend for model-check-oriented tooling |

## Project-Aware Inputs

Most commands support the same input modes:

```bash
# Single file
./build/dev/src/cli/ahflc check examples/refund_audit_core_v0_1.ahfl

# Source graph rooted at one or more directories
./build/dev/src/cli/ahflc check \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl

# Project descriptor
./build/dev/src/cli/ahflc check \
  --project tests/project/check_ok/ahfl.project.json

# Workspace descriptor
./build/dev/src/cli/ahflc check \
  --workspace tests/project/ahfl.workspace.json \
  --project-name check-ok
```

Use `dump-project` when you only want to inspect the loaded source graph.

## Common Commands

```bash
# Configure / build
cmake --preset dev
cmake --build --preset build-dev

# Format handwritten C++ sources
cmake --build --preset build-format
cmake --build --preset build-format-check

# Test
ctest --preset test-dev
ctest --preset test-dev -L ahfl-v0.3

# Regenerate the C++ parser module
ANTLR_JAR=/path/to/antlr-4.x-complete.jar ./scripts/regenerate-parser.sh
```

Available configure presets:

- `dev`
- `release`
- `relwithdebinfo`
- `asan`

## Repository Map

| Path | Purpose |
| --- | --- |
| `grammar/AHFL.g4` | source grammar |
| `src/parser/generated/` | generated ANTLR C++ parser sources |
| `include/ahfl/` | public compiler headers |
| `src/frontend/` | parsing, AST lowering, project loading |
| `src/semantics/` | resolver, type checker, validator |
| `src/ir/` | semantic IR model |
| `src/backends/` | IR, JSON, Native, summary, and SMV emitters |
| `src/cli/ahflc.cpp` | CLI entry point |
| `tests/` | regression and golden tests |
| `docs/` | spec, design, plan, and reference docs |

## Documentation

Start here instead of reading a wall of filenames:

- [`docs/plan/roadmap-v0.11.zh.md`](docs/plan/roadmap-v0.11.zh.md) — current V0.11 execution roadmap
- [`docs/plan/issue-backlog-v0.11.zh.md`](docs/plan/issue-backlog-v0.11.zh.md) — current V0.11 issue backlog
- [`docs/design/native-checkpoint-prototype-bootstrap-v0.11.zh.md`](docs/design/native-checkpoint-prototype-bootstrap-v0.11.zh.md) — V0.11 checkpoint prototype scope and layering
- [`docs/reference/checkpoint-prototype-compatibility-v0.11.zh.md`](docs/reference/checkpoint-prototype-compatibility-v0.11.zh.md) — V0.11 checkpoint artifact compatibility baseline
- [`docs/reference/native-consumer-matrix-v0.11.zh.md`](docs/reference/native-consumer-matrix-v0.11.zh.md) — V0.11 native consumer matrix
- [`docs/reference/contributor-guide-v0.11.zh.md`](docs/reference/contributor-guide-v0.11.zh.md) — current contributor guide for checkpoint-facing artifacts
- [`docs/spec/core-language-v0.1.zh.md`](docs/spec/core-language-v0.1.zh.md) — normative Core language spec
- [`docs/reference/cli-commands-v0.10.zh.md`](docs/reference/cli-commands-v0.10.zh.md) — current CLI command reference
- [`docs/reference/project-usage-v0.5.zh.md`](docs/reference/project-usage-v0.5.zh.md) — project / workspace / package authoring usage
- [`docs/reference/ir-format-v0.3.zh.md`](docs/reference/ir-format-v0.3.zh.md) — stable IR reference
- [`docs/reference/native-package-authoring-compatibility-v0.5.zh.md`](docs/reference/native-package-authoring-compatibility-v0.5.zh.md) — Native package authoring compatibility
- [`docs/reference/native-handoff-usage-v0.5.zh.md`](docs/reference/native-handoff-usage-v0.5.zh.md) — Native handoff usage
- [`docs/reference/runtime-session-compatibility-v0.9.zh.md`](docs/reference/runtime-session-compatibility-v0.9.zh.md) — V0.9 runtime session compatibility
- [`docs/reference/execution-journal-compatibility-v0.9.zh.md`](docs/reference/execution-journal-compatibility-v0.9.zh.md) — V0.9 execution journal compatibility
- [`docs/reference/replay-view-compatibility-v0.9.zh.md`](docs/reference/replay-view-compatibility-v0.9.zh.md) — V0.9 replay view compatibility
- [`docs/reference/audit-report-compatibility-v0.9.zh.md`](docs/reference/audit-report-compatibility-v0.9.zh.md) — V0.9 audit report compatibility
- [`docs/reference/failure-path-compatibility-v0.9.zh.md`](docs/reference/failure-path-compatibility-v0.9.zh.md) — V0.9 failure-path compatibility
- [`docs/reference/checkpoint-prototype-compatibility-v0.11.zh.md`](docs/reference/checkpoint-prototype-compatibility-v0.11.zh.md) — V0.11 checkpoint prototype compatibility baseline
- [`docs/reference/scheduler-prototype-compatibility-v0.10.zh.md`](docs/reference/scheduler-prototype-compatibility-v0.10.zh.md) — V0.10 scheduler prototype compatibility
- [`docs/reference/native-consumer-matrix-v0.11.zh.md`](docs/reference/native-consumer-matrix-v0.11.zh.md) — current native consumer matrix
- [`docs/reference/contributor-guide-v0.11.zh.md`](docs/reference/contributor-guide-v0.11.zh.md) — current contributor guide for checkpoint-facing artifacts
- [`docs/reference/native-consumer-matrix-v0.10.zh.md`](docs/reference/native-consumer-matrix-v0.10.zh.md) — completed scheduler-facing consumer matrix baseline
- [`docs/reference/contributor-guide-v0.10.zh.md`](docs/reference/contributor-guide-v0.10.zh.md) — completed scheduler-facing contributor guide baseline
- [`docs/design/native-checkpoint-prototype-bootstrap-v0.11.zh.md`](docs/design/native-checkpoint-prototype-bootstrap-v0.11.zh.md) — checkpoint prototype / resume-basis boundary
- [`docs/design/native-scheduler-prototype-bootstrap-v0.10.zh.md`](docs/design/native-scheduler-prototype-bootstrap-v0.10.zh.md) — scheduler prototype / checkpoint-friendly boundary
- [`docs/design/native-execution-plan-architecture-v0.6.zh.md`](docs/design/native-execution-plan-architecture-v0.6.zh.md) — Execution plan boundary
- [`docs/design/native-dry-run-bootstrap-v0.6.zh.md`](docs/design/native-dry-run-bootstrap-v0.6.zh.md) — local dry-run bootstrap boundary
- [`docs/design/native-runtime-session-bootstrap-v0.7.zh.md`](docs/design/native-runtime-session-bootstrap-v0.7.zh.md) — runtime session boundary
- [`docs/design/native-execution-journal-v0.7.zh.md`](docs/design/native-execution-journal-v0.7.zh.md) — execution journal boundary
- [`docs/design/native-replay-audit-bootstrap-v0.8.zh.md`](docs/design/native-replay-audit-bootstrap-v0.8.zh.md) — replay / audit boundary
- [`docs/design/native-partial-session-failure-bootstrap-v0.9.zh.md`](docs/design/native-partial-session-failure-bootstrap-v0.9.zh.md) — partial session / failure-path boundary
- [`docs/design/native-package-authoring-architecture-v0.5.zh.md`](docs/design/native-package-authoring-architecture-v0.5.zh.md) — Native package authoring boundary
- [`docs/design/native-consumer-bootstrap-v0.5.zh.md`](docs/design/native-consumer-bootstrap-v0.5.zh.md) — Native consumer bootstrap boundary
- [`docs/design/testing-strategy-v0.5.zh.md`](docs/design/testing-strategy-v0.5.zh.md) — V0.5 testing strategy
- [`docs/plan/roadmap-v0.11.zh.md`](docs/plan/roadmap-v0.11.zh.md) — current execution plan
- [`docs/plan/issue-backlog-v0.11.zh.md`](docs/plan/issue-backlog-v0.11.zh.md) — current execution backlog
- [`docs/plan/roadmap-v0.10.zh.md`](docs/plan/roadmap-v0.10.zh.md) — completed V0.10 baseline
- [`docs/plan/issue-backlog-v0.10.zh.md`](docs/plan/issue-backlog-v0.10.zh.md) — completed V0.10 backlog
- [`docs/plan/roadmap-v0.9.zh.md`](docs/plan/roadmap-v0.9.zh.md) — completed V0.9 baseline
- [`docs/plan/issue-backlog-v0.9.zh.md`](docs/plan/issue-backlog-v0.9.zh.md) — completed V0.9 backlog
- [`docs/plan/roadmap-v0.8.zh.md`](docs/plan/roadmap-v0.8.zh.md) — completed V0.8 baseline
- [`docs/plan/issue-backlog-v0.8.zh.md`](docs/plan/issue-backlog-v0.8.zh.md) — completed V0.8 backlog
- [`docs/design/compiler-architecture-v0.2.zh.md`](docs/design/compiler-architecture-v0.2.zh.md) — compiler architecture
- [`docs/design/formal-backend-v0.2.zh.md`](docs/design/formal-backend-v0.2.zh.md) — current SMV/formal backend boundary
- [`docs/README.md`](docs/README.md) — full document index and naming rules

Version note: the repo has separate document tracks. The Core language spec is
currently V0.1, project / CLI references are V0.5, IR references are V0.3,
Native handoff docs are V0.4-V0.5, and package authoring planning / design are
V0.5. Execution planning / local dry-run planning is V0.6.

## Current Boundaries

AHFL is not a runtime yet. The compiler currently focuses on:

- parsing and lowering AHFL source into a structured AST
- resolving names, aliases, imports, capabilities, and workflow references
- type-checking values, calls, flow statements, contracts, and workflow nodes
- validating agent state machines, contracts, workflow DAGs, and temporal formulas
- emitting stable backend artifacts for integration and verification work

The restricted `emit-smv` backend intentionally abstracts most data/value
semantics. Its current boundary is documented in
[`docs/design/formal-backend-v0.2.zh.md`](docs/design/formal-backend-v0.2.zh.md).

## CI

CI builds `ahflc` on Ubuntu and macOS, checks formatting on Ubuntu, runs labeled
project / IR / backend / scheduler / checkpoint regression slices, and then runs
the full `ctest` suite.
