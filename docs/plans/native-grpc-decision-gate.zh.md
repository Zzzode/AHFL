# Native gRPC Decision Gate

审计日期：2026-07-09

本计划服务 [RFC 0004](../rfcs/0004-native-grpc-transport.zh.md)。当前 AHFL runtime 已有 `grpc_json_transcoding` 路径；这不能证明 native gRPC/Protobuf transport 应该进入实现。本门禁的目的，是在任何 native gRPC 代码落库前完成可复核的 Go/No-Go 决策。

机器可读证据记录在 [native-grpc-decision-evidence.json](./native-grpc-decision-evidence.json)。该文件使用 `ahfl.native_grpc_decision_evidence.v1` schema，由 `scripts/check-native-grpc-gate.py` 校验；RFC0004 仍为 `draft` 时可以保留 `missing` gate，但一旦推进到 `accepted` 或 `implementing`，状态和证据必须与下列门槛一致。所有证据时间戳使用日历有效的 UTC `YYYY-MM-DDTHH:MM:SSZ` 格式，远端证据引用只允许带非空 path、无 credential、无 fragment 的绝对 `https` URL；仓库内证据引用必须使用 POSIX 相对路径且不得包含控制字符；源码、TOML/CMake/YAML/JSON 配置和 proto contract 中的 native marker 都必须经过同一个 gate；feature flag、fallback transport、benchmark transport 和 diagnostics namespace 使用本文列出的 canonical value，不接受同义字符串。

## 决策原则

1. 没有 owner sign-off，不进入 `accepted`。
2. 没有真实 benchmark，不进入 `implementing`。
3. 没有 Linux / macOS / Windows 三平台构建证据，不引入 native gRPC dependency。
4. 没有 fail-closed diagnostics、显式 fallback 规则和 feature flag 策略，不改变 runtime transport 选择。
5. 现有 `grpc_json_transcoding` tests 只能证明 JSON transcoding path，不计入 native gRPC 完成度。

## Gate Matrix

| Gate | Required evidence | Owner | Exit |
| --- | --- | --- | --- |
| Runtime owner decision | 明确 native gRPC 是否属于 AHFL runtime v1 范围 | runtime owner | Go / No-Go |
| Benchmark | 三类真实 workload 的 `http_json`、`http2_json_optimized`、`grpc_json_transcoding`、`native_grpc` P50/P95/P99/throughput/CPU/RSS/payload 数据 | runtime + QE | native 必须击败最佳 JSON/HTTP2 baseline；否则 No-Go 或 Conditional-Go |
| Build matrix | Linux、macOS、Windows 冷构建/增量构建耗时和 dependency footprint | build + runtime | 任一平台不可维护则 No-Go |
| Dependency policy | Protobuf/gRPC C++ dependency 引入方式、license、vendoring/cache 策略 | process owner | 通过后才能改 CMake |
| Feature flag | `AHFL_ENABLE_GRPC_NATIVE` build flag、runtime config、disabled diagnostics | runtime owner | 默认关闭直到 release evidence 完整 |
| Fallback semantics | native unavailable、schema mismatch、transport failure、timeout 的 fail-closed/fallback 规则 | runtime owner | 与现有 JSON path 不冲突 |
| Test strategy | unit、integration、mock server、capability binding、release evidence | QE | 覆盖后才能进入 implementing |

## Owner Decision Requirements

`runtime_owner_decision` gate 标记为 `complete` 时，至少一个 evidence reference 必须是仓库内真实存在的 JSON artifact，schema 为 `ahfl.native_grpc_owner_decision.v1`。该 artifact 是 RFC0004 从 `draft` 进入 `accepted`、`postponed` 或 `rejected` 的唯一机器可复核 owner sign-off 入口，必须包含：

1. `decision`：只能是 `go` 或 `no-go`，且必须与 [native-grpc-decision-evidence.json](./native-grpc-decision-evidence.json) 的 `decision.state` 完全一致。
2. `owner`、`signed_off_at`、`decision_record`、`scope`、`rationale`：均为非空字符串；其中 `signed_off_at` 必须是日历有效的 UTC 时间戳，`owner`、`signed_off_at`、`decision_record` 必须分别与 [native-grpc-decision-evidence.json](./native-grpc-decision-evidence.json) 的 `decision.owner`、`decision.signed_off_at`、`decision.record` 完全一致。
3. `decision = "go"` 时，`required_before_implementation` 必须且只能列出进入 `implementing` 前仍需完成的 gate：`benchmark`、`build_matrix`、`dependency_policy`、`feature_flag`、`fallback_semantics` 和 `test_strategy`；不得缺失、重复或包含未知 gate；`continued_transport_scope` 不允许出现。
4. `decision = "no-go"` 时，`continued_transport_scope` 必须说明继续维护现有 `grpc_json_transcoding` 路径的范围；`required_before_implementation` 不允许出现。

