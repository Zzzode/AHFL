# AHFL Optional Narrowing 设计草案

本文是关于在 AHFL Core V0.x 引入 **Optional 类型 narrowing**（受控控制流分析）的设计草案。当前阶段定位为 RFC，**仅讨论，不落代码**。

关联文档：

- [semantics-architecture.zh.md](./semantics-architecture.zh.md)
- [compiler-phase-boundaries.zh.md](./compiler-phase-boundaries.zh.md)
- [core-language.zh.md](../spec/core-language.zh.md)
- [semantics-typecheck-hardening.zh.md](../plans/semantics-typecheck-hardening.zh.md)

## 1. 背景与动机

AHFL 目前的双向类型检查器（bidirectional typing）显式不做控制流敏感的类型 narrowing：

```ahfl
// 当前行为：x 在 then-block 中仍是 Optional<Reply>
flow Greeting {
    state Idle {
        let x: Optional<Reply> = ctx.reply;
        if (x is Some) {
            // 用户必须再次解构，因为 x 仍是 Optional<Reply>
            // 而非细化后的 Reply
        }
    }
}
```

`semantics-typecheck-hardening.zh.md` 中明确把 **control-flow narrowing** 列为 non-goal——这是因为完整的 CFA（control flow analysis）会显著放大 typecheck 的复杂度（路径敏感、别名分析、终止性）。但在实际 DSL 使用中，`Optional<T>` 解构是高频模式，缺失会让用户写出冗余 cast / 重复 path 访问，影响人体工程学。

本 RFC 讨论：能否在**不破坏 typecheck 单遍 + 局部双向**架构、不引入完整 CFA 的前提下，实现一个**最小可行 narrowing**？

## 2. 范围与非目标

### 2.1 范围（必须支持）

- `if (x is Some) { /* x narrowed to T */ } else { /* x narrowed to Optional<T> 仍然 */ }`
- `if (x != none) { /* x narrowed to T */ }`
- `if (x is None) { /* x is Optional<T>, 仅排除 Some 分支 */ }`
- 嵌套 `if`（narrow 在外层 if 后保留）
- `if (a is Some && b is Some) { ... }` —— 短路求值后内部双 narrow

### 2.2 非目标（明确排除）

- 任何形式的 union type narrowing（AHFL 没有 union type）
- 类型 narrowing 通过赋值传播（`let y = x; if (y is Some) {...}` 不细化 `x`）
- 别名分析：narrowing 仅作用于 simple Identifier 或 simple `ctx.<field>`、`input.<field>` 单层路径
- 通过 capability 或 predicate 调用结果驱动 narrow（无 `assert!` semantic narrow）
- 跨循环 / 跨 goto 的 narrow 持久化
- 以上所有需要"反向数据流"或"过程间分析"的特性

### 2.3 保守失败原则

任何无法静态 100% 证明安全的 narrow 一律 **fall back 到不细化**——绝不在 narrow 上"猜测"。

## 3. 语法形态

无需修改文法。复用现有 `is`/`==`/`!=` + `none` 字面量：

```ahfl
if (x is Some) {...}      // narrow to inner T
if (x is None) {...}      // unchanged (still Optional<T>)
if (x != none) {...}      // narrow to inner T
if (x == none) {...}      // unchanged
```

## 4. 语义定义

设 `narrowable_path(e)` 为可被 narrow 的"简单路径表达式"：

```text
narrowable_path ::= Identifier
                 |  ctx . <field>
                 |  input . <field>
```

排除：嵌套成员访问 / 索引访问 / call / cast 后的 path。

设 `narrow_predicate(c)` 为 narrow 触发条件。条件 `c` 必须直接形如：

```text
narrow_predicate ::= narrowable_path "is" "Some"
                  |  narrowable_path "!=" "none"
                  |  ! ( narrowable_path "is" "None" )
                  |  ! ( narrowable_path "==" "none" )
                  |  narrow_predicate "&&" narrow_predicate   // 仅左到右
```

不支持：

- `c1 || c2`（不能从 disjunction 中推出确定性的 narrow）
- 嵌入 capability / predicate 调用的 condition
- 含副作用的 condition

### 4.1 narrowing 算法

伪代码：

```text
function check_if(if_stmt, ctx):
    cond = if_stmt.condition
    narrows = collect_narrows(cond)        // empty if 不匹配 narrow_predicate

    then_ctx = clone(ctx)
    apply(narrows, then_ctx)                // 把每个 path 的类型从 Optional<T> 改为 T
    check_block(if_stmt.then, then_ctx)

    if has_else:
        else_ctx = clone(ctx)
        // 注意：当前阶段 else 分支不做 inverse narrow（保守）
        check_block(if_stmt.else, else_ctx)
```

`collect_narrows(cond)` 仅识别上文 `narrow_predicate` 文法，否则返回空集合。

