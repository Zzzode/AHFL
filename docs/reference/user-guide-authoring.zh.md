# AHFL 建模指南

本文面向 `.ahfl` 文件作者，说明如何把一个 Agent 工作流拆成数据模型、外部 capability、Agent 状态机、行为契约、状态处理逻辑和 workflow DAG。

示例源码可从 [refund_audit_core_v0_1.ahfl](../../examples/refund_audit_core_v0_1.ahfl) 开始阅读。完整语法以 [core-language.zh.md](../spec/core-language.zh.md) 为准。

## 文件结构

一个典型 `.ahfl` 文件包含：

1. `module`：声明当前文件的模块名。
2. `import`：引入其他模块。
3. `struct` / `enum` / `type` / `const`：定义数据 schema。
4. `capability`：声明外部效果调用点。
5. `predicate`：声明 contract 可引用的纯布尔谓词。
6. `agent`：声明状态机轮廓。
7. `contract for`：声明行为约束。
8. `flow for`：声明每个状态的执行逻辑。
9. `workflow`：声明一个或多个 Agent 节点的 DAG 编排。

最小结构示例：

```ahfl
module refund::audit;

struct RefundRequest {
    order_id: String;
    user_id: String;
    refund_amount: Decimal(2);
}

struct RefundDecision {
    approved: Bool;
    reason: String;
}

struct RefundContext {
    approved: Bool = false;
    reason: String = "pending";
}

capability OrderQuery(order_id: String) -> Bool;
predicate non_empty(value: String) -> Bool;

agent RefundAudit {
    input: RefundRequest;
    context: RefundContext;
    output: RefundDecision;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [OrderQuery];

    transition Init -> Done;
}

contract for RefundAudit {
    ensures: non_empty(output.reason);
}

flow for RefundAudit {
    state Init {
        let exists = OrderQuery(input.order_id);
        ctx.approved = exists;
        ctx.reason = "checked";
        goto Done;
    }

    state Done {
        return RefundDecision {
            approved: ctx.approved,
            reason: ctx.reason,
        };
    }
}

workflow RefundAuditWorkflow {
    input: RefundRequest;
    output: RefundDecision;
    node audit: RefundAudit(input);
    return: audit;
}
```

## 数据建模

使用 `struct` 表达稳定的业务输入、输出和 Agent 上下文：

```ahfl
struct RefundContext {
    result: AuditResult = AuditResult::Reject;
    reason: String = "pending";
    ticket_id: Optional<String> = none;
}
```

建模建议：

| 场景 | 建议 |
|------|------|
| Workflow 输入输出 | 使用具名 `struct`，不要直接依赖松散 JSON |
| Agent context | 所有字段都给默认值，方便 runtime 初始化 |
| 固定枚举值 | 使用 `enum`，例如审批结果、阶段、风险级别 |
| 金额 | 使用 `Decimal(p)`，避免把货币金额写成 `Float` |
| 可缺省字段 | 使用 `Optional<T>`，用 `some(...)` / `none` 表达 |
| 业务集合 | 使用 `List<T>`、`Set<T>`、`Map<K, V>` |

## Capability 与 Predicate

`capability` 表示外部效果调用点，例如查订单、写工单、调用模型、写数据库：

```ahfl
capability OrderQuery(order_id: String) -> OrderInfo;
capability TicketCreate(order_id: String, reason: String) -> String;
```

`predicate` 表示纯、确定、无副作用的布尔判断，主要服务 contract：

```ahfl
predicate order_exists(order_id: String) -> Bool;
predicate refund_amount_within_total(order_id: String, amount: Decimal(2)) -> Bool;
```

使用规则：

1. Agent 只能调用自己 `capabilities: [...]` 白名单中的 capability。
2. Contract 中适合放 predicate，不适合放有副作用的 capability。
3. 将外部系统影响面放在 capability 边界，便于后续 assurance、audit 和 provider readiness 追踪。

## Agent 状态机

`agent` 声明控制轮廓，不写具体业务语句：

```ahfl
agent RefundAudit {
    input: RefundRequest;
    context: RefundContext;
    output: RefundDecision;
    states: [Init, Auditing, Approved, Rejected, Terminated];
    initial: Init;
    final: [Terminated];
    capabilities: [OrderQuery, AuditDecision, TicketCreate];
    quota: {
        max_tool_calls: 5;
        max_execution_time: 30s;
    }

    transition Init -> Auditing;
    transition Auditing -> Approved;
    transition Auditing -> Rejected;
    transition Approved -> Terminated;
    transition Rejected -> Terminated;
}
```

建模时先回答四个问题：

