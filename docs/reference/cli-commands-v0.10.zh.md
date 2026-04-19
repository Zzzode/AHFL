# AHFL CLI Commands V0.10

本文给出 `ahflc` 在 V0.10 阶段的命令参考，重点覆盖 `ExecutionPlan`、`RuntimeSession`、`ExecutionJournal`、`ReplayView`、`SchedulerSnapshot`、`SchedulerDecisionSummary` 的当前 CLI 入口，以及 project / workspace / package / mocks 的组合边界。

关联文档：

- [cli-commands-v0.5.zh.md](./cli-commands-v0.5.zh.md)
- [project-usage-v0.5.zh.md](./project-usage-v0.5.zh.md)
- [scheduler-prototype-compatibility-v0.10.zh.md](./scheduler-prototype-compatibility-v0.10.zh.md)
- [native-consumer-matrix-v0.10.zh.md](./native-consumer-matrix-v0.10.zh.md)
- [contributor-guide-v0.10.zh.md](./contributor-guide-v0.10.zh.md)
- [native-scheduler-prototype-bootstrap-v0.10.zh.md](../design/native-scheduler-prototype-bootstrap-v0.10.zh.md)

## 总览

项目输入模式：

```text
<input-mode> ::=
    <input.ahfl>
  | [--search-root <dir>]... <input.ahfl>
  | --project <ahfl.project.json>
  | --workspace <ahfl.workspace.json> --project-name <name>
```

命令：

```text
ahflc check <input-mode>
ahflc dump-ast <input-mode>
ahflc dump-types <input-mode>
ahflc dump-project <input-mode>
ahflc emit-ir <input-mode>
ahflc emit-ir-json <input-mode>
ahflc emit-native-json [--package <ahfl.package.json>] <input-mode>
ahflc emit-execution-plan [--package <ahfl.package.json>] <input-mode>
ahflc emit-runtime-session --package <ahfl.package.json> --capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] <input-mode>
ahflc emit-execution-journal --package <ahfl.package.json> --capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] <input-mode>
ahflc emit-replay-view --package <ahfl.package.json> --capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] <input-mode>
ahflc emit-scheduler-snapshot --package <ahfl.package.json> --capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] <input-mode>
ahflc emit-scheduler-review --package <ahfl.package.json> --capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] <input-mode>
ahflc emit-audit-report --package <ahfl.package.json> --capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] <input-mode>
ahflc emit-dry-run-trace --package <ahfl.package.json> --capability-mocks <mocks.json> --input-fixture <fixture> [--workflow <canonical>] [--run-id <id>] <input-mode>
ahflc emit-package-review [--package <ahfl.package.json>] <input-mode>
ahflc emit-summary <input-mode>
ahflc emit-smv <input-mode>
```

## 选项

- `--search-root <dir>`
  - 开启 project-aware loader 路径。
- `--project <ahfl.project.json>`
  - 使用单个 project descriptor 作为输入。
- `--workspace <ahfl.workspace.json>`
  - 使用 workspace descriptor 作为输入。
- `--project-name <name>`
  - 与 `--workspace` 配合使用，选择目标 project。
- `--package <ahfl.package.json>`
  - 为 `emit-native-json`、`emit-execution-plan`、runtime-adjacent outputs 与 `emit-package-review` 提供 package authoring descriptor。
- `--capability-mocks <mocks.json>`
  - 为 `emit-runtime-session`、`emit-execution-journal`、`emit-replay-view`、`emit-scheduler-snapshot`、`emit-scheduler-review`、`emit-audit-report`、`emit-dry-run-trace` 提供 deterministic capability mock 输入。
- `--input-fixture <fixture>`
  - 为 runtime-adjacent outputs 选择 package fixture request。
- `--run-id <id>`
  - 为 runtime-adjacent outputs 提供稳定 run identity；不传时会使用默认 run id。
- `--workflow <canonical>`
  - 在 package 暴露多个 workflow entry 时显式选择目标 workflow。

## V0.10 主链路示例

下面这组命令覆盖当前推荐的 `plan -> session -> journal -> replay -> snapshot -> review` 路径：

```bash
./build/dev/src/cli/ahflc emit-execution-plan \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json

./build/dev/src/cli/ahflc emit-runtime-session \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001

./build/dev/src/cli/ahflc emit-execution-journal \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001

./build/dev/src/cli/ahflc emit-replay-view \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001

./build/dev/src/cli/ahflc emit-scheduler-snapshot \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001

./build/dev/src/cli/ahflc emit-scheduler-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.package.json \
  --capability-mocks tests/dry_run/project_workflow_value_flow.pending.mocks.json \
  --input-fixture fixture.request.partial \
  --run-id run-partial-001
```

这表示：

