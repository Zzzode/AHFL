---
rfc: "0017"
title: "BMC Contract Semantics"
status: "stabilized"
area: ["formal", "compiler"]
stability: "experimental"
created: "2026-08-24"
updated: "2026-10-12"
authors: ["zzzode"]
shepherd: "project lead"
owners:
  formal: "formal owner"
  compiler: "compiler owner"
required_reviewers: ["formal", "compiler"]
tracking_issue: "none"
discussion: "none"
implementation_prs: ["8778a333", "46653a24", "93b716d0", "ccdaac82", "289dbe04", "a19826a6", "3a35d6cc", "4b105af1", "1493239d", "5e84d0d1"]
decision_due: "2026-09-30"
---

# RFC 0017: Contract Data Verification Scope

## Summary

本 RFC 的当前实现范围是 **contract_implication**：使用 SMT（可满足性模理论）求解器检查数据 `requires` 是否蕴含数据目标。它没有 flow 执行展开、状态更新或 output 赋值关系，不能证明实现满足合同。历史标题中的 BMC（有界模型检查）保留为 RFC 标识与既有引擎命名，不能据此解释成执行轨迹证明。

默认编译验证仅检查通用 Agent/workflow 控制模型与用户时序目标。用户使用 `--verify-data-contracts` 显式请求 package 数据目标；无请求时报告 not_requested，不启动 Z3，不为这些未请求条款发子集告警。管线与 CLI 规范见 [RFC 0028](./0028-default-pipeline-formal-verification.zh.md) 和 [Assurance Spec](../spec/assurance.zh.md)。

## Motivation

旧说明将无执行关系的公式检查描述为完整 BMC，且 invariant 可能参与循环假设。本次明确请求和证明范围，修复真空/循环证明。

## Goals

- 对现有合同提供确定性的 SMT 表达式编码和源位置诊断。
- 区分可信前提与证明目标，拒绝真空证明和循环证明。
- 显式请求后 fail closed：unsupported、unknown、超时、缺 solver、矛盾前提、反例均失败。
- 保留 artifact emission 和已有求解器实现，不新增 DSL 属性、兼容路径或第二套引擎。
- 不承诺证明 provider 真实性、字符串/集合任意逻辑、运行时合同执行或 flow 实现等价。

## Non-Goals

不证明完整 flow 实现或外部业务真实性，不新增 runtime predicate executor。

## Design

### Request and clause identity

`--verify-data-contracts` 要求 `--verify=on`。数据 `requires` 是环境前提；数据 `ensures`、`invariant`、`forbid` 是目标，其中 forbid 的目标为对应表达式否定。用户控制 temporal clause 继续由 SMV 处理，数据目标的存在不自动请求 SMT。

目标关联 numeric Agent/contract/clause identity 与 source range；名称仅用于用户报告。每个目标独立求解，原始 unsupported 前提不得被静默丢弃，否则将改变用户请求的证明问题。

### Sound contract implication

令 T 为引用符号的类型约束，R 为所有 requires，G 为目标：

1. 先检查 T ∧ R 可满足；UNSAT 为 inconsistent premises，检查失败。unknown/timeout 同样不能算前提有效。
2. 查询 T ∧ R ∧ ¬G：UNSAT 为 proved，SAT 为 refuted 并提供赋值，其他结果失败。
3. 目标 invariant 不得被加入 R；目标不能通过假设自身或另一个未经证明目标获证。

这是一个公式蕴含检查，没有 flow transition relation。输出字段与输入字段彼此独立，除非 requires 显式约束。`ensures: output.total == input.qty * input.price` 不能仅凭 flow 中的赋值获证；SAT 也仅是该公式反例，不必然是可执行路径。

### Verifiable subset and arithmetic

使用现有 `std::variant` visitor 编码 Bool/Int 标量、可编码字段 path、算术/比较/布尔组合。`Int(lo,hi)` 加范围约束；Int 编码为数学整数，运行时 int64 溢出需要独立保证，不能由数学整数蕴含推断。除法/取模编码与有效除数约束须沿用现有语义。

host predicate/capability 调用、字符串内容、无界或不可编码的集合量化等未支持构造必须明确拒绝所请求验证。decreases/ranking 不是完整执行终止证明。子集范围以编码器和 [Core Language Spec](../spec/core-language.zh.md) 的相应规则为准，不把语法可表达等同于当前可获证。

