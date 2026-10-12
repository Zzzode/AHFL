# LLM 默认配置与上下文管理调整

本文记录默认配置调整的已完成阶段。后续容量识别、自动 compact 和内部会话恢复
见[实施计划](./llm-context-and-session-implementation.zh.md)及[当前设计](../design/llm-context-lifecycle.zh.md)。

## 目标与范围

普通用户只需填写 endpoint、model 和凭证引用，其他字段继承内置策略。
所属模块为 `src/runtime/providers/llm/`；修改其默认配置、请求预算与原生测试，
同步执行教程和 execution-demo 配置。评估 compact 与 checkpoint/resume 的边界，
不在本次引入长期会话持久化、模型目录或改变 workflow-recovery.v2。

## 阶段与验收

1. 独立决策：明确应用预算与模型上下文窗口的区别、温度参数策略和证明边界。
2. 实现：完整业务输入不截断；所有轮次的完整请求与工具 schema 在发送前预算。
3. 教程：最小配置优先，预算字段列为高级可选覆盖；记录 compact/resume 设计。
4. 验证：全核构建；原生配置/请求/工具循环回归、CLI runtime smoke、文档门禁。
5. 独立审查：检查超限是否在每次 HTTP 发送前拒绝、是否自动重放已执行工具，以及是否夸大恢复保证。

成功标准：省略五个用户给出的字段可运行；显式覆盖仍生效；超大输入不损坏；
工具历史/定义计入每次请求预算；不把重启请求称为已支持语义压缩或工具恢复。

## 当前状态

2026-10-12：本轮完成。普通用户最小配置为 endpoint/model/api_key_secret，
保留 1024/3072/4096 应用预算、JSON 默认开启，temperature 改为 optional 并默认省略。
普通/工具/fallback 请求统一策略；完整输入不截断，所有工具轮次的完整请求发送前
检查预算并预留响应额度。删除字节截断实现及相关无调用 stub/enum，修复预算加法溢出。

改动范围：LLM provider 配置与请求构造、token 估计和原生测试；execution-demo 配置
及 README、入门/执行教程、文档索引、LLM 上下文设计与本计划。未改变外层 runtime
恢复逻辑、workflow-recovery.v2 或稳定 event JSON。显式 temperature 配置继续生效；
其缺省发送行为改变为采用服务端默认值。

验证证据：

- `cmake --build --preset build-dev -j$(sysctl -n hw.ncpu)` 成功（18 核）。
- LLM/config/tools/failure/secret + 文档/架构定向 ctest：14/14 通过；其中
  `ahfl.llm_provider.all` 为 523/523 断言通过。覆盖默认/显式温度、普通/工具请求
  JSON mode、fallback、完整 UTF-8/JSON、schema/result/assistant arguments/累计历史预算。
- runtime + CLI run + durable resume capstone ctest：69 项，65 通过，4 项 POSIX/Linux
  专用测试在 macOS 跳过；无失败。真实 CLI 本地 mock、绑定、恢复和 profile 流程通过。
- `git diff --check` 通过；独立只读审查未发现 P0/P1/P2；三项 P3（死代码、显式
  fallback 温度覆盖、发送前失败保证的文案）已修复并复核关闭。

限制：token 计数仍是 UTF-8 字节启发式估计；固定值为应用预算，尚无模型容量发现。
语义 compact、内部 LLM session 持久恢复未实现，未进行付费真实模型网络调用。
工具可在产生大 result 后导致下一轮预算拒绝；本 invocation 不自动重放，外层重试
仍须工具幂等协议。既有通用 workflow checkpoint 不能推断为内部工具 exactly-once。
