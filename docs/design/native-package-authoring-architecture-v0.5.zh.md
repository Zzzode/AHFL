# AHFL Native Package Authoring Architecture V0.5

本文冻结 AHFL V0.5 中 package metadata authoring 的输入归属、最小文件模型、校验边界与 compiler pipeline 接入方式。它承接 V0.4 的 Native handoff package 契约，但不定义 runtime launcher、scheduler、connector 或 deployment 配置。

关联文档：

- [native-handoff-architecture-v0.4.zh.md](./native-handoff-architecture-v0.4.zh.md)
- [native-package-metadata-v0.4.zh.md](./native-package-metadata-v0.4.zh.md)
- [project-descriptor-architecture-v0.3.zh.md](./project-descriptor-architecture-v0.3.zh.md)
- [native-package-compatibility-v0.4.zh.md](../reference/native-package-compatibility-v0.4.zh.md)
- [native-consumer-matrix-v0.4.zh.md](../reference/native-consumer-matrix-v0.4.zh.md)
- [roadmap-v0.5.zh.md](../plan/roadmap-v0.5.zh.md)

适用范围：

- `include/ahfl/frontend/frontend.hpp`
- `include/ahfl/handoff/package.hpp`
- `src/frontend/frontend.cpp`
- `src/frontend/project.cpp`
- `src/handoff/package.cpp`
- `src/backends/native_json.cpp`
- `src/cli/ahflc.cpp`
- `docs/reference/`
- `tests/`

## 目标

本文主要回答五个问题：

1. package metadata 应从哪一类输入文件进入编译器。
2. package authoring 与 project descriptor、workspace descriptor、deployment 配置的关系是什么。
3. package identity、entry/export target 与 capability binding alias 的最小 authoring 形态是什么。
4. frontend、resolver / validator、handoff lowering 与 backend 各自承担什么责任。
5. 后续 Issue 04+ 应按什么对象模型和诊断边界实现。

## 非目标

本文不定义：

1. runtime launcher、scheduler 或 host integration
2. capability connector、provider、endpoint、auth、secret 或 `llm_config`
3. deployment environment、tenant、region、rollout、publish / registry 配置
4. 多 package dependency graph 或 package manager
5. 完整 statement/data execution semantics

## 核心决策

V0.5 采用独立 package authoring descriptor 作为正式输入：

- `ahfl.package.json`

它不内联进 `ahfl.project.json`，也不内联进 `ahfl.workspace.json`。

原因是：

1. `ahfl.project.json` 的职责仍然是描述编译输入，即 entry sources 与 search roots。
2. `ahfl.package.json` 的职责是描述编译输出如何作为 Native handoff package 被消费。
3. deployment / connector 配置仍属于 future Native runtime 私有层，不能进入这两个 descriptor。

因此 V0.5 的推荐链路是：

```text
single file / project / workspace input
  + ahfl.package.json
  -> ProjectInput / SourceGraph
  -> validated semantic model
  -> PackageAuthoringInput
  -> handoff PackageMetadata
  -> native handoff package
```

这表示：

1. package authoring descriptor 与 project descriptor 可组合，但职责不合并。
2. CLI 只负责选择 `--package <ahfl.package.json>`，不负责拼装 metadata 字段。
3. handoff lowering 消费已解析 / 已校验的 authoring 对象，不重新读取原始 JSON。

## 为什么不把 package 区段放进 project descriptor

V0.4 已经明确 project descriptor 负责“从哪里编译”，package metadata 负责“编译后交给 Native 的什么工件”。V0.5 继续保留这条边界。

如果把 `package` 区段直接放进 `ahfl.project.json`，会带来三个问题：

1. project input 与 package output 语义混在同一层，后续难以支持一个 project 产出多个 package。
2. workspace 很容易继续膨胀成 build / deployment / publish 描述文件。
3. runtime-facing metadata 变更会反向影响 source graph loader 的兼容性。

因此 V0.5 初始实现不新增 project descriptor 内联 `package` 区段，也不让 workspace 选择 package。

未来如果确实需要简化 UX，可以在保持独立 descriptor 的前提下增加“指向 package descriptor 的路径字段”，但该路径字段不能承载 package metadata 本体，也不能承载 deployment 配置。

## 最小文件模型

V0.5 约定 package authoring descriptor 文件名为：

- `ahfl.package.json`

最小示例：

