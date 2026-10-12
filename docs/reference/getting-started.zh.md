# AHFL 入门教程

本文是一份**动手教程**：从零构建编译器，然后沿着仓库自带的示例，从单文件
程序一路跑到多 Agent 编排、形式化验证、真实 LLM 调用和跨进程持久化恢复。
每一步都给出可直接复制的命令和真实输出。

阅读对象：第一次接触 AHFL、想在 30 分钟内跑通整个工具链的工程师。

- 语言规范以 [core-language.zh.md](../spec/core-language.zh.md) 为准。
- 按主题深入的参考手册见 [用户指南系列](./user-guide-overview.zh.md)。
- 本文所有命令在仓库根目录的相对路径下执行；`AHFLC` 指向上一步构建出的
  编译器二进制。

## 第 0 步：构建编译器

AHFL 是 C++23 项目，用 CMake + Ninja 构建。依赖（ANTLR4、libcurl、nghttp2、
mbedTLS、zlib、NuSMV、Z3）全部 vendored 在 `third_party/`，无需系统安装。

```bash
git clone https://github.com/Zzzode/AHFL.git && cd AHFL
cmake --preset dev
cmake --build --preset build-dev -j$(nproc)
```

构建完成后，编译器在 `build/dev/src/tooling/cli/ahflc`。后续步骤统一用一个
变量指代它：

```bash
AHFLC=$PWD/build/dev/src/tooling/cli/ahflc
"$AHFLC" --help
```

`--help` 会列出全部动作（`check` / `run` / `build` / `emit` / `dump` /
`validate` / `fmt` / `package` / `registry`）。本教程只用到其中一小部分。

## 第 1 步：第一个 `check` —— 单文件退款审核

仓库里最简单的示例是 `examples/refund/audit.ahfl`：一个退款审核 Agent，带
状态机、capability 白名单、合同条款和一个 workflow。它是一个单文件 package，
manifest 就在 `examples/ahfl.toml`。

```bash
cd examples
"$AHFLC" check
```

`check` 不只是类型检查——从 RFC 0028 起，它默认在编译管线里跑**形式化验证**
（`--verify=on` 是 `check` / `build` / `run` 的默认值）。输出末尾是：

```text
ok: formal verification passed with 26 proven specification(s)
formal_backend: nuxmv
checker_status: available
state_space_estimate: 5
state_space_agents: 1
state_space_transitions: 5
state_space_likely_tractable: true
checker: /home/you/AHFL/build/dev/src/tooling/cli/NuSMV
ok: checked 2 source(s), 36 symbol(s), 92 reference(s), 8 named type(s)
```

这表示：NuSMV 对状态机模型证明了 26 条性质（safety / liveness / 合同条款），
状态空间约 5 个状态，可判定。最后一行是常规的 parse / resolve / typecheck /
validation 统计。

输出里可能有几条 `warning[formal.NOT_IN_VERIFIED_SUBSET]`：这是说某些
`requires` / `ensures` 子句用了 SMT 可判定子集之外的表达式（这里是 host
predicate 调用），验证器会把它们**抽象掉而不是证明**，并明确告诉你。这是
fail-closed 设计：不能证明的就说不能证明，绝不假装通过。

> 想跳过形式化验证、只做结构检查，加 `--verify=off`。

## 第 2 步：读懂这个程序

打开 `examples/refund/audit.ahfl`，从上到下依次是 AHFL 的核心建模构件：

```ahfl
module refund::audit;

import std::option as option;

pub struct RefundRequest {
    order_id: String;
    user_id: String;
    refund_amount: Decimal(2);          // 定点十进制，2 位小数
}

pub enum AuditResult {
    Approve,
    Reject,
}

capability OrderQuery(order_id: String) -> OrderInfo;
capability AuditDecision(order: OrderInfo, request: RefundRequest) -> AuditReply;
capability TicketCreate(order_id: String, reason: String) -> String;
```

