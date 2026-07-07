---
rfc: "0003"
title: "Match Exhaustiveness Diagnostics"
status: "implemented"
area: ["language", "compiler", "tooling"]
stability: "developer-facing"
created: "2026-06-25"
updated: "2026-07-06"
authors: ["LLM-orchestrated"]
shepherd: "compiler"
owners:
  language: "compiler"
  compiler: "compiler"
  tooling: "tooling"
required_reviewers: ["language", "compiler", "tooling"]
tracking_issue: "none"
discussion: "none"
implementation_prs: []
decision_due: "2026-07-06"
---

# RFC 0003: Match Exhaustiveness Diagnostics

## Summary

AHFL 的 `match` 对 enum scrutinee 使用结构化穷尽性分析：

1. 缺失 enum variant 是 typecheck error：`MATCH_MISSING_PATTERNS`。
2. 被前序无条件 arm 完全覆盖的 arm 是 warning：`MATCH_UNREACHABLE_ARM`。
3. 与前序 arm 结构性相交的 arm 是 warning：`MATCH_OVERLAP`。

实现采用可替换的 top-level usefulness 子集，而不是把 ad-hoc 字符串拼接留在 `visit_match` 内部。

## Motivation

`match` 是 ADT 可用性的核心。只报告“match 不穷尽”不足以支撑真实工程：

- 用户需要知道缺哪个 variant，并能跳到 enum/variant 声明位置。
- 重复 arm 或 wildcard 后继续写具体 arm 是沉默 bug。
- LSP 和 CLI 需要稳定错误码和 related information，而不是依赖消息字符串。

Rust、Swift、OCaml、GHC 这类成熟实现都把 pattern usefulness 作为 type checking 的一部分。AHFL 当前阶段选择同一方向，但把实现范围限制在现有语言能力可以正确表达的子集。

## Goals

1. 对 enum scrutinee 的 `match` 提供稳定、结构化的穷尽性 error。
2. 对不可达 arm 和结构性重叠 arm 提供 warning，而不是静默接受。
3. 让 CLI 和 LSP 消费稳定诊断码、source range 与 related information。
4. 把覆盖分析从 `visit_match` 的 ad-hoc 逻辑中分离为可替换组件。
5. 在现有 AHFL pattern 能力内保持算法保守、可解释、可测试。

## Non-Goals

1. 不实现完整 nested-pattern usefulness matrix。
2. 不对 literal/range pattern 做数值域穷尽性证明。
3. 不支持 `#[non_exhaustive]` 或类似稳定 ABI 策略。
4. 不生成自动修复 edit。
5. 不把 overlap warning 默认升级为 error。

## Design

### Language Semantics

### Exhaustiveness

当 `match` scrutinee 的类型是 enum 时，compiler 必须证明所有 enum variant 都被覆盖。否则报告：

```text
typecheck.MATCH_MISSING_PATTERNS
```

诊断消息包含缺失 variant 名称；related notes 指向 enum 声明和每个缺失 variant 的声明范围。

以下 pattern 参与覆盖：

- `_`：覆盖所有 variant。
- catch-all binding，例如 `x`：覆盖所有 variant。
- bare variant 名，例如 `Red`：覆盖对应 variant。
- qualified variant 名，例如 `Color::Red`：覆盖对应 variant。
- tuple/struct variant pattern，例如 `Some(x)`、`Data { code }`：覆盖对应 variant；payload shape 的合法性仍由 pattern typecheck 负责。
- or-pattern，例如 `Red | Green`：覆盖所有可识别分支的并集。

### Guarded Arms

带 guard 的 arm 不能证明覆盖，因为 guard 在运行时可能为 false：

```ahfl
match c {
    Red if cond => 1,
    Green => 2,
    Blue => 3,
}
```

上例仍缺失无条件 `Red` 覆盖，必须报告 `MATCH_MISSING_PATTERNS`。

guarded arm 仍可参与结构性 overlap 提示；但它不会让后序同 variant arm 变成 unreachable。

### Unreachable Arms