1. `emit-scheduler-snapshot` 不直接从 source / AST 私造 scheduler state，而是沿着正式 runtime-adjacent artifact 链路生成。
2. `emit-scheduler-review` 是 `SchedulerSnapshot` 的 reviewer-facing projection，不是独立状态机。
3. 若以后探索 persistence / checkpoint ABI，应先扩 `SchedulerSnapshot` compatibility，而不是先改 review 文本。

## 重点命令

### `emit-execution-plan`

用途：

1. 输出 planner / runner 可消费的正式 planning artifact。
2. 是 V0.10 scheduler-facing 主链路的稳定起点。
3. 可单独与 `--package` 组合，不要求 mocks / fixture。

### `emit-runtime-session`

用途：

1. 输出 workflow / node 当前稳定状态快照。
2. 是后续 journal / replay / scheduler snapshot 的直接上游。
3. 适合回答“当前停在哪个状态、哪些 node 已完成 / 失败 / 跳过”。

### `emit-execution-journal`

用途：

1. 输出 deterministic event sequence。
2. 是 failure progression、prefix ordering 与 replay projection 的直接上游。
3. 不负责直接给出 ready set / blocked frontier。

### `emit-replay-view`

用途：

1. 输出 session / journal 的一致性投影视图。
2. 适合回答“当前前缀是否能稳定投影为 replay state”。
3. 不是 scheduler state 的第一事实来源。

### `emit-scheduler-snapshot`

用途：

1. 输出 scheduler-facing local state。
2. 稳定承诺 ready set、blocked frontier、executed prefix、next candidate 与 `checkpoint_friendly`。
3. 不承诺 durable checkpoint id、resume token、host telemetry。

### `emit-scheduler-review`

用途：

1. 输出 reviewer-facing scheduler summary。
2. 稳定承诺 terminal reason、next action、next-step recommendation 与 readable summary。
3. 不承诺独立状态机、recovery ABI 或 persistence mutation plan。

## 选项支持矩阵

| 命令 | `--package` | `--capability-mocks` | `--input-fixture` | `--run-id` | `--workflow` |
|------|-------------|----------------------|-------------------|------------|--------------|
| `emit-native-json` | 可选 | 否 | 否 | 否 | 否 |
| `emit-execution-plan` | 可选 | 否 | 否 | 否 | 否 |
| `emit-runtime-session` | 必需 | 必需 | 必需 | 可选 | 可选 |
| `emit-execution-journal` | 必需 | 必需 | 必需 | 可选 | 可选 |
| `emit-replay-view` | 必需 | 必需 | 必需 | 可选 | 可选 |
| `emit-scheduler-snapshot` | 必需 | 必需 | 必需 | 可选 | 可选 |
| `emit-scheduler-review` | 必需 | 必需 | 必需 | 可选 | 可选 |
| `emit-audit-report` | 必需 | 必需 | 必需 | 可选 | 可选 |
| `emit-dry-run-trace` | 必需 | 必需 | 必需 | 可选 | 可选 |
| `emit-package-review` | 可选 | 否 | 否 | 否 | 否 |

## project-aware 支持矩阵

| 命令 | 单文件 / `--search-root` | `--project` | `--workspace --project-name` |
|------|--------------------------|-------------|-------------------------------|
| `check` | 是 | 是 | 是 |
| `dump-ast` | 是 | 是 | 是 |
| `dump-types` | 是 | 是 | 是 |
| `dump-project` | 是 | 是 | 是 |
| `emit-ir` | 是 | 是 | 是 |
| `emit-ir-json` | 是 | 是 | 是 |
| `emit-native-json` | 是 | 是 | 是 |
| `emit-execution-plan` | 是 | 是 | 是 |
| `emit-runtime-session` | 是 | 是 | 是 |
| `emit-execution-journal` | 是 | 是 | 是 |
| `emit-replay-view` | 是 | 是 | 是 |
| `emit-scheduler-snapshot` | 是 | 是 | 是 |
| `emit-scheduler-review` | 是 | 是 | 是 |
| `emit-audit-report` | 是 | 是 | 是 |
| `emit-dry-run-trace` | 是 | 是 | 是 |
| `emit-package-review` | 是 | 是 | 是 |
| `emit-summary` | 是 | 是 | 是 |
| `emit-smv` | 是 | 是 | 是 |

## 最小验证建议

若改动触及 V0.10 scheduler-facing CLI / docs / regression，最低建议跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.10-scheduler-regression
```

若改动触及整个当前版本主链路，建议再补：

```bash
ctest --preset test-dev --output-on-failure -L ahfl-v0.10
ctest --preset test-dev --output-on-failure
```

## 退出码

- `0`
  - 成功，或按预期输出 dump / emission 结果。
- `1`
  - 进入编译主链路后出现 parse / resolve / typecheck / validate / package / runtime-adjacent validation 错误。
- `2`
  - 命令行参数非法，例如未知选项、缺少参数、动作冲突，或选项与子命令组合不合法。
