---
rfc: "0003"
title: "Match Exhaustiveness Diagnostics"
status: "draft"
area: ["language", "compiler", "tooling"]
stability: "developer-facing"
created: "2026-06-25"
updated: "2026-07-02"
authors: ["LLM-orchestrated"]
shepherd: "TBD"
owners:
  language: "TBD"
  compiler: "TBD"
  tooling: "TBD"
required_reviewers: ["language", "compiler", "tooling"]
tracking_issue: "TBD"
discussion: "TBD"
implementation_prs: []
decision_due: "2026-07-12"
---

# RFC 0003: Match Exhaustiveness Diagnostics

## Summary

Upgrade match diagnostics with structured missing-pattern errors, unreachable-arm warnings, and overlap warnings.

## Motivation

AHFL already has match syntax, but weak exhaustiveness and reachability diagnostics make algebraic data types hard to use safely. Mature language implementations make these diagnostics precise, structured, and tied to source ranges.

## Goals

1. Report missing enum variants with structured diagnostic codes.
2. Warn on unreachable match arms.
3. Warn when a later arm overlaps an earlier arm.
4. Preserve source ranges and related notes for CLI and LSP consumers.

## Non-Goals

1. Do not implement full nested-pattern usefulness checking in the first version.
2. Do not add `#[non_exhaustive]`.
3. Do not generate automatic source edits in this RFC.

## Design

Use a small usefulness-analysis subset over top-level enum variants, wildcard, and binding patterns. The design should be replaceable by a fuller Maranget-style matrix algorithm when nested patterns become first-class.

## User Impact

Users receive better errors for incomplete matches and warnings for arms that cannot run. The diagnostics affect CLI output, LSP related information, and golden tests.

## Compatibility and Migration

The missing-pattern error should preserve the old failure condition while improving the diagnostic code and related information. New warnings must remain non-blocking unless the user enables warnings-as-errors.

## Implementation Plan

Add a match exhaustiveness analyzer, register diagnostic codes, integrate it into type checking, and update CLI/LSP rendering tests.

## Test Plan

Add unit and golden tests for missing variants, wildcard coverage, duplicate variants, binding patterns, unreachable arms, overlaps, and related source ranges.

## Rollout and Stabilization

Start with enum/wildcard/binding coverage only. Stabilization requires conformance tests, LSP related-information coverage, and updated diagnostic reference material.

## Alternatives

1. Keep the current string-list non-exhaustive diagnostic.
2. Implement a full Maranget matrix algorithm immediately.
3. Move the analysis to an external linter.

## Open Questions

1. Whether overlap warnings should become errors under the default compiler profile.
2. How much or-pattern approximation is acceptable before nested-pattern support lands.

## Decision History

- 2026-06-25: Initial draft created during Wave 18 planning.
- 2026-07-02: Canonicalized as RFC 0003.

## Detailed Design Notes

<!--
RFC-ID: 0003
Status: DRAFT
Created: 2026-06-25
Author: LLM-orchestrated
Shepherds: TBD
PR: TBD
Implementation Issue: TBD
Depends On: NONE
-->

---

## 1. Metadata

| 字段 | 值 |
| --- | --- |
| **RFC-ID** | `0003` |
| **Title** | Match Diagnostics Completeness — Missing Patterns / Unreachable Arm / Overlap Warning |
| **Status** | DRAFT |
| **Created** | 2026-06-25 |
| **Shepherd** | TBD |
| **Consult** | RFC 0001, RFC 0002 |
| **Decision Due** | 2026-07-12 |
| **Blocking Items** | Maranget 简化算法 复杂度讨论 |
| **Author** | LLM-orchestrated |
| **Shepherds** | TBD |
| **PR** | TBD |
| **Implementation Issue** | TBD |
| **Depends On** | NONE |

> 说明：本 RFC 不依赖 RFC 0002（若存在）。现有代码已在 `typecheck_expr.cpp` 第 822-961 行实现了一个初版穷尽性检查（variant 名单 + wildcard 兜底），本 RFC 把它从「只给字符串列表」升级为结构化诊断码，并新增 unreachable arm 与 overlap warning。

