# AHFL Assurance Spec v0.1

本文档是 AHFL assurance profile、validation gate 和 assurance bundle 的规范来源。`docs/design/assurance-architecture-v0.1.zh.md` 解释工程设计；本文定义编译器必须遵守的可验证控制系统契约。

## 目标

Assurance v0.1 的目标是让用户定义的 Harness 编排代码具备可静态检查的控制面事实：

- capability 的副作用类别必须显式可见。
- durable 或 financial effect 必须提供恢复所需的幂等和回执事实。
- financial effect 必须提供审批和补偿事实。
- flow handler 对 capability 的调用必须能投影出 checkpoint、recovery 和 production blocker。
- 编译器必须能输出稳定 evidence bundle，并能用 `validate-assurance` 对 production readiness 做硬门禁。
- 编译器必须能通过 `verify-formal` 调用 SMV 兼容 model checker，验证当前 formal backend 输出的控制模型。

本规范不声明 provider 内部实现已经被 model checker 完整证明。provider 语义、外部系统状态和无限数据域属于 v0.1 的抽象或运行时监控边界。

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

`ahflc verify-formal <file.ahfl>` 必须先通过 parse、resolve、typecheck 和基础 validator，再降低到 IR、生成 SMV 模型、调用 SMV 兼容 model checker。

Checker 选择顺序：

1. `--model-checker <path>` 显式指定的可执行文件。
2. `AHFL_SMV_CHECKER` 环境变量。
3. `PATH` 中的 `NuSMV`。
4. `PATH` 中的 `nuXmv`。
5. `PATH` 中的 `nuxmv`。

命令必须把生成的 SMV 文件作为 checker 的第一个参数。
如果传入 `--formal-model-out <model.smv>`，命令必须把同一个 SMV 模型写入该路径，并使用该路径调用 checker，便于 CI 归档和本地复现。

Checker 输出解析规则：

- 包含 `is true` 的 specification 行计为 proven specification。
- 包含 `is false` 的 specification 行计为 failing specification。
- 如果存在 failing specification，命令必须返回非零退出码。
- 如果 checker 进程返回非零退出码且没有 failing specification，命令必须返回 checker error。
- 如果生成的 SMV 模型包含 `LTLSPEC`，但 checker 没有输出对应 specification 结果，命令必须返回 checker error。
- 如果输出包含 `counterexample`、`Trace Description` 或 `Trace Type`，命令失败报告必须保留 counterexample 摘要。

命令成功时必须输出：

```text
ok: formal verification passed
```

`verify-formal` 只证明当前 SMV backend 中被编码的有限控制模型。Provider 实现、无限数据域、外部系统状态、provider failure 注入和 runtime policy 执行仍属于 assurance profile 的抽象或运行时监控边界。

SMV backend 必须把 workflow node 编码为有限 lifecycle 控制模型：

- 每个 node 有目标 agent state 和 lifecycle phase。
- phase 至少区分 idle、running、completed。
- phase 可以额外保留 failed、recovering、recovered、compensating、compensated、terminal_failed 等 recovery lifecycle hook。
- `started(node)`、`running(node)`、`completed(node)`、`failed(node)`、`recovering(node)`、`recovered(node)` 必须是从 lifecycle phase 和目标 agent final-state membership 派生出的控制谓词。
- 每个 node 可以有 generic `failure_requested` environment input，用于在有限模型里触发 failure/recovery lifecycle；用户 workflow safety/liveness 条款应在 no-fault assumption 下证明，recovery lifecycle 由 compiler-generated obligation 单独证明。
- `completed(node)` 必须蕴含 node 当前状态属于目标 agent final state。
- 带 `after` 依赖的 node 在 `running` 或 `completed` 时，必须蕴含所有依赖 node 已经 `completed`。

上述 lifecycle/final/dependency 规则必须作为生成的 `LTLSPEC` 控制义务进入 SMV 模型，使真实 checker 能同时证明用户声明的 workflow temporal 条款和 compiler 自动合成的基础控制不变量。

SMV backend 必须把 `called(k)` 绑定到 flow call event，而不能把它作为自由输入观察量：

- call event 来自 `flow.state_handlers[].summary.called_targets`。
- call event 在 workflow node `running` 且 node state 等于包含该 call 的 handler state 时成立。
- `called(k)` 是目标 agent 上相关 call event 的派生谓词。

SMV backend 可以把已声明 capability effect profile 降成 effect event 和有限控制 obligation：

- external side effect、durable write、financial write 可以生成 audit / receipt / idempotency / approval / compensation 相关 `LTLSPEC`。
- 未声明 effect profile、`unknown` effect 或无法解析的 call target 仍由 `validate-assurance` 作为 production gate 处理。
- embedded data predicate 是 environment observation assumption；在没有 bounded data semantics 之前，不得把它当作已经由 SMV 证明的 guarantee。

`verify-formal` 失败时应尽量输出 counterexample 摘要，并把可识别的 SMV symbol 映射回 AHFL 的 workflow node、agent state、call event、effect event 或 observation owner。

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
ahfl.assurance-bundle.v0.1
```

Bundle 顶层字段：

- `format_version`
- `status`
- `capabilities`
- `flow_effects`
- `policy_obligations`
- `recovery_obligations`
- `formal_model_profile`

`status` 为 `ready` 表示没有 capability 或 flow production blocker；`blocked` 表示至少有一个 blocker。`emit-assurance-json` 是 evidence emission 命令，本身不作为 production gate。CI 或发布流程应使用 `validate-assurance` 和 `verify-formal`。

## Formal Model Profile

v0.1 固定选择 `finite_control_system_v0`，当前 formal backend 标记为 `smv`。

Included:

- agent state space
- flow transition graph
- workflow DAG dependencies
- workflow node lifecycle phase
- compiler-generated lifecycle/final/dependency obligations
- temporal control observations
- flow call events
- capability effect events
- effect policy obligation checks
- recovery/failure lifecycle hooks
- generic failure environment inputs
- counterexample source mapping
- capability effect classes

Abstracted:

- provider implementation
- capability return values
- embedded data predicates
- external system state
- provider failure injection semantics

Runtime monitored:

- provider receipts
- checkpoint records
- audit events
- failure events
- 未满足的 obligation kind

Unsupported:

- full provider semantics
- unbounded data domain model checking
- distributed fairness proof

这意味着 v0.1 证明和检查的是编排控制面与 effect contract，不是任意外部系统的完整数学模型。
