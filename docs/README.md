# AHFL Documentation

Start with the [user guide](./tutorials/README.md), or choose the task you want
to complete below. The guides include commands, expected results, and limits
that matter when running your own workflows.

## Build and run a workflow

| Task | Guide |
| --- | --- |
| Install the compiler and check your first program | [Getting started](./tutorials/getting-started.zh.md) |
| Understand agents, capabilities, states, and workflows | [AHFL concepts](./tutorials/overview.zh.md) |
| Write types, agents, contracts, and workflow nodes | [Authoring workflows](./tutorials/authoring.zh.md) |
| Choose compiler commands and output formats | [Using the CLI](./tutorials/cli.zh.md) |
| Configure an LLM provider and run a workflow | [Execution](./tutorials/execution.zh.md) |
| Check safety, liveness, and data contracts | [Assurance](./tutorials/assurance.zh.md) |
| Diagnose errors and inspect a running workflow | [Debugging](./tutorials/debugging.zh.md) |
| Organize modules, dependencies, and workspace packages | [Packages and workspaces](./tutorials/packages.zh.md) |
| Reuse compilation results and inspect the cache | [Incremental compilation](./tutorials/incremental-compilation.zh.md) |
| Export a workflow for a host application | [Native handoff](./tutorials/native-handoff.zh.md) |
| Use standard library types and functions | [Standard library](./tutorials/standard-library.zh.md) |

## Try an example

- [Incident triage](../examples/execution-demo/README.md): connect multiple
  agents and call an LLM through an OpenAI-compatible endpoint.
- [Durable resume](../examples/durable-resume/README.md): suspend a capability
  call, save a recovery record, and resume in a separate process.
- [Loan origination](../examples/loan-origination/README.md): model parallel
  screening, decision routing, and financial-effect policies.
- [Refund rules](../examples/refund-rules/README.md): prove a bounded amount
  rule and inspect a counterexample.

## Look up details

| Need | Reference |
| --- | --- |
| Command syntax and flags | [CLI commands](./reference/cli-commands.zh.md) |
| Diagnostic codes and suggested fixes | [Error codes](./reference/error-codes.zh.md) |
| Available backend outputs and their limits | [Backend capabilities](./reference/backend-capability-matrix.zh.md) |
| Language rules | [Core language specification](./spec/core-language.zh.md) |
| Verification and effect-policy rules | [Assurance specification](./spec/assurance.zh.md) |
| Single-file compilation options | [Single-file mode](./reference/single-file-mode.zh.md) |
| VS Code installation and configuration | [VS Code extension](./reference/lsp-vscode-extension.zh.md) |
| Execution reports, recovery records, and audit data | [Runtime artifacts](./reference/native-runtime-artifacts.zh.md) |
| Host callbacks and capability results | [Host ABI](./reference/host-abi.zh.md) |
| Compiler intermediate-representation fields | [IR format](./reference/ir-format.zh.md) |

## Work on AHFL itself

These documents are for compiler contributors and maintainers:

- [Contributor guide](./reference/contributor-guide.zh.md): development,
  testing, and contribution workflow.
- [Architecture overview](./design/architecture-overview.zh.md) and
  [repository layout](./design/repository-layout.zh.md): subsystem boundaries
  and source organization.
- [IR and backend architecture](./design/ir-backend-architecture.zh.md):
  compiler representations and backend responsibilities.
- [Design documents](./design/): engineering decisions and implementation
  constraints.
- [Project status](./plans/project-status.zh.md) and
  [backlog](./plans/issue-backlog-global-gaps.zh.md): progress and planned work.
- [RFC process](./reference/rfc-process.zh.md) and
  [RFC registry](./rfcs/README.md): proposals and decision records.
- [Migration policy](./reference/migration-policy.zh.md): compatibility and
  release policy for maintainers.
