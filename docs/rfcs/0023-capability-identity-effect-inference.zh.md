---
rfc: "0023"
title: "Capability-Identity Effect Inference"
status: "implemented"
area: ["language", "compiler"]
stability: "experimental"
created: "2026-08-25"
updated: "2026-08-25"
authors: ["zzzode"]
shepherd: "project lead"
owners:
  language: "language owner"
  compiler: "compiler owner"
required_reviewers: ["language", "compiler"]
tracking_issue: "none"
discussion: "none"
implementation_prs: ["a1d1f9dc"]
decision_due: "2026-11-30"
---

# RFC 0023: Capability-Identity Effect Inference

## Summary

让函数体的 effect 推断携带**它实际调用了哪些 capability 的身份**(capability
`SymbolId` 集合),而不再塌缩为一个不含身份的粗粒度 `ExprEffect::CapabilityCall`
枚举。这样一个声明 `effect Foo`(具名 capability effect)的普通 `fn` 才能在
`fn ... effect Foo { return Foo(); }` 这种"薄包装"下通过 `EFFECT_UNDERDECLARED`
检查——当前做不到,因为体推断出的 capability 集是匿名 `{SymbolId{0}}`,与声明的
`{Foo}` 不相等。同时放开"capability 只能在 Flow 上下文调用"的限制,使模块级 `fn`
可以调用其 effect 行已声明的 capability。这是 [RFC 0022](0022-durable-capability-resume.zh.md)
slice 1c(nondet 内建 `time`/`uuid` 重分类为 host capability)的**前置**:1c 依赖
`pub fn now() -> Timestamp effect Clock { return Clock(); }` 这种 std 包装成立。

## Motivation

[RFC 0022](0022-durable-capability-resume.zh.md) 要把 `wall_clock_now` / `uuid_new_v4`
从语言内建重分类为 host capability(nondeterminism 归宿主,结果走 memo 路径)。设计
评审给出的落地形态是在 `std/*.ahfl` 声明 `capability Clock() -> Timestamp` 并让公共
包装 `now()` 以 `effect Clock` 调用它。实现时被 typechecker 挡下,暴露当前 effect 系统
两条硬约束(均已在源码核实):

1. **表达式级 effect 是不含身份的标量枚举**(`ExprEffect`,`src/compiler/semantics/effects.cpp`)。
   `TypedExpr.effect`(`include/ahfl/compiler/semantics/typed_hir.hpp:398`)只存一个
   `ExprEffect`;capability 调用塌缩为 `ExprEffect::CapabilityCall`,丢弃了"调的是
   哪个 capability"。fn 体聚合(`block_body_effect`,`typecheck.cpp:2831`)join 的也是
   标量。到 `check_fn_effect_underdeclared`(`typecheck_decls.cpp:1369`)时只能
   `project(body_effect)`,无身份可传 → 落到匿名 `SymbolId{0}` 单元素集
   (`effect_judgement.hpp:107` 的 `project` 注释已预留 `called_capability` 参数,但体
   聚合路径没有可传的值)。声明侧 `{Clock}` 与体侧 `{0}` 不等 → `EFFECT_UNDERDECLARED`。
2. **capability 调用被限制在 Flow 上下文**(`typecheck_expr.cpp:4632`:
   `call_context != CallContext::Flow` → `CapabilityNotAllowed`)。模块级 `fn` 体内调用
   capability 直接报错。

不解决这两点,1c 的 std 包装无法成立,[RFC 0022](0022-durable-capability-resume.zh.md)
的确定性前置就卡死。而这两点本身也是 effect 系统的真实欠账:语言语法早已允许
`fn ... effect <capabilityRef>`(`grammar/AHFL.g4` effectSpec),但推断侧从未真正支持它。

## Goals

1. fn 体 effect 推断携带 capability 身份:capability 调用的 `SymbolId` 一路保留到
   `check_fn_effect_underdeclared`,使体侧 capability 集是**具名**集合。
2. 支持 `fn ... effect <capabilityRef>` 的薄包装:声明具名 capability effect 的 fn,
   其体调用同名 capability 时通过 under-declared 检查(`body ⊑ declared`)。
3. 放开 capability 调用的上下文限制:允许在其 effect 行已声明该 capability 的 fn 体内
   调用它(不再强制 Flow 上下文),同时保留 agent 的 `capabilities: [...]` allow-list
   语义。
4. 修正 `judgement_le` 对 CapabilitySet 的子集方向(见 Design),使 `body ⊑ declared`
   语义为 `body.caps ⊆ declared.caps`。

## Non-Goals

