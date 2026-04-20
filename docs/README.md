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
- `docs/design/compiler-phase-boundaries-v0.2.zh.md`
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

- [ast-model-architecture-v0.2.zh.md](./design/ast-model-architecture-v0.2.zh.md)
- [backend-extension-guide-v0.2.zh.md](./design/backend-extension-guide-v0.2.zh.md)
- [cli-pipeline-architecture-v0.2.zh.md](./design/cli-pipeline-architecture-v0.2.zh.md)
- [compiler-architecture-v0.2.zh.md](./design/compiler-architecture-v0.2.zh.md)
- [compiler-phase-boundaries-v0.2.zh.md](./design/compiler-phase-boundaries-v0.2.zh.md)
- [compiler-evolution-v0.2.zh.md](./design/compiler-evolution-v0.2.zh.md)
- [core-scope-v0.1.en.md](./design/core-scope-v0.1.en.md)
- [diagnostics-architecture-v0.2.zh.md](./design/diagnostics-architecture-v0.2.zh.md)
- [frontend-lowering-architecture-v0.2.zh.md](./design/frontend-lowering-architecture-v0.2.zh.md)
- [formal-backend-v0.2.zh.md](./design/formal-backend-v0.2.zh.md)
- [ir-backend-architecture-v0.2.zh.md](./design/ir-backend-architecture-v0.2.zh.md)
- [module-loading-v0.2.zh.md](./design/module-loading-v0.2.zh.md)
- [module-resolution-rules-v0.2.zh.md](./design/module-resolution-rules-v0.2.zh.md)
- [native-dry-run-bootstrap-v0.6.zh.md](./design/native-dry-run-bootstrap-v0.6.zh.md)
- [native-execution-plan-architecture-v0.6.zh.md](./design/native-execution-plan-architecture-v0.6.zh.md)
- [native-partial-session-failure-bootstrap-v0.9.zh.md](./design/native-partial-session-failure-bootstrap-v0.9.zh.md)
- [native-export-package-prototype-bootstrap-v0.13.zh.md](./design/native-export-package-prototype-bootstrap-v0.13.zh.md)
- [native-durable-store-adapter-receipt-prototype-bootstrap-v0.17.zh.md](./design/native-durable-store-adapter-receipt-prototype-bootstrap-v0.17.zh.md)
- [native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md](./design/native-durable-store-adapter-decision-prototype-bootstrap-v0.16.zh.md)
- [native-durable-store-import-prototype-bootstrap-v0.15.zh.md](./design/native-durable-store-import-prototype-bootstrap-v0.15.zh.md)
- [native-store-import-prototype-bootstrap-v0.14.zh.md](./design/native-store-import-prototype-bootstrap-v0.14.zh.md)
- [native-persistence-prototype-bootstrap-v0.12.zh.md](./design/native-persistence-prototype-bootstrap-v0.12.zh.md)
- [native-checkpoint-prototype-bootstrap-v0.11.zh.md](./design/native-checkpoint-prototype-bootstrap-v0.11.zh.md)
- [native-scheduler-prototype-bootstrap-v0.10.zh.md](./design/native-scheduler-prototype-bootstrap-v0.10.zh.md)
- [native-replay-audit-bootstrap-v0.8.zh.md](./design/native-replay-audit-bootstrap-v0.8.zh.md)
- [native-runtime-session-bootstrap-v0.7.zh.md](./design/native-runtime-session-bootstrap-v0.7.zh.md)
- [native-execution-journal-v0.7.zh.md](./design/native-execution-journal-v0.7.zh.md)
- [native-consumer-bootstrap-v0.5.zh.md](./design/native-consumer-bootstrap-v0.5.zh.md)
- [native-handoff-architecture-v0.4.zh.md](./design/native-handoff-architecture-v0.4.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](./design/native-package-authoring-architecture-v0.5.zh.md)
- [native-package-metadata-v0.4.zh.md](./design/native-package-metadata-v0.4.zh.md)
- [project-descriptor-architecture-v0.3.zh.md](./design/project-descriptor-architecture-v0.3.zh.md)
- [repository-layout-v0.2.zh.md](./design/repository-layout-v0.2.zh.md)
- [semantics-architecture-v0.2.zh.md](./design/semantics-architecture-v0.2.zh.md)
- [source-graph-v0.2.zh.md](./design/source-graph-v0.2.zh.md)
- [testing-strategy-v0.3.zh.md](./design/testing-strategy-v0.3.zh.md)
- [testing-strategy-v0.5.zh.md](./design/testing-strategy-v0.5.zh.md)

### Plan

