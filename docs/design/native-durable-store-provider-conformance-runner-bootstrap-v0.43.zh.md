# Native Durable Store Provider Conformance Runner Bootstrap (V0.43)

## 概述

V0.43 实现 Provider Contract Conformance Runner，在不访问真实 provider 的前提下，对 V0.40-V0.42 产出的 compatibility、registry、readiness artifact 执行确定性契约检查。

## 动机

V0.40 引入 compatibility report，V0.41 引入 provider registry 与 selection plan，V0.42 引入 production readiness evidence 与 review。这三层 artifact 共同组成 provider production integration 的准入信号，但缺乏一个统一的 conformance runner 将它们整合为单一的 pass/fail 判定。

V0.43 填补这一空白——在纯 artifact 环境中执行 cross-check，并以 deterministic report 形式输出结果。

## 设计

### 输入

- `ProviderCompatibilityReport` (V0.40)
- `ProviderRegistry` (V0.41)
- `ProviderProductionReadinessEvidence` (V0.42)

### 输出

- `ProviderConformanceReport` — 包含逐项检查结果与 pass/fail/skipped 统计

### 检查项目

| # | 检查名 | 目标 Artifact |
|---|--------|---------------|
| 1 | compatibility_status_passed | CompatibilityReport |
| 2 | mock_adapter_compatible | CompatibilityReport |
| 3 | local_filesystem_alpha_compatible | CompatibilityReport |
| 4 | capability_matrix_complete | CompatibilityReport |
| 5 | registry_mock_adapter_registered | Registry |
| 6 | registry_local_filesystem_alpha_registered | Registry |
| 7 | registry_compatibility_status_match | Registry |
| 8 | security_evidence_passed | ReadinessEvidence |
| 9 | recovery_evidence_passed | ReadinessEvidence |
| 10 | compatibility_evidence_passed | ReadinessEvidence |
| 11 | observability_evidence_passed | ReadinessEvidence |
| 12 | registry_evidence_passed | ReadinessEvidence |
| 13 | no_blocking_issues | ReadinessEvidence |
| 14 | session_id_consistency | Cross-artifact |
| 15 | workflow_name_consistency | Cross-artifact |
| 16 | input_fixture_consistency | Cross-artifact |

### CLI 命令

```
ahflc emit-durable-store-import-provider-conformance-report \
  --package <ahfl.package.json> \
  --capability-mocks <mocks.json> \
  --input-fixture <fixture> \
  [--workflow <canonical>] \
  [--run-id <id>] \
  [--search-root <dir>]... \
  <input.ahfl>
```

### Format Version

```
ahfl.durable-store-import-provider-conformance-report.v1
```

## 约束

- 不调用真实 provider SDK
- 不读取 secret value
- 不发起 network traffic
- 所有检查均为确定性（deterministic）
- 完全基于上游 artifact 的已有字段

## 文件清单

| 文件 | 角色 |
|------|------|
| `include/ahfl/durable_store_import/provider_conformance.hpp` | 数据结构定义 |
| `include/ahfl/backends/durable_store_import_provider_conformance_report.hpp` | Print 函数声明 |
| `src/durable_store_import/provider_conformance.cpp` | 验证与构建逻辑 |
| `src/backends/durable_store_import_provider_conformance_report.cpp` | Print 实现 |
