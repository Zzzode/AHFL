# AHFL

Current normative artifacts:

- `docs/ahfl-spec-v0.1-zh.md`
- `grammar/AHFL.g4`
- `examples/refund_audit_core_v0_1.ahfl`

Supporting design notes:

- `docs/ahfl-core-v0.1.md`
- `docs/frontend-architecture-v0.1.md`
- `docs/roadmap-v0.1.md`
- `docs/issue-backlog-v0.1.md`

C++23 frontend:

- `CMakeLists.txt`
- `include/ahfl/*.hpp`
- `src/*.cpp`
- `src/parser/generated/*`
- `src/tools/ahflc.cpp`
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
./build/dev/ahflc --dump-ast examples/refund_audit_core_v0_1.ahfl
```

Try the resolver-backed checker:

```bash
./build/dev/ahflc check examples/refund_audit_core_v0_1.ahfl
```

Inspect resolved semantic types:

```bash
./build/dev/ahflc dump-types examples/refund_audit_core_v0_1.ahfl
```

Emit the stable semantic IR:

```bash
./build/dev/ahflc emit-ir examples/refund_audit_core_v0_1.ahfl
```

Emit machine-readable JSON IR:

```bash
./build/dev/ahflc emit-ir-json examples/refund_audit_core_v0_1.ahfl
```

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
- reports source diagnostics with line and column
- `check` runs parse + AST lowering + resolver + typecheck + validate diagnostics
- `dump-types` prints the resolved semantic type environment
- `emit-ir` prints the stable semantic IR used as the current backend boundary
- `emit-ir-json` prints the same IR as structured JSON for downstream tooling
- `--dump-ast` prints a full tree view of declarations, types, expressions, statements, and temporal formulas

Not wired yet:

- backend-specific formal lowerings beyond the current stable textual/JSON IR boundary

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

Run parser/resolver/typecheck/IR regression tests:

```bash
ctest --preset test-dev
```

CI:

- `.github/workflows/ci.yml` runs `build-format-check` on Ubuntu
- `.github/workflows/ci.yml` builds `ahflc` on Ubuntu and macOS with the `dev` preset
- `.github/workflows/ci.yml` runs `ctest --preset test-dev`

Ignored by `.clang-format-ignore`:

- `.cache/**`
- `build/**`
- `src/parser/generated/**`
- `third_party/**`
