# AHFL V0.53 Issue Backlog

V0.53 聚焦 Agent State Machine Runtime。

## Planned

- [x] 定义 AgentRuntime 类（持有 AgentDecl + FlowDecl + EvalContext）。
- [x] 定义 AgentState（current_state, ctx_values, tool_call_count, elapsed_time）。
- [x] 实现状态初始化（注入 input → EvalContext, 初始化 ctx 为默认值）。
- [x] 实现 state handler 查找（根据 current_state 在 FlowDecl 中定位 StateHandler）。
- [x] 实现 handler 执行（调用 Statement Executor 执行 Block）。
- [x] 实现 GotoTransition 处理（验证 transition 合法性，更新 current_state）。
- [x] 实现 ReturnValue 处理（验证终态，组装 output）。
- [x] 实现 transition 合法性验证（检查 AgentDecl.transitions）。
- [x] 实现 quota 检查（max_tool_calls, max_execution_time）。
- [x] 实现无限循环检测（state 重复计数）。
- [x] 建立 agent runtime 单元测试。
- [x] 添加 `ahfl-v0.53` 测试标签。

## Deferred

- [ ] StatePolicy retry/retry_on/timeout 真实执行（需要 CapabilityBridge）。
- [ ] 并发 state handler 执行。
- [ ] 状态 checkpoint 与 resume。
