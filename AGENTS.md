# AHFL Repository Guide

## Project

AHFL (Agent Handoff Flow Language) — A strongly typed DSL compiler oriented to AI Agent workflows, supporting state machine modeling, behavioral contracts, DAG orchestration, formal verification (NuSMV) and end-to-end execution.

- **Language**: C++23
- **Build**: CMake / Ninja
- **Parser**: ANTLR4 (vendored in `third_party/antlr4/`)
- **License**: Apache-2.0

## Core Design Principles (Non-Negotiable)

> **All contributors — human and AI — must follow these. PRs that violate these principles will be rejected outright.**

### Principle 1: No Minimal Changes. Aggressive Refactoring. Industry Best Practices First

**We do NOT do "minimal viable changes."**

- If adding a feature gives you a chance to clean up a layer of technical debt, do both.
- If there is an industry-standard approach (Rust / Swift / Clang / GHC / …) and our code does it differently, **change ours** to match the standard.
- One clean large refactor > one hundred tiny workarounds.

**Explicitly forbidden:**

- Adding special-case branches in the wrong abstraction layer just to "touch fewer files"
- Using strings as canonical identity (must be index / ID)
- Keeping dead code around "just in case"
- Wrapping a broken interface in a workaround instead of fixing the interface
- Reinventing wheels when a standard pattern exists

**Explicitly encouraged:**

- Big-bang architectural refactors that move toward industry best practice
- Deleting code (the more the merrier)
- Replacing ad-hoc solutions with `std::variant` / index-based flat stores / hash-consing / other known-good patterns
- Investing in doing it right rather than doing it fast

### Principle 2: Index-Based, Not String-Based

Internal data structures use **numeric indices or IDs** for canonical identity. **Never** strings.

- Type substitution → `vector<TypePtr>` indexed by parameter position (Rust `Substs` pattern)
- Symbol references → `SymbolId` integer
- Flat stores (`vector<T>` + index) over string-keyed hash maps

Strings are only for: source-level names, diagnostic output, user-facing error messages.

### Principle 3: Hash-Consed Types & Flat Stores

- All types are interned through `TypeContext`. Pointer equality ⇔ structural equality. No deep type comparison on hot paths.
- Tree structures (Typed HIR, IR) use flat `vector<T>` storage + index references. Cache-friendly, cheap to clone via index remap.

### Principle 4: `std::variant` + Visitor, Not Inheritance

AST nodes, IR nodes, value types, type payloads — all use `std::variant` + `Overloaded` visitor pattern.
No class inheritance, no `dynamic_cast`, no virtual dispatch for data-carrying types.

### Principle 5: Diagnostics with Source Ranges

Every error / warning carries a `SourceRange`.
Diagnostic messages are human-readable and actionable. No "internal error" messages that users can't understand.

### Reference Hierarchy

When in doubt about how to design something, look to these systems (in priority order):

1. **Rust** — type system, traits, monomorphization, module system
2. **Swift** — type checker, SIL, protocols
3. **Clang** — template instantiation, diagnostic infrastructure
4. **GHC** — type class resolution, type families
5. **Dafny** — verifiable subset, decreases, termination proofs

If our design diverges from the mainstream, there must be a documented, AHFL-specific reason.

---

## Build & Test

```bash
# Configure (dev preset)
cmake --preset dev

# Build
cmake --build --preset build-dev

# Test all
ctest --preset test-dev --output-on-failure

# Filter tests by label
ctest --preset test-dev --output-on-failure -L <label>

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
| `docs/plans/` | Project status, roadmaps, implementation plans |
| `docs/reference/` | CLI, IR format, contributor guides |
| `examples/` | Example `.ahfl` programs |
| `third_party/` | Vendored dependencies |

**Important**: Use `docs/plans/` for ALL planning documents. Do NOT create `docs/plan/` or other variants.

## Code Conventions

- Strict warnings: `-Wall -Wextra -Werror` (enforced in CI)
- Use `[[nodiscard]]` on functions returning values that must be checked
- Anonymous namespaces for internal linkage
- `Owned<T>` (unique_ptr alias) for AST/IR node ownership
- `std::variant` for tagged unions (IR nodes, Value types)
- Diagnostics via `DiagnosticBuilder` with source ranges
- No external runtime dependencies besides vendored ANTLR4

## Key Files

- `docs/plans/project-status.zh.md` — Full project status and evolution records
- `docs/plans/issue-backlog-global-gaps.zh.md` — Future work checklist
- `grammar/AHFL.g4` — Language grammar definition
- `include/ahfl/compiler/ir/ir.hpp` — IR data model
- `src/compiler/backends/driver.cpp` — Backend dispatching
- `src/compiler/backends/smv/smv.cpp` — Formal verification backend

## Commit Style

Use conventional commits: `feat:`, `fix:`, `refactor:`, `build:`, `test:`, `docs:`

Scope examples: `feat(parser):`, `fix(runtime):`, `refactor(backends):`
