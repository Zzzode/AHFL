---
rfc: "0004"
title: "Native gRPC Transport"
status: "draft"
area: ["runtime"]
stability: "experimental"
created: "2026-06-28"
updated: "2026-07-02"
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

Decide whether AHFL should add a native gRPC/Protobuf transport for runtime and LLM capability calls, and define the gated design if the decision is go.

## Motivation

HTTP/JSON remains simple and portable, but long streaming responses and multi-turn tool-calling loops may need lower latency, lower CPU overhead, and first-class bidirectional streaming.

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

Native gRPC should be an optional runtime transport behind an explicit build/runtime gate. The provider registry selects it for `grpc://` bindings only when enabled and healthy, otherwise falling back to the existing JSON path according to documented rules.

## User Impact

Users may opt into `grpc://` capability bindings for supported providers. Default behavior remains unchanged while the RFC is draft or experimental.

## Compatibility and Migration

Existing `http://`, `https://`, and JSON-transcoded gRPC bindings must continue to behave the same. Any deprecation of old aliases requires a release-note and migration plan.

## Implementation Plan

Run benchmarks first, decide go/no-go, then add optional dependency wiring, proto contracts, C++ facade, provider registry selection, fallback diagnostics, and runtime tests.

## Test Plan

Add transport unit tests, fallback integration tests, provider-registry tests, build-matrix checks, and benchmark evidence before moving beyond draft.

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
| Blocking Items | QE 组 3 场景真实 benchmark 数据 |
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

AHFL 的 runtime / LLM provider 子系统（`src/runtime/providers/llm/` 与 `src/runtime/engine/`）当前对外通信全部走 HTTP/1.1 + JSON 语义，具体体现为两条 seam：

1. **LLM provider**（OpenAI-compatible API）：`HttpClient` / `StreamingClient` 走 `aiohttp`/`requests` 风格的 HTTP + JSON，由 `src/runtime/providers/llm/http_client.cpp:31` 定义的 `ChatCompletionsTransport` 注入。
2. **Capability bridge**：`HttpTransport` 与 `GrpcTransport` 共用 wire adapter 层，其中 `GrpcTransport` 当前实为 **gRPC JSON transcoding**（将 HTTP/JSON 映射到已有 gRPC service，而非原生 Protobuf 编解码 + HTTP/2），见 `src/runtime/engine/grpc_transport.cpp:1`。

**Why now**：
- Wave-16 项目状态已将 "native gRPC / Protobuf transport 仍需取舍" 明确列为 BLOCKED 决策项，并要求 P1 周期内给出结论（`docs/plans/phaseb-gap-analysis.zh.md:168`）。
- SSE 风格 token streaming（长响应、chunk 级回调、head-of-line blocking）与 multi-turn tool calling（来回小消息、低延迟往返）两种场景对 HTTP/1.1 的复用率与 JSON 反序列化 CPU 开销敏感，已有 profiling 占位（`docs/plans/issue-backlog-global-gaps.zh.md:30`）。
- 现有 `GrpcTransport` 已打通 HTTP → gRPC JSON transcoding seam，为原生 Protobuf 提供了统一的错误分类、auth、timeout、retry 与 metadata/trailer 传播骨架，可以在此上增量扩展，而非从零搭建。

### 2.2 Decision This RFC Must Produce（本 RFC 必须给出的决策）

本 RFC 交付物 **不是** 直接宣布 go，而是：

1. **Go/No-Go 决策**：基于可量化基准（§3.1）与三平台构建成本（§3.2），在 DRAFT → ACCEPTED 转态时由 Shepherds 签字。
2. **若 Go，则给出接口设计骨架**：proto 定义 + C++ client facade + 自动回退策略（§3.3 / §3.4）。
3. **若 No-Go**：在 §2 首行追加 `[STATUS → OUT-OF-SCOPE @ YYYY-MM-DD]`，并给出 1.0 之后再评估的触发条件（例如某供应商原生 gRPC API 公开或单场景 P99 延迟不可接受）。

