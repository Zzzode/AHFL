# AHFL Assurance Spec

本文定义 capability 副作用声明、生产就绪检查（validation gate）、形式化验证边界，以及验证证据包（assurance bundle）的字段与判定规则。

## 命令名约定

以下 CLI 命令支持**长名**与**短别名**，二者等价：

| 长名（本文统一采用） | 短别名 |
| --- | --- |
| `ahflc validate-assurance <input>` | `ahflc validate <input>` |
| `ahflc emit-assurance-json <input>` | `ahflc emit assurance-json <input>` |

本文使用长名；使用短别名时，参数和行为相同。

`check`/`build`/`run` 默认 `--verify=on`，检查通用控制模型及用户时序目标；数据合同需要 `--verify-data-contracts` 显式请求。请求的检查无法完成或发现问题时，命令返回非零退出码；`--verify=off` 可关闭验证。

## 目标

Assurance 的目标是让用户定义的 Harness 编排代码具备可静态检查的控制面事实：

- capability 的副作用类别必须显式可见。
- durable 或 financial effect 必须提供恢复所需的幂等和回执事实。
- financial effect 必须提供审批和补偿事实。
- flow handler 对 capability 的调用必须能投影出 checkpoint、recovery 和 production blocker。
- 编译器必须能输出稳定 evidence bundle，并能用 `validate-assurance` 对 production readiness 做硬门禁。
- 编译器必须能通过 `check --verify=on` 调用 SMV 兼容 model checker，验证当前 formal backend 输出的控制模型。

本规范不声明 provider 内部实现已经被 model checker 完整证明。provider 语义、外部系统状态和无限数据域属于本规范的抽象或运行时监控边界。

## Capability Effect Profile

Capability 可以用 effect block 声明副作用事实：

```ahfl
capability ChargeCard(request: Payment) -> Receipt {
    effect: financial_write;
    domain: payments;
    idempotency: request.idempotency_key;
    receipt: required;
    retry: safe_if_idempotent;
    timeout: 30s;
    compensation: RefundCard;
    policy: [payments::approval_required, payments::audit_event];
}
```

字段：

- `effect`: 必须是 `read`、`external_side_effect`、`durable_write`、`financial_write` 或 `unknown`。
- `domain`: 可选业务域标签。
- `idempotency`: 可选幂等键路径。
- `receipt`: 必须是 `none`、`optional` 或 `required`。
- `retry`: 必须是 `unsafe`、`safe_if_idempotent` 或 `safe`。
- `timeout`: 可选超时字面量。
- `compensation`: 可选补偿 capability 名称。
- `policy`: 可选策略标签列表。

未写 effect block 的 capability 仍然是合法 AHFL 代码，但不能通过 assurance production gate。

## Production Gate 规则

`ahflc validate-assurance <file.ahfl>` 必须先通过 parse、resolve、typecheck 和基础 validator，再降低到 IR 并构造 assurance bundle。

命令必须在以下情况返回非零退出码：

- capability 没有 effect block: `missing_effect_spec`。
- capability effect 为 `unknown`: `unknown_effect_kind`。
- `durable_write` 或 `financial_write` 没有 `idempotency`: `missing_idempotency_key`。
- `durable_write` 或 `financial_write` 的 `receipt` 不是 `required`: `missing_required_receipt`。
- `financial_write` 没有 `approval_required` 后缀策略: `missing_financial_approval_policy`。
- `financial_write` 没有 `compensation`: `missing_financial_compensation`。
- `retry: safe_if_idempotent` 没有 `idempotency`: `retry_safe_if_idempotent_without_key`。
- flow state 调用的 capability 继承了任何 capability blocker。
- policy obligation 或 recovery obligation 的 `satisfied` 为 `false`。

命令成功时必须输出：

```text
ok: assurance validation ready
```

命令失败时必须输出 violation 列表，并包含 blocker 或 obligation kind，便于 CI 通过正则匹配定位失败原因。

## Formal Verification Gate

`check`、`build`、`run` 默认 `--verify=on`：前端与 IR 结构检查之后，验证有限的 Agent/workflow 控制模型。`--verify=off` 关闭此阶段；其他命令默认关闭，但可以显式开启。验证结果必须区分以下类别：

