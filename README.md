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
| `emit-persistence-descriptor` | deterministic persistence descriptor over checkpoint record |
| `emit-persistence-review` | reviewer-facing persistence summary over persistence descriptor |
| `emit-export-manifest` | deterministic export manifest for store-import bootstrap |
| `emit-export-review` | reviewer-facing export summary over export manifest |
| `emit-store-import-descriptor` | deterministic store-import descriptor over export manifest |
| `emit-store-import-review` | reviewer-facing store-import summary over store-import descriptor |
| `emit-durable-store-import-request` | durable-store-import request artifact for adapter-facing handoff |
| `emit-durable-store-import-review` | reviewer-facing durable-store-import summary over durable request |
| `emit-durable-store-import-decision` | durable adapter decision artifact over durable request |
| `emit-durable-store-import-receipt` | durable adapter receipt artifact over durable decision |
| `emit-durable-store-import-receipt-persistence-request` | durable receipt persistence request artifact over durable receipt |
| `emit-durable-store-import-decision-review` | reviewer-facing durable adapter decision summary |
| `emit-durable-store-import-receipt-review` | reviewer-facing durable adapter receipt summary |
| `emit-durable-store-import-receipt-persistence-review` | reviewer-facing durable receipt persistence summary |
| `emit-durable-store-import-receipt-persistence-response` | durable receipt persistence response artifact over persistence request |
| `emit-durable-store-import-receipt-persistence-response-review` | reviewer-facing durable receipt persistence response summary |
| `emit-durable-store-import-adapter-execution` | local fake durable store adapter execution receipt over persistence response |
| `emit-durable-store-import-recovery-preview` | reviewer-facing recovery command preview over adapter execution receipt |
| `emit-durable-store-import-provider-write-attempt` | provider-neutral durable store write attempt preview over adapter execution receipt |
| `emit-durable-store-import-provider-recovery-handoff` | reviewer-facing provider recovery handoff preview over provider write attempt |
| `emit-durable-store-import-provider-driver-binding` | provider driver binding plan over provider write attempt |
| `emit-durable-store-import-provider-driver-readiness` | reviewer-facing provider driver readiness review over driver binding plan |
| `emit-durable-store-import-provider-runtime-preflight` | provider runtime preflight plan over driver binding plan |
| `emit-durable-store-import-provider-runtime-readiness` | reviewer-facing provider runtime readiness review over runtime preflight plan |
| `emit-durable-store-import-provider-sdk-envelope` | provider SDK request envelope plan over runtime preflight plan |
| `emit-durable-store-import-provider-sdk-handoff-readiness` | reviewer-facing provider SDK handoff readiness review over SDK request envelope plan |
| `emit-durable-store-import-provider-host-execution` | provider host execution plan over SDK request envelope plan |
| `emit-durable-store-import-provider-host-execution-readiness` | reviewer-facing provider host execution readiness review over host execution plan |
| `emit-durable-store-import-provider-local-host-execution-receipt` | simulated provider local host execution receipt over host execution plan |
| `emit-durable-store-import-provider-local-host-execution-receipt-review` | reviewer-facing provider local host execution receipt review |
| `emit-durable-store-import-provider-sdk-adapter-request` | provider SDK adapter request plan over local host execution receipt |
| `emit-durable-store-import-provider-sdk-adapter-response-placeholder` | provider SDK adapter response placeholder over adapter request plan |
| `emit-durable-store-import-provider-sdk-adapter-readiness` | reviewer-facing provider SDK adapter readiness review over response placeholder |
| `emit-durable-store-import-provider-sdk-adapter-interface` | provider SDK adapter interface descriptor plan over adapter request plan |
| `emit-durable-store-import-provider-sdk-adapter-interface-review` | reviewer-facing provider SDK adapter interface readiness review |
| `emit-durable-store-import-provider-config-load` | provider config load plan over adapter interface plan |
| `emit-durable-store-import-provider-config-snapshot` | provider config snapshot placeholder over config load plan |
| `emit-durable-store-import-provider-config-readiness` | reviewer-facing provider config readiness review |
| `emit-durable-store-import-provider-secret-resolver-request` | secret handle resolver request plan over provider config snapshot |
| `emit-durable-store-import-provider-secret-resolver-response` | secret resolver response placeholder over resolver request |
| `emit-durable-store-import-provider-secret-policy-review` | reviewer-facing secret-free policy review |
| `emit-durable-store-import-provider-local-host-harness-request` | test-only local host harness request over secret policy review |
| `emit-durable-store-import-provider-local-host-harness-record` | sandboxed local host harness execution record |
| `emit-durable-store-import-provider-local-host-harness-review` | reviewer-facing local host harness review |
| `emit-durable-store-import-provider-sdk-payload-plan` | fake provider SDK payload materialization plan |
| `emit-durable-store-import-provider-sdk-payload-audit` | reviewer-facing SDK payload redaction and audit summary |
| `emit-durable-store-import-provider-sdk-mock-adapter-contract` | provider SDK mock adapter contract |
| `emit-durable-store-import-provider-sdk-mock-adapter-execution` | mock adapter execution result and normalization |
| `emit-durable-store-import-provider-sdk-mock-adapter-readiness` | reviewer-facing mock adapter readiness review |
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
ctest --preset test-dev -L ahfl-v0.15
ctest --preset test-dev -L ahfl-v0.16
ctest --preset test-dev -L ahfl-v0.19
ctest --preset test-dev -L ahfl-v0.20
ctest --preset test-dev -L ahfl-v0.21
ctest --preset test-dev -L ahfl-v0.22
ctest --preset test-dev -L ahfl-v0.23
ctest --preset test-dev -L ahfl-v0.24
ctest --preset test-dev -L ahfl-v0.25
ctest --preset test-dev -L ahfl-v0.26
ctest --preset test-dev -L ahfl-v0.27
ctest --preset test-dev -L ahfl-v0.28
ctest --preset test-dev -L ahfl-v0.29
ctest --preset test-dev -L ahfl-v0.30
ctest --preset test-dev -L ahfl-v0.31
ctest --preset test-dev -L ahfl-v0.32
ctest --preset test-dev -L ahfl-v0.33

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

