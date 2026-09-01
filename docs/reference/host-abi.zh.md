# AHFL Host Embedding ABI (`ahfl_host.h`)

本文是 [RFC 0021](../rfcs/0021-capability-embedding-abi.zh.md)（Capability Embedding ABI，`implemented`）定义的宿主嵌入 C ABI 的参考文档，配套 [RFC 0022](../rfcs/0022-durable-capability-resume.zh.md)（Durable Capability Resume，`stabilized`）的挂起/恢复语义。规范头文件是 `include/ahfl/runtime/ahfl_host.h`；本文解释契约，头文件是唯一权威。

## 定位

AHFL 是一门**可嵌入、可验证的 agent workflow 编排 DSL**（[RFC 0020](../rfcs/0020-strategic-positioning-embeddable-workflow-dsl.zh.md)）。它只编排 workflow 的结构与行为；**通用计算由宿主经 capability 边界提供**。`ahfl_host.h` 是这条边界的**唯一、语言无关的 C 契约**：宿主可用任意语言（C++/Rust/Go/Java/Node/浏览器 JS）实现它。原生绑定（C++ 函数指针表）与 WASM 绑定（[RFC 0019](../rfcs/0019-wasm-runtime-model.zh.md) 的 `ahfl_cap` import）都从**同一个头文件**派生——一份契约，两种投影。

语言层的 capability 调用**始终是同步观感**（调用 → 得结果 → 继续）；异步性完全由宿主经 `AHFL_CAP_PENDING` 吸收，语言永不引入 `async` / `await`。

## ABI 稳定性规则（不可协商——这是已发布的 FFI 边界）

1. **size-prefixed 结构体**：每个跨边界的多字段结构体以 `uint32_t struct_size`（调用方填 `sizeof`）开头。字段**只能在末尾追加**，已发布字段永不移动或改用途；reader 用 `struct_size` 判断哪些字段存在。
2. **定宽整型**：只用 `stdint.h` 定宽类型；无 bitfield；不把 enum 用作结构体/返回类型（宽度实现定义）——enum 仅作命名常量，ABI 类型永远是 `uint32_t`；无 `#pragma pack`，自然对齐。
3. **符号 append-only**：改签名靠新增 `ahfl_invoke2`，绝不修改 `ahfl_invoke`。
4. **帧字节序钉死**：wire frame 是长度前缀的小端序；字节序现在即固定。

## 版本与 wire format（两条独立轴）

| 符号 | 含义 |
|---|---|
| `AHFL_ABI_VERSION`（宏，当前 `1`） | 结构布局 / 符号契约版本。仅在**破坏性** ABI 变更时 bump。 |
| `uint32_t ahfl_abi_version(void)` | 返回 runtime 链接的 `AHFL_ABI_VERSION`；宿主与 runtime 必须就主契约版本达成一致。 |
| `ahfl_wire_format`（`uint32_t`） | 帧序列化格式，在**连接级**协商（一个连接一种格式，绝不逐帧打 tag）。`AHFL_WIRE_VALUE_JSON = 1`。核心保留 `0x0001..0x0FFF`，`0x1000+` 留给宿主。 |

结构/符号契约与帧格式**独立演进**——一个变了不强制另一个变。

## 能力调用状态：定宽、fail-closed

`ahfl_cap_status` 是 `uint32_t`（不是裸 enum）。核心保留 `0x0001..0x0FFF`，`0x1000+` 留给宿主。**规则（fail-closed）：runtime 不认识的任何状态码都必须当作 `AHFL_CAP_ERROR`。**

| 状态 | 值 | 语义 | 后置条件 |
|---|---|---|---|
| `AHFL_CAP_OK` | 0 | 成功 | callee 用 `ahfl_alloc(host,…)` 分配结果帧，写 `*result_ptr` / `*result_len`；caller 解码后经 `ahfl_dealloc` **恰好释放一次**，释放后不再读。 |
| `AHFL_CAP_ERROR` | 1 | 能力失败 | callee 必须置 `*result_ptr = NULL`、`*result_len = 0`；caller 不得释放。workflow **fail-closed 终止并传播**，绝不静默吞。 |
| `AHFL_CAP_PENDING` | 2 | 异步：workflow 在当前节点挂起 | callee 置 `*result_ptr = NULL`；参数帧所有权在挂起生命周期内转移给宿主。宿主稍后用结果恢复 workflow（见下"durable resume"）。 |

## 不透明宿主句柄

```c
typedef struct ahfl_host ahfl_host;
```

每个入口点都带 `ahfl_host *host`，让 runtime 携带 per-connection / 多实例 / 线程状态而不需要未来破坏签名。宿主定义具体类型，runtime 视之为不透明。