### 2.3 In-Scope / Out-of-Scope（适用范围）

- **In-Scope**：AHFL runtime 作为 **gRPC client** 调用外部 LLM inference / capability 服务（原生 Protobuf 二进制 payload、HTTP/2、server-streaming、bidi-streaming）；跨 Win / macOS / Linux（x86_64 + arm64）的 CMake FetchContent 级构建；自动回退到 HTTP JSON，保证 feature-flag 关闭时行为 100% 与现版本一致。
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

**前置条件（Go/No-Go 的硬门槛）：** 在 3 条真实场景上对 HTTP/1.1 + JSON 与 gRPC + Protobuf 做 P50/P95/P99 延迟与 CPU% 对比；环境采用 CI runner（Linux x86_64）+ 本机 macOS arm64，样本数 N=200，预热 20 次。

| 场景（Scenario） | Payload 特征 | HTTP-JSON 基线（预估值，待实测覆盖） | gRPC-Protobuf 目标（Go 阈值） |
|---|---|---|---|
| S1 — Unary large request | request 64 KiB（大 prompt），response ≤ 2 KiB | P50 ≈ 210 ms / P95 ≈ 380 ms / P99 ≈ 520 ms；CPU% 反序列化 ~28 % of wall | P95 延迟降幅 ≥ 25% **或** CPU% 降幅 ≥ 30%；任一条不满足 → 该场景 No-Go |
| S2 — Streaming long response | 首 token + 每 40 ms 一块、共 8 KiB 文本（SSE vs gRPC server-stream） | 首 token P50 ≈ 190 ms；总体端到端 ~820 ms；chunk dispatch CPU ~22% | 首 token P50 降幅 ≥ 15% **且** 总体内存占用（JSON 中间对象）降幅 ≥ 40% |
| S3 — Tool-call round trip（10 轮） | 每轮 < 512 B，10 轮 interleaved；当前 HTTP 每条独立请求（无复用或复用率低） | 总体 P50 ≈ 1.1 s / P99 ≈ 1.8 s | 总体 P50 ≤ 0.75 s（≥ 30% 降幅）**且** P99 ≤ 1.2 s；否则该场景对 ROI 不成立 |

**Go/No-Go 判定规则（§7 再量化）：**
- **Go = 3 条场景中至少 2 条同时通过阈值，且 §3.2 三平台构建全部通过。**
- **No-Go = 其余所有情况。** 仍保留当前 HTTP JSON transcoding seam 为唯一实现。

### 3.2 Dependency Evaluation（依赖评估）

| 依赖 | Linux x86_64 | macOS (Intel / arm64) | Windows MSVC | 体积 / 构建时长增量（CI） |
|---|---|---|---|---|
| `protobuf-compiler` ≥ 25.x（`protoc`） | apt / FetchContent 均可 | `brew install protobuf` 或 FetchContent | `choco install protoc` / FetchContent | 源码构建：~80 MB，~7 min（冷） |
| `gRPC-cpp` ≥ 1.60 | FetchContent `abseil-cpp + grpc` 或 system | 同上；arm64 需 `-DCMAKE_OSX_ARCHITECTURES=arm64` | FetchContent + `gRPC_BUILD_CSHARP_EXT=OFF` 关无关扩展 | 全量 +~1.4 GB；预编译 SDK 可降到 ~320 MB（三平台 nightly cache） |
| OpenSSL / BoringSSL | 复用现有 `libcrypto`（HTTP mTLS 已引入） | 复用 Security.framework 或 BoringSSL 静态链 | 复用 SChannel 或 BoringSSL | 无新增依赖（mTLS 已在 §2 baseline） |

