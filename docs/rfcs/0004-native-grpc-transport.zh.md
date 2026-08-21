---
rfc: "0004"
title: "Native gRPC Transport"
status: "draft"
area: ["runtime"]
stability: "experimental"
created: "2026-06-28"
updated: "2026-07-09"
authors: ["LLM-orchestrated"]
shepherd: "TBD"
owners:
  runtime: "TBD"
required_reviewers: ["runtime"]
tracking_issue: "TBD"
discussion: "TBD"
implementation_prs: []
decision_due: "2026-07-12"
---

# RFC 0004: Native gRPC Transport

## Summary

Decide whether AHFL should add a native gRPC/Protobuf transport for runtime and LLM capability calls. The default decision posture is not to implement native gRPC until a runtime owner decision, optimized HTTP/2 JSON baseline, benchmark evidence, three-platform build evidence, dependency policy, feature flag plan, fallback semantics, and test strategy all exist.

## Motivation

HTTP/JSON remains simple and portable, and it is still the dominant public LLM API surface. Long streaming responses and multi-turn tool-calling loops may still benefit from lower latency, lower CPU overhead, and first-class bidirectional streaming, but native gRPC is only justified if it beats an optimized HTTP/2 JSON/SSE baseline and if supported providers expose real native gRPC endpoints.

## Current Implementation Audit

As of 2026-07-09, AHFL has runtime support and tests for `grpc_json_transcoding` capability bindings, not native gRPC/Protobuf transport. The concrete implementation surface is `GrpcJsonTranscodingEndpoint`, `GrpcJsonTranscodingRequest`, and `execute_grpc_json_transcoding`; `GrpcJsonTranscodingRequest` still carries a JSON body, and `wire_transport_adapter.cpp` lowers that request to HTTP/2 or h2c through the existing libcurl-backed HTTP seam. This is not a `grpcpp` client.

The current `ProviderRegistry` is also not a transport scheme router. It selects providers by priority and availability status; it does not dispatch `grpc://` to a native facade. Any future native implementation must add a dedicated transport selection seam instead of overloading provider selection.

This evidence does not satisfy this RFC's acceptance bar because it does not add a versioned Protobuf schema contract, a native HTTP/2 gRPC C++ client facade, benchmark evidence, or the required three-platform native gRPC build matrix. RFC 0004 therefore remains `draft` until the owner decision gate in [Native gRPC Decision Gate](../plans/native-grpc-decision-gate.zh.md) is completed. The repository now enforces this boundary with `scripts/check-native-grpc-gate.py` and [native-grpc-decision-evidence.json](../plans/native-grpc-decision-evidence.json): `accepted` requires a signed owner Go decision, `implementing` requires complete evidence for all native transport gates, and CI rejects native implementation markers until those status-specific evidence contracts are satisfied.

## Goals

1. Define the benchmark and build-cost evidence required for a go/no-go decision.
2. Preserve the current HTTP/JSON behavior when native gRPC is disabled.
3. Specify a runtime-facing transport facade that does not expose gRPC internals to language users.
4. Define fallback semantics and diagnostics.

## Non-Goals

1. Do not implement an AHFL gRPC server.
2. Do not add SDKs for other languages.
3. Do not replace the existing HTTP/JSON transport.

## Design

Native gRPC, if approved later, must be an optional runtime transport behind an explicit build/runtime gate. A future transport-selection layer, not the current `ProviderRegistry`, may route `grpc://` bindings to a native facade only when the feature is enabled and the binding is explicitly supported. The default failure mode is fail-closed with actionable diagnostics; automatic fallback is only allowed for cases whose semantic equivalence is proven by tests.

## User Impact

Users may opt into `grpc://` capability bindings for supported providers. Default behavior remains unchanged while the RFC is draft or experimental.

## Compatibility and Migration

Existing `http://`, `https://`, and JSON-transcoded gRPC bindings must continue to behave the same. Any deprecation of old aliases requires a release-note and migration plan.

## Implementation Plan

Run benchmarks first, decide go/no-go, then, only if approved, add optional dependency wiring, proto contracts, a C++ facade, a dedicated transport-selection seam, fail-closed diagnostics, explicit fallback tests, and runtime tests. Native gRPC implementation markers must not enter the repository while this RFC remains `draft`.

## Test Plan

Add transport unit tests, fail-closed/fallback integration tests, transport-selection tests, build-matrix checks, and benchmark evidence before moving beyond draft. The benchmark must compare the existing HTTP/JSON path, an optimized libcurl-multi HTTP/2 JSON/SSE path, current `grpc_json_transcoding`, and native `grpc-cpp + Protobuf`.

## Rollout and Stabilization

Keep native gRPC off by default until benchmark, platform, and fallback evidence exists. Stabilization requires multi-platform CI and documented migration guidance.

## Alternatives

1. Keep only HTTP/JSON and optimize serialization.
2. Use gRPC JSON transcoding as the permanent transport.
3. Use HTTP/3 or QUIC instead of gRPC.
4. Use a lower-level binary RPC format such as Cap'n Proto or FlatBuffers.

## Open Questions

1. Whether expected latency gains justify gRPC C++ build cost.
2. Which providers can expose native gRPC endpoints without custom gateways.

## Decision History