如果一个 arm 的 pattern 被前序**无 guard** arm 的并集完全覆盖，则报告 warning：

```text
typecheck.MATCH_UNREACHABLE_ARM
```

related notes 指向使当前 arm 不可达的前序 arm pattern。arm 序号从 1 开始，按源码顺序计数。

### Overlap

如果后序 arm 与任意前序 arm 在结构上有非空交集，则报告 warning：

```text
typecheck.MATCH_OVERLAP
```

这条 warning 是 pattern-level 提示，不等价于不可达。例如 `Red if cond => ...` 后面的 `Red => ...` 会 overlap，但后者并不 unreachable。

## Diagnostic Contract

| Code | Severity | Message | Related information |
| --- | --- | --- | --- |
| `MATCH_MISSING_PATTERNS` | Error | `non-exhaustive match: missing patterns [{}]` | enum declaration；每个 missing variant declaration |
| `MATCH_UNREACHABLE_ARM` | Warning | `this match arm is unreachable` | `previously covered by arm #N` |
| `MATCH_OVERLAP` | Warning | `pattern overlaps with arm #N` | `overlaps with previous arm #N` |

RFC0003 不保留旧的非结构化穷尽性诊断兼容路径；公开契约只有上表三条诊断码。

### Algorithm

当前实现是 Maranget usefulness 思路的 top-level enum 子集。

每个 arm pattern 被规范化成一个 coverage atom：

- `All`：wildcard 或 catch-all binding。
- `VariantSet`：一个或多个 enum variant tag。
- `Empty`：当前 analyzer 不能证明覆盖的 pattern。

分析按源码顺序执行：

1. 将当前 pattern 规范化为 coverage atom。
2. 与所有前序 arm 的 coverage atom 两两求交，非空则产生 `MATCH_OVERLAP`。
3. 用此前所有**无 guard** arm 的覆盖集合判断当前 arm 是否 unreachable。
4. 如果当前 arm 无 guard，则把它加入 exhaustiveness 覆盖集合。
5. 结束后用 enum variant 全集减去无条件覆盖集合，得到 missing variants。

复杂度是 `O(N^2 + N*K)`，其中 `N` 是 arm 数，`K` 是 enum variant 数。AHFL 当前 enum/arm 规模下这比完整矩阵算法更简单，也足以覆盖现有语义。

## User Impact

1. 用户会在缺失 enum variant 时收到 `typecheck.MATCH_MISSING_PATTERNS` error，诊断包含缺失 variant 列表。
2. 用户会在 wildcard/catch-all 或重复 variant arm 之后收到不可达 warning。
3. 用户会在带 guard 的重叠 arm 上收到 overlap warning，但 guarded arm 不会使后续同 variant arm 不可达。
4. LSP 可以直接把 enum declaration、missing variant declaration 和 covering arm ranges 映射成 related information。
5. 旧的非结构化 missing-pattern 文本不再是用户可依赖接口；稳定接口是诊断码和 source ranges。

## Compatibility and Migration

这是开发者可见的诊断契约变更，不改变合法程序的运行语义。

破坏性影响：

1. 曾经只产生弱诊断或无诊断的非穷尽 `match` 现在会失败。
2. 曾经静默接受的不可达/重叠 arm 现在会产生 warning。
3. 依赖旧消息文本的测试需要迁移到稳定诊断码。

迁移方式：

1. 为 enum `match` 补齐缺失 variant、wildcard 或 catch-all binding。
2. 删除 wildcard/catch-all 之后不可达的具体 arm。
3. 对测试断言使用 `typecheck.MATCH_MISSING_PATTERNS`、`typecheck.MATCH_UNREACHABLE_ARM` 和 `typecheck.MATCH_OVERLAP`。

## Implementation Plan

- `match_exhaustiveness.{hpp,cpp}` 是唯一的覆盖分析组件；`typecheck_expr.cpp::visit_match` 只负责调用并把结果转换成 diagnostics。
- `ExpressionSema` 和 `TypeCheckPass` 支持 warning 级 typecheck diagnostics，避免把 unreachable/overlap 错误地建模成 error。
- missing-pattern diagnostics 使用 `Diagnostic::Related` 承载 enum/variant declaration ranges；LSP 可以直接映射为 related information。
- Analyzer 内部只使用 enum declaration metadata 和 AST pattern，不依赖用户可见字符串作为 canonical identity。