远端 URL 可以作为会议记录或审批系统的补充链接，但不能替代这份仓库内结构化 owner decision artifact；否则机器门禁无法证明 signed-off decision 与 RFC 状态一致。

所有 `ahfl.native_grpc_*.v1` gate artifact 均为 strict JSON 和 closed schema：不得使用 `NaN`、`Infinity`、`-Infinity` 等非标准 JSON 常量；顶层对象和嵌套对象不得携带本文列出的字段之外的额外字段；所有列表型字段不得重复。任何 schema 扩展都必须同步更新 `scripts/check-native-grpc-gate.py`、`tests/scripts/transport_gate_smoke.py` 和本文档。

## Benchmark Requirements

Benchmark 至少包含：

1. Small request / small response capability call。
2. Large structured response capability call。
3. High concurrency repeated capability calls。

每组必须记录 P50 / P95 / P99 latency、throughput、CPU time、peak RSS、serialized payload size，以及 client/server process startup 或 channel warmup 假设。所有数值必须是有限非负 JSON number，不能使用 `NaN` 或无穷值占位。

Benchmark 必须固定输入、输出 schema 和 mock server 行为；不得依赖外部 provider 或公网延迟。

`benchmark` gate 标记为 `complete` 时，至少一个 evidence reference 必须是仓库内真实存在的 JSON artifact，schema 为 `ahfl.native_grpc_benchmark.v1`。该 artifact 至少包含：

1. `environment.platform`、`environment.runner`、`environment.cpu_model`、`environment.timestamp`，其中 `environment.timestamp` 必须是日历有效的 UTC 时间戳。
2. `runs[]` 覆盖三类 workload：`small_unary`、`large_structured_response`、`high_concurrency`。
3. 每类 workload 同时覆盖 `http_json`、`http2_json_optimized`、`grpc_json_transcoding` 与 `native_grpc` 四个 transport；前两者用于证明 native gRPC 不是在击败一个未优化 baseline。
4. 每个 run 的 `metrics` 必须包含 `p50_latency_ms`、`p95_latency_ms`、`p99_latency_ms`、`throughput_qps`、`cpu_time_ms`、`peak_rss_bytes` 和 `serialized_payload_bytes`。

远端 URL 可以作为补充证据，但不能替代这份仓库内结构化 benchmark artifact；否则 owner review 无法复核三类 workload 和四个 transport 是否真的被测过。

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
2. `feature_flag`：`ahfl.native_grpc_feature_flag.v1`，记录 `AHFL_ENABLE_GRPC_NATIVE` build flag、`runtime.transport.native_grpc` runtime config、默认关闭策略、`runtime.grpc_native.*` disabled diagnostics 和 `ahfl.runtime.native_grpc_release_evidence` release evidence gate。
3. `fallback_semantics`：`ahfl.native_grpc_fallback_semantics.v1`，覆盖 `native_unavailable`、`schema_mismatch`、`transport_failure`、`timeout` 四类场景的 fail-closed / fallback 行为；每个场景必须使用 `runtime.grpc_native.*` diagnostic，fallback transport 只能是 `grpc_json_transcoding` 或 `http_json`，并且 `fail_closed = true`。
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

