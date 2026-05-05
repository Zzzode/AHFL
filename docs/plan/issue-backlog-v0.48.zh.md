# AHFL V0.48 Issue Backlog

V0.48 聚焦 real provider opt-in execution guard。

## Planned

- [x] 定义 real provider opt-in guard artifact。
- [x] 校验 approval receipt。
- [x] 校验 config validation 与 runtime policy reference。
- [x] 校验 provider registry selection。
- [x] 输出 allow、deny 与 scoped override summary。
- [x] 添加 CLI emission、golden、direct tests 与 `ahfl-v0.48` 标签。

## Implemented

### v0.48.1 Core Data Model

- `include/ahfl/durable_store_import/provider_opt_in_guard.hpp`
  - `OptInDecision` 枚举: Allow / Deny / DenyWithOverride
  - `OptInDenialReason` 枚举: NoApproval / ConfigInvalid / RegistryMismatch / ReadinessNotMet / ExplicitDeny / ScopeExceeded
  - `OptInGateCheck` 结构: gate_name, passed, denial_reason, evidence_reference
  - `ScopedOverride` 结构: override_scope, override_justification, override_approver, is_valid
  - `ProviderOptInDecisionReport` 结构: 完整的 opt-in 决定报告
  - `validate_provider_opt_in_decision_report()` 验证函数
  - `build_provider_opt_in_decision_report()` 构建函数

### v0.48.2 Implementation

- `src/durable_store_import/provider_opt_in_guard.cpp`
  - 4 个 gate 检查: approval-receipt, config-bundle-validation, registry-selection-plan, security-constraints
  - 默认拒绝逻辑: `is_real_provider_traffic_allowed` 初始为 false
  - Denial reason 汇总

### v0.48.3 Backend & CLI

- `include/ahfl/backends/durable_store_import_provider_opt_in_decision_report.hpp`
- `src/backends/durable_store_import_provider_opt_in_decision_report.cpp`
- `src/cli/command_catalog.hpp` / `command_catalog.cpp`
- `src/cli/pipeline_runner.cpp`

### v0.48.4 Tests

- `tests/opt_in_guard/opt_in_guard.cpp`
- 8 个测试用例，覆盖所有关键路径

### v0.48.5 CMake

- `src/durable_store_import/CMakeLists.txt`
- `src/backends/CMakeLists.txt`
- `tests/cmake/TestTargets.cmake`
- `tests/cmake/ProjectTests.cmake`
- `tests/cmake/LabelTests.cmake`

## Deferred

- [ ] 默认启用真实 provider traffic。
- [ ] 执行 provider mutation。
- [ ] 读取 secret value。
- [ ] 绕过 operator approval。
