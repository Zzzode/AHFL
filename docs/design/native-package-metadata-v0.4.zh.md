# AHFL Native Package Metadata V0.4

本文冻结 AHFL V0.4 中 capability binding、package identity、entry/export target 与 runtime-facing metadata 的最小边界。它的目标不是定义 deployment 配置，而是定义“Native handoff package 至少需要哪些元信息才能被稳定消费”。

关联文档：

- [native-handoff-architecture-v0.4.zh.md](./native-handoff-architecture-v0.4.zh.md)
- [native-package-compatibility-v0.4.zh.md](../reference/native-package-compatibility-v0.4.zh.md)
- [project-descriptor-architecture-v0.3.zh.md](./project-descriptor-architecture-v0.3.zh.md)
- [ir-backend-architecture-v0.2.zh.md](./ir-backend-architecture-v0.2.zh.md)
- [roadmap-v0.4.zh.md](../plan/roadmap-v0.4.zh.md)

适用范围：

- `include/ahfl/frontend/frontend.hpp`
- `include/ahfl/handoff/package.hpp`
- `src/frontend/frontend.cpp`
- `src/handoff/package.cpp`
- `include/ahfl/ir/ir.hpp`
- `src/ir/ir.cpp`
- `src/ir/ir_json.cpp`
- `include/ahfl/backends/driver.hpp`
- `src/backends/driver.cpp`
- `src/cli/ahflc.cpp`
- `docs/reference/`
- `tests/`

## 目标

本文主要回答四个问题：

1. package identity 与当前 project identity 的关系是什么。
2. entry/export target 应属于 project descriptor、handoff package，还是 deployment 层。
3. capability binding metadata 应保留到什么层级。
4. 哪些 runtime-facing metadata 可以进入正式边界，哪些仍然必须留在运行时私有实现里。

## 非目标

本文不定义：

1. capability 的实际 host connector 配置
2. runtime endpoint、密钥、region、租户等 deployment 信息
3. workflow scheduler、重试、超时、并发策略
4. observability / compliance sink 配置
5. package publish、registry、artifact distribution

## 当前问题

V0.3 之后，仓库已经能稳定表达：

1. 哪些 capability 被声明
2. 哪些 workflow / flow / agent 依赖了哪些结构化语义
3. 哪些 project source 构成一次合法编译输入

但 runtime-facing consumer 仍缺少一层正式元信息约束：

1. 编译产物的 package identity 到底是什么
2. future Native 应把哪个 workflow 或 agent 视作导出入口
3. capability 在 handoff package 里应暴露“声明身份”还是“具体绑定配置”

如果这层边界不先冻结，后续最容易出现三类错误：

1. 把 deployment 配置塞回 `ahfl.project.json`
2. 让 handoff package 只靠约定推断 entry/export target
3. 让 capability binding 直接暴露运行时私有 adapter 细节

## 三层身份边界

V0.4 当前建议显式区分三层身份：

### 1. Project Identity

来源：

- `ahfl.project.json`

职责：

- 标识“从哪里编译”
- 服务于 `ProjectInput -> SourceGraph`

典型字段：

- `project.name`
- descriptor root
- entry sources
- search roots

### 2. Package Identity

来源：

- native handoff package metadata

职责：

- 标识“编译后要交给 Native 的哪个 package”
- 服务于 handoff compatibility / artifact identity

典型字段：

- `package.name`
- `package.version`
- `package.format_version`

### 3. Deployment Identity

来源：

- future Native runtime / deployment 层

职责：

- 标识“这个 package 部署到哪里、如何绑定外部世界”

典型字段：

- environment
- region
- connector endpoint
- secrets / credentials

V0.4 的关键约束是：

1. project identity 不等于 package identity，但二者可以默认一一对应。
2. package identity 可以进入正式 handoff 契约。
3. deployment identity 不能进入当前 Core / handoff 公共边界。

## Entry / Export Target 边界

未来 Native consumer 必须知道“这个 package 的执行入口是什么”，但这层信息不能继续隐含在：

1. 某个约定俗成的 workflow 名字
2. CLI 局部变量
3. runtime 侧再扫描一遍所有 declaration 的 ad-hoc 逻辑

V0.4 当前建议把它建模为：

- `entry target`
  - package 的默认执行入口
- `export targets`
  - package 对外暴露的可执行目标集合

当前最小边界建议只支持：

1. `workflow`
2. `agent`

不直接支持：

1. capability 作为 package entry
2. contract / flow 作为独立 package entry
3. 多态或条件化 entry 选择

### 为什么不放回 project descriptor

因为 project descriptor 的职责仍然是：

- 怎样得到编译输入

而不是：

- 编译结果如何作为 Native package 被消费

如果把 entry/export target 直接塞回 project descriptor，很容易把 project input 和 package/export 语义混成一层。

