# AHFL Host Embedding ABI (`ahfl_host.h`)

宿主通过 `ahfl_host.h` 为 AHFL workflow 提供 capability。本页说明 C ABI（Application Binary Interface，应用二进制接口）的当前结构、帧内存所有权、调用状态和挂起约束。接口定义以 `include/ahfl/runtime/ahfl_host.h` 为准。

## 当前结构与帧契约

本 header 定义当前 C host 契约；仓库尚未提供独立的 C `ahfl_invoke` 实现。
Wasm 执行器及 native runtime 使用各自已实现的入口。

结构以 `struct_size` 开头，调用方填写精确 `sizeof`。不支持历史结构、追加字段
读取或版本协商。字段使用定宽整型与自然对齐。帧使用当前 canonical wire JSON；
数值 capability ID 表达身份。

## 能力调用状态：定宽、fail-closed

`ahfl_cap_status` 是 `uint32_t`（不是裸 enum）。核心保留 `0x0001..0x0FFF`，`0x1000+` 留给宿主。**规则（fail-closed）：runtime 不认识的任何状态码都必须当作 `AHFL_CAP_ERROR`。**

| 状态 | 值 | 语义 | 后置条件 |
|---|---|---|---|
| `AHFL_CAP_OK` | 0 | 成功 | callee 用 `ahfl_alloc(host,…)` 分配结果帧，写 `*result_ptr` / `*result_len`；caller 解码后经 `ahfl_dealloc` **恰好释放一次**，释放后不再读。 |
| `AHFL_CAP_ERROR` | 1 | 能力失败 | callee 必须置 `*result_ptr = NULL`、`*result_len = 0`；caller 不得释放。workflow **fail-closed 终止并传播**，绝不静默吞。 |
| `AHFL_CAP_PENDING` | 2 | 异步：workflow 在当前节点挂起 | callee 置 `*result_ptr = NULL`；参数帧所有权在挂起生命周期内转移给宿主。宿主稍后用结果恢复 workflow（见下文“挂起与恢复”）。 |

## 不透明宿主句柄

```c
typedef struct ahfl_host ahfl_host;
```

每个入口点都带 `ahfl_host *host`，让 runtime 携带 per-connection / 多实例 / 线程状态。宿主定义具体类型，runtime 视之为不透明。

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
} ahfl_invoke_args;

ahfl_cap_status ahfl_invoke(ahfl_host *host, ahfl_invoke_args *args);
```

runtime(caller)拥有参数帧;能力(callee)产出结果帧。参数帧 caller-owned,callee 不得在返回后保留——**唯一例外是 `AHFL_CAP_PENDING`**(所有权转移给宿主直到恢复)。

`cap_id` 是 capability 的 `SymbolId`(索引式身份,非名字),延续 [RFC 0019](../rfcs/0019-wasm-runtime-model.zh.md) 的 SymbolId ↔ import 名映射。

## 挂起与恢复

当能力返回 `AHFL_CAP_PENDING`,runtime 把 workflow 挂起为一份**纯可序列化的 resume record**(节点 input + 挂起点之前整个 workflow 的能力调用 memo 表,format `ahfl.workflow-recovery`)。进程可退出;宿主稍后用该调用的结果恢复。恢复的语义约束(确定性重放、memo 命中不二次调用和类型校验)见 [`docs/spec/core-language.zh.md` §3.4.1](../spec/core-language.zh.md) 与 [RFC 0022](../rfcs/0022-durable-capability-resume.zh.md)。

同步能力完成后返回 `AHFL_CAP_OK`。需要异步恢复的能力返回 `AHFL_CAP_PENDING`，宿主负责保存恢复记录、等待外部结果并通过所用 runtime 的恢复接口继续执行。接入前应验证该绑定是否支持完整的持久化与恢复流程。

恢复记录只接受当前完整格式；格式变化时同步更新所有生产者、消费者与测试，旧记录须重新生成。

## 恢复与外部副作用的限制

workflow 执行编译后的 Core Wasm 模块。挂起恢复会在新实例中从头重放模块，已记录能力调用的结果由 memo（已完成调用结果表）提供，避免再次调用这些能力。

持久化记录与宿主外部系统中的副作用不是同一个事务。需要跨进程恢复时，宿主还必须保证快照持久化、结果完整性、写入幂等与重放安全；不要把 memo 命中等同于生产系统端到端 exactly-once（恰好一次）保证。生产部署还需验证所用存储、密钥管理和语言绑定的恢复行为。