---

## 2. Motivation & Scope

### 2.1 Background / 背景

AHFL 已经有 `match` 表达式语法和一个简化版穷尽性检查（`MATCH_NOT_EXHAUSTIVE`），但存在三个明显的诊断质量缺口：

1. **穷尽性报告不结构化** — `MatchNotExhaustive` 目前只输出一个「missing patterns」的字符串拼接，没有最小反例集，没有 origin note 把缺失的 variant 指向 enum 声明位置，也没有「建议插入的 arm」。用户只能看到名字，不知道要改哪里。
2. **没有 unreachable arm 检测** — 当一个 arm 前面已经有 wildcard 或该 variant 的前序 arm 完全覆盖时，这一 arm 的代码**永远不会执行**，但编译器目前静默放过，是经典的沉默 bug 来源。
3. **没有 arm 重叠检测** — 两个 arm 的模式对同一值都可匹配（例如先写 `_ => ...` 再写 `Some(x) => ...`，或同一 variant 写了两次）时，应报告 `warning` 级别的 overlap 提醒，指出与第几号 arm 冲突。

这三类诊断都是成熟语言（Rust、Swift、Scala、F#）的标配，是 match 表达式「可用」与「好用」之间的分水岭。在 AHFL 当前阶段（ADT 已落地、typed HIR 迁移到 P1/P2 主干）推进，风险最低、收益最高。

### 2.2 Scope / 适用范围

本 RFC 只覆盖 **enum variant + wildcard + binding pattern** 的 match 诊断，具体：

- **穷尽性**（error 级）：对 enum scrutinee，计算当前 arm 集合没有覆盖到的值构造器集合，输出 `MATCH_MISSING_PATTERNS`，附带**最小反例集**（最小集合 ≠ 完整笛卡尔积；在本阶段因为不处理嵌套 tuple payload，所以最小反例就是缺失 variant 的名字列表）。
- **不可达 arm**（warning 级）：一个 arm 被前面所有 arm 的并集完全覆盖时，输出 `MATCH_UNREACHABLE_ARM`，指向该 arm 的 `pattern` 范围，并附加一个 origin note 指向「使其不可达的前序 arm 集合」。
- **arm 重叠**（warning 级）：后序 arm 与前序任一号 arm 非空相交时，输出 `MATCH_OVERLAP`，附带 `pattern overlaps with arm #N` 的 origin note（`N` 从 1 开始计数，按源码顺序）。

### 2.3 Typical Use Cases / 典型场景

#### Case 1 — 缺失 variant

```ahfl
enum Color { Red, Green, Blue }

fn describe(c: Color): String {
    match c {
        Red => "warm",
        // 漏掉 Green、Blue
    }    // ^ error: non-exhaustive match — missing patterns: [Green, Blue]
         //      note: enum `Color` declared here (src/file.ahfl:1:6)
}
```

#### Case 2 — wildcard 让后序 arm 不可达

```ahfl
fn head<T>(xs: List<T>): Option<T> {
    match xs {
        _ => None,           // warning: pattern overlaps with arm #2, #3
        Cons(x, _) => Some(x),// warning: this arm is unreachable (covered by arm #1)
        Nil => None,          // warning: this arm is unreachable (covered by arm #1)
    }
}
```

#### Case 3 — 同一 variant 写了两次

```ahfl
enum Result<T, E> { Ok(T), Err(E) }

fn unwrap_or(r: Result<Int, String>, fallback: Int): Int {
    match r {
        Ok(v) => v,
        Ok(x) => x * 2,        // warning: pattern overlaps with arm #1
                               // warning: this arm is unreachable (covered by arm #1)
        Err(_) => fallback,
    }
}
```

---

## 3. Design

### 3.1 Algorithm / 算法选择

采用 **Luc Maranget 模式匹配编译算法**的一个**极小子集**（Rustc `librustc_mir_build/thir/pattern/_match` 的缩小版），只处理：