- 2026-06-28: Initial draft created during Wave 18 planning.
- 2026-07-02: Canonicalized as RFC 0004.
- 2026-07-08: Added a machine gate (`scripts/check-native-grpc-gate.py`) to prevent native gRPC implementation markers from entering the repository before the owner Go/No-Go decision, benchmark evidence, build matrix, dependency policy, feature flag, and fallback semantics are complete.
- 2026-07-08: Added `ahfl.native_grpc_decision_evidence.v1` as the machine-readable evidence contract for RFC status transitions; `accepted` now requires owner Go evidence, while `implementing` and later statuses require all native transport gates to be complete.
- 2026-07-09: Reconciled the RFC with live code and industry practice: current `grpc_json_transcoding` is explicitly not native gRPC, current `ProviderRegistry` is not a transport scheme router, optimized HTTP/2 JSON/SSE is now a required benchmark baseline, Protobuf schema guidance no longer requires field-level JSON mirroring, fallback defaults to fail-closed, and native implementation remains forbidden while the RFC is draft.

## Detailed Design Notes

> 中文翻译紧随对应英文段落，逐段对齐（English-first with Chinese counterparts）。

---

## 1 Metadata

| Field | Value |
|---|---|
| RFC-ID | 0004 |
| Title | Runtime/LLM gRPC transport — go/no-go decision + design skeleton |
| Status | DRAFT |
| Created | 2026-06-28 |
| Shepherd | TBD |
| Consult | NONE |
| Decision Due | 2026-07-12 |
| Blocking Items | runtime owner decision；QE 组真实 benchmark 数据；优化 HTTP/2 JSON/SSE baseline；三平台构建与依赖策略证据 |
| Author | LLM-orchestrated |
| Shepherds | TBD |
| PR | TBD |
| Implementation Issue | TBD |
| Depends On | NONE（与现有 HTTP + gRPC JSON transcoding transport 并列，不阻塞任何功能） |

**中文（对照）：**

| 字段 | 值 |
|---|---|
| RFC 编号 | 0004 |
| 标题 | Runtime/LLM gRPC 传输层：go/no-go 决策 + 设计骨架 |
| 状态 | DRAFT |
| 创建日期 | 2026-06-28 |
| 作者 | LLM 编排生成 |
| 监督人（Shepherd） | 待定 |
| 关联 PR | 待定 |
| 实现 Issue | 待定 |
| 依赖 RFC | 无。与现有 HTTP 与 gRPC JSON transcoding transport 并列存在，不阻塞其他模块。 |

---

## 2 Motivation & Scope（动机与范围）

### 2.1 Background — Why Now（背景：为什么现在做）

AHFL 的 runtime / LLM provider 子系统（`src/runtime/providers/llm/` 与 `src/runtime/engine/`）当前对外通信走 HTTP/JSON 语义，能力桥接路径已经能使用 HTTP/2 / h2c 承载 JSON transcoding，但没有原生 Protobuf 编解码或 `grpcpp` client。具体体现为两条 seam：

1. **LLM provider**（OpenAI-compatible API）：`HttpClient` / `StreamingClient` 走 `aiohttp`/`requests` 风格的 HTTP + JSON，由 `src/runtime/providers/llm/http_client.cpp:31` 定义的 `ChatCompletionsTransport` 注入。
2. **Capability bridge**：`HttpTransport` 与 `GrpcTransport` 共用 wire adapter 层，其中 `GrpcTransport` 当前实为 **gRPC JSON transcoding**（将 HTTP/JSON 映射到 gRPC-shaped service path，而非原生 Protobuf 编解码 + `grpcpp` client）。`GrpcJsonTranscodingRequest` 携带 JSON body，`wire_transport_adapter.cpp` 将其转成 HTTP POST，并设置 HTTP/2 / h2c 与 trailer capture。

**Why now**：
- Wave-16 项目状态已将 "native gRPC / Protobuf transport 仍需取舍" 明确列为 BLOCKED 决策项；决策门槛现由 `docs/plans/native-grpc-decision-gate.zh.md` 承载（机器检查由 `scripts/check-native-grpc-gate.py` 执行）。
- SSE 风格 token streaming（长响应、chunk 级回调、head-of-line blocking）与 multi-turn tool calling（来回小消息、低延迟往返）两种场景对连接复用、HTTP/2 multiplexing、JSON 反序列化 CPU 和 streaming lifecycle 语义敏感，已有 profiling 占位（`docs/plans/issue-backlog-global-gaps.zh.md:30`）。
- 现有 `GrpcTransport` 已打通 HTTP → gRPC JSON transcoding seam，为原生 Protobuf 提供了错误分类、auth、timeout、retry 与 metadata/trailer 传播的参考骨架；但它不能直接算 native gRPC，也不能替代 native gRPC 的 benchmark、schema、build matrix 或 lifecycle 证据。

### 2.2 Decision This RFC Must Produce（本 RFC 必须给出的决策）

本 RFC 交付物 **不是** 直接宣布 go，而是：

1. **Go/No-Go 决策**：基于可量化基准（§3.1）与三平台构建成本（§3.2），在 DRAFT → ACCEPTED 转态时由 Shepherds 签字。
2. **若 Go，则给出接口设计骨架**：proto 定义 + C++ client facade + transport selection seam + fail-closed / explicit fallback 策略（§3.3 / §3.4）。
3. **若 No-Go**：在 §2 首行追加 `[STATUS → OUT-OF-SCOPE @ YYYY-MM-DD]`，并给出 1.0 之后再评估的触发条件（例如某供应商原生 gRPC API 公开或单场景 P99 延迟不可接受）。

### 2.3 In-Scope / Out-of-Scope（适用范围）

- **In-Scope**：AHFL runtime 作为 **gRPC client** 调用外部 LLM inference / capability 服务（原生 Protobuf 二进制 payload、HTTP/2、server-streaming、bidi-streaming）；跨 Win / macOS / Linux（x86_64 + arm64）的 opt-in 构建；feature-flag 关闭时行为 100% 与现版本一致；仅在语义等价、幂等性和显式配置均成立时允许 fallback 到 HTTP JSON / JSON transcoding。
- **Out-of-Scope**：AHFL 作为 gRPC server；Rust / Go / Python 侧 SDK；gRPC authz / 签名校验（Phase 2）；多 region 调度、负载均衡策略细化。

