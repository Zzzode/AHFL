# AHFL 建模指南

本指南面向 workflow 作者：怎样把一个业务过程拆成稳定数据、外部 capability、Agent
状态机、行为契约和 workflow DAG。它讲**如何建模**，不定义所有语法细节；规范性语法和
类型规则以 [AHFL 语言规范](../spec/core-language.zh.md) 为准。工程配置、运行与
provider 配置分别见 [CLI 工作流](./user-guide-cli.zh.md) 和
[执行与包指南](./user-guide-execution.zh.md)。

贯穿本文的真实参考是 [`examples/execution-demo`](../../examples/execution-demo)：
它将 incident intake、决策和 LLM 生成回复拆成三个 Agent node。

## 先划清边界

写 AHFL 前先区分三种事实：

| 放置位置 | 应描述什么 | 不应放什么 |
|---|---|---|
| `.ahfl` | 业务类型、状态、DAG、capability contract、控制约束 | API key、endpoint、数据库连接、部署环境 |
| `ahfl.toml` | package、module root、target、run profile、输入/配置文件路径 | secret 明文、运行历史 |
| 运行配置 JSON | provider endpoint/model、secret handle、预算、retry、cache、fallback | 业务状态机、不可审计的业务规则 |

这个边界很重要：`capability` 让外部影响在源码中可见，但不把外部服务实现伪装成
AHFL 语义。需要真正调用模型、HTTP 或 gRPC JSON transcoding binding 时，交给 runtime
配置；需要效果治理时，补充 effect profile 与 assurance 证据。

## 从故事到模块

一个典型业务故事是：“收到事故请求，规范化服务名，按严重级别决定升级或自助，然后调用
模型草拟回复。” 推荐拆成：

```text
types.ahfl      业务输入、输出、enum、Agent context
intake.ahfl     IntakeAgent：规范化输入
decision.ahfl   DecisionAgent：确定 channel 和 action
response.ahfl   ResponderAgent：调用 DraftIncidentSummary capability
main.ahfl       IncidentWorkflow：声明 DAG、safety/liveness、return
```

每个源文件首先声明 `module`，跨模块依赖显式 `import`：

```ahfl
module support::response;

import support::types as types;

capability DraftSummary(request: types::ResponseInput) -> types::GeneratedSummary;
```

顶层声明默认 package-internal。需要被其他 package、manifest handoff target 或 public
facade 使用时使用 `pub`：

```ahfl
pub struct IncidentRequest {
    ticket_id: String;
    service: String;
}

pub workflow IncidentWorkflow {
    // ...
}
```

`pub` 不是自动跨 package 可见：调用方还需要正确的 dependency、exported module、
显式 import 以及合法的 public signature。不要用字符串约定或复制类型来绕过 package
边界。

## 数据模型

### 为边界创建具名 schema

workflow 和 Agent 的 input/output 应使用具名 `struct`；这让静态类型、runtime JSON、
diagnostic 和 event metadata 都有稳定身份。

```ahfl
pub enum Severity {
    Low,
    High,
}

pub struct IncidentRequest {
    ticket_id: String;
    service: String;
    severity: Severity;
    customer_impact: Bool;
}

pub struct IncidentResponse {
    ticket_id: String;
    summary: String;
}
```

运行时输入不是“任意 JSON”：struct 要有 `_type`，enum 值要有 `_enum` 和 `_variant`。
例如 `IncidentRequest` 的 JSON：

```json
{
  "_type": "execution_demo::types::IncidentRequest",
  "ticket_id": "INC-1001",
  "service": "checkout",
  "severity": {
    "_enum": "execution_demo::types::Severity",
    "_variant": "High"
  },
  "customer_impact": true
}
```

runtime 在调用 provider 前进行精确 schema 校验：缺字段、多字段、错误 `_type`、未知
variant 或字段类型不匹配都会 fail closed。

### 类型选择

| 需求 | 建议 | 原因 |
|---|---|---|
| 业务输入、输出、context | `struct` | 字段与边界明确 |
| 有限业务状态 | `enum` | 防止拼写字符串扩散 |
| 金额或定点精度 | `Decimal(p)` | 不把货币金额建模为 `Float` |
| 可缺失值 | `Optional<T>`、`some(...)`、`none` | 显式表达 absence |
| 有序集合 | `List<T>` | 保留顺序 |
| 去重集合 | `Set<T>` | 语义上不关心顺序 |
| 键值索引 | `Map<K, V>` | 明确 key/value 类型 |
| 复用名称 | `type Name = ...` | 仅在含义不被隐藏时使用 |