1. 不做 effect polymorphism / effect 变量 / row-polymorphic effects(远期)。
2. 不改 agent 的 `capabilities: [...]` allow-list 机制本身。
3. 不引入新的 capability 效应等级或语法(`read`/`durable_write` 等不变)。
4. 不在本 RFC 内做 nondet 内建重分类(那是 [RFC 0022](0022-durable-capability-resume.zh.md)
   slice 1c,本 RFC 只解除其前置阻塞)。

## Design

### 体侧 effect 携带 capability 身份

`TypedExpr` 已有标量 `ExprEffect effect`(`typed_hir.hpp:398`)。为携带身份,在
`TypedExpr` 上增加一个 index-based 的 capability 集字段(`std::vector<SymbolId>` 或
复用现有 flat 存储;空集表示无 capability 调用),仅 `CapabilityCall`/`ExternalEffect`
的 TypedExpr 填充其调用目标的 `SymbolId`(`check_capability_call` 在
`typecheck_expr.cpp:4620` 已持有 `target`)。

`block_body_effect`(`typecheck.cpp:2831`)从"join 标量 `ExprEffect`"升级为同时聚合
capability `SymbolId` 集合,返回一个 `EffectJudgement`(其已含
`CapabilitySymbolSet`,见 `effect_judgement.hpp:35`)而非裸 `ExprEffect`。
`check_fn_effect_underdeclared` 直接拿这个具名 `EffectJudgement` 与声明比较,不再走
无身份的 `project`。

### `judgement_le` 方向修正

`effect_judgement.hpp:141` 的 `judgement_le(lhs=body, rhs=declared)` 对 CapabilitySet
当前返回 `lhs.is_superset_of(rhs)`(即 `body ⊇ declared`)。正确的 `body ⊑ declared`
应为 `body.caps ⊆ declared.caps`(体只调用了声明允许的 capability 子集)。改为
`rhs.is_superset_of(lhs)`(`declared ⊇ body`)。等值集合两个方向都成立,故此前唯一
测过的"相等"路径未暴露该反向;新增的真·子集场景(体调 1 个、声明允许 2 个)会。

### 上下文限制放开

`check_capability_call`(`typecheck_expr.cpp:4632`)不再无条件要求
`CallContext::Flow`。新规则:capability 调用合法当且仅当**它出现在一个 effect 行已
声明该 capability 的 fn/agent 体内**(即被调 capability ∈ 当前可调用上下文声明的
capability 集)。agent 的 `capabilities: [...]` 仍是其声明集来源;普通 fn 的声明集
来自其 `effect <capabilityRef,...>` 行。未声明即调用 → 保留
`CapabilityNotDeclared` 诊断(带 SourceRange)。

### 索引/身份一致性

capability 全程用 `SymbolId`(Principle 2),不以名字为规范身份;体侧集合与声明侧
集合都是 `CapabilitySymbolSet`(`SymbolId` 的集合),比较为集合包含,非字符串比对。

## User Impact

- 现在可以写 `fn wrapper() -> T effect SomeCapability { return SomeCapability(); }`——
  std 库借此把 `now()` / `new_v4()` 表达为具名 capability 的薄包装
  ([RFC 0022](0022-durable-capability-resume.zh.md) 1c)。
- 未声明就调用 capability 仍报错;声明多于实际使用的 capability(super-set)合法
  (over-declaration 允许,under-declaration 报错)——与函数 effect 上界语义一致。
- 对既有源码:纯 additive,现有 agent-内 capability 调用行为不变(其声明集来自
  `capabilities: [...]`)。

## Compatibility and Migration

- **非 breaking**:放宽而非收紧。此前合法的都仍合法。
- **内部(非用户 API)**:`TypedExpr` 增字段、`block_body_effect` 返回类型由
  `ExprEffect` 变 `EffectJudgement`、`judgement_le` CapabilitySet 分支方向修正。这些
  是编译器内部结构;Typed HIR 序列化(`typed_hir_serialization.cpp`)需同步 schema。
- Typed HIR 缓存(RFC 0016)若序列化了 `TypedExpr.effect`,新增字段触发 schema bump;
  该缓存是 experimental,未承诺稳定。

## Implementation Plan

1. `TypedExpr` 增 capability-id 集字段;`check_capability_call` 填充被调
   capability `SymbolId`;Typed HIR 序列化同步。
2. `judgement_le` CapabilitySet 子集方向修正 + 单测锁定(相等、真子集、真超集三向)。
3. `block_body_effect` 聚合 capability 集(递归 expr 子树收集 `effect_capability`),
   返回 `EffectJudgement`;`check_fn_effect_underdeclared` 直接比较具名判定。
