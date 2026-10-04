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
- **Forward-compatibility shims, deprecation periods, "temporarily retained" old paths, legacy/compatibility directories, and old-and-new coexistence of any kind.** A migration is ONE big-bang change: flip every call site, delete the old implementation, delete its flags/env vars/adapters/tests/goldens in the SAME change. There is no transitional state. The `BREAKING CHANGE:` footer marks such deletions; it never apologizes for them.
- **Letting directory structure lie about the architecture.** Package/module placement MUST reflect the real role and weight of a component (a peer-tier execution engine does not live inside a "misc/infra/utils" bucket). When a subsystem outgrows its tier, promote it (move it) in the same work that made it outgrow the tier — no `legacy_*`/`new_*`/`v2_*` sibling directories.
- **Replacing one implementation while leaving the replaced one unreachable but present.** If a grep shows zero non-test production callers, it is dead: delete it and its dedicated tests in the same change.

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

## Autonomous Decision-Making (No Human Gate)

This repository has **no human owner / product-approval gate**. Contributors —
human or AI — never pause work to wait for a user decision. "Needs an owner
decision", "blocked on product", and similar notes are not acceptable statuses.

When a decision is genuinely needed (execution-engine choice, dependency or
embedding strategy, API-contract direction, a trade-off between valid designs,
the scope of a breaking change):

1. **Spawn a dedicated decision agent for that single decision.** Give it the
   relevant RFCs, design docs, code paths, tests, hard constraints, and the
   Reference Hierarchy above. It must produce a written decision: what is
   chosen, why it beats the explicitly named alternatives, the AHFL-specific
   reason for any divergence from mainstream practice, the costs, and concrete
   acceptance / verification criteria.
2. **Keep the decider separate from the builder.** A different agent implements
   the decision; the standard implement -> adversarial review -> fix-forward
   loop still applies. A decision record is not self-approval of the code.
3. **Record the decision immediately** in the owning RFC (dated Decision
   History entry) or design doc, then proceed in the same workflow. The entry
   records the deciding agent's rationale; it must never invent human sign-off.
4. **Revise decisions the same way they were made** — a new decision agent, a
   new dated record, and ONE big-bang change. Never a compatibility flag, an
   environment escape hatch, or a parallel implementation (Principle 1).

The only legitimate "blocked" state is an **external fact no agent can change**:
no network, a missing third-party binary, absent credentials. Report the exact
fact and keep working on everything it does not cover. A decidable engineering
question must never be relabeled as an external block.

---

## Build & Test

### First-time setup (every contributor)

Install the repository's versioned git hooks before making any commits. The
hooks are kept under `scripts/githooks/` and symlinked into `.git/hooks/` so
any later updates are picked up automatically:

```bash
# Default: per-hook symlinks. Preserves third-party hooks (e.g. VibeBuddy).
scripts/install-githooks.sh

# Alternative: set core.hooksPath = scripts/githooks (Windows / no symlinks).
# scripts/install-githooks.sh --via-hooksPath
```

The installer preserves any pre-existing non-AHFL hook by renaming it to
`.git/hooks/<name>.bak.<timestamp>`, so you never lose a hook another tool
installed. See `scripts/install-githooks.sh` for details.

### Configure, build, test

**Parallelism rule (non-negotiable): always saturate the machine. Pass an
explicit `-j` equal to the full core count for BOTH builds and tests — never
an artificial cap, never serial. A many-core box running a serial build or
ctest is a defect, not caution.** (`$(nproc)` on Linux; `sysctl -n hw.ncpu`
on macOS.)

```bash
# Configure (dev preset)
cmake --preset dev

# Build — all cores
cmake --build --preset build-dev -j$(nproc)

# Test all — all cores
ctest --preset test-dev -j$(nproc) --output-on-failure

# Filter tests by label
ctest --preset test-dev -j$(nproc) --output-on-failure -L <label>

# ASan build & test
cmake --preset asan
cmake --build --preset build-asan -j$(nproc)
ctest --preset test-asan -j$(nproc) --output-on-failure
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

**Language rule — non-negotiable.** Commit messages MUST be written in English
ONLY. Chinese / Japanese / Korean (CJK) characters, including fullwidth ASCII
forms, are forbidden anywhere in the subject or body, even inside parentheses
or scopes. The local `commit-msg` git hook enforces this and will reject any
offending commit before it is created.

Examples (do / don't):

```
# ❌ forbidden: contains Chinese
fix(sema): 泛型enum variant构造的双向类型override (Case 9)

# ✅ allowed: equivalent English
fix(sema): bidirectional type override for generic enum variant construction (Case 9)

# ❌ forbidden: mixed CJK inside the scope description
test(sema): effects needles P5容器级+参数级diagnostic origin

# ✅ allowed
test(sema): effects needles P5 container-level + parameter-level diagnostic origin
```

Scope labels themselves (the word in parentheses after the type) must also be
English. If you need to reference a non-English concept, translate it or use a
well-known English loanword / identifier.