1. RFC0004 frontmatter `status` 只能是 `draft`、`accepted`、`implementing`、`implemented`、`stabilized`、`postponed`、`rejected` 或 `out-of-scope`；未知状态一律 fail closed，不能把 status typo 当作 draft。
2. `native-grpc-decision-evidence.json` 的 `gates` 只能包含 Gate Matrix 中定义的七个 gate：`runtime_owner_decision`、`benchmark`、`build_matrix`、`dependency_policy`、`feature_flag`、`fallback_semantics` 和 `test_strategy`；未知 gate 名一律 fail closed，防止 typo 或私有门槛绕过审计。
3. `native-grpc-decision-evidence.json` 是 closed schema：顶层只允许 `schema`、`rfc`、`updated_at`、`decision`、`gates`；`decision` 只允许 `state`、`owner`、`signed_off_at`、`record`；每个 gate 只允许 `status`、`owner`、`evidence`、`notes`。新增字段必须先更新 checker、测试和本文档。
4. `native-grpc-decision-evidence.json.updated_at` 和所有 sign-off / benchmark timestamp 都必须是日历有效的 UTC `YYYY-MM-DDTHH:MM:SSZ`；`updated_at` 的日期部分不能早于 RFC0004 frontmatter `updated` 日期，避免 RFC 文本修改后继续复用过期证据。`decision.state = "pending"` 时，`owner`、`signed_off_at`、`record` 必须为空字符串，且 `runtime_owner_decision` gate 不能为 `complete`；`decision.state = "go"` 或 `"no-go"` 时，`owner` 和 `record` 必须非空，`signed_off_at` 必须是日历有效的 UTC 时间戳，且 `runtime_owner_decision` gate 必须为 `complete` 并带 evidence。
5. 每个 gate 的 `owner` 和 `notes` 必须非空，`evidence` 必须是字符串列表且不得重复。`status = "complete"` 时 `evidence` 必须非空；`status = "missing"`、`"planned"` 或 `"not_applicable"` 时 `evidence` 必须为空，避免用半成熟 artifact 冒充完成证据。
6. `draft` 状态必须保持 `decision.state = "pending"`；已经签署的 Go / No-Go decision 必须与 RFC0004 状态更新同一次落库，不能让 RFC 状态和 owner decision 分叉。
7. `feature_flag` artifact 必须使用精确字段值：`build_flag = "AHFL_ENABLE_GRPC_NATIVE"`、`runtime_config = "runtime.transport.native_grpc"`、`default_enabled = false`、`release_evidence_gate = "ahfl.runtime.native_grpc_release_evidence"`，且所有 disabled diagnostics 都必须位于 `runtime.grpc_native.` namespace 下。
8. `fallback_semantics` artifact 必须覆盖四类场景且不得重复；每个场景的 fallback transport 只能是 `grpc_json_transcoding` 或 `http_json`，diagnostic 必须位于 `runtime.grpc_native.` namespace 下，并保持 `fail_closed = true`，不能用 fail-open 语义通过 gate。
9. `draft`：允许 `native-grpc-decision-evidence.json` 中 gate 仍为 `missing` / `planned`，但仓库禁止 native gRPC build flag、`runtime.transport.native_grpc` / `[runtime.transport] native_grpc` runtime config、C++ gRPC/Protobuf dependency wiring、native proto service contract 和 `native-grpc` implementation artifact；marker 扫描覆盖源码、TOML、CMake、YAML、JSON 和 shell / Python 工具文件，防止通过 `ahfl.toml`、`CMakePresets.json` 或其他配置文件提前打开 native transport。
10. `accepted`：必须有 `decision.state = "go"`，并且 `runtime_owner_decision` gate 为 `complete`，且带仓库内 `ahfl.native_grpc_owner_decision.v1` JSON artifact；仍不允许 implementation marker。
11. `implementing` / `implemented` / `stabilized`：必须有 `decision.state = "go"`，并且所有 gate 都为 `complete` 且带 evidence 引用；此后才允许 native implementation marker。
12. `postponed` / `rejected` / `out-of-scope`：必须有 `decision.state = "no-go"`，并且 `runtime_owner_decision` gate 为 `complete`，且带仓库内 `ahfl.native_grpc_owner_decision.v1` JSON artifact；native implementation marker 仍禁止。

证据引用必须指向可复核 artifact，例如 benchmark 报告、CI run、三平台 build log、dependency review、feature flag design、fallback semantics test matrix 或 release evidence archive 条目。空字符串、口头描述和没有 artifact 的 `complete` 状态都不能作为完成证据。机器门禁会拒绝 `TBD` / `TODO` / `DEFERRED` / `PLACEHOLDER` 证据引用、拒绝非 `https` 远端 URL 的占位 URI、拒绝带 credential / fragment / 空 path 的 `https` URL，并要求仓库内相对路径 evidence artifact 使用 POSIX 分隔符、真实存在且不能逃逸仓库根目录。所有仓库内 JSON evidence artifact 都按 strict JSON 解析，`NaN` / `Infinity` / `-Infinity` 直接 fail closed。

## Current State

截至 2026-07-09：

1. native gRPC owner decision 未完成。
2. benchmark evidence 未完成。
3. 三平台 build evidence 未完成。
4. feature flag 策略已有 RFC 草案描述，但未实现。
5. 仓库级机器门禁已落地：`scripts/check-native-grpc-gate.py` 会在 RFC0004 仍为 `draft` 时拒绝 native gRPC build flag、runtime config marker、C++ gRPC/Protobuf dependency wiring、native proto service contract 和 `native-grpc` implementation artifact；marker 扫描覆盖源码、TOML、CMake、YAML、JSON 和 shell / Python 工具文件，该脚本已接入 CTest 与 CI。
6. Evidence artifact 引用门禁已落地：`complete` gate 和 Go/No-Go decision record 必须引用 `https` URL 或仓库内真实存在的相对 artifact path；占位 URI、陈旧标记和路径逃逸都会 fail closed。
7. 结构化 gate artifact 门禁已落地：`runtime_owner_decision`、`benchmark`、`build_matrix`、`dependency_policy`、`feature_flag`、`fallback_semantics` 和 `test_strategy` gate 一旦标记为 `complete`，必须引用至少一份仓库内对应 schema JSON artifact；这些 artifact 使用 strict JSON 读取，非标准数值常量会被拒绝。
8. 结构化证据文件已落到 [native-grpc-decision-evidence.json](./native-grpc-decision-evidence.json)，当前 `decision.state` 为 `pending`，所有 gate 均为 `missing`。
9. 因此 RFC0004 必须保持 `draft`，不能进入实现。
