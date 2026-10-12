# P1 审查问题修复计划

日期：2026-10-12。

## 目标与范围

修复仓库审查发现的三项正确性问题：同一 Agent 多份合同的语义丢失、CLI 忽略恢复快照加载错误、Kubernetes CRD 输出违反目标平台 schema 约束。

责任模块分别为 compiler/semantics（及直接消费者）、tooling/cli 恢复入口、compiler/backends/infra 的 CRD 投影。只同步这些修复所需的规范、设计和回归；P2 计时、ABI 实现与一般文档整理不在本次范围。未经请求不创建提交。

## 阶段与验收

1. 独立决策 agent 核对合同基数与声明身份，在拥有者设计文档记录决策；实现 agent 与决策 agent 分离。
2. 补原生回归并修复三个责任模块。合同回归覆盖同文件及跨文件条款完整性；恢复回归验证损坏、不可读和未知 schema 在 capability 副作用前终止；CRD 回归覆盖目标平台结构约束与无法表示类型的拒绝路径。
3. 独立审查实现，解决重大发现，审计全部 diff 的必要性与并行改动。
4. 使用 dev preset 按完整 CPU 核数构建和测试；运行架构、RFC 与文档相关门禁。CRD 尽可能使用真实 Kubernetes 服务端验证。

成功标准：合同不静默丢失，只有不存在的快照允许新运行，CRD 引用及已覆盖的 enum/容器投影不生成目标平台禁止的结构，不能表示的递归/tuple/无类型 payload 明确拒绝；相关原生回归通过，完整验证结果及支持限制准确披露。

## 环境与约束

开始时工作树干净，没有 build 目录。开发机为 macOS，CPU 核数 18，AppleClang 21、Python 3.13。初始 Docker daemon 不可连接，kubectl 和 kind 未安装；随后以独立 Colima profile、临时官方 kind/kubectl 建立 Kubernetes v1.37.0 测试集群，不改动原有 default profile。真实服务端验证采用既有 infra target gate 的可选参数，不以局部 schema 检查替代平台验收。

## 已实现的行为与格式调整

- 合同：删除按 Agent 唯一获取合同的 API，独立保存来源和 payload；IR、SMV、handoff 共用 Agent 内累计条款编号。独立决策记录在 `docs/design/semantics-architecture.zh.md`。Typed HIR 与 cache 均升级 V2，旧 V1 缓存拒绝后重新编译，不保留双 reader。
- 恢复：快照 admission 移到 secret/provider 初始化前；仅 Missing 允许 fresh run，损坏、未知 schema、读取及状态查询错误退出 1，带路径诊断，禁止继续外部 capability 调用。持久化 recovery 格式未变。
- CRD：本地展开引用，修正 payload enum 的 structural schema、scalar Set topology、YAML 字符串引用；拒绝递归、位置 tuple、无具体 JSON 类型等不支持结构。用户可修复的 CRD lowering 错误按既有 CLI 路由输出普通错误。共享 OpenAPI mapper、OpenAPI/Terraform 产物未改。

## 首次默认构建暴露的解阻修复

默认 dev 构建没有绕开 solver、WASM 或 registry target，也没有降低严格 warning 门禁。必要修复均经独立只读审查：

- Z3：错误成员拼写；Python 3.12+ 移除的 distutils 改用 stdlib sysconfig。说明记录在 `third_party/z3/VENDORING.md`。
- NuSMV/MiniSat：patch 完成 stamp 与真正 library/header 输出分离；嵌套构建显式全核；旧格式宏词法空白、缺失 C 函数声明头。说明记录在 `third_party/nusmv/VENDORING.md`。
- 本地源码：删除未使用的常量、内部相等运算符、lambda capture；补缺失 `string_view` 头；Linux 专用 payload helper 按平台编译。
- Registry：保留 Linux 原有原子 CLOEXEC 路径，非 Linux 使用 POSIX socket/accept 后设置 FD_CLOEXEC，失败即关闭。该路径目前只有构建及静态审查证据，没有真实 RegistryServer 集成测试。
- 原生测试源码：移除两个未使用声明，将 Linux 专用 worker helper 按平台编译，保持现有测试行为。