**构建成本底线：**
- 启用开关：`AHFL_ENABLE_GRPC_NATIVE=OFF`（**默认 OFF**）；ON 时 CMake 自动 `FetchContent` 解决。
- CI 必跑三平台 + 两架构，构建 artifact 缓存按 `grpc-vX.Y.Z-<os>-<arch>` 维度 key，冷构建不超过现有 CI 时长 20%。若任一平台 **连续 2 次 nightly 构建失败**，该平台默认关闭，Go 决策降级为"仅支持两平台"或整体 No-Go，由 Shepherds 在 wave 例会决定。

### 3.3 Interface Skeleton（接口骨架）

**3.3.1 Proto 定义（`proto/ahfl/runtime/v1/llm_transport.proto`）**

```proto3
syntax = "proto3";

package ahfl.runtime.v1;

// 通用：与现有 JSON schema 字段对齐，保证 1:1 可逆
message LlmMessage { Role role = 1; string content = 2; repeated ToolCall tool_calls = 3; }
enum Role { ROLE_UNSPECIFIED = 0; SYSTEM = 1; USER = 2; ASSISTANT = 3; TOOL = 4; }
message ToolCall { string id = 1; string name = 2; string args_json = 3; }
message ToolResult { string call_id = 1; string result_json = 2; string error = 3; }

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

    // Bidi：对应 Use Case 3，暴露 Rx/Tx 两端，内部持有一个 completion queue。
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
- `GrpcClientFacade` 的构造参数（`base_url`、`api_key`、`timeout_seconds`、`HttpAuthConfig`/其 gRPC 等价物）与 `HttpClient` 一一对应，`provider_registry.cpp`（`src/runtime/providers/llm/provider_registry.cpp:1`）按 binding scheme 选择 `grpc://` → facade，其余 → 现有 HTTP。

### 3.4 Fallback Strategy（回退策略）

1. **编译期开关**：`AHFL_ENABLE_GRPC_NATIVE=OFF` 时 facade 整个 TU 不编译，`provider_registry` 中 `grpc://` scheme 解析器被编译为 `return TransportUnsupported{"grpc native disabled"}`，与现有 "unknown scheme" 诊断路径一致。
2. **运行期自动降级**：当 `grpc://` binding 在 **以下任一条件** 成立时，自动切换到同 URL 的 `http+json://`（JSON transcoding），且在 `--llm-observability` 输出中追加 `fallback_event` 字段：
   - `channel_->WaitForConnected(timeout)` 返回 false（DNS / TLS / 端口不通）；
   - 连续 3 次 `UNAVAILABLE` gRPC status code；
   - `UNIMPLEMENTED`（服务端只暴露 JSON transcoding，不支持原生 proto）。
3. **用户可显式关闭**：`ahflc run --no-grpc-native` 或 capability 级 `binding: "http+json://..."`，完全走老路径。
4. **破坏保证**：所有现有（非 `grpc://`）binding 行为字节级不变。

---

## 4 Alternatives Considered（已考虑的替代方案）

### 4.1 Pros/Cons Matrix（利弊对照表）

