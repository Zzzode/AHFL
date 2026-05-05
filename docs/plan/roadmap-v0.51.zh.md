# AHFL V0.51 Roadmap

V0.51 建立 Expression Evaluator。目标是在 IR Expr 层实现确定性求值，支持基础类型、运算符、路径访问与结构体字面量，为后续 Statement Executor 和 Agent State Machine 奠定基础。

## 目标

1. 定义 `EvalContext` 与 `Value` 类型系统。
2. 实现字面量求值（Bool、Int、Float、String、Duration、Decimal）。
3. 实现二元运算符（算术、比较、逻辑）。
4. 实现一元运算符（not、negate）。
5. 实现路径访问（input.field、ctx.field、node.output）。
6. 实现结构体字面量与列表字面量求值。
7. 建立 CLI emission、单元测试与 `ahfl-v0.51` 测试标签。

## 非目标

1. 函数调用表达式（CallExpr）——延迟到 v0.52 与 capability bridge 一起实现。
2. 类型检查（已有 typecheck pass）。
3. 真实 capability 调用。
4. Statement 执行。

## 里程碑

- [x] M0 冻结 expression evaluator scope 与 Value 类型定义
- [x] M1 实现 EvalContext 与字面量求值
- [x] M2 实现二元/一元运算符求值
- [x] M3 实现路径访问与结构体/列表字面量
- [x] M4 建立 docs、tests、CLI emission 与标签

## 完成定义

V0.51 完成后，AHFL 可以对任意 IR Expr 进行确定性求值，返回类型安全的 Value 对象。例如：

```ahfl
// 以下表达式均可求值
1 + 2                           // => IntValue(3)
input.order_id                  // => StringValue("...")
ctx.result == AuditResult::Approve  // => BoolValue(true)
RefundDecision { result: ctx.result, reason: "ok" }  // => StructValue(...)
```
