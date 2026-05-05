# Native Durable Store Provider Config Bundle Validation Bootstrap (V0.45)

## 概要

V0.45 引入 production config bundle validation，用于在不读取 secret value、不发起网络连接的条件下，静态校验生产配置包的完整性与一致性。

## 动机

在 V0.44 及之前版本中，provider 部署前缺少对配置包完整性的结构化校验。运维人员需要手动确认 provider reference、secret handle、endpoint shape 与 environment binding 是否就绪。V0.45 通过自动化的静态校验弥补这一空白。

## 设计

### 输入

- `ProviderSelectionPlan`：来自 V0.43 的 provider 选择结果
- `ProviderConfigSnapshotPlaceholder`：来自 V0.28 的 config snapshot 占位

### 输出

- `ProviderConfigBundleValidationReport`：包含以下校验结果
  - Provider reference checks
  - Secret handle checks（仅校验 presence / scope / redaction policy，不读取 value）
  - Endpoint shape checks
  - Environment binding checks
  - 汇总统计与 blocking summary

### 安全约束

| 约束 | 保证 |
|------|------|
| 不读取 secret value | `reads_secret_value == false` |
| 不发起网络连接 | `opens_network_connection == false` |
| 不生成生产配置 | `generates_production_config == false` |

### CLI 命令

```
ahflc emit-durable-store-import-provider-config-bundle-validation-report \
    --package <ahfl.package.json> \
    --capability-mocks <mocks.json> \
    --input-fixture <fixture> \
    [--workflow <canonical>] \
    [--run-id <id>] \
    [--search-root <dir>]... \
    <input.ahfl>
```

## 文件结构

```
include/ahfl/durable_store_import/provider_config_bundle_validation.hpp
include/ahfl/backends/durable_store_import_provider_config_bundle_validation_report.hpp
src/durable_store_import/provider_config_bundle_validation.cpp
src/backends/durable_store_import_provider_config_bundle_validation_report.cpp
tests/config_bundle/config_bundle_validation.cpp
```

## 完成定义

- validation report 可通过 CLI emit 命令输出
- 所有安全约束在 validate 函数中断言
- 输出不包含任何敏感信息（redacted）
- CI 标签 `ahfl-v0.45` 覆盖相关测试