| 方案 | Pros（优点） | Cons（缺点） |
|---|---|---|
| **A. 本 RFC 主张：原生 gRPC/Protobuf（条件性 Go）** | HTTP/2 多路复用天然适合 S3（多轮 tool-calling）；Protobuf CPU 序列化开销显著低于 rapidjson/nlohmann；与现有 gRPC JSON transcoding 的 metadata/trailer/auth/timeout seam **天然对齐** | 三平台构建成本（§3.2）；gRPC-cpp API 陡峭；调试工具链比 curl + `jq` 重；对 "纯 OpenAI-compatible 生态" 用户可能是一条死路（不开放原生 gRPC） |
| **B. 停留在 gRPC JSON transcoding（现状强化）** | 零新增第三方依赖；现有回归测试、diagnostic taxonomy 全部命中；调试可直接用 `curl -v`；Win/macOS/Linux 构建矩阵 0 增量 | HTTP/1.1 复用受限于 `Connection: keep-alive`，S3 场景 10 轮 tool-call 无法避免 head-of-line；JSON 反序列化在 64 KiB+ prompt 场景 CPU 占比 ~28%，无上限；bidi streaming **无干净等价物**（需 WebSocket 或长轮询，再引入一套依赖） |
| **C. 换路线：QUIC / HTTP/3 + JSON（msquic / quiche）** | 比 HTTP/2 更抗丢包，S1/S2 在弱网环境理论更好 | C++ 生态碎片化；几乎无 LLM 供应商暴露 HTTP/3 端点；调试工具几乎不存在；对当前 AHFL 代码库是 **全新一条缝**，而非在已有的 `GrpcTransport` 骨架上增量 |
| **D. Cap'n Proto / FlatBuffers（零拷贝 RPC）** | 比 Protobuf 更低延迟（尤其 FlatBuffers）；反序列化几乎零 CPU | LLM 生态无任何供应商用该协议；自身 IDL 与现有 JSON schema 1:1 可逆成本高；调试工具链更差；AHFL 团队无使用经验，知识债务远高于 gRPC |

### 4.2 结论

- **首选 = A（条件性 Go）**：前提是 §3.1 基准 + §3.2 三平台构建同时满足。
- **安全默认 = B（No-Go 时的基线）**：即便不引入原生 gRPC，通过 HTTP/2 的 `libcurl-multi` 多路复用补丁与 JSON parser 热路径 SIMD 化，也能获得 **约 10–15%** 的单点改进（但覆盖不到 bidi streaming 语义层）。
- C / D 均因 "生态兼容 × 知识债务 × 调试成本" 三维乘积劣于 A，直接排除。

---

## 5 Non-Goals（非目标 / 明确不做的事）

1. **不做 gRPC server 端**：AHFL 1.0 及之前仅作 gRPC client。任何 "把 AHFL 暴露为 gRPC 服务" 的诉求进入独立 RFC（预期与 h-22 Playground 的 backend 讨论合并）。
2. **不做 Rust / Go / Python 侧的 SDK**：第一阶段仅交付 C++ client facade；若后续 CLI 插件机制采用 FFI，再派生 C ABI 封装（留 Phase 2）。
3. **不做 Protobuf 签名校验 / mTLS 双向证书策略细化**：复用现有 `HttpAuthConfig` 的 mTLS 三路径（cert / key / ca）与 bearer/OAuth2，gRPC-specific 的 `signing_keys`、token 旋转细粒度、audience 校验进入独立 "gRPC Security Phase 2" 子任务。
4. **不做 L7 负载均衡 / 熔断细化**：仅依赖 gRPC 内置 `pick_first` + `round_robin` channel 策略；高级流量治理（重试预算、退避抖动、端点健康检查 gRPC Health Checking Protocol）作为 Phase 2 follow-up。
5. **不做跨 provider 协议适配**：本 RFC 只定义 AHFL 内部统一 proto 契约与 facade。具体 vendor（OpenAI、Anthropic、Claude、本地 vLLM 等）的适配层沿用 `provider_registry.cpp` 的现有插件式 seam，本 RFC 不新增 vendor。
6. **不改变 capability 的语义与语法**：从 `ahfl` 源代码角度看，用户只需要换 `binding:` scheme，其他 capability 关键字、参数校验、budget contract、diagnostic code 全部不变。
7. **不引入 grpc-web / Envoy 前置代理**：AHFL 进程直接发起 HTTP/2 gRPC call；若生产部署需要 Envoy（TLS 终止、配额、审计）由运维在进程外处理，代码库无配置。

---

## 6 Migration / Backward Compatibility（迁移与向后兼容）

### 6.1 Compatibility Strategy（兼容策略）

