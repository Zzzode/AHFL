# 模型容量、自动压缩与 LLM 会话恢复

## 原始目标与范围

普通用户只配置连接信息，由系统识别容量、分配预算、自动压缩长工具上下文，
并将内部 LLM 会话恢复与现有 workflow checkpoint/resume 接合。
所属模块：LLM provider；跨模块修改限于运行时 pending capability 恢复接口、
CLI 运行配置、必要事件与回归基础设施。保留本工作区此前无关修改。

不引入外部 runtime 依赖，不猜测任意模型名称对应窗口，不将生成摘要当作
权威业务状态，不声称非幂等外部工具能在未知提交状态下 exactly-once 恢复。
显式配置仍可覆盖预算，稳定 workflow-recovery.v2 不重新定义。

## 成功标准

- 最小连接配置可自动识别支持的模型元数据；未知容量有明确来源与保守降级。
- 每个实际 provider/fallback 请求按其容量预留输出和压缩空间，完整输入不截断。
- 工具历史超过软阈值时自动压缩；保留 system、typed input、未完成目标、完整近期
  工具调用/结果，压缩请求自身也必须入预算；无法取得进展明确停止。
- session 在工具执行前保存意图、执行后保存结果；已持久完成的工具不重复执行。
- 冷启动恢复内部历史/压缩材料/轮次与外层 checkpoint，并拒绝绑定不匹配、损坏记录。
- 未知工具提交结果 fail closed 或通过真实幂等接口解决，不自动重复副作用。
- 原生与真实 CLI 本地 mock 流程证明自动容量、压缩、跨进程恢复、故障与覆盖规则。

## 阶段与职责

1. 两个独立专职决策：容量与自动预算；compact/session 恢复协议。
2. 记录设计和接口；独立审查正确性、成本、共享修改与兼容边界。
3. 分离文件所有权实现容量、provider 会话、runtime/CLI 接合和原生回归。
4. 全核构建，定向原生/CLI crash-recovery/压缩与既有恢复检查。
5. 独立 adversarial review，修复后相关广泛回归，更新教程与交付证据。

## 依赖与约束

OpenAI-compatible models 接口不一定提供上下文容量；具体策略由容量决策记录确定。
模型 token 计数能力和工具的幂等/查询协议属于外部契约，必须明确其证明边界。
18 核构建与测试；不提交；真实付费模型调用不作为必要本地验证前提。

## 当前状态

2026-10-12：实现完成，独立审查通过，P0/P1 问题全部关闭。未创建提交。

### 已交付行为

- 最小连接配置自动解析受支持模型的容量，逐个处理 fallback；未知服务使用保守
  应用预算。显式预算与物理容量覆盖继续可用，temperature 缺省不发送。
- 工具历史达到可用 prompt 容量的 80% 时自动语义压缩。保留规则、完整初始输入
  与近期完整交换；保存原始历史，可通过只读历史工具分块找回。压缩请求也计入
  预算；摘要非法、不能缩小或输入本身无法容纳时明确停止。
- LLM session 与外层 checkpoint 两阶段接合，默认 CLI 自动保存并续跑匹配任务。
  完成的工具回执及 capability memo 直接复用；未知外部提交状态停止恢复。
  冷启动保留对话、摘要、轮次、用量预留和成功/终止失败的报告元数据。
- 默认任务完成后清理私有运行记录，再次运行开始新任务。显式路径的新任务正常
  完成后清理本次记录；加载既有快照的恢复继续保留原有文件行为。
- 容量与会话分别由独立代理实现；主代理完成 runtime/CLI 接合。独立审查复核了
  重复副作用、摘要权威性、终态纯回放、费用标记、手工注入与清理边界。

共享修改限于内部 HTTP request transport、runtime 恢复/完成元数据读取接口、
执行配置附加与 CLI 接合。稳定 `ahfl.workflow-recovery`、公共事件和报告格式
保持不变。实现与决策见 [上下文生命周期设计](../design/llm-context-lifecycle.zh.md)，
用户操作见 [执行教程](../tutorials/execution.zh.md#模型会话自动恢复与压缩)。

### 最终验证

在 macOS 上使用全部 18 核完成构建和相关回归：

```bash
cmake --build --preset build-dev -j$(sysctl -n hw.ncpu)
ctest --preset test-dev -j$(sysctl -n hw.ncpu) --output-on-failure \
  -R 'ahfl.llm_provider.|ahfl.cli.llm_run_lease|ahfl.runtime.|ahfl.reference_workflow.(durable_resume_capstone|production_matrix)|ahflc.run.|ahfl.docs.|ahfl.architecture|ahfl.ir.core_json_round_trip' \
  -E 'real_llm|try_operator'
```

构建通过；相关 CTest 共 80 项，76 项通过、4 项平台限制跳过，零失败。
覆盖 provider 原生测试、会话严格准入与终止失败恢复、CLI 自动容量/压缩/跨进程
恢复、历史原文找回、工具不重复执行、用量/费用元数据、原有手工恢复、重复
frontier、事件与报告契约、IR fixture、文档和架构门禁。跳过项为
`core_wasm_resume_controller`、`core_wasm_resume_host`、`payload_store`、
`payload_store_crash_smoke`，均位于 `ahfl.runtime` 测试组。

此前全仓 CTest 共 599 项：590 项通过、4 项跳过、5 项失败。本次新增 fixture
未登记导致的 `ahfl.ir.core_json_round_trip` 已修复并在最终回归通过。
其余范围外失败仍需单独处理：

- `ahfl.product.install_evidence_smoke` 与 `ahfl.product.beta_evidence_bundle_ready`：
  vendored Z3 release 构建中 LP 模板 API 编译失败，包括 `tail_matrix::get_elem`
  和 `static_matrix::get`；`ahfl.product.readme_capabilities` 因安装证据未生成失败。
- `ahflc.run.try_operator.smoke`：并行任务的泛型函数/enum Wasm codegen 问题，
  由该任务处理；未计入本次最终通过范围。

不宣称全仓测试全绿。构建与测试日志分别为
`/tmp/ahfl-llm-final-build.log`、`/tmp/ahfl-llm-final-verification.log`，
此前全仓日志为 `/tmp/ahfl-llm-full-test.log`。`git diff --check` 通过。

### 剩余边界

token 计数是启发式估计；未知服务的保守预算不等于识别其物理容量。持久会话与
排他文件锁当前支持 macOS/Linux；本次模型链路验证使用本地 mock HTTP，未调用
真实付费服务。外部工具提交状态未知时无法保证任意副作用自动恢复，系统明确
停止；中断请求的保守预留可能过计。会话文件包含对话原文，HMAC 只校验完整性，
不加密，也不防持有同一账户与本地密钥的攻击者。