1. 这个 Agent 从哪个状态开始？
2. 哪些状态是终态？
3. 每个非终态能去哪里？
4. 每个状态允许调用哪些外部 capability？

静态检查会拒绝不可达状态、非法转移、终态继续转移、未知 capability 等问题。

## Contract

`contract for` 用于定义执行前后和执行期间必须满足的控制要求：

```ahfl
contract for RefundAudit {
    requires: order_exists(input.order_id);
    requires: refund_amount_within_total(input.order_id, input.refund_amount);
    ensures: non_empty(output.reason);
    invariant: always not called(RefundExecute);
    forbid: always not called(UserInfoModify);
}
```

常见用途：

| 子句 | 何时使用 |
|------|----------|
| `requires` | 输入或外部事实不满足时不应启动 |
| `ensures` | Agent 结束后输出必须满足 |
| `invariant` | 执行期间持续保持的规则 |
| `forbid` | 明确禁止的工具、状态或行为 |

`called(CapabilityName)` 可以把 capability 调用纳入 formal backend 的有限控制模型。复杂业务数据谓词通常会被当作 observation，而不是被证明为完整数学事实。

## Flow

`flow for` 写每个状态的执行逻辑：

```ahfl
flow for RefundAudit {
    state Auditing with {
        retry: 2;
        retry_on: [TimeoutError, ToolError];
        timeout: 30s;
    } {
        let order = OrderQuery(input.order_id);
        let decision = AuditDecision(order, input);

        ctx.reason = decision.reason;

        if decision.result == AuditResult::Approve {
            goto Approved;
        } else {
            goto Rejected;
        }
    }
}
```

Flow 中常用语句：

| 语句 | 用途 |
|------|------|
| `let` | 绑定局部变量 |
| `ctx.field = ...` | 更新 Agent context |
| `if ... else ...` | 条件分支 |
| `goto State;` | 转入另一个状态 |
| `return Expr;` | 结束并输出结果 |
| `assert Expr;` | 在执行路径中检查断言 |

`with` 块可以为状态处理器声明 retry、timeout 等执行策略。需要注意，策略声明只是控制平面事实；真实外部系统重试、幂等和回执仍应通过 capability effect profile、runtime policy 和 provider 证据链闭环。

## Workflow

`workflow` 编排一个或多个 Agent 节点：

```ahfl
workflow RefundAuditWorkflow {
    input: RefundRequest;
    output: RefundDecision;

    node audit: RefundAudit(input);

    safety: always not running(audit) or eventually completed(audit);
    liveness: eventually completed(audit, Terminated);

    return: audit;
}
```

多节点 DAG 可以使用 `after` 表达依赖：

```ahfl
workflow ValueFlowWorkflow {
    input: Request;
    output: Response;

    node first: AliasAgent(input);
    node second: AliasAgent(first.value) after [first];

    return: second;
}
```

建模建议：

1. 每个 node 做一件清晰的事。
2. 用 `after` 表达依赖，而不是在 Agent 内隐式等待其他 Agent。
3. 在 workflow 级别声明 safety / liveness，让 formal backend 有可验证的控制目标。
4. `return` 指向最终业务输出来源。

## 多文件项目

当 `.ahfl` 文件变多时，建议按模块拆分：

| 文件 | 常见内容 |
|------|----------|
| `lib/types.ahfl` | 公共 `struct`、`enum`、`type` |
| `lib/agents.ahfl` | Agent、capability、flow |
| `app/main.ahfl` | workflow 入口 |
| `ahfl.project.json` | entry source 和 search roots |
| `ahfl.package.json` | 包身份、入口、导出和 capability binding |

项目输入和 package authoring 的具体命令见 [CLI 工作流](./user-guide-cli.zh.md) 与 [执行与包指南](./user-guide-execution.zh.md)。

## 校验建模结果

单文件：

```bash
./build/dev/src/tooling/cli/ahflc check examples/refund_audit_core_v0_1.ahfl
./build/dev/src/tooling/cli/ahflc dump ast examples/refund_audit_core_v0_1.ahfl
./build/dev/src/tooling/cli/ahflc emit summary examples/refund_audit_core_v0_1.ahfl
```

项目：

```bash
./build/dev/src/tooling/cli/ahflc check \
  --project tests/integration/workflow_value_flow/ahfl.project.json

./build/dev/src/tooling/cli/ahflc dump project \
  --project tests/integration/workflow_value_flow/ahfl.project.json
```

如果 `check` 失败，先处理 parse、resolve、typecheck 和 validate 诊断；不要跳过静态检查直接进入 package、dry run 或 provider evidence。