| 类别 | 请求方式 | 结论范围 |
|---|---|---|
| 编译器生成的控制义务 | 默认开启 | Agent 初始状态、合法转移、final 吸收性；workflow lifecycle/final/DAG 依赖及模型中的 call/effect/recovery 义务 |
| 用户控制时序目标 | 声明 temporal clause | 在当前控制模型及明确环境假设下检查 `invariant`、`forbid`、workflow `safety`/`liveness` |
| 环境前提 | 数据 `requires` | 可信前提，不作为独立已证明目标 |
| 用户数据目标 | `--verify-data-contracts` | 显式请求 package 中的数据合同蕴含检查；默认 `not_requested` |
| 运行时合同检查 | 无 | 当前 `not_implemented`，不得报告成已执行 |

所有 Agent（包括未被 workflow 引用的 Agent）必须具有生成的基础控制义务。默认义务不自动承诺终止、公平调度或 provider 最终返回；用户声明的完成性目标应列明相应模型假设。控制模型不是完整 statement executor：赋值、返回值和外部 capability 实现尚无完整执行关系证明。

用户时序目标若包含未编码的数据 observation，必须报告为依赖假设的 `conditional` 或 `unsupported`，不得计入无条件已证明的用户目标。`--bmc-depth` 的结论必须保留有界范围，不能冒充无界证明。

### 控制模型检查

`--formal-backend nuxmv|nusmv` 调用 SMV checker。`spin` / `tlaplus` 不支持此验证路径，必须以 `checker_status: verification_unsupported` 失败。checker 选择顺序为：显式 `--model-checker` → `AHFL_SMV_CHECKER` → backend-specific 环境变量 → ahflc 同目录 bundled checker → `PATH`。

SMV checker 以独立进程运行（见 [NuSMV 调用约定](../rfcs/0018-nusmv-library-mode.zh.md)）。NuSMV 与 Z3 随默认构建/安装提供；损坏安装或显式覆盖仍可能导致求解器缺失，此时验证必须失败。

模型以文件参数传给 checker；`--formal-model-out` 保留同一模型便于复现。`is true` / `is false` 分别记录对应模型 specification 成立/失败。反例、缺 checker、超时、未知、进程错误以及specification 结果不匹配必须失败。报告保留 `checker_status`、状态空间估计与可识别的源位置反例映射；估计不等于实际可达状态数。

成功文案必须限定为控制模型验证，不得输出不分范围的“全部形式化验证通过”。生成义务、用户时序目标、条件证明和未支持目标必须分别计数；单独一项计数不能表示全部业务合同已证明。

### 用户按需请求数据验证

```bash
ahflc check --manifest examples/refund-rules/ahfl.toml --verify-data-contracts
```

该选项要求 `--verify=on`，否则为参数错误。普通数据 `requires` 作为环境前提；数据 `ensures`、`invariant`、`forbid` 是选择后的目标。未请求时不启动 SMT 求解器，也不因未请求的数据条款不支持而发出子集告警。

当前 SMT（可满足性模理论）路径的证明范围是 `contract_implication`：在类型边界及所有可编码 `requires` 下，检查目标是否成立。它不编码 flow 的赋值、状态更新或 capability 返回，因此不证明执行实现满足后置条件。`SAT` 反例是合同公式的变量赋值，不是执行轨迹；`UNSAT` 只证明该合同蕴含。

必须先检查前提一致性。矛盾前提不能导致所有目标真空通过；未证明的 invariant 不能作为假设循环证明自身或其他目标。所选目标或其必要前提超出子集、求解器缺失、超时、unknown、前提不一致或反例，均须返回非零退出码并定位相关条款。详细语义见 [RFC 0017](../rfcs/0017-bmc-contract-semantics.zh.md)。

### 控制模型与环境边界

workflow node 模型至少区分 idle、running、completed；恢复模型可以增加 failed、recovering、recovered、compensating、compensated、terminal_failed。`completed(node)` 蕴含目标 Agent final-state membership；带 `after` 的 node 启动/完成时要求依赖完成。

