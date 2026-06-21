# AHFL Docs

This directory uses a typed document taxonomy. The goal is to make document intent obvious from the path alone and to stop ad-hoc names from accumulating in `docs/`.

## Taxonomy

- `docs/spec/`
  - Normative documents.
  - Use this for grammar, type system, static semantics, and any source-of-truth language contract.
- `docs/design/`
  - Non-normative engineering design documents.
  - Use this for architecture, boundaries, repository layout, backend design, and implementation constraints.
- `docs/plans/`
  - Execution-facing planning documents.
  - Use this for roadmap, milestones, backlog, rollout plans, and issue breakdowns.
- `docs/reference/`
  - Evergreen reference material.
  - Use this for how-to, operational reference, glossary, and command/reference sheets.

## Naming Convention

Except for this index, documentation files live under a typed directory and use one current filename per topic:

```text
docs/<class>/<topic>.<lang>.md
```

Rules:

1. `<class>` must be one of `spec`, `design`, `plan`, or `reference`.
2. `<topic>` must be a short kebab-case noun phrase.
3. `<topic>` must not repeat the document class word.
4. Do not add document-version suffixes to filenames.
5. If a topic accumulates multiple historical docs, merge the useful content into the current topic file and delete the redundant files.
6. `<lang>` uses a short language code such as `en` or `zh` and must reflect the dominant prose language.
7. Internal document links should use relative Markdown links, not machine-local absolute paths.
8. New docs should not be added directly under `docs/` unless they are index files like this `README.md`.

Examples:

- `docs/spec/core-language.zh.md`
- `docs/design/compiler-phase-boundaries.zh.md`
- `docs/plans/project-status.zh.md`

Avoid:

- `docs/spec/<topic>-version.zh.md`
- `docs/design/<topic>-dated-draft.zh.md`
- `docs/final-roadmap.md`
- `docs/misc.md`

## Current Map

### Spec

- [assurance.zh.md](./spec/assurance.zh.md)
- [core-language.zh.md](./spec/core-language.zh.md)

### Design

- [assurance-architecture.zh.md](./design/assurance-architecture.zh.md)
- [ast-model-architecture.zh.md](./design/ast-model-architecture.zh.md)
- [backend-extension-guide.zh.md](./design/backend-extension-guide.zh.md)
- [cli-pipeline-architecture.zh.md](./design/cli-pipeline-architecture.zh.md)
- [compiler-architecture.zh.md](./design/compiler-architecture.zh.md)
- [compiler-evolution.zh.md](./design/compiler-evolution.zh.md)
- [compiler-phase-boundaries.zh.md](./design/compiler-phase-boundaries.zh.md)
- [core-scope.en.md](./design/core-scope.en.md)
- [corelib-rfc.zh.md](./design/corelib-rfc.zh.md)
- [diagnostics-architecture.zh.md](./design/diagnostics-architecture.zh.md)
- [durable-store-import-architecture.zh.md](./design/durable-store-import-architecture.zh.md)
- [formal-backend.zh.md](./design/formal-backend.zh.md)
- [frontend-lowering-architecture.zh.md](./design/frontend-lowering-architecture.zh.md)
- [ir-backend-architecture.zh.md](./design/ir-backend-architecture.zh.md)
- [lsp-hover-architecture.zh.md](./design/lsp-hover-architecture.zh.md)
- [module-loading.zh.md](./design/module-loading.zh.md)
- [module-resolution-rules.zh.md](./design/module-resolution-rules.zh.md)
- [native-runtime-architecture.zh.md](./design/native-runtime-architecture.zh.md)
- [optional-narrowing-rfc.zh.md](./design/optional-narrowing-rfc.zh.md)
- [project-descriptor-architecture.zh.md](./design/project-descriptor-architecture.zh.md)
- [release-evidence-archive.zh.md](./design/release-evidence-archive.zh.md)
- [repository-layout.zh.md](./design/repository-layout.zh.md)
- [semantics-architecture.zh.md](./design/semantics-architecture.zh.md)
- [source-graph.zh.md](./design/source-graph.zh.md)
- [spec-restructure.zh.md](./design/spec-restructure.zh.md)
- [testing-strategy.zh.md](./design/testing-strategy.zh.md)

### Plan

- [issue-backlog-global-gaps.zh.md](./plans/issue-backlog-global-gaps.zh.md)
- [project-status.zh.md](./plans/project-status.zh.md)

### Reference

- [backend-capability-matrix.zh.md](./reference/backend-capability-matrix.zh.md)
- [cli-commands.zh.md](./reference/cli-commands.zh.md)
- [contributor-guide.zh.md](./reference/contributor-guide.zh.md)
- [durable-store-import-reference.zh.md](./reference/durable-store-import-reference.zh.md)
- [ir-format.zh.md](./reference/ir-format.zh.md)
- [lsp-vscode-extension.zh.md](./reference/lsp-vscode-extension.zh.md)
- [migration-policy.zh.md](./reference/migration-policy.zh.md)
- [native-handoff-usage.zh.md](./reference/native-handoff-usage.zh.md)
- [native-runtime-artifacts.zh.md](./reference/native-runtime-artifacts.zh.md)
- [project-usage.zh.md](./reference/project-usage.zh.md)
- [user-guide-assurance.zh.md](./reference/user-guide-assurance.zh.md)
- [user-guide-authoring.zh.md](./reference/user-guide-authoring.zh.md)
- [user-guide-cli.zh.md](./reference/user-guide-cli.zh.md)
- [user-guide-execution.zh.md](./reference/user-guide-execution.zh.md)
- [user-guide-overview.zh.md](./reference/user-guide-overview.zh.md)

## Governance

When adding or renaming docs:

1. Classify the document before writing it.
2. Use the typed directory and current topic filename immediately.
3. Merge same-topic historical material instead of creating another versioned file.
4. Update inbound and outbound links in the same change.
5. Keep this index current.