### 2.4 Typical Use Cases（典型使用场景）

以下 `ahfl` 代码块展示未来若 Go 则用户侧可见的 3 类典型调用，**全部与现有能力一一对应**——差异仅在底层 transport。

**Use Case 1：Unary chat completion（单请求大 payload / 小响应）**

```ahfl
capability summarize
  binding: grpc://llm-infra.internal/v1.LLMInference/Chat
  timeout: 30s
  retries: 2

fn exec_summary(doc: str) -> str =
  summarize({ model: "sonnet-4", prompt: doc, max_tokens: 512 }).text
```

*对应现有 HTTP JSON binding 的等价迁移：`http+json://...` → `grpc://...`，其他字段不变。*

**Use Case 2：Server streaming（SSE → gRPC server-stream）**

```ahfl
capability stream_chat
  binding: grpc://llm-infra.internal/v1.StreamingInference/ChatStream
  streaming: server
  chunk_field: delta

fn live_translate(input: str) -> generator<str> =
  stream_chat({ model: "haiku-4", prompt: input, stream: true }).chunks
```

*现有 `StreamingClient`/`SSEParser`（`src/runtime/providers/llm/streaming.hpp:17`）在该 binding 下改为消费 `StreamChunk` proto message；`chunk_field` 与 SSE 的 `data:` 字段语义对齐，`StreamChunkCallback` 签名不变。*

**Use Case 3：Bidirectional tool calling（多轮小消息来回）**

```ahfl
capability agent_loop
  binding: grpc://llm-infra.internal/v1.ToolCalling/BidiAgent
  streaming: bidi
  tools: [search_kb, run_sql, fetch_url]

fn answer(q: str) -> str =
  let loop = agent_loop({ system_prompt: sys, tools: tools_catalog }) in
  loop.send(UserMessage(q));        // client → server message
  for msg in loop.recv() do {       // server → client（多轮 interleaved）
    match msg {
      ToolCall(id, fn, args) => loop.send(ToolResult(id, exec_tool(fn, args))),
      FinalAnswer(t)               => return t,
    }
  }
```

*对应 `tool_calling.hpp`（`src/runtime/providers/llm/tool_calling.hpp:1`）的扩展：将当前 "同步 `invoke + parse`" 模式扩展为 "bidi message stream"，但 `ToolCall` / `ToolResult` 数据结构不变。*

---

## 3 Design（核心设计）

### 3.1 Benchmark — Baseline Measurements（基准测量）

**前置条件（Go/No-Go 的硬门槛）：** 在 3 条固定 workload 上比较 4 条 transport path，做 P50/P95/P99 latency、throughput、CPU time、peak RSS、serialized payload bytes 和 channel / connection warmup 记录；环境采用 CI runner（Linux x86_64）+ 本机 macOS arm64，样本数 N=200，预热 20 次。测试必须使用固定 mock server、固定输入、固定输出 schema，不得依赖公网 provider 延迟。

| Evidence workload | Payload 特征 | 必测 transport path | Native gRPC Go 阈值 |
|---|---|---|---|
| `small_unary` | request/response 均 < 2 KiB，代表普通 capability call | (1) `http_json`：当前 HTTP/JSON path；(2) `http2_json_optimized`：libcurl-multi + HTTP/2 JSON/SSE optimized baseline；(3) `grpc_json_transcoding`：当前 gRPC JSON transcoding path；(4) `native_grpc`：native `grpc-cpp + Protobuf` | native 相对最佳 JSON/HTTP2 baseline 的 P95 延迟降幅 ≥ 15%，且 CPU time 降幅 ≥ 20%；否则该场景不计入 Go |
| `large_structured_response` | request 64 KiB，大结构化 response，验证 JSON 中间对象成本 | 同上 | native 相对最佳 JSON/HTTP2 baseline 的 P95 延迟降幅 ≥ 25% 或 CPU time / peak RSS 任一降幅 ≥ 30%；否则该场景不计入 Go |
| `high_concurrency` | 10 轮小消息、多并发、多 stream；模拟 tool-call round trip | 同上；streaming/bidi 语义必须单独记录是否等价 | native 相对最佳 JSON/HTTP2 baseline 的总体 P50 降幅 ≥ 30%，P99 降幅 ≥ 25%，且无 message-order / cancel / deadline 语义回退；否则该场景不计入 Go |

**Go/No-Go 判定规则（§7 再量化）：**
- **Go = 3 条 workload 中至少 2 条相对最佳 JSON/HTTP2 baseline 同时通过阈值，且 §3.2 三平台构建全部通过。**
- **Conditional Go = 仅企业内网 / 自托管 / 明确暴露 native gRPC endpoint 的 provider 可用，公开 OpenAI-compatible provider 继续走 HTTP/JSON 或 JSON transcoding。**
- **No-Go = 其余所有情况。** 仍保留当前 HTTP JSON / HTTP2 JSON transcoding seam 为唯一实现路线，并优先推进 libcurl-multi + HTTP/2 JSON/SSE 优化。

### 3.2 Dependency Evaluation（依赖评估）