更稳妥的边界是：

1. project descriptor 继续描述编译输入
2. package metadata 描述 handoff 输出身份与入口
3. runtime deployment 层描述如何运行 / 绑定 / 发布

## Capability Binding Metadata 的最小边界

V0.4 只建议把 capability 的“稳定身份”和“绑定槽位”带进 handoff package，而不带入具体 connector 配置。

当前最小 capability metadata 应回答：

1. 这是哪个 capability declaration
2. 哪些 agent / flow / workflow 间接依赖它
3. Native runtime 需要为它保留哪个 binding slot

建议的最小形态类似：

```text
capability binding slot
  - capability canonical name
  - signature digest / stable type surface
  - required_by targets
  - optional binding key / alias
```

### 应保留什么

- capability canonical name
- 输入/输出类型签名
- 稳定 binding key 或 alias
- 被哪些 executable target 依赖

### 不应保留什么

- HTTP URL / RPC endpoint
- provider / model name
- auth / credential 配置
- timeout / retry / concurrency policy
- runtime adapter class 名称

换句话说：

- handoff package 只暴露“这里需要一个 capability binding slot”，不暴露“具体怎么连外部系统”。

## Package Metadata 与 Descriptor 的关系

V0.4 当前建议不要在 M0 就强行决定 package metadata 一定落在哪个文件里，但要先冻结职责拆分：

### 允许的方向

1. project descriptor 增加一个受限 `package` 区段
2. 新增独立 handoff/package descriptor
3. 由 handoff emission 在 CLI 中接受显式 package metadata 输入，再下沉到统一对象

### 不允许的方向

1. 在 workspace 中加入 deployment / binding 规则
2. 在 module loading 规则里夹带 package/export 信息
3. 在 CLI 里散落多个 runtime-facing metadata flag 而不下沉公共对象

因此，Issue 04 之前真正要冻结的是：

- metadata 属于哪一层

而不是：

- 最终 JSON 一定长什么样

## 对 CLI / Backend 的约束

若后续引入 handoff emission 命令，CLI 应只负责：

1. 选择 project input
2. 选择 package metadata 输入来源
3. 驱动统一 backend emission

CLI 不应负责：

1. 拼 capability binding graph
2. 推断 entry/export target 规则
3. 直接生成 runtime-facing JSON 字段

同样，backend / package emitter 必须基于 validate 后的统一边界工作，不得重新扫描 AST 猜：

1. 哪个 workflow 应导出
2. capability 绑定依赖关系
3. package identity 默认值

当前实现备注：

1. `emit-native-json` 已基于 `ahfl::handoff::lower_package(...)` 落地
2. 当前仓库尚未冻结 package metadata 的外部输入来源，因此 emitter 不会为 `package identity`、`entry target`、`export targets` 猜默认值
3. 在未显式提供 metadata 的情况下，当前输出会保留空 metadata 对象，并继续稳定导出 executable targets、capability binding slots 与 formal observations

## 当前建议的最小对象形状

V0.4 当前建议先围绕下面四个公共对象思考：

```text
PackageIdentity
  - name
  - version
  - format_version

EntryTarget
  - kind: workflow | agent
  - canonical_name

ExportTarget
  - kind: workflow | agent
  - canonical_name

CapabilityBindingSlot
  - capability_name
  - binding_key
  - required_by_targets
```

这里刻意不提前加入：

- endpoint
- provider
- auth
- env overlays
- rollout strategy

因为这些都属于 deployment/runtime 私有层，而不是 Core handoff 契约。

## 诊断分层

V0.4 若引入 package metadata 相关错误，建议仍保持分层：

### Metadata 错误

例如：

1. 缺失 package name
2. entry target 不存在
3. export target 重复
4. capability binding key 冲突

### Descriptor / Loader 错误

例如：

1. project descriptor 不合法
2. missing import
3. module ownership 冲突

### Checker / IR 错误

例如：

1. entry workflow 类型不合法
2. target 引用了不可导出的 declaration
3. capability surface 与类型签名不一致

## 对后续实现的约束

Issue 03 之后，V0.4 相关实现必须遵守：

1. package/export/binding metadata 不能与 deployment 配置混在一个公共契约里
2. project descriptor 仍不负责 host integration / deployment
3. capability binding metadata 只暴露稳定 binding slot，不暴露运行时私有 adapter 细节
4. entry/export target 必须能被 project-aware / workspace 路径稳定导出
5. 任何新增 metadata 字段都应先回答它是 project identity、package identity 还是 deployment identity

## 当前状态

本文冻结了 V0.4 在 capability binding、package identity 和 entry/export target 方向的最小 metadata 边界。下一步可以在不混入 deployment/runtime 细节的前提下，继续进入 handoff package 数据模型设计。
