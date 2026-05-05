# AHFL V0.51 Issue Backlog

V0.51 聚焦 Expression Evaluator。

## Planned

- [x] 定义 Value variant 类型（NoneValue, BoolValue, IntValue, FloatValue, StringValue, DecimalValue, DurationValue, StructValue, ListValue, SetValue, MapValue, EnumValue, OptionalValue）。
- [x] 定义 EvalContext（input scope, ctx scope, node output scope, local scope）。
- [x] 实现字面量求值（NoneLiteral, BoolLiteral, IntegerLiteral, FloatLiteral, DecimalLiteral, StringLiteral, DurationLiteral）。
- [x] 实现 BinaryOp 求值（Add, Sub, Mul, Div, Mod, Eq, Neq, Lt, Lte, Gt, Gte, And, Or, Implies）。
- [x] 实现 UnaryOp 求值（Not, Negate, Positive）。
- [x] 实现 PathExpr 求值（input.field, ctx.field, node_name.field）。
- [x] 实现 StructLiteralExpr 求值。
- [x] 实现 ListLiteralExpr 求值。
- [x] 实现 SomeExpr / QualifiedValueExpr 求值。
- [x] 建立 evaluator 单元测试（覆盖所有 Expr variant）。
- [x] 添加 `ahfl-v0.51` 测试标签。

## Deferred

- [ ] CallExpr 求值（需要 CapabilityBridge，延迟到 v0.55）。
- [ ] MapLiteral / SetLiteral 求值（V0.1 Core 未定义字面量语法）。
- [ ] 类型运行时验证（依赖已有 typecheck pass）。