call event 从 `flow.state_handlers[].summary.called_targets` 和 active handler 派生；`called(k)` 不得作为任意自由输入。call/effect 的 committed、failed 子事件互斥并蕴含原事件。effect profile 可以合成 audit/receipt/idempotency/approval/compensation 控制义务，未声明或 unknown effect 仍由 `validate-assurance` 阻止 production readiness。

generic failure input 仅表示模型中的故障触发。用户 workflow 目标与生成恢复义务须保留各自的 no-fault 等环境假设。provider 实现、订单真实性、LLM 输出与真实 receipt 没有因此获证。

宿主 `predicate` 声明不会自动执行；flow 的 `assert` / `requires` statement 也没有运行时支持。业务系统需要实际验证外部数据和计算结果，不能用静态报告替代这些检查。

## Obligation Synthesis

编译器必须从 capability effect profile 合成 policy obligation：

- external side effect、durable write、financial write 产生 `audit_event` obligation。
- durable write 和 financial write 产生 `provider_receipt` obligation。
- financial write 产生 `approval_required` obligation。

编译器必须合成 recovery obligation：

- durable write 和 financial write 产生 `idempotency_guard` obligation。
- `retry: safe_if_idempotent` 产生 `retry_idempotency_proof` obligation。
- financial write 产生 `compensation_path` obligation。

Obligation 的 `satisfied` 只表示 DSL profile 是否声明了对应事实，不表示外部 runtime 已经真实执行了该控制。

## Assurance Bundle

`ahflc emit-assurance-json <file.ahfl>` 输出 format version:

```text
ahfl.assurance-bundle
```

Bundle 顶层字段：

- `format`
- `status`
- `capabilities`
- `flow_effects`
- `policy_obligations`
- `recovery_obligations`
- `formal_model_profile`

`status` 为 `ready` 表示没有 capability 或 flow production blocker；`blocked` 表示至少有一个 blocker。`emit-assurance-json` 是 evidence emission 命令，本身不作为 production gate。CI 或发布流程应使用 `validate-assurance` 和 `check --verify=on`。

## Formal Model Profile

当前 formal backend 标记为 `smv`。Profile 必须按当前 bundle 的 effect/recovery 事实选择：

- 无 capability effect event：`finite_control_system_v0`
- 有 capability effect event 但没有 durable/financial recovery：`finite_effect_control_v0`
- 有 durable/financial recovery obligation：`finite_effect_recovery_control_v0`

Included:

- agent state space
- flow transition graph
- workflow DAG dependencies
- workflow node lifecycle phase
- compiler-generated lifecycle/final/dependency obligations
- temporal control observations
- 可编码的控制时序公式中的 bounded boolean/integer 常量表达式
- unbounded data observation assumptions
- counterexample source mapping
- flow call events、capability effect events、call/effect commit/failure events（当 bundle 含 effect event）
- effect policy obligation checks、capability effect classes（当 bundle 含 effect event）
- recovery/failure lifecycle hooks、generic failure environment inputs（当 bundle 含 recovery obligation）

Abstracted:

- provider implementation
- capability return values
- unbounded or provider data predicates
- external system state
- provider failure injection semantics

运行时 evidence（不代表合同 predicate 已执行）：

- provider receipts
- checkpoint records
- audit events
- failure events
- 未满足的 obligation kind

Unsupported:

- full provider semantics
- unbounded data domain model checking
- distributed fairness proof

这意味着默认验证提供有限编排控制面的证据；用户可以另外请求数据合同蕴含证明。两者都不表示完整执行实现或任意外部系统已获证明。

## 结果字段

控制报告使用 `verification_scope: control_model`、`control_status`，并分别列出 `generated_control_proven` / `generated_control_bounded` 与 `user_temporal_proven` / `user_temporal_bounded` / `user_temporal_conditional` / `user_temporal_unsupported`。无目标时报告 no_targets/not_run，不能声称证明通过。

数据报告使用 `data_contract_status`（not_requested、proved、bounded、refuted、inconsistent_assumptions、unknown、unsupported、solver_unavailable、no_targets）、`data_contract_scope: contract_implication`、`data_contract_assumptions` 和 `data_contract_goals`；逐条目标保留来源。`runtime_contract_checks: not_implemented` 表示当前没有自动 runtime 合同检查。