- **`struct` / `enum`**：强类型数据模型。`Decimal(2)` 是定点数，金额不会有
  浮点误差。
- **`capability`**：Agent 能调用的外部能力的**类型签名**。实现（HTTP、LLM、
  内部服务）在 runtime 配置边界注入，DSL 里只有签名。这是"能力可治理"的基础：
  Agent 只能调用白名单里的 capability。

```ahfl
predicate order_exists(order_id: String) -> Bool;
predicate order_belongs_to_user(order_id: String, user_id: String) -> Bool;
```

- **`predicate`**：host 实现的纯业务规则，用在合同里表达"订单存在"、"金额不
  超过订单总额"这类约束。DSL 声明规则，host 提供判定。

```ahfl
pub agent RefundAudit {
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

- **`agent`**：一个有穷状态机 + 数据边界 + capability 白名单 + 配额。`transition`
  声明合法的状态转移；任何不在声明里的转移都不可达。

```ahfl
contract for RefundAudit {
    requires: order_exists(input.order_id);
    requires: order_belongs_to_user(input.order_id, input.user_id);
    requires: refund_amount_within_total(input.order_id, input.refund_amount);
    ensures: non_empty(output.reason);
    invariant: always not called(RefundExecute);
    forbid: always not called(UserInfoModify);
}
```

- **`contract`**：Agent 的行为合同。`requires` / `ensures` 是数据前后置条件；
  `invariant` / `forbid` 是时序性质——这里声明了"永远不调用退款执行"和"永远
  不修改用户信息"。这些条款会被第 1 步看到的形式化验证证明。

```ahfl
flow for RefundAudit {
    state Auditing with {
        retry: 2;
        retry_on: [TimeoutError, ToolError];
        timeout: 30s;
    } {
        let order = OrderQuery(input.order_id);
        let decision = AuditDecision(order, input);
        // ...
    }
    // ...
}

pub workflow RefundAuditWorkflow {
    input: RefundRequest;
    output: RefundDecision;

    node audit: RefundAudit(input);

    safety: always not running(audit) or eventually completed(audit);
    liveness: eventually completed(audit, Terminated);

    return: audit;
}
```

- **`flow`**：每个状态的处理逻辑，支持 `retry` / `retry_on` / `timeout` 韧性
  策略。
- **`workflow`**：把 Agent 组装成 DAG。`node` 是节点，`after` 声明依赖（这个
  示例只有单节点）。`safety` / `liveness` 是 workflow 级时序条款。

这就是 AHFL 的全部核心建模面：**类型 → 能力 → 谓词 → Agent 状态机 → 合同 →
flow → workflow DAG**。后面的示例只是在这个骨架上加规模。

## 第 3 步：看编译器看到了什么

`dump` 和 `emit summary` 让你检查编译器对源码的理解，调试类型或结构问题时
很有用：

```bash
"$AHFLC" dump types | head -20
```

```text
struct refund::audit::RefundRequest
  order_id: String
  user_id: String
  refund_amount: Decimal(2)
enum refund::audit::AuditResult
  Approve
  Reject