```json
{
  "format_version": "ahfl.package-authoring.v0.5",
  "package": {
    "name": "refund-audit",
    "version": "0.1.0"
  },
  "entry": {
    "kind": "workflow",
    "name": "RefundAuditWorkflow"
  },
  "exports": [
    {
      "kind": "workflow",
      "name": "RefundAuditWorkflow"
    }
  ],
  "capability_bindings": [
    {
      "capability": "OrderQuery",
      "binding_key": "order.query"
    }
  ]
}
```

### 顶层字段

1. `format_version`
   - 固定为 `ahfl.package-authoring.v0.5`
   - 表示 authoring descriptor 格式版本，不等于 emitted handoff package 版本
2. `package`
   - package identity 的 authoring 来源
3. `entry`
   - package 默认执行入口
4. `exports`
   - package 对外暴露的 executable targets
5. `capability_bindings`
   - capability binding slot 的 authoring alias / key

### `package`

最小字段：

```json
{
  "name": "refund-audit",
  "version": "0.1.0"
}
```

约束：

1. `name` 必填，且必须是非空字符串。
2. `version` 必填，且必须是非空字符串。
3. `format_version` 不在此处 author；emitted package 的 `format_version` 仍由 handoff package compatibility 层规范化。

V0.5 不定义：

1. publisher / organization
2. registry URL
3. distribution channel
4. license / provenance attestation

这些可以留给后续 package distribution 设计，不进入当前 authoring 边界。

### `entry`

最小字段：

```json
{
  "kind": "workflow",
  "name": "RefundAuditWorkflow"
}
```

约束：

1. `kind` 只允许 `workflow` 或 `agent`。
2. `name` 指向编译后 source graph 中可解析的 top-level declaration。
3. entry target 必须存在，且 declaration kind 必须与 `kind` 一致。

V0.5 不支持：

1. capability 作为 entry
2. contract / flow 作为 entry
3. 条件化 entry
4. 多 entry dispatch

### `exports`

最小字段：

```json
[
  {
    "kind": "workflow",
    "name": "RefundAuditWorkflow"
  }
]
```

约束：

1. 每个 export target 的 `kind` 只允许 `workflow` 或 `agent`。
2. 每个 export target 必须能在 validated semantic model 中解析。
3. 同一 `(kind, name)` 不得重复。
4. `exports` 不会隐式包含 `entry`；是否导出 entry 由 author 显式决定。

这样做的原因是避免 compiler 猜测 authoring 意图。entry 是默认执行入口，exports 是对外暴露集合，二者相关但不是同一个概念。

### `capability_bindings`

最小字段：

```json
[
  {
    "capability": "OrderQuery",
    "binding_key": "order.query"
  }
]
```

约束：

1. `capability` 必须指向一个已声明 capability。
2. `binding_key` 必须是非空字符串。
3. 同一个 capability 不得声明多个 binding key。
4. 同一个 binding key 不得绑定到多个 capability。
5. 未出现在 `capability_bindings` 中的 capability 仍可由 handoff package 导出 binding slot，但不获得 author-provided binding key。

V0.5 不支持：

1. provider / model 名称
2. endpoint / protocol
3. auth / credential
4. timeout / retry / concurrency
5. adapter class 或 host implementation 名称

## 名称解析规则

V0.5 authoring descriptor 中的 `name` 与 `capability` 字段都属于 metadata reference，不属于 AHFL source grammar。

为保持实现简单和诊断稳定，V0.5 的最小规则是：

1. 先按 validated semantic model 中的 canonical name 匹配。
2. 若没有 canonical name 命中，可按未限定 top-level display name 匹配。
3. 未限定 display name 只能在唯一命中时成立。
4. 多命中必须诊断为 ambiguous metadata reference，而不是任意选择一个。

这条规则的目标是让 authoring 文件可读，同时避免重新实现一套 module resolution 语义。

## 与 handoff package 的关系

`ahfl.package.json` 是 authoring 输入，不是 emitted package。

映射关系是：

```text
PackageAuthoringDescriptor
  -> PackageAuthoringInput
  -> handoff::PackageMetadata
  -> handoff::Package
  -> emit-native-json
```

其中：

1. `PackageAuthoringDescriptor` 表示原始文件格式。
2. `PackageAuthoringInput` 表示 frontend 解析后的稳定对象。
3. `handoff::PackageMetadata` 表示 lowering 可消费的 runtime-facing metadata。
4. `handoff::Package` 继续保持 V0.4 的 emitted package compatibility 契约。

重要约束：

