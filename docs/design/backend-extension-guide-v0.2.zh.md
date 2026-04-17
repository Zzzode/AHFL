# AHFL Core V0.2 Backend Extension Guide

本文给出 AHFL Core V0.2 中新增 backend 的实现落地指南，面向需要在现有 `emit-ir` / `emit-ir-json` / `emit-smv` 之外增加新 backend，或扩展现有 backend 输出边界的工程实现者。

关联文档：

- [backend-capability-matrix-v0.3.zh.md](../reference/backend-capability-matrix-v0.3.zh.md)
- [native-consumer-matrix-v0.4.zh.md](../reference/native-consumer-matrix-v0.4.zh.md)
- [ir-backend-architecture-v0.2.zh.md](./ir-backend-architecture-v0.2.zh.md)
- [formal-backend-v0.2.zh.md](./formal-backend-v0.2.zh.md)
- [compiler-evolution-v0.2.zh.md](./compiler-evolution-v0.2.zh.md)
- [testing-strategy-v0.3.zh.md](./testing-strategy-v0.3.zh.md)
- [cli-pipeline-architecture-v0.2.zh.md](./cli-pipeline-architecture-v0.2.zh.md)

## 目标

本文主要回答五个问题：

1. 新增一个 backend 时，应该先改 IR，还是先改 emitter。
2. `BackendKind`、driver、CLI 和测试应该按什么顺序扩展。
3. 哪些能力应先冻结到 IR，哪些只能留在 backend 私有实现。
4. 如何判断一个 backend 是“保留语义”还是“抽象语义”。
5. 新 backend 的最小交付面应该包含哪些测试和文档。

## 当前 backend 分层

AHFL 当前 backend 相关代码分布在：

- `include/ahfl/backends/driver.hpp`
- `src/backends/driver.cpp`
- `include/ahfl/backends/smv.hpp`
- `src/backends/smv.cpp`
- `include/ahfl/ir/ir.hpp`
- `src/ir/ir.cpp`
- `src/ir/ir_json.cpp`
- `src/cli/ahflc.cpp`

当前已经存在四类 backend 路径：

- `Ir`
- `IrJson`
- `NativeJson`
- `Summary`
- `Smv`

这三者共享一个根前提：

- backend 只消费 validate 之后的稳定语义模型和 IR

其中：

1. `Summary`
   - 是当前仓库里的最小 compiler-facing 参考 backend
   - 用来验证 backend 扩展路径，而不是提供新的重语义执行边界
2. `NativeJson`
   - 是当前仓库里的 runtime-facing 参考 backend
   - 用来验证 handoff package 如何在不绕开 driver / CLI / docs / golden 的前提下形成正式 consumer 契约

## 新增 backend 的标准顺序

建议按下面顺序推进：

```text
确认语义边界
  -> 确认是否需要扩展 IR
  -> 实现 backend emitter
  -> 接入 driver
  -> 接入 CLI
  -> 增加 golden / fail-safe tests
  -> 补设计和 reference 文档
```

不要反过来先在 CLI 里加一个子命令，再回头猜 backend 需要哪些语义输入。

## 第一步：先定义 backend 边界

新增 backend 前，先明确它属于哪一类：

### 1. 保留语义 backend

例如：

- 尽量保留 declaration / expr / temporal 结构的后端

特点：

1. 对 IR 的结构依赖较强
2. 希望下游尽量看到原始语义形状
3. 往往应优先复用或扩展 IR 节点

### 2. 抽象语义 backend

例如：

- 为模型检查、静态分析或摘要消费而做的受限后端

特点：

1. 可能只保留部分语义
2. 可能引入 observation 或抽象状态
3. 必须在设计文档里显式写清楚“不承诺什么”

`emit-smv` 就属于第二类。

若新增的是 runtime-facing consumer 路径，还应再回答一个问题：

- 它是 Package Reader、Execution Planner，还是 Policy / Audit Consumer。

具体分类见：

- [native-consumer-matrix-v0.4.zh.md](../reference/native-consumer-matrix-v0.4.zh.md)

## 第二步：判断是否先扩展 IR

可以用一个简单规则：

1. 若该信息会被多个 backend 共享，应先进入 IR。
2. 若该信息只是某个 backend 的私有渲染技巧，可留在 emitter 内部。

### 应优先进入 IR 的典型内容

- 新 declaration / expr / statement / temporal 节点
- stable provenance 字段
- shared observation 模型
- 多 backend 都会消费的结构约束结果

### 不必强行进入 IR 的典型内容

- 某个 emitter 的格式化缩进
- 某个后端专用的局部符号命名缓存
- 仅用于文本排版的临时字符串

如果一个 backend 为了工作不得不重新回看 AST 或 parse tree，通常说明 IR 边界还不够。

## 第三步：实现 emitter，而不是重写主链

新 backend 的实现应尽量放在：