capability refund::audit::OrderQuery(order_id: String) -> refund::audit::OrderInfo
...
```

```bash
"$AHFLC" emit summary
```

`summary` 输出 protobuf 文本格式的包级摘要：声明种类统计（struct / enum /
capability / agent / contract / flow / workflow 各多少）、形式化观察（哪些
capability 被调用）、flow 摘要（handler 数、goto 目标数）等。这是 CI 和评审
里快速了解一个包"长什么样"的入口。

其他常用检查命令：

```bash
"$AHFLC" dump ast            # 解析后的 AST
"$AHFLC" dump package-graph  # 包依赖图（多包工程）
"$AHFLC" emit package-review # 包级评审：导出面、capability 绑定、policy 义务
```

## 第 4 步：形式化验证

第 1 步的 `check` 已经跑了形式化验证。想看生成的模型：

```bash
"$AHFLC" emit smv | head -30
```

这会输出 NuSMV 模型：每个 Agent 状态机编码成一个 SMV module，状态变量、
转移关系、`SPEC` 性质（合同条款 + workflow safety/liveness）都在里面。

验证分两条路径，都在 `check` 里自动完成：

| 路径 | 求解器 | 证明什么 |
|------|--------|----------|
| SMT-BMC | Z3 4.8.12（bundled） | `requires` / `ensures` / `decreases` 数据谓词条款 |
| SMV | NuSMV 2.6.0（bundled） | `safety` / `liveness` 状态机时序性质 |

两个求解器都随仓库构建，不需要外部安装。任何一条失败（求解器缺失、超时、
性质被驳倒）都会让 `check` 以非零退出——这是 fail-closed。

想看 SMT 编码：

```bash
"$AHFLC" emit smt
```

> 子集外的条款会被抽象并产生 `NOT_IN_VERIFIED_SUBSET` 警告（见第 1 步）。
> 把条款改写进可判定子集（标量比较、布尔组合、`called(...)` / `in_state(...)`
> 原子）可以消除警告并获得完整证明。

## 第 5 步：多文件工程 —— 事件分诊工作流

`examples/execution-demo/` 是 beta 参考工作流：3 个 Agent 组成的事件分诊
流水线，带一个真实 LLM capability。它展示多文件 package 的组织方式。

```bash
cd ../execution-demo
"$AHFLC" check
```

输出末尾：

```text
ok: formal verification passed with 23 proven specification(s)
ok: checked 5 source(s), 17 symbol(s), 44 reference(s), 12 named type(s)
```

5 个源文件（`types.ahfl` / `intake.ahfl` / `decision.ahfl` / `response.ahfl` /
`main.ahfl`）通过 `module` 声明和 `import` 组装成一个 package，manifest 是
当前目录的 `ahfl.toml`。

看 workflow 的 DAG 结构：

```bash
"$AHFLC" emit execution-plan
```

```json
{
  "format_version": "ahfl.execution-plan.v1",
  "entry_workflow_canonical_name": "execution_demo::main::IncidentWorkflow",
  "workflows": [
    {
      "workflow_canonical_name": "execution_demo::main::IncidentWorkflow",
      "entry_nodes": ["intake"],
      "dependency_edges": [
        { "from_node": "intake", "to_node": "decide" },
        { "from_node": "intake", "to_node": "respond" },
        { "from_node": "decide", "to_node": "respond" }
      ]
    }
  ]
}
```

`intake` 是入口，`decide` 和 `respond` 依次接在后面。每个节点的 target、
输入输出类型、`after` 依赖都在这个 JSON 里。

看包级评审（导出面、capability 绑定、policy 义务）：

```bash
"$AHFLC" emit package-review
```

## 第 6 步：真正跑起来

`execution-demo` 的 `ResponderAgent` 会调用一个 LLM capability
（`DraftIncidentSummary`）。`ahflc run` 需要一个 OpenAI-compatible 的 endpoint。

先准备配置（模板在示例目录里）：

```bash
mkdir -p ~/.ahfl
cp llm_config.example.json ~/.ahfl/llm_config.json
# 编辑 ~/.ahfl/llm_config.json：填 endpoint、model，
# 并 export AHFL_LLM_API_KEY='你的 key'
```

`endpoint` 是 base URL，`/chat/completions` 会被追加在后面；OpenAI-compatible
relay 通常需要带 `/v1` 后缀。凭证用 `api_key_secret: "env:AHFL_LLM_API_KEY"`
引用环境变量，不要把 key 写进文件。完整配置说明见
[user-guide-execution.zh.md](./user-guide-execution.zh.md) 和示例目录的
`README.md`。

然后运行：

```bash
"$AHFLC" run
"$AHFLC" run --profile low-risk
"$AHFLC" run --output-format json
```

manifest 的 `[run]` 段提供默认 input（`inputs/high-severity.json`）和 profile
（`low-risk` 用 `inputs/low-risk.json`）。`--output-format` 支持 `human` /
`json` / `jsonl` / `quiet`；自动化消费者应读 `json` 或 `jsonl`，不要解析
human 输出。

`run` 的动态事实源是一个 typed canonical event store：所有节点生命周期、
capability 调用、token / 成本、fallback 都落成事件，再从同一事实派生 human /
JSON / JSONL / replay / audit 投影。

## 第 7 步：持久化与跨进程恢复

`examples/durable-resume/` 展示 RFC 0022 的 exactly-once 持久化模型：一个
capability 返回 `PENDING`，workflow **挂起**并持久化恢复记录，之后在一个
**全新进程**里冷启动恢复到确定性结果。

这个示例**不需要真实 LLM**——`--suspend-capability` 强制 capability 返回
PENDING，恢复时注入结果。完整配方（示例目录 `README.md` 里也有）：

```bash
cd ../durable-resume
echo '{"endpoint":"http://127.0.0.1:1/v1","model":"unused","api_key_secret":"K"}' > /tmp/llm.json
export K=unused
WF=durable_resume::main::ReplyWorkflow

