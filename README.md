# AHFL

Current normative artifacts:

- `docs/ahfl-spec-v0.1-zh.md`
- `grammar/AHFL.g4`
- `examples/refund_audit_core_v0_1.ahfl`

Supporting design notes:

- `docs/ahfl-core-v0.1.md`

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

Current scope of `ahflc`:

- loads AHFL source files
- parses with the generated ANTLR4 C++ lexer/parser from `grammar/AHFL.g4`
- builds a minimal AST outline for module/import/type/capability/agent/contract/flow/workflow envelopes
- reports source diagnostics with line and column

Not wired yet:

- expression/type parsing
- semantic analysis and type checking

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

CI:

- `.github/workflows/ci.yml` runs `build-format-check` on Ubuntu
- `.github/workflows/ci.yml` builds and smoke-tests `ahflc` on Ubuntu and macOS with the `dev` preset

Ignored by `.clang-format-ignore`:

- `.cache/**`
- `build/**`
- `src/parser/generated/**`
- `third_party/**`
