<p align="center">
  <h1 align="center">AHFL</h1>
  <p align="center">
    <strong>A typed DSL and C++23 compiler for auditable agent workflows</strong>
  </p>
  <p align="center">
    <a href="https://github.com/Zzzode/AHFL/actions/workflows/ci.yml"><img src="https://github.com/Zzzode/AHFL/actions/workflows/ci.yml/badge.svg?branch=main" alt="CI"></a>
    <img src="https://img.shields.io/badge/C%2B%2B-23-blue.svg" alt="C++23">
    <img src="https://img.shields.io/badge/License-Apache--2.0-green.svg" alt="Apache-2.0">
  </p>
  <p align="center">
    <a href="README.zh.md">Chinese README</a>
    ·
    <a href="docs/README.md">Documentation Index</a>
    ·
    <a href="docs/reference/cli-commands.zh.md">CLI Reference</a>
  </p>
</p>

AHFL (Agent Handoff Flow Language) is a strongly typed domain-specific language (DSL) and C++23 compiler for modeling and executing auditable agent workflows.

## What You Can Do

AHFL is a beta tool for workflows that need explicit execution order,
capability boundaries, failure handling, replay, and audit. Language and command
changes may affect your projects when upgrading.

- <!-- beta-capability:BETA-01 schema=ahfl.beta-evidence.run-profiles --> Save workflow inputs and options in manifest run profiles, then select a profile with `ahflc run --profile`.
- <!-- beta-capability:BETA-02 schema=ahfl.beta-evidence.runtime-identity --> Use numeric IDs in execution reports to associate workflows, nodes, agents, capabilities, invocations, values, and events.
- <!-- beta-capability:BETA-03 schema=ahfl.beta-evidence.event-projections --> Inspect the same execution as readable text, JSON, or JSONL, and use its events for replay and audit.
- <!-- beta-capability:BETA-04 schema=ahfl.beta-evidence.lifecycle-matrix --> Use terminal events to identify the outcome of a run, including success, failure, cancellation, and timeout.
- <!-- beta-capability:BETA-05 schema=ahfl.beta-evidence.formatter-idempotence --> Format AHFL source while preserving comments, attributes, effects, generics, imports, and visibility; formatting it again produces no further changes.
- <!-- beta-capability:BETA-06 schema=ahfl.beta-evidence.stdlib-container-migration --> Use the standard library generic types `List`, `Map`, `Set`, and `Option` for collections and optional values.
- <!-- beta-capability:BETA-08 schema=ahfl.beta-evidence.install-smoke --> Install the CLI and language server with the standard library, or install a platform VSIX that bundles the language server and standard library.
- <!-- beta-capability:BETA-10 schema=ahfl.beta-evidence.product-scope-freeze --> Find available commands and output artifacts in the CLI reference, and use `ahflc --help` to inspect the installed compiler.

Start with the [incident-triage workflow](examples/execution-demo/README.md): it
connects three agents and calls an LLM through an OpenAI-compatible endpoint.
For source-level contracts and temporal verification, see the
[assurance guide](docs/tutorials/assurance.zh.md).

Native Protobuf transport, multi-region operation, an official package registry,
a hosted browser playground, and Marketplace distribution are outside the
verified beta capabilities. The local playground is available for evaluation;
see its [setup instructions](tools/playground/README.md).

## Language Preview

Excerpt from [examples/refund/audit.ahfl](examples/refund/audit.ahfl):

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
    ensures: non_empty(output.reason);
    invariant: always not called(RefundExecute);
}

workflow RefundAuditWorkflow {
    input: RefundRequest;
    output: RefundDecision;
    node audit: RefundAudit(input);
    liveness: eventually completed(audit, Terminated);
    return: audit;
}
```

## Quick Start

### Prerequisites

| Tool | Requirement |
| --- | --- |
| C++ compiler | C++23 support. GCC 13+, Clang 17+, or Apple Clang 15+ recommended. |
| CMake | 3.22+ |
| Ninja | Recommended generator used by the presets. |
| NuSMV / Z3 | Bundled and built from source for control and opt-in data verification. |
| Node.js | Optional, only needed for VS Code extension development or packaging. |

### Build from source

```bash
git clone https://github.com/Zzzode/AHFL.git
cd AHFL

AHFL_BUILD_JOBS=$(getconf _NPROCESSORS_ONLN)
cmake --preset dev
cmake --build --preset build-dev -j"$AHFL_BUILD_JOBS"
```

### Run the compiler

Use `check` to find type errors and verify declared state-machine and workflow
rules. Add `--verify-data-contracts` to prove data rules from their stated
preconditions. Those proofs do not establish that a flow implementation meets
the rules, and the runtime does not automatically check host predicates. See
the [refund rules example](examples/refund-rules/README.md).

Before running the incident workflow, follow its
[provider setup](examples/execution-demo/README.md). The example reads its API
key from an environment variable.

```bash
# Check types and the default finite control model.
./build/dev/src/tooling/cli/ahflc check examples/refund/audit.ahfl

# Request a bounded refund rule contract implication proof.
./build/dev/src/tooling/cli/ahflc check --manifest examples/refund-rules/ahfl.toml --verify-data-contracts

# Run the incident workflow from its manifest profile.
cd examples/execution-demo
../../build/dev/src/tooling/cli/ahflc run --output-format json

# Inspect all commands and artifacts.
../../build/dev/src/tooling/cli/ahflc --help
```

For provider options, inputs, and execution reports, see the
[execution guide](docs/tutorials/execution.zh.md).

## Documentation

| Topic | Entry point |
| --- | --- |
| Documentation index | [docs/README.md](docs/README.md) |
| User guide | [docs/tutorials/README.md](docs/tutorials/README.md) |
| Language specification | [docs/spec/core-language.zh.md](docs/spec/core-language.zh.md) |
| CLI reference | [docs/reference/cli-commands.zh.md](docs/reference/cli-commands.zh.md) |
| IR format | [docs/reference/ir-format.zh.md](docs/reference/ir-format.zh.md) |
| Project and workspace usage | [docs/tutorials/packages.zh.md](docs/tutorials/packages.zh.md) |
| Runtime events and recovery | [docs/reference/native-runtime-artifacts.zh.md](docs/reference/native-runtime-artifacts.zh.md) |
| VS Code LSP extension | [docs/reference/lsp-vscode-extension.zh.md](docs/reference/lsp-vscode-extension.zh.md) |
| Contributor guide | [docs/reference/contributor-guide.zh.md](docs/reference/contributor-guide.zh.md) |

## Edit workflows in VS Code

The language server supplies diagnostics, completion, and navigation through
the Language Server Protocol (LSP). Build it with:

```bash
cmake --build --preset build-dev --target ahfl-lsp -j"$(getconf _NPROCESSORS_ONLN)"
```

Create and install a platform VSIX extension package that bundles the language
server and standard library:

```bash
scripts/package-vscode-vsix-release.sh
code --install-extension tools/vscode/dist/ahfl-language-<version>-<target>.vsix
```

Open an `.ahfl` file to get diagnostics, completion, and navigation. See the
[VS Code guide](docs/reference/lsp-vscode-extension.zh.md) for configuration and troubleshooting.

## Contributing

For compiler development and contribution workflow, use the
[contributor guide](docs/reference/contributor-guide.zh.md).

## License

AHFL is licensed under the [Apache License 2.0](LICENSE).
