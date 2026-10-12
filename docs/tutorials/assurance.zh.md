# 检查行为约束与验证结果

为 workflow 添加约束后，先检查外部调用声明是否完整，再分别阅读控制模型、数据规则和实际运行结果。本指南说明要运行哪些命令、如何修复失败，以及部署前需要验证什么。

`validate` 检查外部影响声明；`check` 检查源码并默认验证有限控制模型；`--verify-data-contracts` 额外请求数据规则证明。它们不能替代订单服务、支付网关或模型服务对业务数据的实际校验。

## 选择检查方式

| 你想检查什么 | 使用什么 |
| --- | --- |
| 类型、状态转移和 workflow 依赖 | `ahflc check` |
| 幂等键、回执、审批和补偿是否声明完整 | `ahflc validate` |
| 声明的控制时序要求 | 默认控制验证，结合报告中的目标与环境假设阅读 |
| 数据前提是否蕴含目标 | `ahflc check --verify-data-contracts` |
| 实际节点结果、模型用量和失败原因 | `ahflc run --output-format json` 或 `jsonl` |

完整语言规则见 [行为约束规范](../spec/assurance.zh.md)，运行配置见 [执行与包指南](./execution.zh.md)。

## 声明外部调用的影响

### 为写入和资金操作添加 effect 声明

任何有 durable 或 financial 影响的 capability 都应写 effect block。纯 read 也可以
声明，便于审计完整性：

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

| 字段 | 表示的可检查事实 | 高风险建议 |
|---|---|---|
| `effect` | `read`、`external_side_effect`、`durable_write`、`financial_write`、`unknown` | 不使用 `unknown` |
| `domain` | 业务域标签 | 让审计/策略名称稳定可读 |
| `idempotency` | 请求中的幂等键路径 | durable / financial 必填 |
| `receipt` | `none`、`optional`、`required` | durable / financial 使用 `required` |
| `retry` | `unsafe`、`safe_if_idempotent`、`safe` | 不要无幂等键地声称 `safe_if_idempotent` |
| `timeout` | 控制面 deadline | 明确外部调用上限 |
| `compensation` | 补偿 capability 名称 | financial 必填 |
| `policy` | 审批、审计等策略标签 | financial 包含 `approval_required` |

缺少 effect 声明的源码可能通过类型检查，但 `validate` 会报告缺项。声明回执、审批和补偿只表达调用要求；实际服务仍需实现这些要求，并保存真实调用结果。

## 检查声明是否完整

```bash
AHFLC=./build/dev/src/tooling/cli/ahflc

"$AHFLC" emit assurance-json tests/golden/assurance/ok_effects.ahfl
"$AHFLC" validate tests/golden/assurance/ok_effects.ahfl
```

成功时输出：

```text
ok: assurance validation ready
```

检查失败时，按报告中的 blocker（阻断原因）修复：

| 阻断原因 | 原因 | 修复方向 |
|---|---|---|
| `missing_effect_spec` | capability 没有 effect block | 为外部 effect 声明 profile |
| `unknown_effect_kind` | effect 是 `unknown` | 选择真实 effect 类别 |
| `missing_idempotency_key` | durable / financial write 缺幂等键 | 将 request 的稳定 key 映射到 `idempotency` |
| `missing_required_receipt` | durable / financial write 未要求 receipt | 使用 `receipt: required` |
| `missing_financial_approval_policy` | financial write 缺审批策略 | 添加 `approval_required` policy |
| `missing_financial_compensation` | financial write 缺补偿路径 | 声明补偿 capability |
| `retry_safe_if_idempotent_without_key` | 重试语义没有可证明依据 | 添加 idempotency 或改为 `unsafe` |

先修复 `check` 报告的源码错误，再补齐外部影响声明。声明应与实际业务一致：资金操作需要按资金操作处理，不能用 `read` 掩盖写入。

## 默认控制验证与按需业务目标

从仓库根目录运行：

```bash
AHFLC="$PWD/build/dev/src/tooling/cli/ahflc"
"$AHFLC" check --manifest examples/ahfl.toml \
  --formal-model-out build/refund-audit.smv
"$AHFLC" check --manifest examples/refund-rules/ahfl.toml --verify-data-contracts
```

`check` / `build` / `run` 默认开启控制模型验证（`--verify=on`），检查初始状态、合法转移、终态和节点依赖。你声明的 safety（持续安全要求）、liveness（最终完成要求）和其他时序目标会单独列出；阅读结论时同时检查环境假设，例如假定外部调用不发生故障。默认检查不自动证明每个 Agent 都会结束或外部服务一定返回。

| 报告类别 | 阅读方式 |
|---|---|
| 生成控制义务 | 编译器合成的模型一致性保证 |
| 用户时序目标 | 纯控制目标可获证；数据 observation 抽象时为 conditional/unsupported |
| 环境前提 | requires 等可信声明，未被独立证明 |
| 数据目标 | 默认 not_requested；显式请求后分别 proved/refuted/unsupported/unknown |
| runtime 合同 | not_implemented，host predicate 不会因声明自动执行 |