| 依赖 | Linux x86_64 | macOS (Intel / arm64) | Windows MSVC | 体积 / 构建时长增量（CI） |
|---|---|---|---|---|
| `protobuf-compiler` ≥ 25.x（`protoc`） | system package / vcpkg / Conan / pinned CI cache | `brew install protobuf` / vcpkg / Conan / pinned CI cache | vcpkg / Conan / pinned CI cache | 源码构建只能作为受控 CI 备选，必须记录冷构建时间与 cache key |
| `gRPC-cpp` ≥ 1.60 | system package / vcpkg / Conan / pinned CI cache | 同上；arm64 需显式 arch evidence | 同上；MSVC runtime 与 OpenSSL/BoringSSL 组合必须单独记录 | 不允许默认 configure 下载；FetchContent 只能作为 opt-in 受控 CI 路径，必须 pin 版本、license review、cache key |
| OpenSSL / BoringSSL | 复用现有 `libcrypto`（HTTP mTLS 已引入） | 复用 Security.framework 或 BoringSSL 静态链 | 复用 SChannel 或 BoringSSL | 无新增依赖（mTLS 已在 §2 baseline） |

**构建成本底线：**
- 启用开关：`AHFL_ENABLE_GRPC_NATIVE=OFF`（**默认 OFF**）；只有 opt-in preset 才允许查找 `gRPC::grpc++` / `protobuf::libprotobuf`。
- 默认 CMake configure 不得 FetchContent gRPC，不得要求本地开发者安装 gRPC。
- CI 必跑三平台 + 两架构，构建 artifact 缓存按 `grpc-vX.Y.Z-<os>-<arch>-<compiler>` 维度 key，冷构建不超过现有 CI 时长 20%。若任一平台 **连续 2 次 nightly 构建失败**，该平台默认关闭，Go 决策降级为"仅支持两平台"或整体 No-Go，由 Shepherds 在 wave 例会决定。

### 3.3 Interface Skeleton（接口骨架）

**3.3.1 Proto 定义（`proto/ahfl/runtime/v1/llm_transport.proto`）**

```proto3
syntax = "proto3";

package ahfl.runtime.v1;

import "google/protobuf/struct.proto";

// 通用：proto 是版本化 wire contract；JSON 只是 compatibility mapping。
// 语义必须可逆并有测试证明，但不要求字段级 1:1 复制 JSON。
message LlmMessage { Role role = 1; string content = 2; repeated ToolCall tool_calls = 3; }
enum Role { ROLE_UNSPECIFIED = 0; SYSTEM = 1; USER = 2; ASSISTANT = 3; TOOL = 4; }
message ToolCall {
  string id = 1;
  string name = 2;
  oneof args {
    AhflWireValue ahfl_value = 3;
    google.protobuf.Struct provider_json = 4; // compatibility escape hatch only
  }
}
message ToolResult {
  string call_id = 1;
  oneof result {
    AhflWireValue ahfl_value = 2;
    google.protobuf.Struct provider_json = 3; // compatibility escape hatch only
  }
  string error = 4;
}
message AhflWireValue { bytes canonical_payload = 1; string schema = 2; }

// Service 1 — Unary inference
service LLMInference {
  rpc Chat(ChatRequest) returns (ChatResponse);
}
message ChatRequest { string model = 1; repeated LlmMessage messages = 2; uint32 max_tokens = 3; float temperature = 4; }
message ChatResponse { LlmMessage message = 1; Usage usage = 2; }
message Usage { uint32 prompt_tokens = 1; uint32 completion_tokens = 2; }

// Service 2 — Server streaming inference
service StreamingInference {
  rpc ChatStream(ChatRequest) returns (stream StreamChunk);
}
message StreamChunk {
  oneof event { DeltaChunk delta = 1; Usage final_usage = 2; FinishReason finish = 3; };
}
message DeltaChunk { uint32 index = 1; string content_delta = 2; ToolCall tool_call_delta = 3; }
message FinishReason { enum Kind { NONE = 0; STOP = 1; LENGTH = 2; TOOL_CALLS = 3; }; Kind kind = 1; }

// Service 3 — Bidi tool calling
service ToolCalling {
  rpc BidiAgent(stream AgentClientMsg) returns (stream AgentServerMsg);
}
message AgentClientMsg {
  oneof msg { LlmMessage user_or_tool = 1; Cancel cancel = 2; Heartbeat heartbeat = 3; };
}
message AgentServerMsg {
  oneof msg { LlmMessage assistant = 1; ToolCall tool_call = 2; FinalAnswer final = 3; Error error = 4; Heartbeat heartbeat = 5; };
}
message Cancel { string reason = 1; }
message Heartbeat { uint64 monotonic_ms = 1; }
message Error { uint32 code = 1; string message = 2; string taxonomy = 3; }
message FinalAnswer { string text = 1; Usage total_usage = 2; }
```

**Proto governance：**
- 不复用 tag；删除字段必须 `reserved` tag 和 name。
- enum 第 0 项必须是 `*_UNSPECIFIED`，新增 response enum value 需要 documented unknown handling。
- 不新增 required field；不改变已有字段类型；不移动已发布 message / service 到其他 `.proto` 文件。
- 生成代码不得作为 AHFL 手写 API 暴露面；runtime 只暴露 facade 和 AHFL 自己的 typed value / error taxonomy。
- 如果引入 Buf 或等价工具，必须在 CI 中运行 format、lint、breaking-change check；否则 `protoc` 版本和 plugin 版本必须由仓库内证据锁定。

**3.3.2 C++ Facade（Client Layer，不直接暴露 `grpc::CompletionQueue`）**

