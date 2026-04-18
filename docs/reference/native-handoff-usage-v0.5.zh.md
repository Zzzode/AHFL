# AHFL Native Handoff Usage V0.5

本文给出 AHFL V0.5 handoff package 的实际使用方式，重点覆盖 package authoring 输入、`emit-package-review` 调试面，以及 reference consumer helper 的最小接入路径。

关联文档：

- [project-usage-v0.5.zh.md](./project-usage-v0.5.zh.md)
- [native-handoff-usage-v0.4.zh.md](./native-handoff-usage-v0.4.zh.md)
- [native-consumer-matrix-v0.5.zh.md](./native-consumer-matrix-v0.5.zh.md)
- [cli-commands-v0.5.zh.md](./cli-commands-v0.5.zh.md)
- [native-package-compatibility-v0.4.zh.md](./native-package-compatibility-v0.4.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](../design/native-package-authoring-architecture-v0.5.zh.md)
- [native-consumer-bootstrap-v0.5.zh.md](../design/native-consumer-bootstrap-v0.5.zh.md)

## 当前路径

V0.5 当前推荐路径是：

```text
ahfl.package.json
  -> handoff::PackageMetadata
  -> handoff::Package
  -> Package Reader Summary
  -> Execution Planner Bootstrap
```

对应到仓库实现：

1. `ahflc emit-native-json --package ...`
2. `ahflc emit-package-review --package ...`
3. `handoff::build_package_reader_summary(...)`
4. `handoff::build_execution_planner_bootstrap(...)`
5. `handoff::build_entry_execution_planner_bootstrap(...)`

## CLI 用法

### 发射 native package

```bash
./build/dev/src/cli/ahflc emit-native-json \
  --project tests/project/workflow_value_flow/ahfl.project.json \
  --package tests/project/workflow_value_flow/ahfl.package.json
```

### 发射 package review

```bash
./build/dev/src/cli/ahflc emit-package-review \
  --workspace tests/project/handoff.workspace.json \
  --project-name workflow-value-flow \
  --package tests/project/workflow_value_flow/ahfl.display.package.json
```

## C++ helper 用法

最小 reference consumer 路径：

```text
lower_package(...)
  -> build_package_reader_summary(...)
  -> build_execution_planner_bootstrap(...)
```

helper 当前保证：

1. 只消费 `handoff::Package`
2. 不回退读取 AST、raw source、project descriptor 或 `ahfl.package.json`
3. 会对 entry/export target、binding key、workflow dependency 与 workflow target 类型做最小一致性检查
4. 不承诺 scheduler、retry、timeout、deployment 或 connector 语义

## 本地验证建议

只验证 V0.5 authoring + review + consumer bootstrap 时，优先跑：

```bash
ctest --preset test-dev --output-on-failure -L v0.5-package-authoring-validation
ctest --preset test-dev --output-on-failure -L v0.5-package-review
ctest --preset test-dev --output-on-failure -L v0.5-reference-consumer
```