- [roadmap-v0.17.zh.md](./plan/roadmap-v0.17.zh.md) - current execution plan
- [issue-backlog-v0.17.zh.md](./plan/issue-backlog-v0.17.zh.md) - current execution backlog
- [roadmap-v0.16.zh.md](./plan/roadmap-v0.16.zh.md) - completed baseline
- [issue-backlog-v0.16.zh.md](./plan/issue-backlog-v0.16.zh.md) - completed baseline
- [roadmap-v0.15.zh.md](./plan/roadmap-v0.15.zh.md) - completed baseline
- [issue-backlog-v0.15.zh.md](./plan/issue-backlog-v0.15.zh.md) - completed baseline
- [roadmap-v0.14.zh.md](./plan/roadmap-v0.14.zh.md) - completed baseline
- [issue-backlog-v0.14.zh.md](./plan/issue-backlog-v0.14.zh.md) - completed baseline
- [roadmap-v0.13.zh.md](./plan/roadmap-v0.13.zh.md) - completed baseline
- [issue-backlog-v0.13.zh.md](./plan/issue-backlog-v0.13.zh.md) - completed baseline
- [roadmap-v0.12.zh.md](./plan/roadmap-v0.12.zh.md) - completed baseline
- [issue-backlog-v0.12.zh.md](./plan/issue-backlog-v0.12.zh.md) - completed baseline
- [roadmap-v0.11.zh.md](./plan/roadmap-v0.11.zh.md) - completed baseline
- [issue-backlog-v0.11.zh.md](./plan/issue-backlog-v0.11.zh.md) - completed baseline
- [roadmap-v0.10.zh.md](./plan/roadmap-v0.10.zh.md) - completed baseline
- [issue-backlog-v0.10.zh.md](./plan/issue-backlog-v0.10.zh.md) - completed baseline
- [roadmap-v0.9.zh.md](./plan/roadmap-v0.9.zh.md) - completed baseline
- [issue-backlog-v0.9.zh.md](./plan/issue-backlog-v0.9.zh.md) - completed baseline
- [roadmap-v0.8.zh.md](./plan/roadmap-v0.8.zh.md) - completed baseline
- [issue-backlog-v0.8.zh.md](./plan/issue-backlog-v0.8.zh.md) - completed baseline
- [roadmap-v0.7.zh.md](./plan/roadmap-v0.7.zh.md) - completed baseline
- [issue-backlog-v0.7.zh.md](./plan/issue-backlog-v0.7.zh.md) - completed baseline
- [roadmap-v0.6.zh.md](./plan/roadmap-v0.6.zh.md) - completed baseline
- [issue-backlog-v0.6.zh.md](./plan/issue-backlog-v0.6.zh.md) - completed baseline
- [roadmap-v0.5.zh.md](./plan/roadmap-v0.5.zh.md) - completed baseline
- [issue-backlog-v0.5.zh.md](./plan/issue-backlog-v0.5.zh.md) - completed baseline
- [roadmap-v0.4.zh.md](./plan/roadmap-v0.4.zh.md) - completed baseline
- [issue-backlog-v0.4.zh.md](./plan/issue-backlog-v0.4.zh.md) - completed baseline
- [roadmap-v0.3.zh.md](./plan/roadmap-v0.3.zh.md) - completed baseline
- [issue-backlog-v0.3.zh.md](./plan/issue-backlog-v0.3.zh.md) - completed baseline
- [roadmap-v0.2.zh.md](./plan/roadmap-v0.2.zh.md) - completed baseline
- [issue-backlog-v0.2.zh.md](./plan/issue-backlog-v0.2.zh.md) - completed baseline
- [roadmap-v0.1.zh.md](./plan/roadmap-v0.1.zh.md) - historical baseline
- [issue-backlog-v0.1.zh.md](./plan/issue-backlog-v0.1.zh.md) - historical baseline

### Reference

