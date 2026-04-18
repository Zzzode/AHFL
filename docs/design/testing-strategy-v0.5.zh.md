# AHFL Core V0.5 Testing Strategy

本文说明 AHFL Core V0.5 当前测试资产在 package authoring、package review 与 reference consumer bootstrap 上的分层方式、标签切片与 CI 接入方式。

关联文档：

- [testing-strategy-v0.3.zh.md](./testing-strategy-v0.3.zh.md)
- [native-package-authoring-architecture-v0.5.zh.md](./native-package-authoring-architecture-v0.5.zh.md)
- [native-consumer-bootstrap-v0.5.zh.md](./native-consumer-bootstrap-v0.5.zh.md)
- [native-package-authoring-compatibility-v0.5.zh.md](../reference/native-package-authoring-compatibility-v0.5.zh.md)
- [native-handoff-usage-v0.5.zh.md](../reference/native-handoff-usage-v0.5.zh.md)

## 目标

本文主要回答四个问题：

1. V0.5 当前测试按哪些层次组织。
2. authoring、package review 与 reference consumer 各自落在哪些测试目录和标签下。
3. CI 当前如何显式执行这些 V0.5 回归切片。
4. 后续新增 V0.5 字段或 consumer 时，最低应补哪些测试。

## 当前 V0.5 测试层次

V0.5 当前主要分成四层：

1. authoring descriptor 解析
   - `tests/project/project_parse.cpp`
   - `tests/project/package_authoring_*/`
2. authoring semantic validation
   - `tests/handoff/package_model.cpp`
   - `tests/project/workflow_value_flow/*.package.json`
3. package-aware review 输出
   - `tests/review/*.review`
   - `emit-package-review` CLI golden
4. direct handoff reference consumer helper
   - `tests/handoff/package_model.cpp`
   - `build_package_reader_summary(...)`
   - `build_execution_planner_bootstrap(...)`

## 当前 V0.5 标签切片

当前显式标签为：

- `v0.5-package-authoring-model`
  - descriptor 形状、必填字段、format version 与 descriptor-level 负例
- `v0.5-package-authoring-emission`
  - `emit-native-json --package` 的单文件 / project / workspace 正例与 CLI 约束
- `v0.5-package-authoring-validation`
  - display name 规范化、wrong kind、unknown capability、规范化后重复项
- `v0.5-package-review`
  - `emit-package-review` 的 single-file / project / workspace golden
- `v0.5-reference-consumer`
  - direct handoff Package Reader Summary / Execution Planner Bootstrap 正例与负例

这些标签都挂在统一总标签：

- `ahfl-v0.5`

## 为什么分成这四层

这样分层是为了让失败定位可以直接落到边界，而不是只看到“V0.5 某处坏了”：

1. authoring model 失败
   - 说明 parser / descriptor shape 出问题
2. authoring validation 失败
   - 说明 semantic normalization 或 package metadata validation 出问题
3. package review 失败
   - 说明 review/debug 输出边界或 golden 漂移
4. reference consumer 失败
   - 说明 direct handoff helper 或 consumer bootstrap 一致性检查出问题

## CI 当前执行方式

CI 当前显式执行下面三组 V0.5 切片，再继续跑全量测试：

```bash
ctest --preset test-dev --output-on-failure -L 'v0.5-package-authoring-(model|emission|validation)'
ctest --preset test-dev --output-on-failure -L 'v0.5-package-review'
ctest --preset test-dev --output-on-failure -L 'v0.5-reference-consumer'
```

这表示：

1. V0.5 主线能力在 CI 日志中有单独失败面
2. 即使全量测试后面也会跑，V0.5 回归仍能在更早阶段暴露
3. 新增 V0.5 边界时，应先进入这些标签体系，而不是只依赖全量 `ctest`

## 后续新增能力的最低补测要求

### 1. 改 authoring descriptor

至少补：

1. `tests/project/` 中的正例或负例
2. `v0.5-package-authoring-model` 标签注册
3. 若影响 emitted metadata，再补 `tests/native/` 或 `tests/review/`

### 2. 改 semantic normalization / validation

至少补：

1. `tests/handoff/package_model.cpp` 中的 direct validation 用例
2. `v0.5-package-authoring-validation` 标签注册
3. 必要时补 CLI failure regression

### 3. 改 review/debug 输出

至少补：

1. `tests/review/*.review` golden
2. `v0.5-package-review` 标签注册

### 4. 改 direct reference consumer helper

至少补：

1. `tests/handoff/package_model.cpp` 中的 helper 正例 / 负例
2. `v0.5-reference-consumer` 标签注册
3. 若输出边界变化，再同步更新 review/native golden