实现状态：已落库。当前实现位置：

- `src/compiler/semantics/match_exhaustiveness.hpp`
- `src/compiler/semantics/match_exhaustiveness.cpp`
- `src/compiler/semantics/typecheck_expr.cpp`
- `include/ahfl/base/support/diagnostics.hpp`
- `tests/unit/compiler/semantics/adt_match.cpp`

RFC0003 仍是 developer-facing，而不是 stable-language：当前算法覆盖 AHFL 已支持的 enum variant、wildcard、binding、or-pattern 外层语义，但还不是完整 Maranget 决策矩阵。

## Test Plan

当前覆盖点位于 `tests/unit/compiler/semantics/adt_match.cpp`：

- payload-less enum 完整覆盖。
- payload enum 完整覆盖。
- 缺失单个 variant。
- 缺失多个 variant。
- wildcard/catch-all binding 覆盖。
- duplicate variant arm overlap + unreachable。
- wildcard 后续 concrete arm overlap + unreachable。
- 完整 variant 覆盖之后的 catch-all unreachable。
- guarded arm 不贡献穷尽性覆盖。
- guarded arm 不使后续同 variant arm unreachable。
- related notes 包含 enum 和 missing variant 声明来源。

验证命令：

```bash
cmake --build --preset build-dev --target ahfl_semantics_adt_match_tests
ctest --preset test-dev --output-on-failure -R '^ahfl\.semantics\.adt_match_all$'
cmake --build --preset build-dev
ctest --preset test-dev --output-on-failure -j8
```

## Rollout and Stabilization

状态推进：

1. `draft`：定义 match missing-pattern、unreachable arm 和 overlap 诊断契约。
2. `implemented`：覆盖分析组件、typecheck 接入、稳定诊断码、related information 与 unit tests 已落库。
3. `stabilized`：当 nested pattern、literal/range pattern、payload destructuring 的语义稳定，并且完整 usefulness matrix 另行设计或本 RFC 明确收窄为永久子集后，再推进。

当前状态是 `implemented`。它不能直接标记为 `stabilized`，因为完整 Maranget matrix 仍不在本 RFC 范围内，且后续 pattern 语义可能扩展 analyzer 的输入域。

## Alternatives

1. 继续在 `visit_match` 内拼接字符串诊断。缺点是无法稳定提供 related information，也无法支持 LSP 结构化展示。
2. 立即实现完整 Maranget matrix。缺点是 AHFL 当前缺少稳定的 nested/literal/range pattern 语义，完整矩阵会提前冻结尚未成熟的语言面。
3. 只报告 missing patterns，不报告 unreachable/overlap。缺点是用户仍会保留死代码和重复 arm，编译器无法帮助发现常见逻辑错误。

## Open Questions

完整 Maranget matrix 已转入 [RFC 0011](./0011-pattern-usefulness-matrix.zh.md) 独立推进，前置条件是 AHFL 的 nested pattern、literal/range pattern 和 payload destructuring 语义全部稳定。届时本 RFC 的 analyzer 可以被替换，但诊断码和用户可见契约应保持不变。

## Decision History

- 2026-06-25: 初稿创建，目标是补齐 match missing/unreachable/overlap diagnostics。
- 2026-07-02: 纳入 RFC 目录，作为 RFC0003。
- 2026-07-06: 实现落库；删除旧的非结构化穷尽性诊断路径；明确 guarded arm 不贡献穷尽性覆盖。
- 2026-07-07: 将完整 pattern usefulness matrix 后续工作拆入 RFC 0011。

## References

1. Luc Maranget, "Compiling Pattern Matching to Good Decision Trees".
2. Rust pattern usefulness checking: rustc match checking design.
3. Swift `switch` exhaustiveness diagnostics.