- **默认 behavior 不变**：`AHFL_ENABLE_GRPC_NATIVE=OFF` 为默认，所有现有 capability binding、`ahflc run`、`ahflc verify` 的 CLI 输出、diagnostic taxonomy 全部与 0.x 基线字节级一致。
- **binding scheme 分层解析**：
  - `http://` / `https://` → 旧 `HttpTransport`，完全原路径；
  - `http+json://` / `grpc+json://`（别名） → 现有 gRPC JSON transcoding（`GrpcTransport`）；
  - `grpc://` / `grpcs://` → **新**原生 gRPC facade（仅当 feature-flag ON 且运行时可连通时启用，否则降级到 `grpc+json://`）。
- **Diagnostic 兼容**：所有原生 gRPC 产生的 `LlmProviderError` 映射到现有枚举（`auth_fail / timeout / schema_mismatch / retry_exhausted / unavailable`），并在 `additional_info` 中附加 `grpc_status_code` 与 `grpc_status_details`，**不新增顶层 error kind**，保证下游 `ahflc` formatter、LSP、golden 测试不破。

### 6.2 Deprecation Path（弃用路径，至少 2 个 release cycle）

| 阶段 | Release | 行为 |
|---|---|---|
| Alpha（默认 OFF） | 0.24.x | `grpc://` 仅在 `--feature=grpc-native`（或环境变量 `AHFL_ENABLE_GRPC_NATIVE=1`）下启用；未启用时给出 warning（`deprecated_preview_feature_hint`，非 error）。 |
| Beta（默认 ON，可关） | 0.26.x | `grpc://` 默认启用，失败自动回退；`--no-grpc-native` 可强制关闭。老 binding `grpc+json://` 标记 **soft-deprecated**，文档中在对应 capability 示例上给出 `[deprecation-note: prefer grpc:// which auto-falls-back]`。 |
| Stable（默认 ON） | 1.0 GA | 老 `grpc+json://` scheme 标记 **hard-deprecated**，仍可工作，启动时 1 条 warning。1.2 GA 起删除该 scheme alias，但保留 auto-fallback 行为（即用户可只写 `grpc://`，代码库自己决定）。 |
| 删除期（如有必要） | ≥ 1.4 GA | 仅在 "所有回归 + 生态反馈显示零用户" 的前提下移除 `grpc+json://`；否则保留为 "永久 alias"。 |

### 6.3 Automated Tooling（自动化迁移工具）

- AHFL 工具链暂不提供 `rustfix`/`cargo fix` 风格的 source-to-source 重写。
- 替代方案：`ahflc migrate <dir> --from=grpc+json --to=grpc` 子命令：
  - **dry-run**：打印命中的 binding 行（file:line）与拟替换内容；
  - **apply**：就地替换 `grpc+json://` → `grpc://`，并生成 `.migrate-report.json` 含统计；
  - 同时检查 `binding:` 所在 capability 是否引用了只在 JSON transcoding 下存在的字段（例如 `query_params:`），命中则在 report 中标记 `needs_manual_review`。
- 该子命令非 release-blocking：可以放在 Alpha/Beta 之间的 PR 中独立合并，与 §7.1 Beta 验收条件关联。

---

## 7 Rollout Plan + Acceptance Criteria（发布计划与验收标准）

### 7.1 Rollout — 3 Phases（三阶段发布）

> 与 Playground / Package Registry（PB-01 h.8 Cross-cutting）独立解耦。

| 阶段 | 时间窗（目标 wave） | Feature Flag | 行为门禁 |
|---|---|---|---|
| **Alpha — 单元测试门控** | Wave-18（假设本 RFC 在 wave-17 结束前 ACCEPTED） | `AHFL_ENABLE_GRPC_NATIVE=ON`（仅 CI，用户默认 OFF） | 仅跑 `ahfl.runtime.grpc_native_*` 单元测试 + mock gRPC server；**不进入 CLI 端到端** |
| **Beta — CLI golden 输出** | Wave-20（Alpha 稳定 ≥ 2 wave） | `AHFL_ENABLE_GRPC_NATIVE=ON`（CI 默认；用户可 `OFF`） | `ahflc run --capability-bindings ...` 新增 8 条原生 gRPC golden，覆盖 S1/S2/S3 + 5 类失败（auth / timeout / schema / unimplemented / unavailable 自动降级） |
| **Stable — 默认 feature** | 1.0 GA（或 Beta 稳定 ≥ 3 wave + 零 fuzzer crash） | 默认 ON，保留 `--no-grpc-native` | 进入默认构建矩阵；所有老 binding 回归不变；发布 Release Notes 章节 "New transport: native gRPC" |