Use the repo index for the full typed doc map:

- [`docs/README.md`](docs/README.md) — full document index and naming rules

Recommended entry points:

- Current plan
  - [`docs/plan/roadmap-v0.26.zh.md`](docs/plan/roadmap-v0.26.zh.md)
  - [`docs/plan/issue-backlog-v0.26.zh.md`](docs/plan/issue-backlog-v0.26.zh.md)
- Current provider-local-host-execution boundary
  - [`docs/design/native-durable-store-provider-local-host-execution-prototype-bootstrap-v0.26.zh.md`](docs/design/native-durable-store-provider-local-host-execution-prototype-bootstrap-v0.26.zh.md)
  - [`docs/reference/durable-store-provider-local-host-execution-prototype-compatibility-v0.26.zh.md`](docs/reference/durable-store-provider-local-host-execution-prototype-compatibility-v0.26.zh.md)
  - [`docs/reference/native-consumer-matrix-v0.26.zh.md`](docs/reference/native-consumer-matrix-v0.26.zh.md)
  - [`docs/reference/contributor-guide-v0.26.zh.md`](docs/reference/contributor-guide-v0.26.zh.md)
- Completed provider-host-execution baseline
  - [`docs/plan/roadmap-v0.25.zh.md`](docs/plan/roadmap-v0.25.zh.md)
  - [`docs/plan/issue-backlog-v0.25.zh.md`](docs/plan/issue-backlog-v0.25.zh.md)
  - [`docs/design/native-durable-store-provider-host-execution-prototype-bootstrap-v0.25.zh.md`](docs/design/native-durable-store-provider-host-execution-prototype-bootstrap-v0.25.zh.md)
  - [`docs/reference/durable-store-provider-host-execution-prototype-compatibility-v0.25.zh.md`](docs/reference/durable-store-provider-host-execution-prototype-compatibility-v0.25.zh.md)
  - [`docs/reference/native-consumer-matrix-v0.25.zh.md`](docs/reference/native-consumer-matrix-v0.25.zh.md)
  - [`docs/reference/contributor-guide-v0.25.zh.md`](docs/reference/contributor-guide-v0.25.zh.md)