1. authoring descriptor 的 `format_version` 是 `ahfl.package-authoring.v0.5`。
2. emitted package 的 `format_version` 仍是 `ahfl.native-package.v1`，除非 handoff package ABI 本身发生 breaking change。
3. authoring descriptor 变化不必自动 bump emitted package format version；只有 emitted package ABI breaking change 才需要 bump `ahfl::handoff::kFormatVersion`。

## CLI 边界

V0.5 推荐新增统一参数：

```text
--package <ahfl.package.json>
```

它可与下列输入路径组合：

```bash
ahflc emit-native-json --package ahfl.package.json app/main.ahfl

ahflc emit-native-json \
  --project ahfl.project.json \
  --package ahfl.package.json

ahflc emit-native-json \
  --workspace ahfl.workspace.json \
  --project-name refund-audit \
  --package ahfl.package.json
```

CLI 只负责：

1. 解析 `--package` 路径
2. 调用 frontend 解析 package authoring descriptor
3. 把 source input 与 authoring input 一起交给统一 pipeline

CLI 不负责：

1. 拼 package identity
2. 推断 entry/export target
3. 推断 capability dependency surface
4. 直接生成 emitted JSON 字段

## Frontend 边界

frontend 负责：

1. 读取 `ahfl.package.json`
2. 校验 JSON 结构、字段类型、路径规范化和 format version
3. 生成 `PackageAuthoringInput`
4. 产出 metadata diagnostics

frontend 不负责：

1. 判断 `entry` target 是否存在
2. 判断 capability 是否存在
3. 判断 target kind 是否与 resolved declaration kind 一致
4. 拼 handoff package

这些需要 validated semantic model 的信息，应留给后续 authoring validation / handoff lowering 阶段。

## Validation 边界

authoring validation 负责：

1. resolve `entry` 与 `exports` target
2. resolve `capability_bindings.capability`
3. 检查 duplicate export target
4. 检查 duplicate capability binding 与 duplicate binding key
5. 将 authoring references 收敛到 stable canonical identity

authoring validation 不负责：

1. connector 可达性
2. runtime host 是否支持某 capability
3. package 是否可发布
4. scheduler 是否能执行该 workflow

## Handoff Lowering 边界

handoff lowering 负责把已校验的 authoring 输入投影到 `handoff::PackageMetadata`：

1. `package.name` / `package.version`
2. `entry`
3. `exports`
4. capability binding key / alias

handoff lowering 继续从 IR / semantic model 中导出：

1. executable targets
2. execution graph
3. lifecycle summary
4. policy obligations
5. formal observations
6. workflow value summary
7. capability dependency surface

因此 V0.5 不允许 handoff lowering 从 authoring descriptor 中读取 runtime-only 信息，也不允许 authoring descriptor 覆盖 validated semantic facts。

## 诊断分层

V0.5 metadata authoring 错误分三层：

### 1. Descriptor 结构错误

例如：

1. `format_version` 缺失或不支持
2. `package.name` 不是字符串
3. `entry.kind` 不是 `workflow` 或 `agent`
4. `capability_bindings` 不是数组

这类错误属于 frontend descriptor diagnostics。

### 2. Metadata reference 错误

例如：

1. entry target 不存在
2. export target kind 不匹配
3. capability 不存在
4. metadata reference ambiguous

这类错误需要 semantic model，属于 authoring validation diagnostics。

### 3. Metadata consistency 错误

例如：

1. duplicate export target
2. duplicate capability binding
3. duplicate binding key
4. package name / version 为空

这类错误可以由 frontend 或 authoring validation 发现，但最终应以 metadata authoring diagnostics 暴露，不应伪装成 source language type error。

## 对后续实现的约束

后续 Issue 04+ 实现必须遵守：

1. `ahfl.package.json` 是 V0.5 package metadata authoring 的正式输入来源。
2. `ahfl.project.json` 不内联 package metadata。
3. `ahfl.workspace.json` 不选择 package，也不承载 deployment 配置。
4. `--package` 必须下沉到统一 authoring 对象，而不是散落成多个 CLI flag。
5. entry/export/capability reference 必须基于 validated semantic model 校验。
6. emitted package ABI 的 breaking change 仍按 V0.4 handoff compatibility 流程处理。
7. 新增 authoring 字段必须先说明它属于 package identity、export surface、binding alias，还是 future deployment 私有层。

## 当前状态

本文冻结 V0.5 package metadata authoring 的最小边界：采用独立 `ahfl.package.json` descriptor，通过 `--package` 与单文件 / project / workspace 输入组合，显式 author package identity、entry/export target 与 capability binding key。下一步可以进入 authoring 数据模型、frontend 解析和 CLI 接入实现。