## 剩余支持限制

- CRD mapper 未保留 Map key 类型/长度约束，该能力未在本次补齐；对象/数组 Set 在当前投影子集内拒绝，不能据此声称 Kubernetes 平台不支持它们。
- `ContractInstantiation` hover 目前无实际索引生成路径；本次仅迁移旧 API 消费者，没有新增合同 hover 功能。
- SMT BMC 保留逐合同查询与前提边界，新增合同回归集中于 Typed HIR/IR/SMV/handoff；不声称新增了 BMC 跨合同组合语义。
- 本机验证不覆盖 Linux 专用 payload store、自定义 MiniSat 配置或其他 Python 安装 scheme 的 Z3Py 安装流程。

## 验证记录

- `cmake --preset dev` 与 `cmake --build --preset build-dev -j18` 通过，完成全部默认 target；重试后的二次增量构建为 `ninja: no work to do`。
- `ctest --preset test-dev -j18 --output-on-failure` 执行全部 577 项，首次结果 568 通过、5 跳过、4 失败，耗时 189.82 秒。
- Core-IR 语料 pin 因新增 CRD fixture 失败，已登记 3 个可以降低为合法 Core 的拒绝用例，原生复测通过。初始未配置的 optional real SMV BMC 测试，在 dev cache 显式设置 bundled `NuSMV` 为 `AHFL_SMV_CHECKER` 后原生复测通过。4 个 Linux 专用测试仍跳过。
- 全量结果与上述复测合计 570 通过、4 跳过、3 未解决失败，不声称全量全绿。最终合同/IR/后端/恢复/真实 SMV BMC/架构/文档/RFC 专项 CTest **12/12 通过**，使用 `-j18`，耗时 21.56 秒。
- CRD 原生后端断言 **60/60**；既有 infra gate 在 Kubernetes v1.37.0 服务端明确拒绝旧 golden 的 `$ref`/`definitions`，接受新 5 组 fixture 的 **6 份 CRD**。3 个负例均退出 1、stdout 为空且不含 internal error。OpenAPI/Terraform snapshots 未变。临时集群、Colima profile/data、kubectl/kind/kubeconfig 已清理。
- 合同、恢复、CRD 与编译解阻变更由不同实现者及只读 reviewer 独立检查；默认 provenance 冲突、恢复 stat 异常、格式版本与文档 gate 漂移均已处理。最终 `git diff --check` 通过。

### 未解决的 Release 安装证据链

`ahfl.product.install_evidence_smoke` 在 clean Release 构建 vendored Z3 时失败：

- `third_party/z3/upstream/src/math/lp/tail_matrix.h:46` 在非 Z3DEBUG 路径调用不存在的 `get_elem`。
- `third_party/z3/upstream/src/math/lp/static_matrix.h:83` 调用不存在的 `get`。

已核对 HEAD，两处问题在本次修改前即存在；本次未修改这两个文件。该失败导致 `ahfl.product.beta_evidence_bundle_ready` 失败及 `ahfl.product.readme_capabilities` 缺少 install-smoke.json。安装/发布链应作为下一项修复。clean install smoke 仍内嵌 `-j8`，需在该后续修复中按仓库全核心规则纠正。

## 变更文件清单

清单来自最终工作树，未创建提交。

