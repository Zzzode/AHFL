# Native gRPC Decision Gate

审计日期：2026-07-08

本计划服务 [RFC 0004](../rfcs/0004-native-grpc-transport.zh.md)。当前 AHFL runtime 已有 `grpc_json_transcoding` 路径；这不能证明 native gRPC/Protobuf transport 应该进入实现。本门禁的目的，是在任何 native gRPC 代码落库前完成可复核的 Go/No-Go 决策。

机器可读证据记录在 [native-grpc-decision-evidence.json](./native-grpc-decision-evidence.json)。该文件使用 `ahfl.native_grpc_decision_evidence.v1` schema，由 `scripts/check-native-grpc-gate.py` 校验；RFC0004 仍为 `draft` 时可以保留 `missing` gate，但一旦推进到 `accepted` 或 `implementing`，状态和证据必须与下列门槛一致。

## 决策原则

1. 没有 owner sign-off，不进入 `accepted`。
2. 没有真实 benchmark，不进入 `implementing`。
3. 没有 Linux / macOS / Windows 三平台构建证据，不引入 native gRPC dependency。
4. 没有 fallback diagnostics 和 feature flag 策略，不改变 runtime transport 选择。
5. 现有 `grpc_json_transcoding` tests 只能证明 JSON transcoding path，不计入 native gRPC 完成度。

## Gate Matrix

| Gate | Required evidence | Owner | Exit |
| --- | --- | --- | --- |
| Runtime owner decision | 明确 native gRPC 是否属于 AHFL runtime v1 范围 | runtime owner | Go / No-Go |
| Benchmark | 三类真实 workload 的 HTTP/JSON transcoding vs native gRPC P50/P95/throughput/CPU 数据 | runtime + QE | P95 至少一个核心场景显著改善，否则 No-Go |
| Build matrix | Linux、macOS、Windows 冷构建/增量构建耗时和 dependency footprint | build + runtime | 任一平台不可维护则 No-Go |
| Dependency policy | Protobuf/gRPC C++ dependency 引入方式、license、vendoring/cache 策略 | process owner | 通过后才能改 CMake |
| Feature flag | `AHFL_ENABLE_GRPC_NATIVE` build flag、runtime config、disabled diagnostics | runtime owner | 默认关闭直到 release evidence 完整 |
| Fallback semantics | native unavailable、schema mismatch、transport failure、timeout 的 fail-closed/fallback 规则 | runtime owner | 与现有 JSON path 不冲突 |
| Test strategy | unit、integration、mock server、capability binding、release evidence | QE | 覆盖后才能进入 implementing |

## Owner Decision Requirements

`runtime_owner_decision` gate 标记为 `complete` 时，至少一个 evidence reference 必须是仓库内真实存在的 JSON artifact，schema 为 `ahfl.native_grpc_owner_decision.v1`。该 artifact 是 RFC0004 从 `draft` 进入 `accepted`、`postponed` 或 `rejected` 的唯一机器可复核 owner sign-off 入口，必须包含：

1. `decision`：只能是 `go` 或 `no-go`，且必须与 [native-grpc-decision-evidence.json](./native-grpc-decision-evidence.json) 的 `decision.state` 完全一致。
2. `owner`、`signed_off_at`、`decision_record`、`scope`、`rationale`：均为非空字符串。
3. `decision = "go"` 时，`required_before_implementation` 必须列出进入 `implementing` 前仍需完成的 gate：`benchmark`、`build_matrix`、`dependency_policy`、`feature_flag`、`fallback_semantics` 和 `test_strategy`。
4. `decision = "no-go"` 时，`continued_transport_scope` 必须说明继续维护现有 `grpc_json_transcoding` 路径的范围。

远端 URL 可以作为会议记录或审批系统的补充链接，但不能替代这份仓库内结构化 owner decision artifact；否则机器门禁无法证明 signed-off decision 与 RFC 状态一致。

## Benchmark Requirements

Benchmark 至少包含：

1. Small request / small response capability call。
2. Large structured response capability call。
3. High concurrency repeated capability calls。

每组必须记录 P50 / P95 / P99 latency、throughput、CPU time、peak RSS、serialized payload size，以及 client/server process startup 或 channel warmup 假设。

Benchmark 必须固定输入、输出 schema 和 mock server 行为；不得依赖外部 provider 或公网延迟。

`benchmark` gate 标记为 `complete` 时，至少一个 evidence reference 必须是仓库内真实存在的 JSON artifact，schema 为 `ahfl.native_grpc_benchmark.v1`。该 artifact 至少包含：

1. `environment.platform`、`environment.runner`、`environment.cpu_model`、`environment.timestamp`。
2. `runs[]` 覆盖三类 workload：`small_unary`、`large_structured_response`、`high_concurrency`。
3. 每类 workload 同时覆盖 `grpc_json_transcoding` 与 `native_grpc` 两个 transport。
4. 每个 run 的 `metrics` 必须包含 `p50_latency_ms`、`p95_latency_ms`、`p99_latency_ms`、`throughput_qps`、`cpu_time_ms`、`peak_rss_bytes` 和 `serialized_payload_bytes`。

远端 URL 可以作为补充证据，但不能替代这份仓库内结构化 benchmark artifact；否则 owner review 无法复核三类 workload 和两个 transport 是否真的被测过。

## Build Requirements

三平台评估必须记录：