- Completed provider-SDK-envelope baseline
  - [`docs/plan/roadmap-v0.24.zh.md`](docs/plan/roadmap-v0.24.zh.md)
  - [`docs/plan/issue-backlog-v0.24.zh.md`](docs/plan/issue-backlog-v0.24.zh.md)
  - [`docs/design/native-durable-store-provider-sdk-envelope-prototype-bootstrap-v0.24.zh.md`](docs/design/native-durable-store-provider-sdk-envelope-prototype-bootstrap-v0.24.zh.md)
  - [`docs/reference/durable-store-provider-sdk-envelope-prototype-compatibility-v0.24.zh.md`](docs/reference/durable-store-provider-sdk-envelope-prototype-compatibility-v0.24.zh.md)
  - [`docs/reference/native-consumer-matrix-v0.24.zh.md`](docs/reference/native-consumer-matrix-v0.24.zh.md)
  - [`docs/reference/contributor-guide-v0.24.zh.md`](docs/reference/contributor-guide-v0.24.zh.md)
- Completed provider-runtime baseline
  - [`docs/plan/roadmap-v0.23.zh.md`](docs/plan/roadmap-v0.23.zh.md)
  - [`docs/plan/issue-backlog-v0.23.zh.md`](docs/plan/issue-backlog-v0.23.zh.md)
  - [`docs/design/native-durable-store-provider-runtime-preflight-prototype-bootstrap-v0.23.zh.md`](docs/design/native-durable-store-provider-runtime-preflight-prototype-bootstrap-v0.23.zh.md)
  - [`docs/reference/durable-store-provider-runtime-preflight-prototype-compatibility-v0.23.zh.md`](docs/reference/durable-store-provider-runtime-preflight-prototype-compatibility-v0.23.zh.md)
  - [`docs/reference/native-consumer-matrix-v0.23.zh.md`](docs/reference/native-consumer-matrix-v0.23.zh.md)
  - [`docs/reference/contributor-guide-v0.23.zh.md`](docs/reference/contributor-guide-v0.23.zh.md)
- Completed provider-driver baseline
  - [`docs/plan/roadmap-v0.22.zh.md`](docs/plan/roadmap-v0.22.zh.md)
  - [`docs/plan/issue-backlog-v0.22.zh.md`](docs/plan/issue-backlog-v0.22.zh.md)
  - [`docs/design/native-durable-store-provider-driver-prototype-bootstrap-v0.22.zh.md`](docs/design/native-durable-store-provider-driver-prototype-bootstrap-v0.22.zh.md)
  - [`docs/reference/durable-store-provider-driver-prototype-compatibility-v0.22.zh.md`](docs/reference/durable-store-provider-driver-prototype-compatibility-v0.22.zh.md)
  - [`docs/reference/native-consumer-matrix-v0.22.zh.md`](docs/reference/native-consumer-matrix-v0.22.zh.md)
  - [`docs/reference/contributor-guide-v0.22.zh.md`](docs/reference/contributor-guide-v0.22.zh.md)
- Completed provider-adapter baseline
  - [`docs/plan/roadmap-v0.21.zh.md`](docs/plan/roadmap-v0.21.zh.md)
  - [`docs/plan/issue-backlog-v0.21.zh.md`](docs/plan/issue-backlog-v0.21.zh.md)
  - [`docs/design/native-durable-store-provider-adapter-prototype-bootstrap-v0.21.zh.md`](docs/design/native-durable-store-provider-adapter-prototype-bootstrap-v0.21.zh.md)
  - [`docs/reference/durable-store-provider-adapter-prototype-compatibility-v0.21.zh.md`](docs/reference/durable-store-provider-adapter-prototype-compatibility-v0.21.zh.md)
  - [`docs/reference/native-consumer-matrix-v0.21.zh.md`](docs/reference/native-consumer-matrix-v0.21.zh.md)
  - [`docs/reference/contributor-guide-v0.21.zh.md`](docs/reference/contributor-guide-v0.21.zh.md)