- **原子模式（Atomic Pattern）**：`Variant(variant_name)`、`Wildcard`、`Binding(name)`。其中 `Binding` 在覆盖语义上等价于 `Wildcard`（绑定本身不影响可达性/穷尽性）。
- **不处理**：tuple payload 的分量、literal、range、or-pattern、nested pattern、guard。（见 §5 Non-Goals。）

因为范围被限制，算法可以**用「模式-对」的两两判定 + 集合运算**表达，不需要构造完整的决策矩阵：

1. **规范化（Canonicalize）**：把每个 arm 的模式翻译为「原子模式集合的并」：
   - `Variant(A)` → `{ {tag=A} }`
   - `Binding(x)` / `Wildcard(_)` → `{ ∗ }`（全匹配原子）
2. **覆盖度计算（Covered Set）**：按 arm 顺序，每加入一个 arm，就把它的原子集合并入 `covered`；如果某 arm 的所有原子都已在 `covered` 中，则标为 **不可达**。
3. **重叠检测（Overlap）**：对 arm `j` 与所有 `i < j`，若 `patterns[i] ∩ patterns[j] ≠ ∅`，则报告 overlap。注意 wildcard 与任何 pattern 都相交。
4. **穷尽性（Exhaustiveness）**：设 enum 的 variant 全集为 `V`，`covered_tags = { tag | Variant(tag) ∈ covered } ∪ (若含 ∗ 则 = V)`；若 `covered_tags ≠ V`，则缺失集合 = `V \ covered_tags`。
<!-- See [RFC 0002](0002-optional-narrowing.zh.md) §3 for type-narrowing witness 在 pattern overlap 判定中的共享 -->

复杂度：O(N² · K)，其中 N 是 arm 数，K 是 variant 数；对 AHFL 现阶段的 enum 规模（K ≤ 20，N ≤ 30），完全可接受。

### 3.2 New Diagnostic Codes / 新增三条诊断码

三条都归到 `DiagnosticCategory::TypeCheck`，在 `include/ahfl/base/support/diagnostics.hpp` 现有的 `namespace typecheck` 块中追加：

| 标识符 | 严重级别 | 模板消息 | related notes（origin） |
| --- | --- | --- | --- |
| `MATCH_MISSING_PATTERNS` | **Error** | `"non-exhaustive match: missing patterns [{}]"` (花括号是逗号分隔的最小反例 variant 名列表) | 1 条 note 指向 `scrutinee` 的 enum 声明位置；每条 missing variant 可追加 1 条 note 指向该 variant 的声明行。 |
| `MATCH_UNREACHABLE_ARM` | **Warning** | `"this match arm is unreachable"` | 1+ 条 note：`"previously covered by arm #N"` + 该 arm 的 pattern range。 |
| `MATCH_OVERLAP` | **Warning** | `"pattern overlaps with arm #N"` | 1 条 note：指向冲突的前序 arm #N 的 pattern range。 |

> 命名说明：保留已存在的 `MatchNotExhaustive` 作为「scrutinee 不是 enum 或 enum 解析失败时的回退码」，本 RFC 的 `MATCH_MISSING_PATTERNS` 是**升级版**——当能拿到完整 enum 信息时用新码，信息缺失时退化到旧码，保持向后兼容。

### 3.3 Integration Points / 集成位置

- **诊断计算**：新建 `src/compiler/semantics/match_exhaustiveness.{hpp,cpp}`（或在 `typecheck_expr.cpp` 的 `visit_match` 内以一个静态 helper 函数开始，后续抽出），函数签名：
  ```cpp
  struct MatchDiag {
      struct Missing { SourceRange arm_range; std::vector<std::string> variants; SourceRange enum_decl_range; };
      struct Unreachable { size_t arm_index; SourceRange pattern_range; std::vector<size_t> covering_arms; };
      struct Overlap { size_t arm_index; SourceRange pattern_range; size_t overlaps_with; SourceRange other_range; };
      std::optional<Missing> missing;
      std::vector<Unreachable> unreachable;
      std::vector<Overlap> overlaps;
  };
  MatchDiag analyze_match(const EnumTypeInfo& info, std::span<const std::unique_ptr<ast::MatchArmSyntax>> arms);
  ```