```cpp
// src/runtime/providers/llm/grpc_client_facade.hpp
namespace ahfl::llm_provider {

class GrpcClientFacade {
  public:
    // 与 HttpClient::chat_completions 同签名，只是 body 由 proto ⇄ JSON 转换；
    // 便于 provider 层通过 Transport 接口二选一，调用端无感。
    [[nodiscard]] auto chat_completions(const ChatRequestProto&)
        -> tl::expected<ChatResponseProto, LlmProviderError>;

    // 与 StreamingClient 同回调签名，StreamChunkCallback 可复用。
    void chat_stream(const ChatRequestProto&, StreamChunkCallback cb);

    // Bidi：对应 Use Case 3，暴露 Rx/Tx 两端；实现优先使用 gRPC C++ callback API。
    struct BidiAgent {
        void send(const AgentClientMsgProto&);
        [[nodiscard]] auto recv()
            -> tl::expected<AgentServerMsgProto, LlmProviderError>;
        void cancel();
    };
    [[nodiscard]] auto bidi_agent() -> tl::expected<std::unique_ptr<BidiAgent>,
                                                    LlmProviderError>;

  private:
    std::shared_ptr<grpc::Channel> channel_;
    // 复用现有 auth / tls / timeout / retry 配置，由 llm_provider_config 注入
    std::shared_ptr<const LlmProviderConfig> config_;
};

} // namespace ahfl::llm_provider
```

**关键设计约束：**
- `LlmProviderError` 枚举（auth fail、timeout、schema mismatch、retry exhaustion、unavailable…）**完全复用** `src/runtime/providers/llm/llm_provider_config.hpp` 已定义的 taxonomy，保证 diagnostic 与回退决策一致。
- `GrpcClientFacade` 的构造参数（endpoint、auth、timeout、deadline、retry budget、mTLS）与 `HttpClient` / `StreamingClient` 的语义一一对应；调用端不得看到 `grpc::CompletionQueue`、stub lifetime、channel args 或 generated proto class。
- 当前 `ProviderRegistry` 只做 provider priority / status selection，不做 scheme dispatch。若 Go，必须新增 `TransportSelector` / `CapabilityTransportSelector` 之类的明确 seam，由它解析 `grpc://` / `http+json://` / `grpc+json://`，再选择 facade 或现有 transport。

### 3.4 Fallback Strategy（回退策略）

1. **编译期开关**：`AHFL_ENABLE_GRPC_NATIVE=OFF` 时 facade 整个 TU 不编译，future transport selector 对 `grpc://` 返回 `TransportUnsupported{"grpc native disabled"}`，并发出 `runtime.grpc_native.disabled` 诊断。
2. **默认 fail-closed**：native unavailable、schema mismatch、transport failure、timeout 默认不自动切换 transport；诊断必须包含 `transport_attempted`、`grpc_status_code`、`grpc_status_details`、`deadline_ms` 和 provider endpoint 指纹。
3. **显式 fallback 条件**：只有同时满足以下条件才允许 fallback 到 `grpc_json_transcoding` / `http+json://`：
   - capability 或 runtime config 显式允许 fallback；
   - 调用是 unary 且幂等，或已有 test evidence 证明 retry/fallback 不会重复外部副作用；
   - native proto contract 与 JSON transcoding contract 的语义等价有 golden / integration test；
   - observability machine JSON 记录 `transport_attempted`、`transport_used`、`fallback_reason`、`grpc_status_code`、`fallback_transport`。
4. **streaming / bidi 限制**：server-streaming 和 bidi tool calling 不默认 fallback；message order、client half-close、cancel、deadline、backpressure 语义无法用 HTTP JSON 长轮询等价表达时必须 fail closed。
5. **用户可显式关闭**：`ahflc run --no-grpc-native` 或 capability 级 `binding: "http+json://..."`，完全走老路径。
6. **破坏保证**：所有现有（非 `grpc://`）binding 行为字节级不变。

---

## 4 Alternatives Considered（已考虑的替代方案）

### 4.1 Pros/Cons Matrix（利弊对照表）

| 方案 | Pros（优点） | Cons（缺点） |
|---|---|---|
| **A. 原生 gRPC/Protobuf（条件性 Go）** | HTTP/2 多路复用、status/trailer/deadline 与 streaming lifecycle 都由成熟 gRPC runtime 管理；Protobuf 控制层可强类型化；适合企业内网、自托管或明确暴露 native gRPC endpoint 的 provider | 三平台构建成本（§3.2）；gRPC-cpp API 陡峭；调试工具链比 curl + `jq` 重；对 "纯 OpenAI-compatible HTTP/JSON 生态" 可能没有 provider endpoint 可用 |
| **B. 先强化 HTTP/2 JSON/SSE + gRPC JSON transcoding（No-Go 默认路线）** | 零新增 gRPC C++ 依赖；现有回归测试、diagnostic taxonomy 全部命中；调试可直接用 `curl -v`；Win/macOS/Linux 构建矩阵 0 增量；libcurl-multi + HTTP/2 multiplexing 能作为低风险性能基线 | JSON payload 和 provider compatibility mapping 仍在热路径；status/trailer/deadline/retry 需要继续由 AHFL 自己收敛；bidi streaming 没有与 native gRPC 完全等价的 HTTP/JSON 表达 |
| **C. 换路线：QUIC / HTTP/3 + JSON（msquic / quiche）** | 比 HTTP/2 更抗丢包，`small_unary` / `high_concurrency` 在弱网环境理论更好 | C++ 生态碎片化；几乎无 LLM 供应商暴露 HTTP/3 端点；调试工具几乎不存在；对当前 AHFL 代码库是 **全新一条缝**，而非在已有 HTTP/2 JSON seam 上增量 |
| **D. Cap'n Proto / FlatBuffers（零拷贝 RPC）** | 比 Protobuf 更低延迟（尤其 FlatBuffers）；反序列化几乎零 CPU | LLM 生态无任何供应商用该协议；自身 IDL 与现有 JSON compatibility mapping 成本高；调试工具链更差；AHFL 团队无使用经验，知识债务远高于 gRPC |