Agent context 是运行时初始化的状态容器；若一个 `struct` 被用作 `context`，所有字段都
必须有默认值：

```ahfl
pub struct DecisionContext {
    channel: Channel = Channel::SelfServe;
    action: Action = Action::AutoReply;
    reason: String = "queued";
}
```

## Capability 与 Predicate

### Capability 是外部效果边界

`capability` 表示模型、数据库、HTTP 服务、工单系统或其他不可由纯 AHFL 表达式实现的
调用点：

```ahfl
capability LookupOrder(order_id: String) -> OrderInfo;
capability CreateTicket(order_id: String, reason: String) -> TicketReceipt;
```

一个 Agent 只能调用自己 `capabilities: [...]` 中列出的 capability。把每项外部影响
显式放在 capability 声明处，能够让类型检查、assurance、runtime event 和审计使用相同
的 canonical identity。

对 durable 或 financial 效果，应在 capability 上声明 effect profile：

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

这不是普通注释。`validate` 会据此检查幂等、receipt、审批和补偿义务；详细规则见
[保障与生产证据指南](./user-guide-assurance.zh.md)。

### Predicate 是纯布尔事实

`predicate` 用于 contract 中的无副作用判断：

```ahfl
predicate non_empty(value: String) -> Bool;
predicate amount_within_order(order_id: String, amount: Decimal(2)) -> Bool;
```

不要将写入、模型调用或外部 HTTP 读取伪装成 predicate。contract 中不允许 capability
调用；外部效果必须在 flow 中经 capability 完成。

## Agent 状态机

`agent` 只声明控制轮廓：输入、context、输出、状态、可达转移和 capability 白名单；
具体语句写在后续 `flow for`。

```ahfl
pub agent RefundAudit {
    input: RefundRequest;
    context: RefundContext;
    output: RefundDecision;
    states: [Init, Auditing, Approved, Rejected, Done];
    initial: Init;
    final: [Done];
    capabilities: [LookupOrder, CreateTicket];
    quota: {
        max_tool_calls: 5;
        max_execution_time: 30s;
    }

    transition Init -> Auditing;
    transition Auditing -> Approved;
    transition Auditing -> Rejected;
    transition Approved -> Done;
    transition Rejected -> Done;
}
```

设计前逐项回答：

1. 初始状态是什么？哪些是 final states？
2. 每个非终态要么转移，要么在哪个分支终态化？
3. 每次外部调用是否在 capability 白名单中？
4. 是否需要把失败、重试、人工审批或补偿建模为显式状态？
5. 输出是否只依赖已初始化的 context / node input？

前端会检查未知状态、不可达状态、非法转移、final state 出边、未知 capability 和
handler 覆盖等控制问题。它不能替代对外部服务可靠性的验证。

## Contract

`contract for` 为 Agent 声明输入前提、输出后置条件和有限控制要求：

```ahfl
contract for RefundAudit {
    requires: amount_within_order(input.order_id, input.refund_amount);
    ensures: non_empty(output.reason);
    invariant: always not called(ChargeCard);
    forbid: always not called(UserInfoModify);
}
```

| 子句 | 适合描述 | 不适合描述 |
|---|---|---|
| `requires` | 输入或可观察事实的前提 | 外部调用结果 |
| `ensures` | 结束时 output 的纯性质 | 任意自然语言质量 |
| `invariant` | 生命周期中持续成立的控制性质 | 未建模的真实世界状态 |
| `forbid` | 禁止 capability / 控制事件 | provider 内部实现细节 |

`called(CapabilityName)`、`running(node)`、`completed(node)` 等 temporal observation
可进入 SMV 有限控制模型。复杂的业务数据 predicate 可能作为 environment observation
保留，不能把它们误读成完整数学证明。

## Flow：状态中的业务语句

`flow for` 定义每个状态如何绑定局部值、更新 context、调用 capability、分支、转移或
返回：

```ahfl
flow for RefundAudit {
    state Auditing with {
        retry: 2;
        retry_on: [TimeoutError, ToolError];
        timeout: 30s;
    } {
        let order = LookupOrder(input.order_id);
        ctx.reason = "order checked";

        if order.refundable {
            goto Approved;
        } else {
            goto Rejected;
        }
    }

    state Approved {
        let receipt = CreateTicket(input.order_id, ctx.reason);
        ctx.ticket_id = some(receipt.id);
        goto Done;
    }

    state Done {
        return RefundDecision {
            approved: ctx.ticket_id != none,
            reason: ctx.reason,
        };
    }
}
```

