# AHFL Docs

This directory uses a typed document taxonomy. The goal is to make document intent obvious from the path alone and to stop ad-hoc names from accumulating in `docs/`.

## Taxonomy

- `docs/spec/`
  - Normative documents.
  - Use this for grammar, type system, static semantics, and any source-of-truth language contract.
- `docs/design/`
  - Non-normative engineering design documents.
  - Use this for architecture, boundaries, repository layout, backend design, and implementation constraints.
- `docs/plan/`
  - Execution-facing planning documents.
  - Use this for roadmap, milestones, backlog, rollout plans, and issue breakdowns.
- `docs/reference/`
  - Evergreen reference material.
  - Use this for how-to, operational reference, glossary, and command/reference sheets.

## Naming Convention

Except for this file, every documentation file must follow this path pattern:

```text
docs/<class>/<topic>-v<version>.<lang>.md
```

Rules:

1. `<class>` must be one of `spec`, `design`, `plan`, or `reference`.
2. `<topic>` must be a short kebab-case noun phrase.
3. `<topic>` must not repeat the document class word.
4. `<version>` uses the repository doc version form such as `v0.1`, `v1.0`, `v1.1`.
5. `<lang>` uses a short language code such as `en` or `zh`.
6. `<lang>` must reflect the dominant natural language of the prose body, not just the title.
6. Internal document links should use relative Markdown links, not machine-local absolute paths.
7. New docs should not be added directly under `docs/` unless they are index files like this `README.md`.

Examples:

- `docs/spec/core-language-v0.1.zh.md`
- `docs/design/frontend-architecture-v0.1.zh.md`
- `docs/plan/roadmap-v0.1.zh.md`

Avoid:

- `docs/spec.md`
- `docs/final-roadmap.md`
- `docs/ahfl_new_notes.md`
- `docs/misc.md`

## Current Map

### Spec

- [core-language-v0.1.zh.md](./spec/core-language-v0.1.zh.md)

### Design

- [core-scope-v0.1.en.md](./design/core-scope-v0.1.en.md)
- [frontend-architecture-v0.1.zh.md](./design/frontend-architecture-v0.1.zh.md)
- [formal-backend-v0.1.zh.md](./design/formal-backend-v0.1.zh.md)
- [repository-layout-v0.1.zh.md](./design/repository-layout-v0.1.zh.md)

### Plan

- [roadmap-v0.1.zh.md](./plan/roadmap-v0.1.zh.md)
- [issue-backlog-v0.1.zh.md](./plan/issue-backlog-v0.1.zh.md)

## Governance

When adding or renaming docs:

1. Classify the document before writing it.
2. Use the typed directory and standard file name immediately.
3. Update inbound and outbound links in the same change.
4. Keep this index current.

This convention is also captured as a local skill at `.agents/skills/ahfl-docs-governance/`.