- `docs/design/ir-backend-architecture.zh.md`
- `docs/design/semantics-architecture.zh.md`
- `docs/plans/issue-backlog-global-gaps.zh.md`
- `docs/plans/p1-review-remediation.zh.md`
- `docs/plans/q4-2026-roadmap.zh.md`
- `docs/reference/ir-format.zh.md`
- `docs/tutorials/execution.zh.md`
- `docs/tutorials/incremental-compilation.zh.md`
- `docs/rfcs/0016-incremental-cache-contract.zh.md`
- `docs/rfcs/0026-ir-tower-and-execution-model.zh.md`
- `docs/spec/core-language.zh.md`
- `include/ahfl/compiler/handoff/package.hpp`
- `include/ahfl/compiler/ir/decl.hpp`
- `include/ahfl/compiler/ir/identity.hpp`
- `include/ahfl/compiler/semantics/declaration_info.hpp`
- `include/ahfl/compiler/semantics/typecheck.hpp`
- `include/ahfl/compiler/semantics/typed_hir.hpp`
- `include/ahfl/compiler/semantics/typed_hir_serialization.hpp`
- `scripts/check-ir-doc-sync.py`
- `src/compiler/backends/infra/k8s_crd.cpp`
- `src/compiler/backends/infra/k8s_crd.hpp`
- `src/compiler/backends/infra/lower.cpp`
- `src/compiler/backends/smv/smv_indexing.cpp`
- `src/compiler/backends/smv/smv_printer.hpp`
- `src/compiler/backends/smv/smv_specs.cpp`
- `src/compiler/backends/wasm/core_wasm_codegen.cpp`
- `src/compiler/handoff/package.cpp`
- `src/compiler/ir/core_json.cpp`
- `src/compiler/ir/identity.cpp`
- `src/compiler/ir/ir_lower.cpp`
- `src/compiler/semantics/type_environment.cpp`
- `src/compiler/semantics/typecheck.cpp`
- `src/compiler/semantics/typecheck_decls.cpp`
- `src/compiler/semantics/typed_hir_serialization.cpp`
- `src/runtime/engine/payload_store.cpp`
- `src/runtime/engine/workflow_recovery.cpp`
- `src/runtime/wasm_host/wasm3_engine.hpp`
- `src/runtime/wasm_host/workflow_session.cpp`
- `src/tooling/cache/cache_core.hpp`
- `src/tooling/cli/cli_analysis_helpers.cpp`
- `src/tooling/cli/workflow_run.cpp`
- `src/tooling/lsp/hover_service.cpp`
- `src/tooling/registry/server.cpp`
- `tests/golden/infra/crd_payload.ahfl`
- `tests/golden/infra/crd_payload.k8s-crd.yaml`
- `tests/golden/infra/crd_recursive.ahfl`
- `tests/golden/infra/crd_tuple.ahfl`
- `tests/golden/infra/crd_unsupported.ahfl`
- `tests/golden/infra/multi_agent.k8s-crd.yaml`
- `tests/golden/infra/palette.k8s-crd.yaml`
- `tests/golden/infra/service_mesh.k8s-crd.yaml`
- `tests/golden/infra/stateless.k8s-crd.yaml`
- `tests/integration/payload_store_worker.cpp`
- `tests/scripts/durable_resume_cli_smoke.py`
- `tests/scripts/infra_target_gate.py`
- `tests/unit/compiler/backends/registry.cpp`
- `tests/unit/compiler/backends/target_backends.cpp`
- `tests/unit/compiler/ir/core_json_round_trip.cpp`
- `tests/unit/compiler/query/frontend_queries_equiv.cpp`
- `tests/unit/compiler/query/hir_query_equiv.cpp`
- `tests/unit/compiler/semantics/typed_hir.cpp`
- `third_party/nusmv/MiniSat/CMakeLists.txt`
- `third_party/nusmv/MiniSat/MiniSat_v37dc6c6_nusmv.patch`
- `third_party/nusmv/NuSMV/code/nusmv/shell/trace/traceCmd.c`
- `third_party/nusmv/VENDORING.md`
- `third_party/z3/VENDORING.md`
- `third_party/z3/upstream/scripts/mk_util.py`
- `third_party/z3/upstream/src/math/lp/column_info.h`
