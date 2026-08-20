---
name: rfc
description: Draft, review, update, or implement AHFL RFCs under docs/rfcs/. Use whenever the user asks to write an RFC, 设计草案 / 设计决策 / RFC, propose a language/compiler/runtime/tooling design decision, advance an RFC's status, or implement code against an accepted RFC. The RFC file itself is the unit of tracking — do not create companion docs/plans entries for RFC work.
---

# AHFL RFC Skill

AHFL records durable design decisions as numbered RFCs in `docs/rfcs/`. The RFC
file is the single source of truth for a decision: its status, implementation
PRs, and tracking issue live in its frontmatter. **Do not** create companion
planning docs under `docs/plans/` for RFC work — implement directly against the
RFC's own Implementation Plan / Test Plan sections.

The final language contract still lives in `docs/spec/`. An accepted RFC
explains *why* a decision was made; it does not replace the spec, design docs,
reference, tests, or release notes that make the decision operational.

## Before you start

Always read these first (they are authoritative and change over time):

- `docs/rfcs/README.md` — registry rules, status model, areas
- `docs/rfcs/0000-template.zh.md` — canonical template
- `docs/rfcs/index.yml` — existing RFCs (next number, title collisions)
- `AGENTS.md` — core design principles every Design section must respect

## Creating a new RFC

1. **Number**: next id is `max(existing ids) + 1`, zero-padded to 4 digits.
   Ids must be contiguous from 0001 (CI-enforced).
2. **Filename**: `docs/rfcs/NNNN-kebab-slug.zh.md` (lowercase, digits and
   hyphens only, `.zh.md` suffix). No subdirectories, no wave-local files.
3. **Copy the template** and fill its frontmatter. Required fields (all of
   them, or `check-rfc.py` fails):
   - `rfc`: same 4-digit id as the filename
   - `title`: short English title
   - `status`: start at `draft` (the only status where `TBD` is allowed)
   - `area`: one or more of `language`, `compiler`, `ir`, `stdlib`, `runtime`,
     `tooling`, `formal`, `process` — **every** listed area needs a matching
     `owners` entry
   - `stability`: one of `experimental`, `internal`, `developer-facing`,
     `stable-language`, `stable-artifact`
   - `created` / `updated`: today, `YYYY-MM-DD`
   - `authors`: ask the user, or fall back to `git config user.name`
   - `shepherd`, `tracking_issue`, `discussion`: `TBD` is fine while `draft`
   - `implementation_prs`: `[]`
4. **Keep all 13 required sections, in this exact order** (the checker verifies
   both presence and order): Summary, Motivation, Goals, Non-Goals, Design,
   User Impact, Compatibility and Migration, Implementation Plan, Test Plan,
   Rollout and Stabilization, Alternatives, Open Questions, Decision History.
   A short section is fine; a missing one is a CI failure.
5. **Register in `index.yml`**: append an entry with `rfc`, `title`, `status`,
   `file`, `area`, `stability` — all must match the frontmatter exactly — and
   bump the top-level `updated` date to today (it must be ≥ every RFC's
   `updated` date).
6. **Verify**: `python3 scripts/check-rfc.py` must pass.
7. **Commit**: `docs(rfcs): add RFC NNNN <title>` — English only, the
   commit-msg hook rejects CJK characters.

## Hard rules enforced by `scripts/check-rfc.py` (and CI)

- Status values: `draft`, `review`, `fcp`, `accepted`, `implementing`,
  `implemented`, `stabilized`, `rejected`, `withdrawn`, `postponed`,
  `superseded`.
- From `review` onward: no `TBD`/`TODO`/`DEFERRED` anywhere — frontmatter or
  body. Resolve every open marker before requesting review.
- `review` and later also require non-stale `shepherd`, `owners`,
  `tracking_issue`, `discussion`.
- Diagrams must be Mermaid fences. Fences tagged `dot`/`graphviz`/`plantuml`/
  `ascii` are rejected, and any fence whose nearby text says
  diagram/architecture/架构图/流程图/状态图 must be `mermaid`.
- Links must be relative, stay inside the repo, and point at files that
  exist. No absolute or machine-local paths.
- One RFC per file; the registry directory may contain only
  `README.md`, `index.yml`, `0000-template.zh.md`, and canonical RFC files.

## Content guidance (house style)

- **Design section must respect AGENTS.md's non-negotiable principles**:
  index-based identity (never strings as canonical identity), hash-consed
  types and flat stores, `std::variant` + visitor over inheritance,
  diagnostics with `SourceRange`. When diverging from mainstream compilers,
  document the AHFL-specific reason; the reference hierarchy is
  Rust → Swift → Clang → GHC → Dafny.
- **Runtime / tooling RFCs** must honor the artifact-chain boundaries:
  each layer consumes only the previous layer's machine artifacts; persisted
  artifacts are secret-free (handles/references only); artifact identity is
  deterministic (no wall clock, pid, host path, random seed).
- **Compatibility and Migration**: state explicitly whether the change is
  breaking. Breaking changes need impact scope and migration steps. The
  project does not promise forward compatibility, but breakage must be
  documented.
- **Alternatives**: compare at least two serious alternatives and explain why
  they lose — "we didn't think of it" is not an alternatives section.
- **Test Plan**: name concrete test kinds per the repo's taxonomy — unit,
  golden (positive and negative), integration, fuzz, mutation,
  release-evidence / budget gates.
- Cite real files and paths (`src/...`, `docs/spec/...`, `grammar/AHFL.g4`)
  rather than vague module names. Verify every cited path exists.

## Lifecycle and implementation

Status flow: `draft → review → fcp → accepted → implementing → implemented →
stabilized`, with `rejected` / `withdrawn` / `postponed` / `superseded` as
terminal or parking states.

When the user asks to implement against an RFC:

1. Confirm the RFC is `accepted` (or drive it through review/fcp first).
2. Move it to `implementing`: fill `tracking_issue` and `discussion`, clear
   every `TBD`/`TODO`/`DEFERRED`, set `updated` to today.
3. Implement in the slices the RFC's own Implementation Plan lists — one
   logical change per commit, conventional commits, English messages.
4. Append each landed PR to `implementation_prs`; add a dated
   Decision History entry for meaningful changes.
5. When code, tests, and docs have landed: `implemented`.
6. When the decision is reflected in `docs/spec/`, reference docs, and
   release evidence: `stabilized`.
7. Every RFC edit also bumps `index.yml`'s `updated` date and keeps the
   index entry's `status`/`stability`/`title`/`area` in sync.
8. Run `python3 scripts/check-rfc.py` after every RFC edit, and the relevant
   build/test presets (`cmake --build --preset build-dev`,
   `ctest --preset test-dev`) when implementation landed.

## Finishing

Never consider an RFC task done until `python3 scripts/check-rfc.py` passes.
Report the RFC id, file path, and new status in your final summary.
