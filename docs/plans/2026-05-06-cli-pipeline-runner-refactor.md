# CLI Pipeline Runner 拆分重构 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 将 `src/cli/pipeline_runner.cpp`（4098 行）拆分为多个编译单元，消除模板重复，并保持所有现有 CLI 命令行为不变。

**Architecture:** 将 `PackagePipelineContext` 和 `emit_*` 函数签名提取到共享内部头文件；按领域拆分命令实现到三个 `.cpp`：core commands、durable store import 阶段链、provider 命令；主文件仅保留分发表和公共 API。

**Tech Stack:** C++23, CMake 3.22+

---

## Task 1: 创建 `pipeline_context.hpp` 内部共享头文件

**Files:**
- Create: `src/cli/pipeline_context.hpp`

把 `PackagePipelineContext` 结构体、`PackageCommandHandler` 类型别名和 `invoke_package_command` 模板从 `pipeline_runner.cpp` 底部的匿名命名空间提升为内部头文件（在 `ahfl::cli::detail` 命名空间），供多个 `.cpp` 共用。

---

## Task 2: 创建 `pipeline_core_commands.cpp`

**Files:**
- Create: `src/cli/pipeline_core_commands.hpp`
- Create: `src/cli/pipeline_core_commands.cpp`

迁移 `pipeline_runner.cpp` 中 L159-L518 的核心 emit 函数：
- `emit_execution_plan_with_diagnostics`
- `emit_dry_run_trace_with_diagnostics`
- `emit_runtime_session_with_diagnostics`
- `emit_execution_journal_with_diagnostics`
- `emit_replay_view_with_diagnostics`
- `emit_audit_report_with_diagnostics`
- `emit_scheduler_snapshot_with_diagnostics`
- `emit_scheduler_review_with_diagnostics`
- `emit_checkpoint_record_with_diagnostics`
- `emit_checkpoint_review_with_diagnostics`
- `emit_persistence_descriptor_with_diagnostics`
- `emit_persistence_review_with_diagnostics`
- `emit_export_manifest_with_diagnostics`
- `emit_export_review_with_diagnostics`
- `emit_store_import_descriptor_with_diagnostics`
- `emit_store_import_review_with_diagnostics`

头文件声明所有函数。

---

## Task 3: 创建 `pipeline_durable_store_import.cpp`

**Files:**
- Create: `src/cli/pipeline_durable_store_import.hpp`
- Create: `src/cli/pipeline_durable_store_import.cpp`

迁移 `pipeline_runner.cpp` L520-L997 的 durable store import 主阶段链：
- `build_durable_store_import_request_for_cli`
- `build_durable_store_import_decision_for_cli`
- `build_durable_store_import_receipt_for_cli`
- `build_durable_store_import_receipt_persistence_request_for_cli`
- `emit_durable_store_import_request_with_diagnostics`
- `emit_durable_store_import_review_with_diagnostics`
- `emit_durable_store_import_decision_with_diagnostics`
- `emit_durable_store_import_decision_review_with_diagnostics`
- `emit_durable_store_import_receipt_with_diagnostics`
- `emit_durable_store_import_receipt_persistence_request_with_diagnostics`
- `emit_durable_store_import_receipt_review_with_diagnostics`
- `emit_durable_store_import_receipt_persistence_review_with_diagnostics`
- `emit_durable_store_import_receipt_persistence_response_with_diagnostics`
- `emit_durable_store_import_receipt_persistence_response_review_with_diagnostics`
- `emit_durable_store_import_adapter_execution_with_diagnostics`
- `emit_durable_store_import_recovery_preview_with_diagnostics`
- 以及所有对应的 `build_*_for_cli` 辅助函数

头文件声明所有需跨文件调用的 `build_*_for_cli` 和 `emit_*` 函数。

---

## Task 4: 创建 `pipeline_durable_store_import_provider.cpp`

**Files:**
- Create: `src/cli/pipeline_durable_store_import_provider.hpp`
- Create: `src/cli/pipeline_durable_store_import_provider.cpp`

迁移 L998-L3812 的所有 provider 命令函数。这包括：
- `emit_durable_store_import_provider_write_attempt_with_diagnostics`
- `emit_durable_store_import_provider_recovery_handoff_with_diagnostics`
- `emit_durable_store_import_provider_driver_binding_with_diagnostics`
- ... 以及所有其他 provider 相关的 `emit_*` 和 `build_*_for_cli` 函数

---

## Task 5: 精简 `pipeline_runner.cpp` 为纯分发入口

**Files:**
- Modify: `src/cli/pipeline_runner.cpp`

重构后的 `pipeline_runner.cpp` 只保留：
1. include 新的内部头文件
2. `PackagePipelineContext` 定义（已移到 `pipeline_context.hpp`）
3. `kPackageCommandDispatch[]` 分发表
4. `dispatch_package_command_impl` 和公共 `dispatch_package_command` 函数

---

## Task 6: 更新 `CMakeLists.txt`

**Files:**
- Modify: `src/cli/CMakeLists.txt`

添加新的编译单元到 `ahflc` 目标：
```cmake
add_executable(ahflc
    ahflc.cpp
    cli_pipeline_artifacts.cpp
    command_catalog.cpp
    pipeline_runner.cpp
    pipeline_core_commands.cpp
    pipeline_durable_store_import.cpp
    pipeline_durable_store_import_provider.cpp
)
```

---

## Task 7: 编译验证

运行 CMake 配置和构建，确保零编译错误。

Run: `cmake --preset default && cmake --build --preset default`

---

## Task 8: 测试验证

运行完整测试套件确认行为不变。

Run: `ctest --preset default`
