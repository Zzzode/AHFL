# AHFL

Current normative artifacts:

- `docs/README.md`
- `docs/spec/core-language-v0.1.zh.md`
- `grammar/AHFL.g4`
- `examples/refund_audit_core_v0_1.ahfl`

Supporting design notes:

- `docs/design/ast-model-architecture-v0.2.zh.md`
- `docs/design/backend-extension-guide-v0.2.zh.md`
- `docs/design/cli-pipeline-architecture-v0.2.zh.md`
- `docs/design/compiler-architecture-v0.2.zh.md`
- `docs/design/compiler-phase-boundaries-v0.2.zh.md`
- `docs/design/compiler-evolution-v0.2.zh.md`
- `docs/design/core-scope-v0.1.en.md`
- `docs/design/diagnostics-architecture-v0.2.zh.md`
- `docs/design/frontend-lowering-architecture-v0.2.zh.md`
- `docs/design/formal-backend-v0.2.zh.md`
- `docs/design/ir-backend-architecture-v0.2.zh.md`
- `docs/design/module-loading-v0.2.zh.md`
- `docs/design/module-resolution-rules-v0.2.zh.md`
- `docs/design/native-handoff-architecture-v0.4.zh.md`
- `docs/design/native-package-metadata-v0.4.zh.md`
- `docs/design/project-descriptor-architecture-v0.3.zh.md`
- `docs/design/repository-layout-v0.2.zh.md`
- `docs/design/semantics-architecture-v0.2.zh.md`
- `docs/design/source-graph-v0.2.zh.md`
- `docs/design/testing-strategy-v0.3.zh.md`
- `docs/plan/roadmap-v0.4.zh.md`
- `docs/plan/issue-backlog-v0.4.zh.md`
- `docs/plan/roadmap-v0.3.zh.md`
- `docs/plan/issue-backlog-v0.3.zh.md`
- `docs/plan/roadmap-v0.2.zh.md`
- `docs/plan/issue-backlog-v0.2.zh.md`
- `docs/plan/roadmap-v0.1.zh.md`
- `docs/plan/issue-backlog-v0.1.zh.md`

Reference docs:

- `docs/reference/project-usage-v0.3.zh.md`
- `docs/reference/cli-commands-v0.3.zh.md`
- `docs/reference/ir-format-v0.3.zh.md`
- `docs/reference/ir-compatibility-v0.3.zh.md`
- `docs/reference/backend-capability-matrix-v0.3.zh.md`
- `docs/reference/native-package-compatibility-v0.4.zh.md`
- `docs/reference/native-consumer-matrix-v0.4.zh.md`
- `docs/reference/native-handoff-usage-v0.4.zh.md`
- `docs/reference/contributor-guide-v0.3.zh.md`

C++23 implementation:

- `CMakeLists.txt`
- `cmake/modules/*.cmake`
- `include/ahfl/{support,frontend,semantics,ir,backends}/*.hpp`
- `include/ahfl/*.hpp` for compatibility forwarding headers
- `src/{frontend,semantics,ir,backends,cli}/*.cpp`
- `src/parser/generated/*`
- `src/*/CMakeLists.txt`
- `src/cli/ahflc.cpp`
- `tests/CMakeLists.txt`
- `third_party/antlr4/CMakeLists.txt`
- `third_party/antlr4/runtime/src/*`

Build:

```bash
cmake -S . -B build
cmake --build build
```

Or use presets:

```bash
cmake --preset dev
cmake --build --preset build-dev
```

Available configure presets:

- `dev`
- `release`
- `relwithdebinfo`
- `asan`

The presets also enable `CMAKE_EXPORT_COMPILE_COMMANDS`, and configuration will
create a source-root `compile_commands.json` symlink to the active build directory.

Try the parser:

```bash
./build/dev/src/cli/ahflc --dump-ast examples/refund_audit_core_v0_1.ahfl
```

Try the resolver-backed checker:

```bash
./build/dev/src/cli/ahflc check examples/refund_audit_core_v0_1.ahfl
```

Try the project-aware checker:

```bash
./build/dev/src/cli/ahflc check \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

Try the manifest-driven project-aware checker:

```bash
./build/dev/src/cli/ahflc check \
  --project tests/project/check_ok/ahfl.project.json
```

Try the workspace-driven project-aware checker:

```bash
./build/dev/src/cli/ahflc check \
  --workspace tests/project/ahfl.workspace.json \
  --project-name check-ok
```

Inspect the project-aware AST:

```bash
./build/dev/src/cli/ahflc dump-ast \
  --project tests/project/check_ok/ahfl.project.json