有界量化属于已实现子集：[RFC 0024](./0024-bounded-collection-quantification.zh.md) / [RFC 0025](./0025-bounded-collection-types.zh.md) 允许带静态容量（capacity）的 `List<T>(N)` / `Set<T>(N)` / `Map<K,V>(N)`，编码器按 N 个符号槽有限展开 `forall` 为合取（全部成立）、`exists` 为析取（至少一项成立）；N=0 分别为 true/false。非零容量展开时，元素（Map 的键和值）需为 Bool/Int/BoundedInt，量化体和集合操作数也必须可编码；无静态容量、无法映射 SMT 标量类型或量化体不支持时拒绝。已有真实 Z3 测试证明 `List<Int>(3)` 上 `forall x: x > 0` 蕴含 `forall x: x >= 1`，并反驳无前提的后者。该符号槽模型不包含 flow 操作集合或实际动态长度的执行关系。

### Solvers and artifacts

`AHFL_Z3_PATH` → ahflc 同目录 bundled Z3 → PATH 是当前 solver 发现顺序。Z3 随默认构建/安装提供；缺失/无效覆盖仍须 fail closed。

`emit smt` 是确定性 SMT-LIB artifact，不启动 solver。artifact 的 `(check-sat)` 只检查其中断言一致性；直接将 emission 喂给 Z3，不等同于 CLI 的逐目标反驳证明。

### Reports

报告包含请求状态、目标数、已证明/反驳/unsupported/unknown、环境前提、`contract_implication` 范围以及条款源位置。数据报告与控制报告独立；即使两条路径成功，也不能写“业务实现已形式化证明”。runtime contract checks 当前为 not_implemented。

## User Impact

退款规则 package（`examples/refund-rules/`，命令见[入门教程](../tutorials/getting-started.zh.md)） 使用 `Int(0,1000000)` 最小货币单位：requires amount ≤ total，ensures total − amount ≥ 0。显式请求获得合同蕴含证明；改成严格 > 0 时 amount = total 是公式反例。默认不请求数据检查。

## Compatibility and Migration

保持 `--verify=off|on`、合同语法和现有 artifact。依赖默认 SMT 的 CI 需添加 `--verify-data-contracts`。所请求 unsupported 不再仅 warning/skip；前提矛盾及循环证明也不再算成功。historical BMC result 字段若保留，应配合明确 contract_implication scope 阅读。

## Implementation Plan

1. CLI 选择请求并区分报告。
2. SMT 引擎检查前提一致性并删除 invariant 的循环假设。
3. 文档和退款示例同步，用原生测试与真实 solver 验收。

## Test Plan

验收覆盖：有界整数正例/反例；无请求不启动 SMT；unsupported 前提/目标拒绝；矛盾前提拒绝；互相循环 invariant 不获证；缺 solver/unknown/timeout 拒绝；源位置与变量赋值可诊断；控制/数据/运行时范围分离。

## Rollout and Stabilization

相关 CLI、SMT 单元/integration、docs 门禁及退款 manifest 命令通过后交付。已有 stabilized 状态不表示 flow 实现证明已经具备。

## Alternatives

拒绝默认隐式检查所有数据目标、新增 DSL 属性、第二套验证引擎及假 k-induction。既有合同和一个明确请求选项足以覆盖当前范围。

## Open Questions

当前缺少 executable IR 的 sound transition relation，无法证明 flow 实现满足合同。应移除这个实际能力约束：编码赋值、分支、循环/递归、状态更新及 capability 模型，再建立实现与形式模型一致性的证据。达到该条件时按独立决策修订本 RFC；本次不以“loop-free”或伪 k-induction 宣称已有完整执行证明。

## Decision History

- 2026-10-12: The dedicated verification-scope decision agent selected explicit package data-contract requests with contract_implication scope. The builder must check premise consistency, never assume unproved invariants, and reject unsupported selected premises/goals, unavailable solver, unknown, timeout, and counterexamples. Default control verification does not request these data goals. Alternatives rejected: implicit all-contract proving (confuses premises and goals), new syntax/parallel engines (unnecessary complexity), and claiming flow execution proof without a transition relation. Acceptance requires real solver positive/negative tests and honest source-mapped reports. The end-state is sound executable-IR transition semantics; obtaining that relation and evidence triggers a new scope decision. This records agent rationale, not human sign-off.

以下历史记录描述当时设计与实现判断；涉及执行展开、k-induction 或 invariant-as-assumption 的旧宣称由上述当前语义与决策取代。


