# AHFL V0.52 Issue Backlog

V0.52 聚焦 Statement Executor。

## Planned

- [x] 定义 ExecContext（继承 EvalContext，增加 mutable ctx、local bindings、transition state）。
- [x] 定义 ExecResult（Ok, GotoTransition, ReturnValue, AssertFailed, Error）。
- [x] 实现 LetStatement 执行（绑定到 local scope）。
- [x] 实现 AssignStatement 执行（仅允许赋值 ctx 字段）。
- [x] 实现 IfStatement 执行（条件求值 + 分支选择）。
- [x] 实现 GotoStatement 执行（返回 GotoTransition 信号）。
- [x] 实现 ReturnStatement 执行（求值表达式并返回 ReturnValue）。
- [x] 实现 AssertStatement 执行（求值为 false 时返回 AssertFailed）。
- [x] 实现 ExprStatement 执行（求值并丢弃）。
- [x] 实现 Block 执行（顺序执行 statements 直到遇到 Goto/Return）。
- [x] 建立 executor 单元测试（覆盖所有 Statement variant）。
- [x] 添加 `ahfl-v0.52` 测试标签。

## Deferred

- [ ] 异常处理与错误恢复。
- [ ] 调试信息与执行追踪。
- [ ] 执行步数限制。