### 4.2 结论

- **当前默认 = B（No-Go / evidence missing 时的路线）**：先把 `http2_json_optimized` 跑成可量化基线，再决定 native gRPC 是否值得引入。
- **A 只允许条件性 Go**：前提是 §3.1 基准、§3.2 三平台构建、dependency policy、feature flag、fallback semantics 和 test strategy 同时满足。
- C / D 均因 "生态兼容 × 知识债务 × 调试成本" 三维乘积劣于 A，直接排除。

---

## 5 Non-Goals（非目标 / 明确不做的事）

1. **不做 gRPC server 端**：AHFL 1.0 及之前仅作 gRPC client。任何 "把 AHFL 暴露为 gRPC 服务" 的诉求进入独立 RFC（预期与 h-22 Playground 的 backend 讨论合并）。
2. **不做 Rust / Go / Python 侧的 SDK**：第一阶段仅交付 C++ client facade；若后续 CLI 插件机制采用 FFI，再派生 C ABI 封装（留 Phase 2）。
3. **不做 Protobuf 签名校验 / mTLS 双向证书策略细化**：复用现有 `HttpAuthConfig` 的 mTLS 三路径（cert / key / ca）与 bearer/OAuth2，gRPC-specific 的 `signing_keys`、token 旋转细粒度、audience 校验进入独立 "gRPC Security Phase 2" 子任务。
4. **不做 L7 负载均衡 / 熔断细化**：仅依赖 gRPC 内置 `pick_first` + `round_robin` channel 策略；高级流量治理（重试预算、退避抖动、端点健康检查 gRPC Health Checking Protocol）作为 Phase 2 follow-up。
5. **不做跨 provider 协议适配**：本 RFC 只定义 AHFL 内部统一 proto 契约与 facade。具体 vendor（OpenAI、Anthropic、Claude、本地 vLLM 等）的适配仍由各 provider adapter 或未来明确的 transport-selection seam 承担；当前 `ProviderRegistry` 只做 provider priority / status selection，不能被描述为 scheme dispatch 插件层。
6. **不改变 capability 的语义与语法**：从 `ahfl` 源代码角度看，用户只需要换 `binding:` scheme，其他 capability 关键字、参数校验、budget contract、diagnostic code 全部不变。
7. **不引入 grpc-web / Envoy 前置代理**：AHFL 进程直接发起 HTTP/2 gRPC call；若生产部署需要 Envoy（TLS 终止、配额、审计）由运维在进程外处理，代码库无配置。

---

## 6 Migration / Backward Compatibility（迁移与向后兼容）

### 6.1 Compatibility Strategy（兼容策略）

- **默认 behavior 不变**：`AHFL_ENABLE_GRPC_NATIVE=OFF` 为默认，所有现有 capability binding、`ahflc run`、`ahflc verify` 的 CLI 输出、diagnostic taxonomy 全部与 0.x 基线字节级一致。
- **binding scheme 分层解析**：
  - `http://` / `https://` → 旧 `HttpTransport`，完全原路径；
  - `http+json://` / `grpc+json://`（别名） → 现有 gRPC JSON transcoding（`GrpcTransport`）；
  - `grpc://` / `grpcs://` → **未来**原生 gRPC facade（仅当 feature-flag ON、依赖可用且 binding 明确支持时启用；否则默认 fail closed，只有满足 §3.4 的显式条件才可 fallback）。
- **Diagnostic 兼容**：所有原生 gRPC 产生的 `LlmProviderError` 映射到现有枚举（`auth_fail / timeout / schema_mismatch / retry_exhausted / unavailable`），并在 `additional_info` 中附加 `grpc_status_code` 与 `grpc_status_details`，**不新增顶层 error kind**，保证下游 `ahflc` formatter、LSP、golden 测试不破。

### 6.2 Deprecation Path（弃用路径，至少 2 个 release cycle）

| 阶段 | Release | 行为 |
|---|---|---|
| Alpha（默认 OFF） | 待 Go 决策后另定 | `grpc://` 仅在 opt-in build/runtime flag 下启用；未启用时 fail closed 并发出 `runtime.grpc_native.disabled`，不悄悄改走 JSON path。 |
| Beta（opt-in canary） | 待 Alpha 证据稳定后另定 | 仍不对公开默认构建打开；选定 CI / 企业内网部署可以启用。native 失败默认 fail closed；只有满足 §3.4 的 unary/idempotent/equivalence/config 四条件时才可显式 fallback。 |
| Stable（证据完整后） | 待 release evidence 后另定 | 只对支持 native endpoint 的 capability / provider 打开；`grpc+json://` 保留为显式 JSON transcoding path，不因 native 稳定而自动弃用。 |
| 删除期 | 本 RFC 不安排 | 是否移除 `grpc+json://` alias 需要独立 owner decision；RFC0004 不用 native gRPC 作为强制迁移理由。 |

### 6.3 Automated Tooling（自动化迁移工具）

- AHFL 工具链暂不提供 `rustfix`/`cargo fix` 风格的 source-to-source 重写。
- 替代方案：若 Go 后确有迁移需求，先提供 `ahflc migrate <dir> --from=grpc+json --to=grpc --dry-run` 审计子命令：
  - **dry-run**：打印命中的 binding 行（file:line）、目标 endpoint 能力、是否满足 native support evidence、是否允许显式 fallback；
  - **apply**：只有在用户显式传入 `--apply --require-native-support-evidence` 且 capability 不含 JSON-only 字段时才允许就地替换 `grpc+json://` → `grpc://`，并生成 `.migrate-report.json`；
  - 同时检查 `binding:` 所在 capability 是否引用了只在 JSON transcoding 下存在的字段（例如 `query_params:`），命中则在 report 中标记 `needs_manual_review`，不得自动改写。