- **调用点**：`src/compiler/semantics/typecheck_expr.cpp` 第 827 行的 `visit_match()`，在现有 exhaustive 检查处替换为 `analyze_match()`，然后根据返回的 `MatchDiag` 通过 `services_.typecheck_error_here()` / `services_.typecheck_warning_here()` 分别发出。
- **错误码注册**：`include/ahfl/base/support/diagnostics.hpp` 第 206-223 行附近（现有 `Match*` 组）追加三条新码；对应 `messages::typecheck::*` 在消息模板定义处同步追加。

### 3.4 Origin Notes / 结构化定位

每条诊断都按仓库现有「diagnostic + relatedInformation 列表」的规范（见 `diagnostics.hpp` 第 102-220 行），把「指向 enum 声明 / 前序 arm / missing variant 行」的信息作为 `Diagnostic::Related` 附带，从而 CLI 渲染器（`src/tooling/cli/diagnostic_consumer.cpp`）和未来的 LSP handler 可以直接输出，无需后续改造。

---

## 4. Alternatives Considered / 替代方案

### 4.1 方案对比表

| # | 方案 | Pros / 优点 | Cons / 缺点 |
| --- | --- | --- | --- |
| A | **Maranget 极小子集（推荐）** — 两两相交 + 集合覆盖，只支持 Variant/Wildcard/Binding，在本阶段实现。 | (1) 代码量小（单文件 ~300 行），易于 review；(2) 覆盖所有 AHFL 当前代码会写出的场景；(3) 与 Rust/Swift 的警告文本一致，用户熟悉；(4) 未来加 tuple/or-pattern 时可平滑替换为完整决策树算法。 | (1) 当 payload 是 tuple 时会误判「unreachable」（tuple 内 wildcard 与其他 pattern 的相交不被检测），但 §5 已显式声明不处理；(2) 对 or-pattern 退化为「只看首支」的近似，重叠检测可能漏报。 |
| B | **一步到位上完整 Maranget 决策矩阵** — 支持 tuple payload、literal、or-pattern、nested 构造子、guard 对穷尽性的削弱。 | (1) 一次性把能力做到 Rust 水平；(2) 后续 no-op。 | (1) 复杂度陡增：完整算法要维护一个 `Matrix<Pattern>`，要求 Pattern 类型具备 `split` / `specialize` / `is_useful` 三个操作（Rust 对应 1k+ SLOC）；(2) AHFL 现有 typed HIR 尚未完成 nested pattern binding 推断，即便做了决策矩阵，也无法端到端校验；(3) 交付窗口延长到 2+ 个 wave，占用 P3/P4 预算。 |
| C | **不改编译器，在 formatter/linter 外挂做启发式检测** — 用 AST walk 判断 wildcard 是否出现在最后，判断 variant 是否重复出现。 | (1) 零改 typechecker；(2) 容易写单测。 | (1) 穷尽性检测要求 enum 解析 + name resolve，本质上要重新实现 resolve 的一半；(2) CLI 用户默认不会单独跑 linter，等于把错误推迟到 lint 阶段，违背「typecheck 即告警」的习惯；(3) 无法复用已有的 `DiagnosticReporter` 管线，维护两套 diagnostic 输出格式。 |

### 4.2 Recommendation / 推荐

采用 **方案 A**；方案 B 作为 P3+ 的独立 RFC（与 nested patterns / or-patterns 一起合入）；方案 C 不推荐。

---

## 5. Non-Goals / 非目标

以下内容**明确不在本 RFC 范围内**，后续单独 RFC：

