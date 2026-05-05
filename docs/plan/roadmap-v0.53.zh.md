# AHFL V0.53 Roadmap

V0.53 建立 Agent State Machine Runtime。目标是驱动单个 agent 的状态转换循环：从初始状态开始，执行 state handler，根据 goto/return 指令转换状态，直到到达终态并返回输出。

## 目标

1. 定义 `AgentRuntime` 与 `AgentState` 类型。
2. 实现状态初始化（input 注入、ctx 初始化）。
3. 实现 state handler 查找与执行。
4. 实现状态转换（goto 指令处理）。
5. 实现终态检测与输出返回。
6. 实现 quota 检查（max_tool_calls、max_execution_time）。
7. 建立 CLI emission、单元测试与 `ahfl-v0.53` 测试标签。

## 非目标

1. 多 agent workflow 编排（v0.54）。
2. 真实 capability 调用（v0.55）。
3. 持久化与 checkpoint。
4. 并发执行。

## 里程碑

- [x] M0 冻结 agent state machine scope 与 AgentRuntime 定义
- [x] M1 实现状态初始化与 handler 查找
- [x] M2 实现状态转换循环（state → handler → transition → next state）
- [x] M3 实现 quota 检查与终态处理
- [x] M4 建立 docs、tests、CLI emission 与标签

## 完成定义

V0.53 完成后，AHFL 可以完整执行单个 agent。例如：

```ahfl
agent RefundAudit {
    states: [Init, Auditing, Approved, Rejected, Terminated];
    initial: Init;
    final: [Terminated];
    // ...
}

flow for RefundAudit {
    state Init { goto Auditing; }
    state Auditing { /* ... */ }
    state Approved { goto Terminated; }
    state Rejected { goto Terminated; }
    state Terminated { return RefundDecision { ... }; }
}

// 执行：Init → Auditing → (Approved|Rejected) → Terminated → Output
```