- 该子命令非 release-blocking：可以放在 Alpha/Beta 之间的 PR 中独立合并，与 §7.1 Beta 验收条件关联。

---

## 7 Rollout Plan + Acceptance Criteria（发布计划与验收标准）

### 7.1 Rollout — 3 Phases（三阶段发布）

> 与 Playground / Package Registry（PB-01 h.8 Cross-cutting）独立解耦。

| 阶段 | 时间窗（目标 wave） | Feature Flag | 行为门禁 |
|---|---|---|---|
| **Alpha — 单元测试门控** | 待 RFC accepted 后第一阶段 | `AHFL_ENABLE_GRPC_NATIVE=ON`（仅 CI，用户默认 OFF） | 仅跑 `ahfl.runtime.grpc_native_*` 单元测试 + mock gRPC server；**不进入 CLI 端到端** |
| **Beta — CLI golden 输出** | 待 Alpha 稳定 ≥ 2 wave 且 owner 决策确认后 | `AHFL_ENABLE_GRPC_NATIVE=ON`（CI / canary；公开默认仍可保持 OFF） | `ahflc run --capability-bindings ...` 新增原生 gRPC golden，覆盖 `small_unary` / `large_structured_response` / `high_concurrency` + 5 类失败（auth / timeout / schema / unimplemented / unavailable）；失败默认 fail closed，显式 fallback 单独 golden |
| **Stable — 默认 feature** | 1.0 GA（或 Beta 稳定 ≥ 3 wave + 零 fuzzer crash） | 默认 ON，保留 `--no-grpc-native` | 进入默认构建矩阵；所有老 binding 回归不变；发布 Release Notes 章节 "New transport: native gRPC" |

### 7.2 Acceptance Criteria（验收标准，可量化 ≥ 4 条）

**AC-1（基准门槛，决定 Go/No-Go）**：§3.1 三条场景中 **至少 2 条** 同时满足 Go 阈值，且 CI 三平台（Linux x86_64 / macOS arm64 / Windows MSVC）原生 gRPC 构建 **连续 3 个 nightly 全绿**（PASS 率 = 3/3）。

**AC-2（回归）**：原生 gRPC 合入后，`ctest -L runtime` 全量通过。当前基线 ≥ 980/980（wave-17 终态），**最终 ctest 通过数不得低于基线**，即 `(980 + grpc_new_tests) / (980 + grpc_new_tests)` = 100%；若新增测试 N，则 `pass/(980+N) = 100%`。

**AC-3（可观测性覆盖）**：canonical `ahfl.run-event` JSONL 与 OTLP-compatible projection 中，native 成功用例必须记录 `transport_attempted = "native_grpc"` 且 `transport_used = "native_grpc"`；fail-closed 用例必须记录 `transport_attempted`、`grpc_status_code`、`grpc_status_details`、`deadline_ms` 和 diagnostic code；只有显式 fallback 用例才允许 `transport_used = "grpc_json_transcoding"` 或 `"http_json"`，且必须记录 `fallback_reason` 与 `fallback_transport`。不得为 native gRPC 恢复独立 provider observability artifact。

**AC-4（代码质量）**：新增 TU（`src/runtime/providers/llm/grpc_*` + `proto/` 生成代码外的手写代码）行覆盖率 ≥ **85%**，mutation score ≥ **70%**（复用 h-18/h-19 的 mutation runner 框架）。

**AC-5（安全 / 鲁棒性）**：基于 `libFuzzer` 的 `grpc_facade_proto_fuzzer`（随机合法 / 非法 proto 字节流喂给 facade 的 mock channel）在 CI nightly 任务上 **连续 14 天零 crash**，累计 corpus 大小 ≥ 1 MiB，累计执行次数 ≥ 2^28。

**AC-6（向后兼容）**：使用 0.22.x（引入 gRPC JSON transcoding 的基线版本）所有 capability 用例与 30 条官方示例，在 feature-flag ON 时对 human 输出、stderr 和 exit code做语义回归，并对版本化 JSON/JSONL 使用 schema-aware subset diff。允许的新增 transport 字段必须复用 canonical execution serializer。

### 7.3 Decision Gate（决策门）

- 在 Alpha 阶段结束时，Shepherds 基于 AC-1 与 §3.2 构建报告进行 **正式 Go/No-Go 签字**：
  - **Go**：RFC 状态 DRAFT → ACCEPTED；进入 Beta。
  - **Conditional Go（仅两平台）**：若 Windows 构建长期失败但另两平台全过，Shepherds 可决定 "Win 默认 OFF，其他默认 Beta ON"，RFC 追加 §8 补充说明章节 "Platform Restrictions"。
  - **No-Go**：RFC 状态 DRAFT → REJECTED；本文件顶部追加 `[STATUS → OUT-OF-SCOPE @ YYYY-MM-DD]` 注释；所有 Alpha 代码回滚或留在 feature-flag OFF 的孤立目录中，不在 release 构建。

---

## 8. Decision Block / 决策块