## 分配器:单一所有者规则

```c
uint8_t *ahfl_alloc(ahfl_host *host, uint32_t len);
void     ahfl_dealloc(ahfl_host *host, uint8_t *ptr, uint32_t len);
```

两者都带 `host`,保证一个帧总由**造它的同一个分配器**释放(不跨分配器 free)。WASM 绑定把它们投影到模块导出的 alloc/dealloc([RFC 0019](../rfcs/0019-wasm-runtime-model.zh.md));原生绑定里它们是 runtime 在同一地址空间上的帧分配器。

## 能力调用

```c
typedef struct ahfl_invoke_args {
    uint32_t       struct_size;   /* caller 填 = sizeof(ahfl_invoke_args)  */
    uint32_t       cap_id;        /* capability SymbolId(索引式身份)     */
    const uint8_t *args_ptr;      /* caller 拥有的序列化参数帧             */
    uint32_t       args_len;
    uint8_t      **result_ptr;    /* out: callee 分配的结果帧,或 NULL     */
    uint32_t      *result_len;    /* out: 结果帧长度,或 0                 */
    /* 未来字段只在此末尾追加(timeout / trace id / resume handle / …)。 */
} ahfl_invoke_args;

ahfl_cap_status ahfl_invoke(ahfl_host *host, ahfl_invoke_args *args);
```

runtime(caller)拥有参数帧;能力(callee)产出结果帧。参数帧 caller-owned,callee 不得在返回后保留——**唯一例外是 `AHFL_CAP_PENDING`**(所有权转移给宿主直到恢复)。

`cap_id` 是 capability 的 `SymbolId`(索引式身份,非名字),延续 [RFC 0019](../rfcs/0019-wasm-runtime-model.zh.md) 的 SymbolId ↔ import 名映射。

## Durable resume(挂起 / 恢复,RFC 0022)

当能力返回 `AHFL_CAP_PENDING`,runtime 把 workflow 挂起为一份**纯可序列化的 resume record**(节点 input + 该节点已完成能力调用的 memo 表,schema `ahfl.workflow-recovery.v2`)。进程可退出;宿主稍后用该调用的结果恢复。恢复的语义契约(确定性重放、memo 命中不二次调用、exactly-once、fail-closed 类型校验)见 [`docs/spec/core-language.zh.md` §3.4.1](../spec/core-language.zh.md) 与 [RFC 0022](../rfcs/0022-durable-capability-resume.zh.md)。

- **原生 / server WASM**:宿主内部 block 等待,直接返回 `AHFL_CAP_OK`,通常不用 `PENDING`。
- **浏览器**:JSPI 或 Asyncify;能力返回 `PENDING`,微任务里用结果恢复。
- **serverless**:能力返回 `PENDING`,函数返回;后续事件(webhook / 回调)携带结果恢复。

recovery schema v2 是 **stable-artifact**:已持久化的 v2 resume record 在后续版本仍可加载;schema 演进 append-only(新字段可选、老 reader 忽略),破坏性变更须发新版本号。

## 一致性保证

同一个带 capability 的 workflow,经原生绑定与经"直连 invoker 返回同值"在 L1(workflow status + 确定性 `value_json` 逐字节输出)与 L2(capability 调用序列)上**可观察等价**——由 `tests/unit/runtime/engine/native_wasm_differential.cpp` 验证。`tests/integration/durable_resume_capstone.cpp` 验证的是**同一进程内**两个 fresh `WorkflowRuntime` scope 的 suspend → 落盘快照 → 重新加载 → 确定恢复(非跨进程)。真正的**跨进程冷启动 + 崩溃(SIGKILL)** 证据来自 `tests/scripts/reference_workflow_recovery_smoke.py` + `tests/integration/reference_workflow_recovery_worker.cpp`。以上全部为**原生 evaluator** 证据,均不执行真实 WASM 模块。

> **范围说明**:仓内现在能 emit Core Wasm 二进制(`core_wasm_codegen`),并有 Node / reference-host(module-byte inspector)证据;但仍无 production WASM 运行时 / generic embedder([RFC 0019](../rfcs/0019-wasm-runtime-model.zh.md) / [RFC 0021](../rfcs/0021-capability-embedding-abi.zh.md))。故本节的差分与 capstone 是"原生绑定 vs 直连 invoker" / 原生 suspend-resume,**不能**冒充执行中的 WASM 模块的 durable resume。各语言宿主 SDK(Rust/Go/Node)与完整 production Wasm host / durable resume 是 RFC 0021 / RFC 0026 E4-B2 声明的后续工作,不在本契约的当前投影内。
