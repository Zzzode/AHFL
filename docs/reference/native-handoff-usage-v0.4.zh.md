# AHFL Native Handoff Usage V0.4

本文给出 AHFL V0.4 handoff package 的实际使用方式，重点覆盖 `emit-native-json` 的输入模式、输出边界、当前限制，以及本地验证建议。

关联文档：

- [cli-commands-v0.3.zh.md](./cli-commands-v0.3.zh.md)
- [project-usage-v0.3.zh.md](./project-usage-v0.3.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-consumer-matrix-v0.4.zh.md](./native-consumer-matrix-v0.4.zh.md)
- [native-handoff-architecture-v0.4.zh.md](../design/native-handoff-architecture-v0.4.zh.md)

## 目标

本文主要回答四个问题：

1. 当前如何导出 handoff package。
2. `emit-native-json` 支持哪些输入模式。
3. 当前输出里哪些字段已经稳定，哪些仍然故意留空或保持抽象。
4. 本地要如何验证 handoff 路径没有回归。

## 当前命令

V0.4 当前统一通过：

```bash
./build/dev/src/cli/ahflc emit-native-json <input-mode>
```

其中 `<input-mode>` 与其他 project-aware 命令一致：

```text
<input-mode> ::=
    <input.ahfl>
  | [--search-root <dir>]... <input.ahfl>
  | --project <ahfl.project.json>
  | --workspace <ahfl.workspace.json> --project-name <name>
```

## 典型用法

单文件：

```bash
./build/dev/src/cli/ahflc emit-native-json \
  tests/ir/ok_expr_temporal.ahfl
```

project-aware `--search-root`：

```bash
./build/dev/src/cli/ahflc emit-native-json \
  --search-root tests/project/workflow_value_flow \
  tests/project/workflow_value_flow/app/main.ahfl
```

project manifest：

```bash
./build/dev/src/cli/ahflc emit-native-json \
  --project tests/project/workflow_value_flow/ahfl.project.json
```

workspace：

```bash
./build/dev/src/cli/ahflc emit-native-json \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow
```

## 当前输出边界

`emit-native-json` 当前稳定导出：

1. 顶层 `format_version`
2. `metadata`
3. `executable_targets`
4. workflow `execution_graph`
5. workflow node `lifecycle`
6. `capability_binding_slots`
7. `policy_obligations`
8. `formal_observations`
9. workflow `input_summary` / `return_summary`

当前输出故意不承诺：

1. deployment / environment / connector 配置
2. 完整 temporal / expr 公式树作为长期 runtime ABI
3. 完整 flow statement / path-sensitive dataflow 语义
4. scheduler、timeout、retry 等运行时私有策略

## metadata 当前状态

当前仓库已经有稳定的 `PackageMetadata` / `PackageIdentity` 数据模型，但 metadata 的外部输入来源还没有冻结到正式 CLI/descriptor 契约。

因此，当前行为是：

1. 若没有显式 metadata 输入来源，`emit-native-json` 会稳定输出空 metadata 区段
2. `PackageIdentity.format_version` 会被规范化到 `ahfl.native-package.v1`
3. consumer 当前不应假设 package identity / entry / export targets 总是非空

## 适合谁消费

当前 `emit-native-json` 主要适合三类 consumer：

1. Package Reader
   - 读取版本、metadata、capability / policy surface
2. Execution Planner
   - 读取 workflow DAG、node lifecycle、workflow value summary
3. Policy / Audit Consumer
   - 读取 `policy_obligations`、`formal_observations` 与 capability dependency

更细的 consumer 分类见：

- [native-consumer-matrix-v0.4.zh.md](./native-consumer-matrix-v0.4.zh.md)

## 本地验证建议

只想验证 handoff 路径时，优先跑：

```bash
ctest --test-dir build/dev --output-on-failure -L v0.4-package-model
ctest --test-dir build/dev --output-on-failure -L v0.4-package-emission
ctest --test-dir build/dev --output-on-failure -L v0.4-package-compat
```

三组测试分别覆盖：

1. `v0.4-package-model`
   - handoff object model / lowering
2. `v0.4-package-emission`
   - `emit-native-json` 输出与 golden
3. `v0.4-package-compat`
   - versioning / compatibility 守卫

## 对贡献者的含义

若你要扩 handoff package 或新增 runtime-facing consumer，至少应同步更新：

1. `include/ahfl/handoff/package.hpp`
2. `src/handoff/package.cpp`
3. `src/backends/native_json.cpp`
4. `tests/handoff/`
5. `tests/native/`
6. [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
7. [native-consumer-matrix-v0.4.zh.md](./native-consumer-matrix-v0.4.zh.md)

## 当前状态

截至当前实现：

1. `emit-native-json` 已支持单文件、`--search-root`、`--project`、`--workspace --project-name`
2. handoff package 的 runtime-facing 输出边界已冻结到 V0.4 当前文档体系
3. regression / compatibility / CI 覆盖已进入显式 V0.4 handoff 标签组