| 评审维度 | 选择 / 说明 |
|---|---|
| **当前状态** | DRAFT · Awaiting inputs |
| **go-criteria 满足度** | (1) §3.1 三条场景至少 2 条同时通过 Go 阈值；(2) 三平台（Linux x86_64 / macOS arm64 / Win MSVC）原生 gRPC 构建连续 3 次 nightly 全绿；(3) mutation score ≥ 70% 且 proto fuzzer 14 天零 crash |
| **no-go 否决条件** | 若 QE 组真实 benchmark 数据显示三场景 P95 降幅均 < 10%，则 No-Go；若三平台中任一平台 CI 构建增量时长 > 20% 且无法通过缓存缓解超过 1 个 wave，则 No-Go |
| **仍需决策** | runtime owner 是否给出 Go / Conditional-Go / No-Go；若 Go，是否仅限企业内网 / 自托管 / 明确暴露 native gRPC endpoint 的 provider；公开 HTTP/JSON provider 默认继续走 `http_json` / `http2_json_optimized` / `grpc_json_transcoding` |
| **依赖其他 RFC** | NONE |
| **预计实现工作量** | L（>1 月）— proto 契约 + facade + opt-in dependency package/cache 策略 + 三平台构建证据 + benchmark + mock server + 文档/示例/tutorial；默认 CMake configure 不允许无条件 FetchContent gRPC |
| **破坏性变更风险** | NONE — 默认 `AHFL_ENABLE_GRPC_NATIVE=OFF`，仅 `grpc://` scheme 新绑定受影响，现有 binding 字节级不变 |
| **Wave 归属** | Wave-21+（需先拿到 QE 组 benchmark 数据 + 完成 Shepherds 在 Alpha 结束时 Go/No-Go 签字） |

---

## 9. References（参考资料）

### 9.1 Internal / Repository（仓库内，file:line 可点击）

1. [docs/plans/native-grpc-decision-gate.zh.md](../plans/native-grpc-decision-gate.zh.md) — gRPC go/no-go 决策门槛与验收契约（机器检查由 `scripts/check-native-grpc-gate.py` 执行）。
2. [`docs/plans/issue-backlog-global-gaps.zh.md:30`](../plans/issue-backlog-global-gaps.zh.md#L30-L30) — 全局缺口清单：transport 基线（HTTP / gRPC JSON transcoding）与 "native gRPC / Protobuf 仍未定案" 的原始陈述。
3. [`docs/plans/project-status.zh.md:138`](../plans/project-status.zh.md#L138-L138) — Runtime transport 产品化矩阵行：明确列出 "native gRPC / Protobuf transport 仍需取舍"。
4. [`src/runtime/engine/grpc_transport.cpp:1`](../../src/runtime/engine/grpc_transport.cpp#L1-L1) — 现有 `GrpcTransport`（实为 JSON transcoding，而非原生 Protobuf）实现入口，为原生 gRPC 扩展的 seam。
5. [`src/runtime/engine/http_transport.cpp:7`](../../src/runtime/engine/http_transport.cpp#L7-L19) — `HttpTransport::build_curl_command` 与 `HttpTransport::execute` 基线实现，为 §3.4 fallback 提供参考签名。
6. [`src/runtime/providers/llm/http_client.hpp:31`](../../src/runtime/providers/llm/http_client.hpp#L31-L55) — `HttpClient` 的 `ChatCompletionsTransport` 注入签名与 `chat_completions` 方法，§3.3.2 facade 必须 1:1 语义对齐。
7. [`src/runtime/providers/llm/streaming.hpp:12`](../../src/runtime/providers/llm/streaming.hpp#L12-L31) — `StreamChunkCallback` 与 `SSEParser` 现有 streaming seam，§3.3.2 中在 gRPC server-stream 模式下复用回调签名。
8. 现有 mutation runner 框架（h-18/h-19 引入），为 §7 AC-4 的 mutation score ≥ 70% 提供工具链基础。

### 9.2 External（外部参考）

9. **libcurl Official — multi interface overview**：<https://curl.se/libcurl/c/libcurl-multi.html> — 支持 `http2_json_optimized` baseline 的并发传输与 event-driven integration 依据。
10. **Everything curl — Multiplexing**：<https://everything.curl.dev/libcurl-http/multiplexing.html> — 支持 HTTP/2 multiplexing JSON/SSE 基线的 transport 行为依据。
11. **gRPC Official — Performance Best Practices**：<https://grpc.io/docs/guides/performance/> — 支持 channel/stub 复用、streaming 场景和性能门槛设计。
12. **gRPC Official — C++ Best Practices**：<https://grpc.io/docs/languages/cpp/best_practices/> — 支持 C++ callback API、streaming lifecycle、读写约束和 API 封装边界。
13. **gRPC Official — Deadlines**：<https://grpc.io/docs/guides/deadlines/> — 支持每个 RPC 必须有 deadline 与 fallback/fail-closed diagnostic 设计。
14. **Protocol Buffers — Best Practices**：<https://protobuf.dev/best-practices/dos-donts/> — 支持 tag reserve、enum 0、避免 required field 和 schema compatibility 治理。

---

*End of RFC 0004 DRAFT. Status 变更与 Shepherds 签字将在 Alpha 阶段结束后更新此文件顶部 Metadata。*

---

### Change history / 变更记录

- 2026-06-28 (V1.1): Added Decision Block §8; Consult / Decision Due / Blocking Items; cross-RFC refs
- 2026-06-28 (V1.0): Initial DRAFT
- 2026-07-07: Added explicit decision-gate plan covering owner sign-off, benchmark evidence, build matrix, dependency policy, feature flag, fallback semantics and test strategy.
- 2026-07-09: Reconciled RFC0004 with live code: current `grpc_json_transcoding` is not native gRPC, `ProviderRegistry` is not a scheme router, native gRPC stays draft-only, benchmark evidence now requires HTTP/JSON and optimized HTTP/2 JSON baselines, fallback defaults to fail-closed, and proto governance follows Protobuf compatibility rules.