- 2026-08-24: Draft opened.
- 2026-08-24: All six Open Questions resolved (Int→math SMT Int + optional overflow
  check; `/`/`%`→truncating with `divisor != 0` obligation; Z3-only initially; add
  `emit smt`; List/Map quantification excluded to a follow-up RFC; k-induction falls
  back to bounded `bounded_safe`). Design / User Impact / Implementation Plan / Test
  Plan updated to match. Status draft → review.
- 2026-08-24: Owner sign-off; status review → implementing. Slicing per the
  Implementation Plan, starting with the SMT encoding layer (slice 2).
- 2026-08-24: Slice 2 landed — `src/verification/formal/smt_encode.{hpp,cpp}`
  implement the pure IR-expression → SMT-LIB 2 encoder over the data-predicate
  subset (bool/int literals, contract paths, member access, comparison /
  logical / arithmetic operators, `!=` rewrite, `/` and `%` with `divisor != 0`
  obligations, optional INT64 overflow guards, deterministic symbol naming).
  Subset-boundary violations surface as a structured `SmtEncodeRejection`.
  Covered by `tests/unit/verification/formal/smt_encode.cpp`
  (`ahfl.formal.smt_encode_all`).
- 2026-08-24: Slice 1 landed — verifiable-subset eligibility. New `formal`
  diagnostic category + `formal.NOT_IN_VERIFIED_SUBSET` code/message
  (`include/ahfl/base/support/diagnostics.hpp`), distinct from the typecheck
  effect-subset code. `src/verification/formal/subset.{hpp,cpp}` walk every
  contract data-predicate clause through the slice-2 encoder and report the
  ineligible ones with clause kind / target / rejection reason. `ahflc verify`
  now warns per abstracted clause instead of silently dropping it. A canonical
  `ir::contract_clause_kind_name` was promoted to `identity.{hpp,cpp}` and the
  duplicate SMV-backend helper deleted. Covered by
  `tests/unit/verification/formal/subset_eligibility.cpp`
  (`ahfl.formal.subset_eligibility_all`) and the golden
  `ahflc.check.verify.not_in_verified_subset_warning`.
- 2026-08-24: Slice 3 landed — `ahflc emit smt`. The slice-2 encoder now also
  collects each predicate's free symbols with their SMT sort (Bool/Int, plus
  `Int(lo,hi)` bounds) in first-encounter order. `smt_emit.{hpp,cpp}` render a
  full SMT-LIB 2 document (QF_LIA, sort-correct `declare-const`s, per-clause
  assertions with `forbid` negated, divisor obligations, out-of-subset clauses
  as comments, trailing `check-sat`) — deterministic, no solver required. New
  CLI command `EmitSmt` (`emit smt`), single-file capable. Covered by
  `tests/unit/verification/formal/smt_emit.cpp` (`ahfl.formal.smt_emit_all`)
  and the byte-compared golden `ahflc.emit_smt.encoding`.
- 2026-08-24: Slice 4 landed — SMT solver seam. `smt_solver.{hpp,cpp}` run an
  SMT-LIB 2 document through Z3 (`z3 -in`, resolved via explicit path →
  `AHFL_Z3_PATH` → PATH) and report a three-valued verdict plus the not-run
  states (`solver_unavailable` / `solver_error` / `timeout`) — a missing binary
  is never a silent pass. The verdict parser `parse_solver_output` is a pure
  function split out for testing without a binary. `emit smt` now uses
  `QF_NIA` so nonlinear postconditions (e.g. `qty * price`) are accepted. New
  CMake `AHFL_Z3_SOLVER` detection (mirrors `AHFL_SMV_CHECKER`). Covered by
  `tests/unit/verification/formal/smt_solver.cpp` (`ahfl.formal.smt_solver_all`;
  a real-Z3 round-trip `ahfl.formal.smt_solver_real_z3` runs when Z3 is present).