`apply(narrows, ctx)` 对每个被 narrow 的 path：

- 如果 path 是 Identifier，覆盖 `ctx.bindings[name]` 的类型为 `Optional<T>::first`
- 如果 path 是 `ctx.<field>` 或 `input.<field>`，把 `ctx`/`input` 类型 entry 替换为新的 struct 类型副本（`field` 类型变成 `T`）

后者比前者复杂——需要在 ValueContext 中能区分"原始 binding"vs"narrowed 副本"。

### 4.2 退出路径

narrow 仅在 then-block 内生效。**`if (x is Some) { return ... }; /* x 在这里不 narrow */`**——这是简化处理，避免对 return / goto / fall-through 做 reachability 分析。Rust / TypeScript 在这里有更精细的 fall-through narrow，但需要 CFG，超出本 RFC 范围。

## 5. 实现影响

### 5.1 typecheck 模块

- `check_statement (If 分支)`：新增 `collect_narrows` 调用，构造 narrow context
- `ValueContext::bindings`：现已是 `BindingMap = unordered_map<string, TypePtr>`，可直接 in-place 覆盖
- 对 `ctx.<field>` 的 narrow 需要"struct 副本"——hash-cons 后零代价，但 ValueContext 中每个 struct 路径可能要持 narrowed 的临时 type

### 5.2 IR Lowering 影响

无。typed AST/IR side table 已在 `ExpressionTypeInfo` 中按 NodeId 存储，narrowed 的 path 被 typecheck 用于 path 子表达式的本地推导，不污染上层 IR。

### 5.3 诊断影响

新增可选诊断：当 narrow 失败（path 形态过于复杂）时，发 `note: cannot narrow this expression; consider extracting it to a let-binding first`。

### 5.4 测试矩阵

- 正向 golden：`narrow_optional_is_some.ahfl`、`narrow_optional_neq_none.ahfl`、`narrow_nested_if.ahfl`、`narrow_with_and.ahfl`
- 负向 golden：`narrow_with_or.ahfl`（不细化）、`narrow_complex_path.ahfl`（不细化）、`narrow_after_capability_call.ahfl`（不细化）
- 单元：`tests/unit/compiler/semantics/narrowing.cpp` 验证 collect_narrows 的语法识别

## 6. 与业界对比

| 语言 | narrowing 范围 | 复杂度 |
|------|---------------|--------|
| TypeScript | union narrowing + control flow + alias | 完整 CFG |
| Kotlin | smart cast，含赋值传播 | DFA |
| Rust | match exhaustiveness + bind | pattern + scope |
| Swift | `if let` 语法显式引入新 binding | 无隐式 narrow |

AHFL 在本 RFC 下采取的策略**最接近 Swift `if let` 的隐式语义版**——即不引入新文法、narrow 范围最小、可静态识别。

## 7. 决策点

需要 reviewer 决议的开放问题：

1. **是否突破 non-goal**：当前 hardening 文档明确把 narrowing 列为 non-goal。本 RFC 承认这是有意识的范围放宽，需 maintainer 显式批准。
2. **`ctx.<field>` narrow 是否纳入首版**：实现复杂度高于 Identifier narrow。可选先只支持 Identifier，下阶段再扩展。
3. **else 分支 inverse narrow**：首版不做。是否应作为后续里程碑？
4. **诊断 fallback note**：开 / 关 / 仅 verbose 模式开。
5. **与 #11 任务里"先做最小可行版"的取舍**：是先落 Identifier-only narrow 再迭代，还是一次性把 `ctx.<field>` / `input.<field>` 一起做。

## 8. 推荐实施顺序（如批准）

- Phase 1：Identifier-only narrow，`is Some` / `!= none` 两种条件
- Phase 2：扩展到 `ctx.<field>` / `input.<field>` 单层路径
- Phase 3：`&&` 短路条件、嵌套 if
- Phase 4：（可选）else 分支 inverse narrow

每个 phase 独立可发布，独立 commit。建议 phase 1 优先于其他大型语义工作并落到 hardening 计划新增 milestone。

## 9. 风险

| 风险 | 缓解 |
|------|------|
| narrow 静默失败让用户困惑 | 开诊断 fallback note；docs 显式列出"narrow 不工作的情形" |
| 副作用条件被误识别为可 narrow | `collect_narrows` 强校验：condition 不能含 Call、capability，否则返回空 |
| 破坏 typecheck pass 的"无 CFA"哲学 | RFC 明确 narrowing 的 scope 局限于 if then-block 局部、不跨控制流持久化 |
| ctx schema 变体爆炸 | hash-cons 类型，narrowed struct 与原 struct 共享 interned 节点；只额外加一个 entry |

## 10. 决议

**待 maintainer 评审**。批准后将更新 `docs/plans/semantics-typecheck-hardening.zh.md` 新增 Milestone 10「Optional Narrowing Phase 1」并启动实现。