常用构造：

| 构造 | 作用 |
|---|---|
| `let name = Expr;` | 绑定局部不可变值 |
| `ctx.field = Expr;` | 更新 Agent context |
| `if` / `else`、`if let` | 进行显式控制分支和 Optional/enum 匹配 |
| `goto State;` | 进入声明过的目标状态 |
| `return Expr;` | 终态 handler 返回 Agent output |
| `assert Expr;` | 在路径中要求 Bool 条件成立 |

`state ... with { retry, retry_on, timeout }` 描述控制策略。它不是对外部服务的可靠性
保证：涉及 durable/financial write 时仍必须建立 idempotency、receipt、compensation、
runtime retry 与 evidence。

## Workflow：把 Agent 组成 DAG

`workflow` 把一个输入分派给多个 Agent node，并用 `after` 表示显式依赖：

```ahfl
pub workflow IncidentWorkflow {
    input: IncidentRequest;
    output: IncidentResponse;

    node intake: IntakeAgent(input);
    node decide: DecisionAgent(intake) after [intake];
    node respond: ResponderAgent(ResponseInput {
        ticket_id: intake.ticket_id,
        service: intake.normalized_service,
        channel: decide.channel,
        action: decide.action,
        reason: decide.reason,
    }) after [intake, decide];

    safety: always (not running(decide) or completed(intake));
    safety: always (not running(respond) or completed(decide));
    liveness: eventually completed(respond, Done);

    return: respond;
}
```

建模规则：

1. node 名在 workflow 内唯一；`after` 只能引用已声明 node，图必须无环。
2. node input 只能读取 workflow input 或依赖 node 的 output；不要用隐式共享状态。
3. node 的表达式类型必须与目标 Agent input **精确**匹配。
4. `return` 类型必须与 workflow output 精确匹配。
5. safety/liveness 聚焦有限控制事实；业务数据完整性仍用类型、assert、predicate 和
   assurance profile 表达。

执行前使用 `emit execution-plan` 检查 entry node、dependency edges、input reads 和
return reads；不要只从源码肉眼推测最终 DAG。

## 从单文件到 PackageGraph

单个 detached `.ahfl` 文件适合语言试验：只能使用本文件声明与 primitive prelude，不能
隐式 import `std` 或邻近文件。需要 `std`、跨模块 import、target 或真实运行时，应尽早
建立 package：

```text
incident-workflow/
  ahfl.toml
  src/types.ahfl
  src/intake.ahfl
  src/decision.ahfl
  src/response.ahfl
  src/main.ahfl
```

最小 manifest、workspace、sysroot 和 visibility 的完整规则见
[Package Usage](./project-usage.zh.md)。用于 `run` 的 `[run]` 与 profile 属于运行配置，
不要放进 `.ahfl`。

## 作者检查清单

完成一个 workflow 前，按以下顺序检查：

```bash
AHFLC=./build/dev/src/tooling/cli/ahflc

"$AHFLC" check --manifest examples/execution-demo/ahfl.toml \
  --target workflow --sysroot .
"$AHFLC" dump ast examples/execution-demo/src/main.ahfl
"$AHFLC" dump types examples/execution-demo/src/main.ahfl
"$AHFLC" dump package-graph --manifest examples/execution-demo/ahfl.toml \
  --sysroot .
"$AHFLC" emit summary --manifest examples/execution-demo/ahfl.toml \
  --target workflow --sysroot .
"$AHFLC" emit execution-plan --manifest examples/execution-demo/ahfl.toml \
  --target workflow --sysroot .
```

然后人工复核：

- 所有 runtime 输入/输出都是具名 schema，context 字段有默认值。
- 所有外部影响都经过 capability，Agent 白名单没有漏项或多余项。
- durable/financial capability 有 effect profile，并且 profile 与实际 retry/补偿策略一致。
- workflow `after` 明确表达每个 data dependency。
- public target 引用的 workflow / Agent / type 确实是 `pub` 并位于 exported module。
- 未把 secret、endpoint、环境路径或 provider 行为写入 `.ahfl`。

若 `check` 失败，先修复 parse、resolve、typecheck 和 validate diagnostics；不要跳过静态
检查直接进入 `run`、dry run 或 release evidence。
