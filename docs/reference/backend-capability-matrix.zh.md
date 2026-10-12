# AHFL 导出产物选择参考

根据要完成的任务选择 `ahflc emit` 产物。导出的计划、部署描述和验证模型有各自的语义范围；运行 workflow 使用 `ahflc run`，构建可执行 Wasm 使用 `ahflc build`。

## 按任务选择

| 任务 | 命令 | 输出与使用限制 |
| --- | --- | --- |
| 阅读声明、flow 和 workflow 结构 | `emit ir` | 人类可读 IR；排版不是稳定脚本接口。 |
| 在脚本中遍历验证与编排结构 | `emit ir-json` | `ahfl.ir` JSON；校验 `format` 后读取结构化字段。 |
| 检查执行层结构 | `emit core-ir-json` | `ahfl.core` JSON；包含逻辑身份，不含目标平台物理布局。 |
| 检查优化效果 | `emit opt-ir` / `emit opt-ir-json` | 文本或 `AHFL_OPT_IR` JSON；用 `-O` 比较优化结果。 |
| 查看程序能力摘要 | `emit summary` | 声明、来源和行为摘要；不保留完整表达式树。 |
| 交接给宿主运行环境 | `emit native-json --manifest <ahfl.toml> --target <name>` | handoff package，包含目标、绑定槽位、策略义务和 workflow 执行图。 |
| 查看 workflow 的 DAG（有向无环图） | `emit execution-plan` | `ahfl.execution-plan` 执行计划，不包含可执行代码。 |
| 用固定输入和 mock 检查执行计划 | `emit dry-run-trace` | 确定性模拟轨迹；不调用真实服务。 |
| 审查 package 导出内容 | `emit package-review` | package 评审视图。 |
| 生成公开 API 快照和文档 | `emit public-api` / `emit public-api-docs` | 根据 `[exports].modules` 生成 JSON 快照或 Markdown。 |
| 比较 API 兼容性 | `emit public-api-diff` | 比较两个快照；可配合 SemVer 版本检查。 |
| 导出控制与时序验证模型 | `emit smv` | NuSMV 模型；适用范围见[保证与验证指南](../tutorials/assurance.zh.md)。 |
| 检查数据合同的编码 | `emit smt` | SMT-LIB 文本；不启动 solver，不等于证明已通过。 |
| 读取保证案例 | `emit assurance-json` | 结构化验证目标、范围和结果。 |
| 生成部署描述 | `emit k8s-crd` / `emit openapi` / `emit terraform` | CRD、OpenAPI 或 Terraform 描述；不能替代 workflow 执行与验证。 |

命令参数见 [CLI 参考](./cli-commands.zh.md)。

## 结构化产物包含什么

| 信息 | `ir` / `ir-json` | `native-json` | `summary` | `smv` |
| --- | --- | --- | --- | --- |
| declaration 结构 | 完整 | 可执行目标子集 | 计数与摘要 | 验证所需部分 |
| declaration 来源 | 有 | 有 | 计数与开关 | 不直接输出 |
| package metadata | 无 | 有 | 无 | 无 |
| execution graph / lifecycle | 无 | 有 | 无 | 间接使用 |
| capability binding slots | 无 | 有 | 无 | 无 |
| policy obligations | 无 | 有 | 无 | 间接使用 |
| `formal_observations` | 有 | 有 | 计数与摘要 | 用于验证 |
| flow statement tree | 完整 | 无 | 无 | 不直接输出 |
| flow summary | 有 | 无 | 汇总 | 用于控制模型 |
| workflow expression tree | 完整 | 无 | 无 | 不直接输出 |
| workflow value summary | 有 | 有 | 汇总 | 不使用 |
| temporal formulas | 完整 | 不导出公式树 | 无 | 可支持部分转换为 LTL |

IR 的 JSON 结构、身份和源范围见 [IR 格式参考](./ir-format.zh.md)。表中的结构完整性不代表模型能证明完整运行时值语义；形式化结果必须结合证明范围和未支持目标读取。

## 接入宿主运行环境

`emit native-json` 输出 handoff package。它包含 `execution_graph`、workflow 节点的 `lifecycle` 摘要和 package 级 `policy_obligations`，便于宿主审查绑定和运行约束。

package identity、entry 和 export 来自 manifest target metadata；没有 metadata 来源时对应区段为空。需要完整配置和导出步骤时，参阅 [Native handoff 指南](../tutorials/native-handoff.zh.md)；运行事件和恢复格式见 [Native runtime 产物参考](./native-runtime-artifacts.zh.md)。