### 7.2 Acceptance Criteria（验收标准，可量化 ≥ 4 条）

**AC-1（基准门槛，决定 Go/No-Go）**：§3.1 三条场景中 **至少 2 条** 同时满足 Go 阈值，且 CI 三平台（Linux x86_64 / macOS arm64 / Windows MSVC）原生 gRPC 构建 **连续 3 个 nightly 全绿**（PASS 率 = 3/3）。

**AC-2（回归）**：原生 gRPC 合入后，`ctest -L runtime` 全量通过。当前基线 ≥ 980/980（wave-17 终态），**最终 ctest 通过数不得低于基线**，即 `(980 + grpc_new_tests) / (980 + grpc_new_tests)` = 100%；若新增测试 N，则 `pass/(980+N) = 100%`。

**AC-3（可观测性覆盖）**：`--llm-observability` 输出的 machine JSON 中，`transport_used` 字段在所有 8 条 Beta golden 中 **准确为 `"grpc_native"` 或 `"http_json"`（fallback 时）**，且 `fallback_event.reason` 在 5 类失败用例中均非空、命中错误枚举。

**AC-4（代码质量）**：新增 TU（`src/runtime/providers/llm/grpc_*` + `proto/` 生成代码外的手写代码）行覆盖率 ≥ **85%**，mutation score ≥ **70%**（复用 h-18/h-19 的 mutation runner 框架，参考 `docs/plans/phaseb-gap-analysis.zh.md:212`）。

**AC-5（安全 / 鲁棒性）**：基于 `libFuzzer` 的 `grpc_facade_proto_fuzzer`（随机合法 / 非法 proto 字节流喂给 facade 的 mock channel）在 CI nightly 任务上 **连续 14 天零 crash**，累计 corpus 大小 ≥ 1 MiB，累计执行次数 ≥ 2^28。

**AC-6（向后兼容）**：使用 0.22.x（引入 gRPC JSON transcoding 的基线版本）所有 capability 用例与 30 条官方示例，在 feature-flag ON 时 **逐行对比 CLI stdout / stderr / exit code 与基线完全一致**。允许的唯一差异是 `--llm-observability` 中新增字段（按 JSON 键子集 diff 判定）。

### 7.3 Decision Gate（决策门）

- 在 Alpha 阶段结束（Wave-19 开 wave 例会）时，Shepherds 基于 AC-1 与 §3.2 构建报告进行 **正式 Go/No-Go 签字**：
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
| **未解决争议** | (1) gRPC-cpp vs 自封装 libcurl-multi + HTTP/2 的取舍；(2) proto schema 与现有 JSON schema 1:1 可逆是否允许字段子集；(3) `grpc+json://` 别名是否在 1.4 GA 强制移除 |
| **依赖其他 RFC** | NONE |
| **预计实现工作量** | L（>1 月）— proto 契约 + facade + CMake 三平台 FetchContent 2 周；三阶段 rollout + benchmark + mock server 2 周；文档/示例/tutorial 1 周+ |
| **破坏性变更风险** | NONE — 默认 `AHFL_ENABLE_GRPC_NATIVE=OFF`，仅 `grpc://` scheme 新绑定受影响，现有 binding 字节级不变 |
| **Wave 归属** | Wave-21+（需先拿到 QE 组 benchmark 数据 + 完成 Shepherds 在 Alpha 结束时 Go/No-Go 签字） |