1. Native dependency 获取方式。
2. Clean configure time。
3. Clean build time。
4. Incremental build time after touching runtime transport source。
5. Binary size delta。
6. CI cache strategy。
7. Failure mode and local developer setup impact。

`build_matrix` gate 标记为 `complete` 时，至少一个 evidence reference 必须是仓库内真实存在的 JSON artifact，schema 为 `ahfl.native_grpc_build_matrix.v1`。该 artifact 必须覆盖 `linux`、`macos`、`windows` 三个平台，并为每个平台记录 dependency source、clean configure/build time、incremental build time、binary size delta、CI cache strategy、failure mode 和 local setup impact。

其余实现前置 gate 也必须有仓库内结构化 JSON artifact：

1. `dependency_policy`：`ahfl.native_grpc_dependency_policy.v1`，记录 dependency source、license review、vendoring/cache policy、local setup impact 和 process owner approval。
2. `feature_flag`：`ahfl.native_grpc_feature_flag.v1`，记录 `AHFL_ENABLE_GRPC_NATIVE` build flag、runtime config、默认关闭策略、disabled diagnostics 和 release evidence gate。
3. `fallback_semantics`：`ahfl.native_grpc_fallback_semantics.v1`，覆盖 `native_unavailable`、`schema_mismatch`、`transport_failure`、`timeout` 四类场景的 fail-closed / fallback 行为、diagnostic 和 fallback transport。
4. `test_strategy`：`ahfl.native_grpc_test_strategy.v1`，覆盖 unit、integration、mock server、capability binding 和 release evidence 五类测试入口。

这些 schema artifact 不代替 owner sign-off；owner sign-off 由 `ahfl.native_grpc_owner_decision.v1` 单独表达。它们只保证 RFC0004 一旦从 `draft` 推进到实现阶段，所有实现前置证据都具备机器可复核结构。

## Decision Outputs

Go 决策必须写回 RFC0004：

1. Decision block 追加 Go 记录。
2. `status` 从 `draft` 推到 `accepted`。
3. Implementation Plan 切成 dependency wiring、proto contract、C++ facade、provider selection、diagnostics、tests、release evidence。

No-Go 决策必须写回 RFC0004：

1. Decision block 追加 No-Go 原因。
2. `status` 改为 `postponed` 或 `rejected`。
3. 明确继续维护 `grpc_json_transcoding` 的范围。

## Machine Gate Contract

`scripts/check-native-grpc-gate.py` 现在同时检查 RFC 状态、实现 marker 和结构化证据：

1. `draft`：允许 `native-grpc-decision-evidence.json` 中 gate 仍为 `missing` / `planned`，但仓库禁止 native gRPC build flag、C++ gRPC/Protobuf dependency wiring、native proto service contract 和 `native-grpc` 源文件。
2. `accepted`：必须有 `decision.state = "go"`，并且 `runtime_owner_decision` gate 为 `complete`，且带仓库内 `ahfl.native_grpc_owner_decision.v1` JSON artifact；仍不允许 implementation marker。
3. `implementing` / `implemented` / `stabilized`：必须有 `decision.state = "go"`，并且所有 gate 都为 `complete` 且带 evidence 引用；此后才允许 native implementation marker。
4. `postponed` / `rejected` / `out-of-scope`：必须有 `decision.state = "no-go"`，并且 `runtime_owner_decision` gate 为 `complete`，且带仓库内 `ahfl.native_grpc_owner_decision.v1` JSON artifact；native implementation marker 仍禁止。

证据引用必须指向可复核 artifact，例如 benchmark 报告、CI run、三平台 build log、dependency review、feature flag design、fallback semantics test matrix 或 release evidence archive 条目。空字符串、口头描述和没有 artifact 的 `complete` 状态都不能作为完成证据。机器门禁会拒绝 `TBD` / `TODO` / `DEFERRED` / `PLACEHOLDER` 证据引用、拒绝非 `http(s)` 的占位 URI，并要求仓库内相对路径 evidence artifact 真实存在且不能逃逸仓库根目录。

## Current State

截至 2026-07-08：

1. native gRPC owner decision 未完成。
2. benchmark evidence 未完成。
3. 三平台 build evidence 未完成。
4. feature flag 策略已有 RFC 草案描述，但未实现。
5. 仓库级机器门禁已落地：`scripts/check-native-grpc-gate.py` 会在 RFC0004 仍为 `draft` 时拒绝 native gRPC build flag、C++ gRPC/Protobuf dependency wiring 和 native proto service contract；该脚本已接入 CTest 与 CI。
6. Evidence artifact 引用门禁已落地：`complete` gate 和 Go/No-Go decision record 必须引用 `http(s)` URL 或仓库内真实存在的相对 artifact path；占位 URI、陈旧标记和路径逃逸都会 fail closed。
7. 结构化 gate artifact 门禁已落地：`runtime_owner_decision`、`benchmark`、`build_matrix`、`dependency_policy`、`feature_flag`、`fallback_semantics` 和 `test_strategy` gate 一旦标记为 `complete`，必须引用至少一份仓库内对应 schema JSON artifact。
8. 结构化证据文件已落到 [native-grpc-decision-evidence.json](./native-grpc-decision-evidence.json)，当前 `decision.state` 为 `pending`，所有 gate 均为 `missing`。
9. 因此 RFC0004 必须保持 `draft`，不能进入实现。