- `include/ahfl/backends/<name>.hpp`
- `src/backends/<name>.cpp`

其核心职责应是：

1. 消费 `ir::Program`
2. 输出特定目标格式

不该做：

1. 重新读取文件
2. 重新跑 resolve/typecheck/validate
3. 直接读取 parse tree

## 第四步：接入 driver

新增 backend 后，应按统一方式接入：

1. 扩展 `BackendKind`
2. 在 `emit_backend(BackendKind, const ir::Program &, ...)` 中新增分支
3. 继续复用已有的 AST/SourceGraph -> IR overload

这一步的设计目标是：

- 所有 backend 仍然共享统一分发入口

不要为了一个新 backend 绕过 `driver.cpp`，直接在 CLI 中调用专用实现。

## 第五步：接入 CLI

CLI 接入应尽量是薄的一层：

1. 增加 action flag
2. 更新 usage
3. 在 validate 成功后调用 `emit_backend(...)`

CLI 仍然不应理解：

- backend-specific 中间对象
- 某个 backend 的私有语义判定

如果新增 backend 需要 CLI 写一大段专属编排，通常说明 backend/driver 边界还没抽清。

## 新 backend 的最小测试面

按当前仓库策略，至少应补：

1. 一个成功的 golden 输出
2. 若 backend 共享 IR 新字段，再补对应 `.ir` / `.json` golden
3. 若 backend 有 project-aware 差异，再补一个 project-aware golden

### 为什么不只测 CLI 成功

因为 backend 是稳定输出边界，单纯断言命令“返回 0”并不能冻结：

1. 输出结构
2. 字段命名
3. 抽象边界是否漂移

## 对抽象语义 backend 的额外要求

若 backend 会抽象语义，例如：

- observation-backed export
- 状态压缩
- 值流省略

则必须补两类文档说明：

1. 保留哪些语义
2. 明确不保留哪些语义

否则下游很容易误以为该 backend 代表完整执行语义。

若 backend 面向 runtime-facing consumer，还必须额外补：

3. compatibility / versioning 文档
4. consumer matrix 中的落点与使用边界

## 常见错误模式

### 1. 先写 emitter，再补 IR

问题：

1. emitter 会开始直接依赖 AST 细节
2. 第二个 backend 很快复制同样逻辑

### 2. 在 CLI 中堆 backend 专用逻辑

问题：

1. CLI 变成实现层
2. project-aware 和单文件路径容易分叉失控

### 3. 把 backend 私有抽象偷偷塞成 IR 唯一语义

问题：

1. IR 被某个单一 backend 绑架
2. 其他 backend 失去更完整的结构信息

### 4. 只补文本输出，不补 JSON/IR 关联测试

问题：

1. 很难确认语义变化究竟发生在 IR 还是 emitter
2. golden 漂移时难以定位问题层次

## 新 backend 的最小交付清单

一个较完整的新 backend 交付，至少应包含：

1. emitter 实现
2. `BackendKind` 与 driver 接入
3. CLI 接入
4. 至少一个 golden
5. 若涉及共享边界变化，补 IR/JSON golden
6. 一篇 design 或 reference 文档

若是抽象语义 backend，通常还应补：

7. 单独的 boundary 文档

## 当前参考实现

若你需要一个最小可抄的 backend 扩展样例，当前应优先参考：

- `include/ahfl/backends/summary.hpp`
- `src/backends/summary.cpp`
- `include/ahfl/backends/native_json.hpp`
- `src/backends/native_json.cpp`
- `src/backends/driver.cpp`
- `src/cli/ahflc.cpp`
- `tests/summary/*`
- `tests/native/*`

它验证了：

1. 新 backend 如何只消费 `ir::Program`
2. 如何接入 `BackendKind`
3. 如何接入 CLI 子命令
4. 如何补 single-file 与 project-aware golden
5. 如何区分 compiler-facing 与 runtime-facing 两类 backend 扩展路径

各 backend 消费哪些 IR 能力，见：

- [backend-capability-matrix-v0.3.zh.md](../reference/backend-capability-matrix-v0.3.zh.md)

## 推荐阅读顺序

建议按下面顺序读：

1. `include/ahfl/backends/driver.hpp`
2. `src/backends/driver.cpp`
3. `include/ahfl/ir/ir.hpp`
4. `src/ir/ir.cpp`
5. `src/backends/smv.cpp`
6. `src/cli/ahflc.cpp`

## 对后续实现的约束

后续继续扩展 backend 体系时，应保持以下原则：

1. backend 始终建立在稳定 IR 之上，而不是直接建立在 AST 或 parse tree 之上。
2. 新 backend 统一经 `BackendKind` 和 driver 分发。
3. 共享语义先冻结到 IR，再由各 backend 消费。
4. 抽象语义必须文档化，不得只藏在 emitter 实现里。
5. backend 变更应伴随 golden 和设计文档，而不只是代码实现。