1. **不处理嵌套 pattern（tuple / nested enum）的深层重叠。** 例如 `Some((_, true))` 与 `Some((0, _))` 是否相交、是否穷尽 — 本 RFC 只看最外层 variant tag。
2. **不处理 or-patterns（`A | B`）。** 遇到 or-pattern 时，分析器保守地：(a) 若任一分支都已 covered 则判 unreachable（不判「部分覆盖」）；(b) overlap 只对或分支的首支做判定；(c) 穷尽性只把所有或分支名加入 covered（不做笛卡尔乘积拆分）。
3. **不处理 literal pattern（数字、字符串、bool）的覆盖度分析。** 即使用户写了 `match n { 0 => ..., 1 => ..., _ => ... }`，本 RFC 也不报告「literal 覆盖率 2/∞ 不完全」，只按 wildcard 判定为 exhaustive。
4. **不处理 guard 对穷尽/可达性的削弱。** 例如 `Some(x) if x > 0 => ...` 与 `Some(x) => ...` 的相交 — 本阶段把所有 guard 视为**不影响覆盖语义**（guard 永远不覆盖，也永不补覆盖），因此永远不会因为 guard 而报告 missing，也永远不会因为 guard 而把后序 arm 判为「可达」。
5. **不提供 `#[non_exhaustive]` 或自定义穷尽性宽松策略。** enum 一律按「所有 variant 必须覆盖」处理（除非末尾有 wildcard/binding arm）。
6. **不生成自动修复 edit。** rustfix 风格的「自动补齐 missing variant arm skeleton」留给未来 `e-3: match diagnostics with structured suggestions` RFC。

---

## 6. Migration / Backward Compatibility / 迁移与向后兼容

### 6.1 对现有代码的影响

- **Breaking Change？No。** 本 RFC 新增的 3 条码中，只有 1 条是 error（`MATCH_MISSING_PATTERNS`）——它替换的是已存在的 `MatchNotExhaustive`，**在完全相同的条件下触发**，只是消息更结构化、附加更多 origin note；之前能过编译的代码依旧能过，之前过不了的依旧过不了，**只是 error code 的字符串 id 从 `typecheck.MATCH_NOT_EXHAUSTIVE` 改成 `typecheck.MATCH_MISSING_PATTERNS`**。
- **两条 warning（`MATCH_UNREACHABLE_ARM`、`MATCH_OVERLAP`）默认开启**。warning 不阻止编译（`-Werror` 开启时除外），因此不构成破坏性变更。

### 6.2 向后兼容的 deprecation 路径（≥ 2 release cycle）

| Cycle | 行为 |
| --- | --- |
| **Cycle 1（Alpha/Beta，本 RFC 发布后的首个 release R_n）** | `MatchNotExhaustive` 保留，编译器在「能拿到 enum info」时发新码 `MATCH_MISSING_PATTERNS`，在「enum 解析失败 / scrutinee 类型是 error」时继续发旧码；在 stderr 附加一条 `note: diagnostic code MATCH_NOT_EXHAUSTIVE is deprecated, use MATCH_MISSING_PATTERNS instead`。 |
| **Cycle 2（R_n+1）** | 默认不再向 stderr 附加 deprecation note（改以 `-Wdeprecated` 门控）；文档标记 `MatchNotExhaustive` 为 `[deprecated]`。 |
| **Cycle 3（R_n+2，2 个 release 之后）** | `MatchNotExhaustive` 的 `inline constexpr` 行前加 `[[deprecated("use MATCH_MISSING_PATTERNS")]]`；若代码中未再被引用（理想情况），直接删除。 |

### 6.3 Automated Tooling / 自动化工具

- **不提供 rustfix 风格的 cargo fix**（见 §5 Non-Goals #6）。
- **提供辅助迁移脚本**（Python，位置 `tools/migrate-match-codes.py`）：grep 任何依赖了旧 error code `MATCH_NOT_EXHAUSTIVE` 的 `.golden` / `.rs` / `.cpp` 测试文件，并给出 sed 命令替换建议。这部分属于 rollout 脚本，不进入编译器产物。