`--verify-data-contracts` 需要开启控制验证。数据验证的范围是 `contract_implication`（合同蕴含）：在 `requires` 前提成立时，目标是否成立。它不证明 flow 赋值或外部服务实现。反例给出使公式失败的变量值，不是实际运行轨迹。前提矛盾、循环不变式不受支持、目标不支持、求解结果未知、缺少求解器或出现反例时，命令都不会通过。

`examples/refund/audit.ahfl` 中的订单存在等 predicate 只有签名，没有可供求解器证明的规则；显式请求这些数据目标会失败。需要动手验证金额公式时，使用 [退款规则示例](../../examples/refund-rules/README.md)：它演示前提蕴含非负余额，以及严格大于零目标的反例。

checker 支持 nuxmv/nusmv；spin/tlaplus 返回 verification_unsupported。checker 顺序为 --model-checker → AHFL_SMV_CHECKER → backend-specific 环境变量 → bundled → PATH。NuSMV、Z3 随构建安装提供。timeout/unknown/缺工具为失败；state_space_estimate 是规模提示，不能当作可达状态统计。

## 保存并检查实际运行结果

在仓库根目录运行已配置模型服务的事件分诊示例：

```bash
./build/dev/src/tooling/cli/ahflc run \
  --manifest examples/execution-demo/ahfl.toml --target workflow --sysroot . \
  --output-format jsonl --verbosity trace > run.events.jsonl
```

JSONL 每行是一条运行事件。检查以下信息：

1. workflow 和 node 的完成、失败或挂起状态是否符合预期。
2. capability 的开始和结束是否能通过 invocation ID（调用编号）关联；失败时读取 code、message 和源码位置。
3. 模型用量、成本和备用服务切换是否符合配置的预算与策略。
4. 恢复任务时，结果是否与预期一致，已经完成的外部调用是否被重复发起。
5. 业务服务是否保存了所需回执、审批结果和幂等记录。

使用 JSON 汇总报告或 JSONL 事件进行自动化处理，不解析终端的 human 文本。字段定义见 [运行产物参考](../reference/native-runtime-artifacts.zh.md)。恢复步骤见 [挂起与恢复](./execution.zh.md#挂起与恢复-workflow)。

## 读懂验证状态

控制报告使用 `verification_scope: control_model` 和 `control_status`。其中 `generated_control_proven` / `generated_control_bounded` 描述编译器生成的控制义务；`user_temporal_proven` / `user_temporal_bounded` / `user_temporal_conditional` / `user_temporal_unsupported` 描述你声明的时序目标。`bounded` 仅覆盖指定界限，`conditional` 依赖报告中的数据假设，`unsupported` 表示目标不能由当前模型证明。

数据报告使用 `data_contract_scope: contract_implication`，并分别列出 `data_contract_assumptions`（前提）和 `data_contract_goals`（目标）。

| `data_contract_status` | 意义与处理 |
| --- | --- |
| `not_requested` | 未请求数据验证；需要时添加 `--verify-data-contracts` |
| `proved` | 在报告前提下目标成立；仍需由业务服务检查真实前提 |
| `bounded` | 只验证了指定界限内的情况 |
| `refuted` | 存在反例；按反例修正目标或前提 |
| `inconsistent_assumptions` | 前提互相矛盾；先修正前提 |
| `unsupported` | 含当前无法编码的规则；按诊断简化或重新表达 |
| `unknown` | 求解器无法确定结果；不能视为通过 |
| `solver_unavailable` | 无法运行求解器；检查安装和路径 |
| `no_targets` | 没有可检查的目标 |

`no_targets` / `not_run` 不表示证明成功。`runtime_contract_checks: not_implemented` 表示当前没有自动运行时合同检查：声明 predicate 不会自动执行宿主校验。

## 部署前验证自己的工作流

| 检查项 | 操作 |
| --- | --- |
| 输入边界 | 尝试缺字段、错误类型和未知 enum variant，确认调用前被拒绝 |
| 节点依赖 | 检查 execution plan，演练成功和失败路径 |
| 高风险调用 | 运行 `validate`，并核对服务实际实现的幂等、回执、审批和补偿 |
| 外部故障 | 在测试服务中验证超时、断连、错误响应和重试耗尽 |
| 预算 | 验证输入过大、工具会话过长和费用超限时的处理 |
| 恢复 | 用自己的输入和服务测试挂起、落盘、重启与恢复，并检查重复外部调用 |
| 持续运行 | 在目标环境持续运行，观察内存、延迟、重试和模型用量 |

一次成功运行或一份示例测试报告只覆盖相应路径。验收时保存所用源码、配置、工具链版本和运行结果，确保结果对应实际部署的版本；外部服务的副作用仍需服务端幂等与事务保障。
