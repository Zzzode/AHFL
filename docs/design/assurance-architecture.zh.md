# AHFL Assurance Architecture

本文档描述 AHFL 当前实现的 assurance 编译器切片。目标不是把外部支付、邮件、数据库等系统完整建模进 SMV，而是让用户写的 Harness 编排代码先变成一个可审计、可阻断、可恢复、可继续接入 model checker/runtime policy 的控制系统。

## 目标

v0.1 提供一个端到端闭环：

1. DSL 允许 capability 显式声明副作用事实。
2. AST/IR 保留这些事实，不依赖注释或名称猜测。
3. assurance analysis 汇总 capability 和 flow handler 的调用路径。
4. 编译器生成 policy obligation、recovery obligation、formal model profile。
5. `emit-assurance-json` 输出稳定 JSON 证据工件。
6. `validate-assurance` 把 production blocker 和未满足 obligation 变成 CI 可用的硬门禁。

这个闭环是后续接入真实 runtime enforcement、NuSMV/nuXmv 检查器、审计归档和发布门禁的基础。

## Capability Effect Profile

Capability 可以继续使用旧语法：

```ahfl
capability ReadUser(id: String) -> User;
```

也可以声明 effect block：

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

字段含义：

- `effect`: `read`、`external_side_effect`、`durable_write`、`financial_write`、`unknown`。
- `domain`: 业务域，用于审计和策略路由。
- `idempotency`: 幂等键路径，用于重试和恢复判定。
- `receipt`: 外部调用回执要求，当前支持 `required`、`optional`、`none`。
- `retry`: 重试安全性，当前支持 `safe`、`safe_if_idempotent`、`unsafe`。
- `timeout`: capability 级超时事实。
- `compensation`: 补偿动作名称。
- `policy`: 运行时策略标签，例如审批、审计、人工确认。

## Analysis Pipeline

### Effect Analysis

Effect analysis 读取 IR 中的 `CapabilityEffectSpec`，判断每个 capability 是否具备生产可验证所需的最小事实。

当前规则：

- 未声明 effect 的 capability 产生 `missing_effect_spec` blocker。
- `unknown` effect 产生 `unknown_effect_kind` blocker。
- `durable_write` 和 `financial_write` 必须有 idempotency key 和 required receipt。
- `financial_write` 必须有 approval policy 和 compensation path。
- `safe_if_idempotent` 必须绑定 idempotency key。

这些规则不是业务逻辑替代品，而是控制系统的最低可恢复性约束。

### Flow Effect Summary

IR lowering 已经为 state handler 计算 `called_targets`。Assurance analysis 使用这些调用摘要把 capability effect 投影到 flow state：

- 哪些 state 调用了外部 effect。
- 哪些 state 需要 checkpoint。
- 哪些 state 需要 recovery。
- 哪些 state 因 capability blocker 不能进入生产 assurance。

这样后端不需要重新解析表达式，也不需要猜测控制流。

### Policy Obligation Synthesis

Policy obligation 是运行时必须满足的控制义务，例如：

- `audit_event`: 外部 effect 必须进入审计流。
- `provider_receipt`: durable write 必须提供回执。
- `approval_required`: financial write 必须经过审批门禁。

`emit-assurance-json` 会标出每个 obligation 是否已由 DSL profile 满足。

### Recovery Obligation Synthesis

Recovery obligation 是失败、重试、恢复时必须成立的义务，例如：

- `idempotency_guard`: durable/financial write 必须避免重复外部写。
- `retry_idempotency_proof`: `safe_if_idempotent` 必须证明幂等键存在。
- `compensation_path`: financial write 必须有补偿路径。

这些义务后续会接到 checkpoint、receipt persistence、runtime replay 和人工操作台。

### Formal Model Profile Selection

当前 profile 是 `finite_control_system_v0`，选择 SMV 作为形式化控制模型 backend。

Included:

- agent state space
- flow transition graph
- workflow DAG dependencies
- workflow node lifecycle phase
- compiler-generated lifecycle/final/dependency obligations
- temporal observations
- capability effect classes

Abstracted:

- provider implementation
- capability return values
- embedded data predicates
- external system state

Runtime monitored:

- provider receipts
- checkpoint records
- audit events
- 未满足的 policy/recovery obligation

Unsupported:

- full provider semantics
- unbounded data domain model checking
- distributed fairness proof

这个 profile 明确划分了“model checker 能证明的控制部分”和“必须由 runtime policy/receipt/审计兜底的外部部分”。

## Backend Boundary

新增 backend：

```text
ahflc emit-assurance-json <file.ahfl>
```

输出格式版本为：

```text
ahfl.assurance-bundle.v0.1
```

该 backend 消费 IR，不直接读取 AST，不重跑 parser，也不替代 SMV backend。SMV backend 继续负责控制模型输出；assurance backend 负责生产级证据 bundle。

新增 validation gate：

```text
ahflc validate-assurance <file.ahfl>
```

该命令先执行常规 parse、resolve、typecheck 和基础 validator，然后降低到 IR、构建 assurance bundle，并把 capability blocker、flow blocker、未满足 policy obligation、未满足 recovery obligation 作为非零退出。它是发布/CI 的门禁命令；`emit-assurance-json` 保持为纯证据产物输出。

新增 formal verification gate：

```text
ahflc verify-formal [--formal-backend <nuxmv|nusmv|spin|tlaplus>] [--model-checker <path>] [--formal-model-out <model.smv>] <file.ahfl>
```

该命令复用 `emit-smv` 的 lowering，调用 NuSMV/nuXmv 兼容 checker，并把 checker 返回的 `is true` / `is false` specification 结果映射成 CLI 成功或失败。真实 checker 的发现顺序是 `--model-checker`、`AHFL_SMV_CHECKER`、backend-specific 环境变量和 PATH 中的 `NuSMV`、`nuXmv`、`nuxmv`、`nusmv`。`spin` / `tlaplus` 目前只在 capability matrix 中声明 emit-only，`verify-formal` 会报告 `checker_status: verification_unsupported`。

## 当前限制

v0.1 实现编译期证据合成、JSON emission、production assurance validation gate 和 SMV checker invocation：

- `verify-formal` 已可调用外部 NuSMV/nuXmv 兼容进程，但本仓库不捆绑 checker 二进制。
- `ModelCheckerBackend` 暴露机器可读 capability/availability matrix：nuXmv/NuSMV 标记为当前 AHFL SMV 外部验证路径；SPIN/TLA+ 当前只承诺模型 emission，不承诺 AHFL property 外部验证；CLI/report 已输出 `missing_binary`、`verification_unsupported`、`checker_error` 的稳定状态。
- `check` 仍然只负责语言合法性；production assurance 必须显式运行 `validate-assurance`。
- 尚未把 obligation 自动编译成 runtime policy。
- 尚未验证 compensation capability 的签名兼容性。

这些限制是显式设计边界，不应被解读为已经完成工业级 end-to-end 形式化验证。
