# AHFL V0.54 Roadmap

V0.54 建立 Workflow Integration。目标是将 v0.51-v0.53 的 interpreter 接入现有 runtime_session，实现多 agent workflow 的真实执行。Workflow DAG 调度已在 v0.6-v0.9 完成，本版本只需将 mock_result_fixture 替换为真正的 agent interpreter 调用。

## 目标

1. 定义 `WorkflowRuntime` 与 `WorkflowExecutionContext` 类型。
2. 实现 node → agent 实例化（input 绑定）。
3. 实现跨 node 依赖传递（node.output → downstream node input）。
4. 实现 workflow 执行循环（按 DAG 顺序依次执行每个 agent node）。
5. 实现 workflow 返回值组装。
6. 集成现有 RuntimeSession artifact 输出。
7. 建立 CLI emission、单元测试与 `ahfl-v0.54` 测试标签。

## 非目标

1. 真实 capability 调用（v0.55）。
2. 并行 node 执行。
3. 持久化与 checkpoint。
4. 分布式执行。

## 里程碑

- [x] M0 冻结 workflow integration scope
- [x] M1 实现单个 agent node 执行（集成 AgentRuntime）
- [x] M2 实现多 node DAG 调度与依赖传递
- [x] M3 实现 workflow 返回值与 RuntimeSession 集成
- [x] M4 建立 docs、tests、CLI emission 与标签

## 完成定义

V0.54 完成后，AHFL 可以真实执行多 agent workflow。例如：

```ahfl
workflow RefundAuditWorkflow {
    input: RefundRequest;
    output: RefundDecision;

    node audit: RefundAudit(input);
    node notify: NotifyService(audit);
    node log: AuditLog(audit, notify);

    return: audit;
}

// 执行顺序：audit → notify → log → return
// 每个 node 执行完整的 agent state machine
// 依赖传递：audit.output → notify.input
```