# 第 1 次运行：DraftReply 被强制 PENDING，workflow 挂起并写恢复记录。
# 退出码 0 表示"已持久化挂起"，不是失败。
"$AHFLC" run --manifest ahfl.toml --workflow "$WF" --llm-config /tmp/llm.json \
  --suspend-capability durable_resume::main::DraftReply \
  --recovery-store /tmp/reply.snapshot
```

```text
Steps
  suspended  reply  durable_resume::main::ReplyAgent

Result
  (none)

suspended 1/1 nodes
```

```bash
# 第 2 次运行：全新进程，唯一联系是磁盘上的 snapshot。
# --resume-pending-result 注入 host 最终起草的回复；workflow 从 memo 重放，
# 在 PENDING 调用处注入结果，完成。DraftReply 不会被重复调用。
"$AHFLC" run --manifest ahfl.toml --workflow "$WF" --llm-config /tmp/llm.json \
  --recovery-store /tmp/reply.snapshot \
  --resume-pending-result '{"_type":"durable_resume::main::Reply","id":"T-42","answer":"Use the reset link."}'
```

```text
durable_resume::main::ReplyWorkflow  completed

Steps
  completed  reply  durable_resume::main::ReplyAgent

Result
  durable_resume::main::Reply { answer: "Use the reset link.", id: "T-42" }