4. `check_capability_call` 上下文规则:实现时核实发现 **无需改动**——fn 体已在
   `CallContext::Flow` 下类型检查(`typecheck.cpp:2961`/`3173`),capability 调用本就
   被 4632 处的守卫放行;"必须已声明"的保证由步骤 3 的 under-declared 检查(body ⊑
   declared)提供。故本步为空操作,Goal 3 已由现有上下文 + 步骤 3 满足。
5. golden 正/负例(见 Test Plan)。

## Test Plan

- **单元**:`judgement_le` 三向(相等 / body⊂declared / body⊃declared);
  `block_body_effect` 对含 capability 调用的块聚合出具名集合。
- **golden 正例**:`fn f() -> T effect Cap { return Cap(); }` 通过;
  `fn g() -> T effect A, B { return A(); }`(声明超集)通过。
- **golden 负例**:`fn h() -> T effect A { return B(); }`(调用未声明的 B)→
  `CapabilityNotDeclared`;`fn k() -> T Pure { return Cap(); }`(体调 capability 但声明
  Pure)→ `EFFECT_UNDERDECLARED`。
- **回归**:现有 agent-内 capability workflow(`examples/refund`、
  `ahfl.runtime.*` capability 测试)无回归;`ctest --preset test-dev`。

## Rollout and Stabilization

1. `draft → review`:清 open marker,language + compiler owner sign-off。
2. `accepted → implementing`:按切片实现。
3. `implemented`:5 个切片 + 测试落库,现有测试无回归。
4. `stabilized`:effect 系统语义写入 `docs/spec`;解除
   [RFC 0022](0022-durable-capability-resume.zh.md) 1c 的前置阻塞后,在 0022 记录联动。

## Alternatives

1. **不改 effect 系统,capability 仅 agent 作用域**:则 nondet 内建无法表达为 std
   包装,[RFC 0022](0022-durable-capability-resume.zh.md) 1c 只能让 `now()`/`new_v4()`
   保持 `effect Nondet` 并在运行时把结果塞进 memo。失败原因:`Nondet` 是不含身份的
   taint,replay 无法按 capability 身份 memoize/校验,且违反"nondeterminism 归宿主 +
   索引式身份"定位;把 nondeterminism 留在语言内建里正是要消除的欠账。
2. **全套 effect polymorphism(effect 变量 + row)**:能表达"多态转发某 fn 的 effect"。
   失败原因:与本 RFC 要解决的问题(具名 capability 身份进入体推断)不成比例——那是
   一个独立的大型类型系统特性,本 RFC 只需具名集合即可解锁 1c;polymorphism 列为远期
   Non-Goal。

## Open Questions

无。两条实现细节(判定方向、上下文规则)已在 Design 定稿,进入 review 前清零。

## Decision History

- 2026-08-25: Draft。起因于 [RFC 0022](0022-durable-capability-resume.zh.md) slice 1c
  实现时的增量验证——`pub fn now() effect Clock { return Clock(); }` 触发
  `visibility.PRIVATE_IN_PUBLIC`(已知修法:capability 需 `pub`)后进一步触发
  `EFFECT_UNDERDECLARED` 与 `CapabilityNotAllowed`,核实根因为 `ExprEffect` 粗粒度枚举
  不携带 capability 身份、且 capability 调用被限制在 Flow 上下文。据此立项:让 effect
  推断携带 capability 身份 + 放开 fn 级 capability 调用,作为 1c 的前置。设计经一次
  多 lens 评审(语言模型 / 迁移兼容 / 运行时确定性)确认 `read` 效应等级 + 保留 std
  包装 + 宿主提供默认实现的方向,本 RFC 承接其"让包装成立"的类型系统缺口。
- 2026-08-25: Owner sign-off。Status draft → review → accepted → implementing。两条实现
  细节(judgement_le 方向、fn 级 capability 调用上下文规则)在 Design 定稿,无 open
  question。按 [Q4 2026 Roadmap](../plans/q4-2026-roadmap.zh.md) M1 作为 1c 前置推进。
- 2026-08-25: Implemented(PR a1d1f9dc)。`ExpressionValue`/`TypedExpr` 增
  `effect_capability`;`block_body_effect` 返回 `EffectJudgement` 并递归收集具名
  capability 集;`judgement_le` CapabilitySet 方向修正为 body ⊆ declared。实现时核实
  Goal 3(上下文放开)无需改动——fn 体已在 `CallContext::Flow` 下检查。4 个 effects
  golden(薄包装通过 / over-declare 通过 / under-declare 与 Pure-调-capability 均报
  `EFFECT_UNDERDECLARED`);effects + typed_hir 全绿。解除 [RFC 0022](0022-durable-capability-resume.zh.md)
  slice 1c 的类型系统前置阻塞。
