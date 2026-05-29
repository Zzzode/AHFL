# AHFL Repository Guide

## Project

AHFL (Agent Handoff Flow Language) — 面向 AI Agent 工作流的强类型 DSL 编译器，支持状态机建模、行为契约、DAG 编排、形式化验证 (NuSMV) 与端到端执行。

- **Language**: C++23
- **Build**: CMake 3.22+ / Ninja
- **Parser**: ANTLR4 4.13.1 (vendored in `third_party/antlr4/`)
- **License**: Apache-2.0

## Build & Test

```bash
# Configure (dev preset)
cmake --preset dev

# Build
cmake --build --preset build-dev

# Test all
ctest --preset test-dev --output-on-failure

# Test specific version label
ctest --preset test-dev --output-on-failure -L ahfl-v0.42

# ASan build & test
cmake --preset asan
cmake --build --preset build-asan
ctest --preset test-asan --output-on-failure
```

## Directory Conventions

| Path | Purpose |
|------|---------|
| `grammar/` | ANTLR4 grammar (`AHFL.g4`) |
| `include/ahfl/` | Public headers |
| `src/` | Implementation |
| `tests/` | All tests (golden-file + C++ unit tests) |
| `docs/spec/` | Language specifications |
| `docs/design/` | Architecture & design documents |
| `docs/plan/` | Project status, roadmaps, implementation plans |
| `docs/reference/` | CLI, IR format, contributor guides |
| `examples/` | Example `.ahfl` programs |
| `third_party/` | Vendored dependencies |

**Important**: Use `docs/plan/` for ALL planning documents. Do NOT create `docs/plans/` or other variants.

## Code Conventions

- Strict warnings: `-Wall -Wextra -Werror` (enforced in CI)
- Use `[[nodiscard]]` on functions returning values that must be checked
- Anonymous namespaces for internal linkage
- `Owned<T>` (unique_ptr alias) for AST/IR node ownership
- `std::variant` for tagged unions (IR nodes, Value types)
- Diagnostics via `DiagnosticBuilder` with source ranges
- No external runtime dependencies besides vendored ANTLR4
- No forward compatibility will be maintained. We adopt aggressive refactoring guided by industry best practices.

## Key Files

- `docs/plan/project-status.zh.md` — 项目完整状态与演进记录
- `docs/plan/issue-backlog-global-gaps.zh.md` — 未来工作清单
- `grammar/AHFL.g4` — 语言文法定义
- `include/ahfl/compiler/ir/ir.hpp` — IR 数据模型
- `src/compiler/backends/driver.cpp` — 后端分发
- `src/compiler/backends/smv/smv.cpp` — 形式化验证后端

## Commit Style

Use conventional commits: `feat:`, `fix:`, `refactor:`, `build:`, `test:`, `docs:`

Scope examples: `feat(parser):`, `fix(runtime):`, `refactor(backends):`