- [durable-store-adapter-receipt-prototype-compatibility-v0.17.zh.md](./reference/durable-store-adapter-receipt-prototype-compatibility-v0.17.zh.md)
- [native-consumer-matrix-v0.17.zh.md](./reference/native-consumer-matrix-v0.17.zh.md)
- [contributor-guide-v0.17.zh.md](./reference/contributor-guide-v0.17.zh.md)
- [durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md](./reference/durable-store-adapter-decision-prototype-compatibility-v0.16.zh.md)
- [native-consumer-matrix-v0.16.zh.md](./reference/native-consumer-matrix-v0.16.zh.md)
- [contributor-guide-v0.16.zh.md](./reference/contributor-guide-v0.16.zh.md)
- [durable-store-import-prototype-compatibility-v0.15.zh.md](./reference/durable-store-import-prototype-compatibility-v0.15.zh.md)
- [native-consumer-matrix-v0.15.zh.md](./reference/native-consumer-matrix-v0.15.zh.md)
- [contributor-guide-v0.15.zh.md](./reference/contributor-guide-v0.15.zh.md)
- [store-import-prototype-compatibility-v0.14.zh.md](./reference/store-import-prototype-compatibility-v0.14.zh.md)
- [native-consumer-matrix-v0.14.zh.md](./reference/native-consumer-matrix-v0.14.zh.md)
- [contributor-guide-v0.14.zh.md](./reference/contributor-guide-v0.14.zh.md)
- [project-usage-v0.5.zh.md](./reference/project-usage-v0.5.zh.md)
- [cli-commands-v0.10.zh.md](./reference/cli-commands-v0.10.zh.md)
- [project-usage-v0.3.zh.md](./reference/project-usage-v0.3.zh.md)
- [cli-commands-v0.5.zh.md](./reference/cli-commands-v0.5.zh.md)
- [ir-format-v0.3.zh.md](./reference/ir-format-v0.3.zh.md)
- [ir-compatibility-v0.3.zh.md](./reference/ir-compatibility-v0.3.zh.md)
- [backend-capability-matrix-v0.3.zh.md](./reference/backend-capability-matrix-v0.3.zh.md)
- [native-package-authoring-compatibility-v0.5.zh.md](./reference/native-package-authoring-compatibility-v0.5.zh.md)
- [native-package-compatibility-v0.4.zh.md](./reference/native-package-compatibility-v0.4.zh.md)
- [execution-plan-compatibility-v0.6.zh.md](./reference/execution-plan-compatibility-v0.6.zh.md)
- [native-consumer-matrix-v0.10.zh.md](./reference/native-consumer-matrix-v0.10.zh.md)
- [native-consumer-matrix-v0.12.zh.md](./reference/native-consumer-matrix-v0.12.zh.md)
- [native-consumer-matrix-v0.13.zh.md](./reference/native-consumer-matrix-v0.13.zh.md)
- [export-package-prototype-compatibility-v0.13.zh.md](./reference/export-package-prototype-compatibility-v0.13.zh.md)
- [native-consumer-matrix-v0.11.zh.md](./reference/native-consumer-matrix-v0.11.zh.md)
- [contributor-guide-v0.13.zh.md](./reference/contributor-guide-v0.13.zh.md)
- [contributor-guide-v0.12.zh.md](./reference/contributor-guide-v0.12.zh.md)
- [contributor-guide-v0.11.zh.md](./reference/contributor-guide-v0.11.zh.md)
- [contributor-guide-v0.10.zh.md](./reference/contributor-guide-v0.10.zh.md)
- [native-consumer-matrix-v0.9.zh.md](./reference/native-consumer-matrix-v0.9.zh.md)
- [contributor-guide-v0.9.zh.md](./reference/contributor-guide-v0.9.zh.md)
- [runtime-session-compatibility-v0.9.zh.md](./reference/runtime-session-compatibility-v0.9.zh.md)
- [execution-journal-compatibility-v0.9.zh.md](./reference/execution-journal-compatibility-v0.9.zh.md)
- [replay-view-compatibility-v0.9.zh.md](./reference/replay-view-compatibility-v0.9.zh.md)
- [audit-report-compatibility-v0.9.zh.md](./reference/audit-report-compatibility-v0.9.zh.md)
- [failure-path-compatibility-v0.9.zh.md](./reference/failure-path-compatibility-v0.9.zh.md)
- [persistence-prototype-compatibility-v0.12.zh.md](./reference/persistence-prototype-compatibility-v0.12.zh.md)
- [checkpoint-prototype-compatibility-v0.11.zh.md](./reference/checkpoint-prototype-compatibility-v0.11.zh.md)
- [scheduler-prototype-compatibility-v0.10.zh.md](./reference/scheduler-prototype-compatibility-v0.10.zh.md)
- [runtime-session-compatibility-v0.7.zh.md](./reference/runtime-session-compatibility-v0.7.zh.md)
- [execution-journal-compatibility-v0.7.zh.md](./reference/execution-journal-compatibility-v0.7.zh.md)
- [replay-view-compatibility-v0.8.zh.md](./reference/replay-view-compatibility-v0.8.zh.md)
- [audit-report-compatibility-v0.8.zh.md](./reference/audit-report-compatibility-v0.8.zh.md)
- [dry-run-trace-compatibility-v0.6.zh.md](./reference/dry-run-trace-compatibility-v0.6.zh.md)
- [native-consumer-matrix-v0.8.zh.md](./reference/native-consumer-matrix-v0.8.zh.md)
- [native-consumer-matrix-v0.7.zh.md](./reference/native-consumer-matrix-v0.7.zh.md)
- [native-consumer-matrix-v0.6.zh.md](./reference/native-consumer-matrix-v0.6.zh.md)
- [native-consumer-matrix-v0.5.zh.md](./reference/native-consumer-matrix-v0.5.zh.md)
- [native-handoff-usage-v0.5.zh.md](./reference/native-handoff-usage-v0.5.zh.md)
- [contributor-guide-v0.8.zh.md](./reference/contributor-guide-v0.8.zh.md)
- [contributor-guide-v0.7.zh.md](./reference/contributor-guide-v0.7.zh.md)
- [contributor-guide-v0.6.zh.md](./reference/contributor-guide-v0.6.zh.md)
- [contributor-guide-v0.5.zh.md](./reference/contributor-guide-v0.5.zh.md)
- [contributor-guide-v0.3.zh.md](./reference/contributor-guide-v0.3.zh.md)

## Governance

When adding or renaming docs:

1. Classify the document before writing it.
2. Use the typed directory and standard file name immediately.
3. Update inbound and outbound links in the same change.
4. Keep this index current.

This convention is also captured as a local skill at `.agents/skills/ahfl-docs-governance/`.