```

Inspect resolved semantic types:

```bash
./build/dev/src/cli/ahflc dump-types examples/refund_audit_core_v0_1.ahfl
```

Emit the stable semantic IR:

```bash
./build/dev/src/cli/ahflc emit-ir examples/refund_audit_core_v0_1.ahfl
```

Emit project-aware IR with declaration provenance:

```bash
./build/dev/src/cli/ahflc emit-ir \
  --search-root tests/project/check_ok \
  tests/project/check_ok/app/main.ahfl
```

Emit project-aware IR from a project descriptor:

```bash
./build/dev/src/cli/ahflc emit-ir \
  --project tests/project/check_ok/ahfl.project.json
```

Emit project-aware IR from a workspace-selected project:

```bash
./build/dev/src/cli/ahflc emit-ir \
  --workspace tests/project/ahfl.workspace.json \
  --project-name check-ok
```

Emit machine-readable JSON IR:

```bash
./build/dev/src/cli/ahflc emit-ir-json examples/refund_audit_core_v0_1.ahfl
```

Emit backend capability summary:

```bash
./build/dev/src/cli/ahflc emit-summary tests/ir/ok_workflow_value_flow.ahfl
```

Emit Native handoff package JSON:

```bash
./build/dev/src/cli/ahflc emit-native-json \
  --project tests/project/workflow_value_flow/ahfl.project.json
