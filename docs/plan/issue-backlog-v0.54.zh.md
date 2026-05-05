# AHFL V0.54 Issue Backlog

V0.54 聚焦 Workflow Integration。

## Planned

- [x] 定义 WorkflowRuntime 类（持有 WorkflowDecl + AgentRuntime 实例池）。
- [x] 定义 WorkflowExecutionContext（node outputs map, workflow input）。
- [x] 实现 node → AgentRuntime 实例化（绑定 node input 表达式）。
- [x] 实现 node input 依赖解析（node_name.output → Value 传递）。
- [x] 实现 DAG 拓扑排序执行（复用现有 runtime_session 调度逻辑）。
- [x] 实现跨 node output 传递（前序 node output 注入后续 node EvalContext）。
- [x] 实现 workflow 返回值组装（return 表达式求值）。
- [x] 实现 node 失败处理（单 node 失败时 workflow 状态标记）。
- [x] 集成 RuntimeSession artifact 输出（与现有 session.cpp 对接）。
- [x] 建立多 agent workflow 端到端测试。
- [x] 添加 `ahfl-v0.54` 测试标签。

## Deferred

- [ ] 并行 node 执行。
- [ ] Node 级重试与 failover。
- [ ] Workflow checkpoint 与 resume。
- [ ] 分布式 node 调度。