---

## 7. Rollout Plan + Acceptance Criteria / 上线计划与验收标准

### 7.1 三阶段 Rollout

| 阶段 | 代号 | 内容 | Feature Flag |
| --- | --- | --- | --- |
| **1 — Alpha** | `e2_alpha_unittests_only` | 仅在 `sema/match_exhaustiveness_test.cpp` 单测目录运行；默认 feature flag `AHFL_MATCH_DIAG=alpha`。不接入 `typecheck_expr.cpp::visit_match` 的主路径，用 `if constexpr` / `#ifdef` 门控，保证 release 编译零副作用。 | 默认 OFF |
| **2 — Beta** | `e2_beta_golden_cli` | 接入主路径；warning 默认开启；error 默认开启。新增 20+ 个 golden 文件（`tests/goldens/match-diag-*.golden`）覆盖 §2.3 三个 use case、边界情形（空 enum、单 variant enum、wildcard 在不同位置、binding vs wildcard 等价性、enum 解析失败退化到旧码）。 | 默认 ON（warning）/ ON（error） |
| **3 — Stable** | `e2_stable_default` | feature flag 移除；deprecation note 按 §6.2 进入 Cycle 1；文档（`docs/reference/match.zh.md` / `en`）同步更新。 | 永久 ON |

### 7.2 验收标准（可量化）

以下 **4 条全部通过** 才能标记 Stable：

1. **单元测试**：`tests/semantics/match_exhaustiveness_test.cpp` 的断言数 ≥ 80 条（含至少 10 条 overlap、10 条 unreachable、20 条 missing patterns、10 条空/单变体边界、10 条 origin note range 正确性、20 条退化路径）。
2. **集成测试**：`ctest -R match` 或等价的命名测试组 **100% 通过**（预期 ≥ 40/40 ctest）；全仓库 `ctest` 基线不掉项（Wave-17 终点 979/979，本 RFC 交付时 ≥ 1020/1020）。
3. **变异测试**：`mutation score ≥ 85%`（用仓库现有 fuzz/mutator 框架对 `analyze_match` 输入枚举做条件/边界翻转，至少 17 个 mutation 算子被单测捕获）。
4. **Fuzzer 稳定性**：`fuzz_match_exhaustiveness` target 连续 **7 天零 crash**（在 CI nightly 上运行，fuzz budget ≥ 24 CPU·小时）。

附加的**定性**条目（Blocking，但不量化）：

- 5. CLI 渲染 `diagnostic_consumer.cpp` 输出的 origin note 在 `--color=never` 模式下与 `tests/goldens/match-diag-*.golden` 完全字节对齐；LSP `relatedInformation` 字段非空条目数 ≥ 每条诊断 ≥ 1。
- 6. 代码评审：实现 PR 必须通过至少 2 位 shepherd 的 review，且 `analyze_match` 的圈复杂度 ≤ 15（`clang-tidy readability-function-cognitive-complexity` 检查）。

---

## 8. Decision Block / 决策块

| 评审维度 | 选择 / 说明 |
|---|---|
| **当前状态** | DRAFT · Awaiting inputs |
| **go-criteria 满足度** | (1) match_exhaustiveness_test.cpp 断言 ≥ 80 条；(2) ctest 全量 ≥ 基线且 match 组 100% 通过；(3) analyze_match mutation score ≥ 85% |
| **no-go 否决条件** | 若 N²·K 算法在 N=30/K=20 规模上 P99 typecheck 耗时 > 10ms 且无法通过缓存削减，则否决；若 MATCH_MISSING_PATTERNS 对存量代码误报率 > 3%（合法代码被新码拦截），则否决 |
| **未解决争议** | (1) Binding 与 Wildcard 在覆盖语义上等价是否需额外 warning；(2) or-pattern 退化到"只看首支"是否用户可接受；(3) overlap warning 是否默认升级为 error under `-Werror` |
| **依赖其他 RFC** | RFC 0001, RFC 0002 |
| **预计实现工作量** | S（<1 周）— 单 analyze_match 函数 ~300 SLOC + 三条诊断码注册 + origin note 对接；单元测试可复用现有 matcher 脚手架 |
| **破坏性变更风险** | LOW — error 码仅字符串 id 变化（触发条件一致），新增 warning 不阻塞编译；存量 golden 可通过迁移脚本批量替换 |
| **Wave 归属** | Wave-20（RFC 0001 struct-variant arm 语义 review 后启动，Maranget 复杂度讨论需先通过） |