- 2026-08-24: Slice 5 landed — SMT-BMC engine. `smt_bmc.{hpp,cpp}` turn each
  contract's data predicates into proof-by-refutation queries: `requires`
  become assumptions, `invariant`/`ensures` become goals (an invariant is also
  assumed for other goals but never for itself), `forbid` proves the negation,
  and each divide/modulo divisor obligation is its own goal. A goal is proven
  when `assumptions ∧ ¬goal` is `unsat`; `sat` ⇒ Unsafe, `unknown` ⇒ Unknown, a
  missing solver ⇒ SolverUnavailable — never a silent Safe. The data-predicate
  fragment is loop-free so one query per goal is complete (unbounded k is the
  temporal fragment's concern, slice 6). Covered by
  `tests/unit/verification/formal/smt_bmc.cpp` (`ahfl.formal.smt_bmc_all` for
  the solver-independent behaviour; `ahfl.formal.smt_bmc_real_z3` proves an
  `ensures` Safe and refutes an unconditioned one Unsafe against real Z3).
- 2026-08-24: Slice 6 landed — k-induction data extension. `SmtBmcOptions`
  gains `use_k_induction`; when set, each proven base-case goal is examined for
  an inductive step. The data-predicate fragment is loop-free (no
  data-transition relation to induct over), so the step cannot be strengthened
  and the goal is flagged `bounded_only`, downgrading the run to a new
  `BoundedSafe` status instead of over-claiming an unbounded `Safe` (RFC 0017
  Q6). Refutation/unknown/unavailable precedence is unchanged. Covered by
  `ahfl.formal.smt_bmc_real_z3` (a provable goal becomes BoundedSafe under
  k-induction with every proven goal flagged bounded_only).
- 2026-08-24: Slice 7 landed — counterexample materialization. The solver seam
  gains `SmtSolverOptions::request_model`: on a Sat verdict it appends
  `(get-model)` and parses the block into `(name, value)` assignments via the
  pure `parse_solver_model` (handles Z3's multi-line `define-fun`, negative
  `(- N)` values, Bool values). The SMT-BMC engine requests a model on every
  query and attaches the violating assignment to the refuted goal
  (`SmtBmcGoal::counterexample`). Covered by `parse_solver_model` unit cases and
  a real-Z3 round-trip that asserts a refuted `ensures` carries a concrete
  counterexample.
- 2026-08-24: Slice 8 landed — verify report integration; status implementing →
  implemented. `ahflc verify` now runs the SMT-BMC data path after the SMV
  temporal path and prints an `smt_bmc_*` report block (status, goal/proven
  counts, per-goal refutations with counterexample assignments, bounded goals,
  and the solver skip reason). A genuine SMT-BMC refutation (`Unsafe`) fails the
  command; a missing solver / no goals / unknown is a skip that never turns a
  pass into a failure and never fakes a pass (`smt_bmc_result_is_failure`).
  Covered by the Z3-guarded golden `ahflc.check.verify.smt_bmc_refutation`
  (fake SMV passes, SMT-BMC refutes, verify fails with `smt_bmc_refuted`). All
  eight Implementation-Plan slices are landed with unit + golden + real-Z3
  coverage. Remaining for `stabilized`: `docs/spec/` verifiable-subset
  definition and `docs/reference/` verify data-predicate guide.
- 2026-08-24: Stabilization; status implemented → stabilized. The decision is
  now reflected in the operational docs: `docs/spec/core-language.zh.md` §5.6
  defines the SMT verifiable subset (encodable nodes, out-of-subset nodes, the
  proof-by-refutation semantics, and the `bounded_safe` fallback);
  `docs/reference/cli-commands.zh.md` documents `emit smt` and the `verify`
  `smt_bmc_*` report block with its skip semantics; and
  `docs/reference/error-codes.zh.md` catalogues `formal.NOT_IN_VERIFIED_SUBSET`
  (distinct from the typecheck-time code of the same name). The scope-freeze
  baseline (`config/product-scope-freeze.json`) records the new `emit smt`
  surface.
- 2026-09-21: Dead-code cleanup (KR7.2-FAKE). The state-graph `run_k_induction`
  and `run_cegar` in `src/verification/formal/bmc.cpp` were production-dead
  (zero non-test callers) and unsound — the "k-induction" was a mislabelled
  one-step neighbour scan and `run_cegar` always returned `Unknown`. Both, their
  `BmcOptions::use_k_induction` / `enable_cegar` toggles, and their fake-only
  tests were deleted rather than deepened, per the project's delete-dead-code
  principle. What remains is the real bounded-reachability `run_bmc` engine; the
  RFC's k-induction goal is honoured by the SMT-BMC data extension
  (`SmtBmcOptions::use_k_induction` / `BoundedSafe`), which is the only sound
  k-induction surface. Real stateful k-induction stays tracked as KR7.2.
