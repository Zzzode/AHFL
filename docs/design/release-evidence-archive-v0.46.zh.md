# AHFL Release Evidence Archive V0.46

本文冻结 AHFL V0.46 中 release evidence archive manifest 的最小边界。它消费 V0.42–V0.45 的 readiness、conformance、schema compatibility 与 config bundle validation evidence，目标是生成可复现的 release evidence 汇总视图，明确证据缺失、过期或不兼容状态。

关联文档：

- [roadmap-v0.46.zh.md](../plan/roadmap-v0.46.zh.md)
- [issue-backlog-v0.46.zh.md](../plan/issue-backlog-v0.46.zh.md)

## 目标

1. 定义 `ReleaseEvidenceArchiveManifest` artifact 的最小 schema。
2. 汇总四类 evidence：conformance、schema_compatibility、config_validation、readiness。
3. 记录 artifact digest、identity 与 generation timestamp。
4. 输出 archive_summary、missing_evidence_summary、stale_evidence_summary 与 incompatible_evidence_summary。
5. 提供 `is_release_ready` 布尔判定。

## 非目标

1. 上传 release archive 到外部系统。
2. 生成组织级发布审批。
3. 包含 secret value 或 credential。
4. 执行真实 provider mutation。
5. 真实 SHA-256 digest 计算（使用确定性 placeholder）。

## 数据模型

### EvidenceItem

| 字段 | 类型 | 描述 |
|------|------|------|
| evidence_type | string | "conformance" / "schema_compatibility" / "config_validation" / "readiness" |
| evidence_identity | string | 引用的 artifact identity |
| format_version | string | 该 evidence 的 format_version |
| digest | string | SHA-256 摘要或语义哈希 |
| generation_timestamp | string | UTC ISO 8601 时间戳 |
| is_present | bool | evidence 是否存在 |
| is_valid | bool | evidence 是否通过校验 |

### ReleaseEvidenceArchiveManifest

| 字段 | 类型 | 描述 |
|------|------|------|
| format_version | string | 固定为 `ahfl.durable-store-import-provider-release-evidence-archive-manifest.v1` |
| workflow_canonical_name | string | 工作流标识 |
| session_id | string | 会话 ID |
| run_id | optional string | 运行 ID |
| evidence_items | vector<EvidenceItem> | 所有 evidence 条目 |
| total_evidence_count | int | 总数 |
| present_evidence_count | int | 存在数 |
| valid_evidence_count | int | 有效数 |
| missing_evidence_count | int | 缺失数 |
| invalid_evidence_count | int | 无效数 |
| is_release_ready | bool | 是否 release ready |

## 构建逻辑

`build_release_evidence_archive_manifest` 接收四个上游 artifact：

1. `ProviderConformanceReport` (v0.43)
2. `ProviderSchemaCompatibilityReport` (v0.44)
3. `ProviderConfigBundleValidationReport` (v0.45)
4. `ProviderProductionReadinessEvidence` (v0.42)

从每个 artifact 提取 evidence item，计算 present/valid/missing/invalid 统计，生成汇总摘要，判定 `is_release_ready`。

## CLI 集成

```
ahflc emit-durable-store-import-provider-release-evidence-archive-manifest \
  --package <ahfl.package.json> \
  --capability-mocks <mocks.json> \
  --input-fixture <fixture> \
  [--workflow <canonical>] \
  [--run-id <id>] \
  [--search-root <dir>]... \
  <input.ahfl>
```

## 约束

- 所有时间戳使用 UTC ISO 8601 格式。
- 不包含任何 secret value 或 credential。
- 输出为确定性的（deterministic）。
- 不对外系统产生副作用。

## 完成定义

V0.46 完成后：
- `ctest --preset test-dev -L ahfl-v0.46` 全部通过。
- CLI 可输出 release evidence archive manifest。
- 文档、golden 与测试标签就绪。