---

## 9. References / 参考

### 8.1 仓库内源码 / 文档链接（file:line）

1. `include/ahfl/base/support/diagnostics.hpp:206-223` — 现有 `Match*` 诊断码定义组；本 RFC 的三条新码直接追加到同一 namespace 块中。
2. `src/compiler/semantics/typecheck_expr.cpp:822-961` — 现有 `visit_match()` 主路径；本 RFC 的 `analyze_match()` 在此被调用并替换第 950-960 行的穷尽性检查。
3. `src/compiler/syntax/frontend/ast.cpp:290-333` — `MatchExpr` / `MatchArmSyntax` 的 AST 结构定义与合法性校验；是本 RFC `analyze_match` 读取 arm pattern 的上游契约。
4. `docs/design/corelib-type-system.zh.md:189-237` — §1.6 `match` + 模式 EBNF 与 narrowing 语义表；本 RFC 的 pattern 分类（Variant / Wildcard / Binding）直接来源于该节。
5. `docs/design/corelib-type-system.zh.md:726` — 母 RFC P1 ADT 阶段的「主要代码点」表格；本 RFC 的修改位于表格建议的 `semantics/typecheck_expr.cpp` 范围内。
6. `src/tooling/cli/diagnostic_consumer.cpp:1` （文件顶部）— CLI diagnostic 渲染管线；本 RFC 的 origin note 列表（`Diagnostic::Related`）由其负责渲染。

### 8.2 外部参考

1. **Rust RFC 2093 — Inference for `match` arms with semicolons / exhaustive integer ranges**（不直接相关，但 Rustc 目前 match-check 的代码位置即在此后形成）；Rustc `librustc_mir_build/src/thir/pattern/_match.rs` 的文档注释：<https://doc.rust-lang.org/nightly/nightly-rustc/rustc_mir_build/thir/pattern/_match/index.html> — 实现了完整的 Maranget "usefulness" 算法，是本 RFC 方案 A 的设计对照（我们只取 usefulness 的原子 case）。
2. **Luc Maranget, "Compiling Pattern Matching to Good Decision Trees" (ML 2008)** — 经典论文；Rust/Swift/Haskell/OCaml 的 match check 都基于此。PDF：<https://moscova.inria.fr/~maranget/papers/ml05e-maranget.pdf>
3. **Swift — Pattern Completeness** — Swift 语言参考中 "Switch Statement" 的穷尽性要求 + `@unknown default` 语法：<https://docs.swift.org/swift-book/documentation/the-swift-programming-language/controlflow/#Switch> — 参考其对 non-exhaustive enum 的处理思路（本 RFC §5 #5 暂不做 `#[non_exhaustive]`，但对齐了"用户可控的穷尽性开关"这一语言设计方向）。
4. **TypeScript 4.9 — Exhaustive Checks with `satisfies` + `never` Pattern** — TS 社区广为使用的穷尽性检查惯用法 `const _exhaustive: never = value`：<https://www.typescriptlang.org/docs/handbook/2/narrowing.html#exhaustiveness-checking> — 证明「穷尽性是静态类型语言 match 的刚需」，但 TS 不提供 overlap 诊断，这也是 AHFL 要做到更好的动力之一。

---

*End of RFC 0003.*

---

### Change history / 变更记录

- 2026-06-28 (V1.1): Added Decision Block §8; Consult / Decision Due / Blocking Items; cross-RFC refs
- 2026-06-28 (V1.0): Initial DRAFT