- Previous completed baseline
  - [`docs/plan/roadmap-v0.18.zh.md`](docs/plan/roadmap-v0.18.zh.md)
  - [`docs/plan/issue-backlog-v0.18.zh.md`](docs/plan/issue-backlog-v0.18.zh.md)
  - [`docs/design/native-durable-store-adapter-receipt-persistence-prototype-bootstrap-v0.18.zh.md`](docs/design/native-durable-store-adapter-receipt-persistence-prototype-bootstrap-v0.18.zh.md)
  - [`docs/reference/durable-store-adapter-receipt-persistence-prototype-compatibility-v0.18.zh.md`](docs/reference/durable-store-adapter-receipt-persistence-prototype-compatibility-v0.18.zh.md)
  - [`docs/reference/native-consumer-matrix-v0.18.zh.md`](docs/reference/native-consumer-matrix-v0.18.zh.md)
  - [`docs/reference/contributor-guide-v0.18.zh.md`](docs/reference/contributor-guide-v0.18.zh.md)
  - [`docs/plan/roadmap-v0.17.zh.md`](docs/plan/roadmap-v0.17.zh.md)
  - [`docs/plan/issue-backlog-v0.17.zh.md`](docs/plan/issue-backlog-v0.17.zh.md)
  - [`docs/design/native-durable-store-adapter-receipt-prototype-bootstrap-v0.17.zh.md`](docs/design/native-durable-store-adapter-receipt-prototype-bootstrap-v0.17.zh.md)
  - [`docs/reference/durable-store-adapter-receipt-prototype-compatibility-v0.17.zh.md`](docs/reference/durable-store-adapter-receipt-prototype-compatibility-v0.17.zh.md)
  - [`docs/reference/native-consumer-matrix-v0.17.zh.md`](docs/reference/native-consumer-matrix-v0.17.zh.md)
  - [`docs/reference/contributor-guide-v0.17.zh.md`](docs/reference/contributor-guide-v0.17.zh.md)
  - [`docs/plan/roadmap-v0.16.zh.md`](docs/plan/roadmap-v0.16.zh.md)
  - [`docs/plan/issue-backlog-v0.16.zh.md`](docs/plan/issue-backlog-v0.16.zh.md)
  - [`docs/design/native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md`](docs/design/native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md)
  - [`docs/reference/durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md`](docs/reference/durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md)
  - [`docs/reference/native-consumer-matrix-v0.16.zh.md`](docs/reference/native-consumer-matrix-v0.16.zh.md)
  - [`docs/reference/contributor-guide-v0.16.zh.md`](docs/reference/contributor-guide-v0.16.zh.md)
  - [`docs/plan/roadmap-v0.15.zh.md`](docs/plan/roadmap-v0.15.zh.md)
  - [`docs/plan/issue-backlog-v0.15.zh.md`](docs/plan/issue-backlog-v0.15.zh.md)
  - [`docs/design/native-durable-store-import-prototype-bootstrap-v0.15.zh.md`](docs/design/native-durable-store-import-prototype-bootstrap-v0.15.zh.md)
  - [`docs/reference/durable-store-import-prototype-compatibility-v0.15.zh.md`](docs/reference/durable-store-import-prototype-compatibility-v0.15.zh.md)
  - [`docs/reference/native-consumer-matrix-v0.15.zh.md`](docs/reference/native-consumer-matrix-v0.15.zh.md)
  - [`docs/reference/contributor-guide-v0.15.zh.md`](docs/reference/contributor-guide-v0.15.zh.md)
  - [`docs/plan/roadmap-v0.14.zh.md`](docs/plan/roadmap-v0.14.zh.md)
  - [`docs/reference/store-import-prototype-compatibility-v0.14.zh.md`](docs/reference/store-import-prototype-compatibility-v0.14.zh.md)
  - [`docs/plan/roadmap-v0.13.zh.md`](docs/plan/roadmap-v0.13.zh.md)
  - [`docs/reference/export-package-prototype-compatibility-v0.13.zh.md`](docs/reference/export-package-prototype-compatibility-v0.13.zh.md)
- Core references
  - [`docs/spec/core-language-v0.1.zh.md`](docs/spec/core-language-v0.1.zh.md)
  - [`docs/reference/cli-commands-v0.10.zh.md`](docs/reference/cli-commands-v0.10.zh.md)
  - [`docs/reference/project-usage-v0.5.zh.md`](docs/reference/project-usage-v0.5.zh.md)
  - [`docs/reference/ir-format-v0.3.zh.md`](docs/reference/ir-format-v0.3.zh.md)

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
project / IR / backend / scheduler / checkpoint / persistence / export-package /
durable-adapter-decision / durable-adapter-receipt / durable-adapter-receipt-persistence /
durable-adapter-receipt-persistence-response / provider-driver / provider-runtime /
provider-SDK-envelope / provider-host-execution / provider-local-host-execution regression slices, and then runs the full
`ctest` suite.