```

Emit restricted SMV for model-check-oriented tooling:

```bash
./build/dev/src/cli/ahflc emit-smv examples/refund_audit_core_v0_1.ahfl
```

The current `emit-smv` boundary is documented in
`docs/design/formal-backend-v0.2.zh.md`.

Current scope of `ahflc`:

- loads AHFL source files
- parses with the generated ANTLR4 C++ lexer/parser from `grammar/AHFL.g4`
- lowers into a structured top-level AST for declarations such as `struct`, `enum`, `capability`, `agent`, `contract`, `flow`, and `workflow`
- lowers AHFL surface types into a structured type AST, including `Optional`, `List`, `Set`, `Map`, bounded `String`, and `Decimal(scale)`
- lowers value expressions, flow statements, and temporal formulas into dedicated AST layers
- builds a stable symbol table for top-level declarations
- resolves type names, agent targets, callable references, capability references in temporal formulas, and import-alias-qualified names
- reports duplicate declarations, unresolved references, duplicate import aliases, ambiguous callable names, and type alias cycles
- resolves AST type syntax into alias-transparent semantic types
- builds declaration-level semantic signatures for struct, enum, capability, predicate, agent, and workflow declarations
- validates that agent `input` / `context` / `output` types resolve to `struct` schemas
- type-checks literals, paths, member access, qualified enum/const values, calls, struct literals, collection literals, `some` / `none`, and unary/binary operators
- type-checks `let`, assignment, `if`, `assert`, and `return` inside flow handlers
- checks workflow node input expressions and workflow return expressions against agent/workflow schemas
- checks `requires` / `ensures` expressions are typed `Bool` and remain pure
- type-checks embedded value expressions inside temporal formulas
- validates agent state-machine structure: declared states, initial/final consistency, transition endpoints, final-state out-edges, reachability, and duplicate capability entries
- validates contract temporal atoms, flow handler shape, workflow node/edge integrity, workflow DAGs, and `safety` / `liveness` temporal formulas in a dedicated `validate` stage
- lowers the validated frontend result into a structured `ahfl::ir::Program` model
- models const values, contract/workflow formulas, and flow statements as recursive IR nodes instead of flattened strings
- emits a stable textual IR from the validated semantic model via `emit-ir`
- emits a stable machine-readable JSON form of the structured IR via `emit-ir-json`
- emits a capability-oriented summary backend via `emit-summary`
- emits a runtime-facing Native handoff package via `emit-native-json`
- emits a restricted SMV backend from validated state machines, flow/workflow control structure, temporal formulas, and limited contract `requires` / `ensures` export via `emit-smv`
- reports source diagnostics with line and column
- `check` runs parse + AST lowering + resolver + typecheck + validate diagnostics
- `check --search-root ...` runs the same pipeline in project-aware mode over a source graph
- `check --project <ahfl.project.json>` resolves a V0.3 project descriptor into the same project-aware source graph pipeline
- `check --workspace <ahfl.workspace.json> --project-name <name>` selects one project from a workspace and runs the same project-aware pipeline
- `dump-types` prints the resolved semantic type environment
- `dump-ast` prints the full hand-written AST tree
- `dump-ast --search-root ... <entry.ahfl>` prints a project-aware AST view grouped by source/module
- `dump-ast --project <ahfl.project.json>` prints the same project-aware AST view from a descriptor-driven project input
- `dump-ast --workspace <ahfl.workspace.json> --project-name <name>` prints the same project-aware AST view from a workspace-selected project
- `dump-types --search-root ...` prints the merged semantic type environment for a project-aware source graph
- `dump-project --search-root ...` prints the loaded source graph without entering later semantic/backend stages
- `dump-project --project <ahfl.project.json>` prints the same source graph built from a project descriptor
- `dump-project --workspace <ahfl.workspace.json> --project-name <name>` prints the same source graph built from a workspace-selected project
- `emit-ir` prints the stable semantic IR used as the current backend boundary
- `emit-ir --search-root ...` prints project-aware IR with per-declaration provenance (`module_name`, `source_path`)
- `emit-ir --project <ahfl.project.json>` prints the same project-aware IR from a descriptor-driven project input
- `emit-ir --workspace <ahfl.workspace.json> --project-name <name>` prints the same project-aware IR from a workspace-selected project
- `emit-ir-json` prints the same IR as structured JSON for downstream tooling, including shared `formal_observations`
- `emit-ir-json --search-root ...` prints the same project-aware IR provenance in structured JSON form
- `emit-ir-json --project <ahfl.project.json>` prints the same descriptor-driven project-aware IR provenance in structured JSON form
- `emit-ir-json --workspace <ahfl.workspace.json> --project-name <name>` prints the same workspace-selected project-aware IR provenance in structured JSON form
- `emit-summary` prints a capability-oriented backend summary over the validated IR boundary
- `emit-summary --search-root ...` prints the same summary for a project-aware source graph
- `emit-summary --project <ahfl.project.json>` prints the same summary for a descriptor-driven project-aware source graph
- `emit-summary --workspace <ahfl.workspace.json> --project-name <name>` prints the same summary for a workspace-selected project-aware source graph
- `emit-native-json` prints the runtime-facing handoff package used as the current Native contract boundary
- `emit-native-json --search-root ...` prints the same handoff package for a project-aware source graph
- `emit-native-json --project <ahfl.project.json>` prints the same handoff package for a descriptor-driven project-aware source graph
- `emit-native-json --workspace <ahfl.workspace.json> --project-name <name>` prints the same handoff package for a workspace-selected project-aware source graph
- `emit-smv` prints a restricted formal-backend lowering with flow-aware transitions, workflow start/completion latches, and abstracted boolean observations for contract / temporal pure expressions
- `emit-smv --search-root ...` prints the same restricted formal backend for a project-aware source graph
- `emit-smv --project <ahfl.project.json>` prints the same restricted formal backend for a descriptor-driven project-aware source graph
- `emit-smv --workspace <ahfl.workspace.json> --project-name <name>` prints the same restricted formal backend for a workspace-selected project-aware source graph
- `--dump-ast` prints a full tree view of declarations, types, expressions, statements, and temporal formulas

Current project-aware CLI limit:

- `--search-root`、`--project`、`--workspace --project-name` are currently supported by `check`, `dump-ast`, `dump-types`, `dump-project`, `emit-ir`, `emit-ir-json`, `emit-native-json`, `emit-summary`, and `emit-smv`

Current `emit-smv` limits:

- contract `requires` / `ensures` are exported only through observation abstraction, not direct data/value semantics
- statement/data semantics beyond the current `goto` / final-state `return` / workflow lifecycle subset remain out of scope
- capability timing/count/order, parameter values, and workflow dataflow/value semantics are still abstracted rather than directly encoded

Regenerate the C++ parser module:

```bash
ANTLR_JAR=/path/to/antlr-4.x-complete.jar ./scripts/regenerate-parser.sh
```

Format hand-written C++ sources:

```bash
cmake --build --preset build-format
```

Check formatting without rewriting files:

```bash
cmake --build --preset build-format-check
```

Run full regression tests:

```bash
ctest --preset test-dev
```

Run the focused V0.3 regression slice:

```bash
ctest --preset test-dev -L ahfl-v0.3
```

CI:

- `.github/workflows/ci.yml` runs `build-format-check` on Ubuntu
- `.github/workflows/ci.yml` builds `ahflc` on Ubuntu and macOS with the `dev` preset
- `.github/workflows/ci.yml` runs labeled V0.3 project / IR / backend regression slices before the full test suite
- `.github/workflows/ci.yml` runs `ctest --preset test-dev --output-on-failure`

Ignored by `.clang-format-ignore`:

- `.cache/**`
- `build/**`
- `src/parser/generated/**`
- `third_party/**`