---

## 9. References（参考资料）

### 8.1 Internal / Repository（仓库内，file:line 可点击）

1. [`docs/plans/phaseb-gap-analysis.zh.md:168`](../plans/phaseb-gap-analysis.zh.md#L168-L168) — gRPC go/no-go 决策占位与验收门槛原文。
2. [`docs/plans/issue-backlog-global-gaps.zh.md:30`](../plans/issue-backlog-global-gaps.zh.md#L30-L30) — 全局缺口清单：transport 基线（HTTP / gRPC JSON transcoding）与 "native gRPC / Protobuf 仍未定案" 的原始陈述。
3. [`docs/plans/project-status.zh.md:138`](../plans/project-status.zh.md#L138-L138) — Runtime transport 产品化矩阵行：明确列出 "native gRPC / Protobuf transport 仍需取舍"。
4. [`src/runtime/engine/grpc_transport.cpp:1`](../../src/runtime/engine/grpc_transport.cpp#L1-L1) — 现有 `GrpcTransport`（实为 JSON transcoding，而非原生 Protobuf）实现入口，为原生 gRPC 扩展的 seam。
5. [`src/runtime/engine/http_transport.cpp:7`](../../src/runtime/engine/http_transport.cpp#L7-L19) — `HttpTransport::build_curl_command` 与 `HttpTransport::execute` 基线实现，为 §3.4 fallback 提供参考签名。
6. [`src/runtime/providers/llm/http_client.hpp:31`](../../src/runtime/providers/llm/http_client.hpp#L31-L55) — `HttpClient` 的 `ChatCompletionsTransport` 注入签名与 `chat_completions` 方法，§3.3.2 facade 必须 1:1 语义对齐。
7. [`src/runtime/providers/llm/streaming.hpp:12`](../../src/runtime/providers/llm/streaming.hpp#L12-L31) — `StreamChunkCallback` 与 `SSEParser` 现有 streaming seam，§3.3.2 中在 gRPC server-stream 模式下复用回调签名。
8. [`docs/plans/phaseb-gap-analysis.zh.md:212`](../plans/phaseb-gap-analysis.zh.md#L212-L212) — h-18/h-19 mutation runner 框架，为 §7 AC-4 的 mutation score ≥ 70% 提供现有工具链引用。

### 8.2 External（外部参考）

9. **gRPC Official — Language Guide (proto3)**：<https://protobuf.dev/programming-guides/proto3/> — 本 RFC §3.3.1 所采用的 proto3 语法规范与保留字段策略的来源。
10. **Rust RFC 2907 — "Native async I/O" 中关于 gRPC client facade 的设计讨论（间接借鉴）**：<https://github.com/rust-lang/rfcs/blob/master/text/2907-native-async-io.md> — 核心思路："对外暴露与现有同步接口同签名的 facade，底层由 transport 特性切换；默认不破坏"，与本 RFC §3.3.2 + §6.1 设计原则对齐。
11. **gRPC Official — gRPC over HTTP/2**：<https://grpc.io/docs/reference/glossary/> — 解释 HTTP/2 多路复用对 "多轮低延迟小消息"（§3.1 S3）的收益原理，为基准阈值设定提供理论基线。
12. **ConnectRPC (Buf) — "Comparing Connect, gRPC, and gRPC-Web"**：<https://connectrpc.com/docs/comparison/> — 对 "gRPC native vs gRPC Web / JSON transcoding" 三路线在生态与调试成本上的对比，为 §4 Alternatives B vs A 的权衡提供第三方参照。

---

*End of RFC 0004 DRAFT. Status 变更与 Shepherds 签字将在 Alpha 阶段结束后更新此文件顶部 Metadata。*

---

### Change history / 变更记录

- 2026-06-28 (V1.1): Added Decision Block §8; Consult / Decision Due / Blocking Items; cross-RFC refs
- 2026-06-28 (V1.0): Initial DRAFT