completed 1/1 nodes
```

关键点：

- **语言层面没有 `async` / `await`**——`DraftReply(...)` 就是一次普通调用，
  挂起对源码不可见。
- **跨进程**：两次运行之间唯一共享的是磁盘上的 snapshot（recovery schema
  `v2`，稳定 artifact）。
- **exactly-once**：恢复时已完成的调用从 memo 重放，绝不重复调用；
  `DraftReply` 声明为 `durable_write`，dispatch 前会写一条 write-ahead intent
  （幂等键），加 `--intent-log /tmp/intent.jsonl` 可以看到它只在挂起那次记录
  一次，恢复时不重复记录。

## 第 8 步：复杂编排 —— 贷款发放

`examples/loan-origination/` 是项目级展示示例：6 个模块、5 节点 DAG，覆盖
fan-out / fan-in、refinement 类型、完整 effect 阶梯和生产级 capability 策略。

```bash
cd ../loan-origination
"$AHFLC" check
"$AHFLC" emit summary
"$AHFLC" emit execution-plan
```

`check` 默认开启形式化验证（`--verify=on`），这个示例完整通过：

```text
ok: formal verification passed with 79 proven specification(s)
formal_backend: nuxmv
state_space_estimate: 540
ok: checked 7 source(s), 57 symbol(s), 180 reference(s), 23 named type(s)
```

（输出里的 `formal.NOT_IN_VERIFIED_SUBSET` 警告是 SMT 路径对无 sort 映射的
operand 类型做抽象化处理，属正常现象；这些条款不参与 BMC/SMT 证明。）

这个示例展示：

- **并行 fan-out / join**：`intake` 之后 `credit` 和 `fraud` 两个筛查分支并行，
  `underwrite` 用 `after [intake, credit, fraud]` 汇合。
- **多终态路由**：`UnderwritingAgent` 路由到 `Approved` / `Rejected` /
  `Escalated` 三个终态之一。
- **refinement 类型**：信任边界上的 `Int(300,850)` 信用分、`String(1,64)`
  标识符、`Decimal(2)` 金额。
- **完整 effect 阶梯**：read（信用 / 欺诈查询）→ `external_side_effect`
  （通知）→ `durable_write`（saga 补偿）→ `financial_write`（放款，带幂等键、
  必需回执、`safe_if_idempotent` 重试、补偿、审批策略）。
- **"money invariant"**：`safety: always (not completed(fulfill, Disbursed)
  or completed(underwrite, Approved))`——只有 underwriting 到达 Approved 终态
  才会放款。这是银行审计员会问的那条性质，用语言写下来、机器可检查。

> **关于 money invariant 的验证子集**：上面那条 "money invariant"
> （workflow `safety[4]`）关联了两个节点的特定终态——`fulfill` 的
> `Disbursed` 与 `underwrite` 的 `Approved`。SMV 后端把多终态 agent 的
> 终态选择建模为非确定选择，无法证明跨节点终态关联，因此这条条款被排除
> 在 SMV 可证子集之外：在生成的 SMV 模型中以 `outside_smv_subset` 注释
> 标出，既不静默丢弃也不误报失败，其余 79 条规格全部得证。
>
> ```text
> -- workflow loan_origination::main::LoanOriginationWorkflow safety[4]
> -- outside_smv_subset: correlates a specific final state of a multi-final-state
> -- node with another node; the SMV model selects final states nondeterministically
> -- and cannot prove cross-node final-state correlations
> ```
>
> 这是验证器的建模边界，不是示例语义错误；条款仍写在源码里、机器可读，
> 只是当前后端不对它下结论。

## 下一步

读完本教程，你已经跑通了 AHFL 的完整工具链：建模 → 类型检查 → 形式化验证
→ 执行 → 持久化恢复。按主题深入：

| 你想做什么 | 读哪里 |
|------------|--------|
| 写自己的 Agent workflow | [建模指南](./user-guide-authoring.zh.md) |
| 掌握全部 CLI 动作和 artifact | [CLI 工作流](./user-guide-cli.zh.md)、[CLI 命令参考](./cli-commands.zh.md) |
| 配置 package 运行、provider、secret | [执行与包指南](./user-guide-execution.zh.md) |
| 加 gate、读 release evidence | [保障与生产证据指南](./user-guide-assurance.zh.md) |
| 排查编译 / 配置 / 运行失败 | [调试指南](./user-guide-debugging.zh.md)、[错误码参考](./error-codes.zh.md) |
| 理解语言规范 | [core-language.zh.md](../spec/core-language.zh.md)、[assurance.zh.md](../spec/assurance.zh.md) |
| 用 VS Code 写 AHFL | [LSP / VS Code 扩展](./lsp-vscode-extension.zh.md) |
| 嵌入自己的 host runtime | [host-abi.zh.md](./host-abi.zh.md)、[native-handoff-usage.zh.md](./native-handoff-usage.zh.md) |

示例索引：

| 示例 | 展示什么 |
|------|----------|
| `examples/refund/` | 单文件、状态机、合同、predicate、quota |
| `examples/execution-demo/` | 多文件 package、3-Agent DAG、真实 LLM capability、run profile |
| `examples/durable-resume/` | PENDING 挂起、跨进程恢复、exactly-once |
| `examples/loan-origination/` | fan-out/fan-in、refinement 类型、effect 阶梯、financial_write |
