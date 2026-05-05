# AHFL V0.52 Roadmap

V0.52 建立 Statement Executor。目标是在 IR Statement 层实现确定性执行，支持 let 绑定、赋值、条件分支、状态跳转与返回，为 Agent State Machine 提供语句级执行能力。

## 目标

1. 定义 `ExecContext` 与 `ExecResult` 类型。
2. 实现 `LetStatement` 执行（变量绑定）。
3. 实现 `AssignStatement` 执行（ctx 字段更新）。
4. 实现 `IfStatement` 执行（条件分支）。
5. 实现 `GotoStatement` 执行（状态跳转标记）。
6. 实现 `ReturnStatement` 执行（返回输出）。
7. 实现 `AssertStatement` 执行（断言检查）。
8. 实现 `ExprStatement` 执行（表达式求值并丢弃结果）。
9. 建立 CLI emission、单元测试与 `ahfl-v0.52` 测试标签。

## 非目标

1. Agent 状态机驱动（v0.53）。
2. 真实 capability 调用（v0.55）。
3. 异常处理与错误恢复。

## 里程碑

- [x] M0 冻结 statement executor scope 与 ExecContext 定义
- [x] M1 实现 LetStatement 与 AssignStatement
- [x] M2 实现 IfStatement 与 ExprStatement
- [x] M3 实现 GotoStatement、ReturnStatement 与 AssertStatement
- [x] M4 建立 docs、tests、CLI emission 与标签

## 完成定义

V0.52 完成后，AHFL 可以对 IR Statement 序列进行确定性执行。例如：

```ahfl
// 以下语句可执行
let order = OrderQuery(input.order_id);   // LetStatement
ctx.result = decision.result;             // AssignStatement
if decision.result == AuditResult::Approve {  // IfStatement
    goto Approved;                        // GotoStatement
}
return RefundDecision { ... };            // ReturnStatement
```
