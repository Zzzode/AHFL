# AHFL 入门教程

本文是一份**动手教程**：从零构建编译器，然后沿着仓库自带的示例，从单文件
程序一路跑到多 Agent 编排、形式化验证、真实 LLM 调用和跨进程持久化恢复。
每一步都给出可直接复制的命令和真实输出。

阅读对象：第一次接触 AHFL、希望从构建开始逐步运行示例的工程师。

- 语言规范以 [core-language.zh.md](../spec/core-language.zh.md) 为准。
- 按主题深入的参考手册见 [用户指南系列](./overview.zh.md)。
- 本文从仓库根目录开始，按各步骤的 `cd` 命令切换目录；`REPO_ROOT` 保存仓库
  根目录的绝对路径，`AHFLC` 保存编译器二进制的绝对路径。后续命令在同一个
  shell 会话中执行。

## 第 0 步：构建编译器

AHFL 是 C++23 项目，用 CMake + Ninja 构建。依赖（ANTLR4、libcurl、nghttp2、
mbedTLS、zlib、NuSMV、Z3）全部 vendored 在 `third_party/`，无需系统安装。

```bash
git clone https://github.com/Zzzode/AHFL.git && cd AHFL
REPO_ROOT=$PWD
cmake --preset dev
# Linux 用 nproc；macOS 用 sysctl -n hw.ncpu。构建使用全部核心。
case "$(uname -s)" in
  Darwin) BUILD_JOBS=$(sysctl -n hw.ncpu) ;;
  *) BUILD_JOBS=$(nproc) ;;
esac
cmake --build --preset build-dev -j"$BUILD_JOBS"
```

构建完成后，编译器在 `build/dev/src/tooling/cli/ahflc`。后续步骤统一用一个
变量指代它：

```bash
AHFLC="$REPO_ROOT/build/dev/src/tooling/cli/ahflc"
"$AHFLC" --help
```

`--help` 会列出全部动作（`check` / `run` / `build` / `emit` / `dump` /
`validate` / `fmt` / `package` / `registry`）。本教程只用到其中一小部分。

## 第 1 步：第一个 `check` —— 退款审核 package

仓库里最简单的示例是 `examples/refund/audit.ahfl`：一个退款审核 Agent，带
状态机、capability 白名单、合同条款和一个 workflow。这个 package 的业务源码
只有一个文件，manifest（包配置文件）位于 `examples/ahfl.toml`，模块根目录
配置为 `refund`，workflow 入口为 `refund::audit::RefundAuditWorkflow`。

```bash
cd "$REPO_ROOT/examples"
"$AHFLC" check
```

不带输入参数的 `check` 会读取**当前目录**的 `ahfl.toml`，因此这里检查的是
退款审核 package。也可以在任意目录显式指定它：

```bash
"$AHFLC" check --manifest "$REPO_ROOT/examples/ahfl.toml"
```

这是 package 模式：`audit.ahfl` 导入了 `std::option`，编译器还会加载该标准库
模块，所以输出统计为 2 个源文件。“一个业务源码文件”不代表没有依赖的单文件
模式；省略输入参数也不表示自动检查 `examples/` 下的所有示例。

`check` 默认验证通用 Agent/workflow **有限控制模型**（`check` / `build` / `run` 默认 `--verify=on`）。报告按范围区分生成控制义务、用户时序目标、环境前提与数据目标。

关键状态应包含：

```text
verification_scope: control_model
control_status: passed
data_contract_status: not_requested
data_contract_scope: contract_implication
runtime_contract_checks: not_implemented
```

生成义务检查状态初始化、合法转移、final 吸收性、workflow lifecycle 和 DAG 依赖等模型关系。它不自动证明 Agent 终止或 provider 返回；用户显式声明的 temporal 目标及其环境假设单独列出。报告中 generated_control_proven 与 user_temporal_proven 是不同范围的计数，conditional/unsupported 不计为无条件用户证明。

这个示例的三个 host predicate requires 是环境前提；non_empty ensures 是默认未请求的数据目标。默认不启动这些条款的 SMT 检查，也不发未请求条款的子集 warning。不能因此认定订单存在、归属正确或审核理由非空，更不能把这些声明当成 runtime 自动校验。

需要检查业务数据目标时显式添加 `--verify-data-contracts`；原退款审核因 host predicate 没有可编码定义而失败并定位条款。第 4 步提供一个真正可编码的金额规则正例与反例。

> `--verify=off` 关闭形式化阶段；不能与 `--verify-data-contracts` 同用。最后的 `ok: checked ...` 只是前端检查统计，不代表所有业务合同通过。

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

- **`predicate`**：业务条件的签名声明，用于表达“订单存在”等前提或目标。本例没有规则定义，当前 runtime 也不会因为合同声明自动调用宿主判定。

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
  不修改用户信息"。这里的 `invariant` / `forbid` 可以由状态机模型验证；调用
  host predicate 的数据 `requires` 是环境前提，`ensures` 默认未请求；显式请求数据验证时才会因不支持而失败。

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

`summary` 输出人类可读的包级摘要：声明种类统计（struct / enum /
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

第 1 步已运行默认控制模型验证。查看模型：

```bash
"$AHFLC" emit smv | head -30
```

模型包含 Agent 状态、flow 终结控制摘要、workflow lifecycle 与 LTLSPEC。它不是完整执行器：没有每条赋值和 provider 返回值的证明关系。

| 路径 | 请求方式 | 范围 |
|---|---|---|
| SMV / bundled NuSMV | 默认 `--verify=on` | 通用控制义务和用户时序目标；抽象数据目标标明 conditional/unsupported |
| SMT / bundled Z3 | `--verify-data-contracts` | 数据 requires 蕴含 goal，范围 contract_implication |

用户按需声明金额规则的独立 package 在 `examples/refund-rules/`：

```bash
"$AHFLC" check --manifest "$REPO_ROOT/examples/refund-rules/ahfl.toml"
"$AHFLC" check --manifest "$REPO_ROOT/examples/refund-rules/ahfl.toml" --verify-data-contracts
"$AHFLC" emit smt --manifest "$REPO_ROOT/examples/refund-rules/ahfl.toml"
```

它采用 `Int(0,1000000)` 最小货币单位，requires `amount <= total`，ensures `total - amount >= 0`。默认报告数据 not_requested，显式请求报告 proved。将目标改成严格 `> 0` 会发现 `amount = total` 的公式反例，具体复制和修改命令见 [退款规则 README](../../examples/refund-rules/README.md)。

SMT 的结果证明该公式蕴含，不证明 flow 实现；反例是变量赋值，不是执行轨迹。所选条款不支持、矛盾前提、unknown、缺 solver、反例均失败。invariant 不可用于循环假设。runtime 合同检查尚未实现，需要业务实现真实校验前提和结果。

## 第 5 步：多文件工程 —— 事件分诊工作流

`examples/execution-demo/` 是事件分诊工作流：3 个 Agent 组成的
流水线，带一个真实 LLM capability。它展示多文件 package 的组织方式。

```bash
cd "$REPO_ROOT/examples/execution-demo"
"$AHFLC" check
```

控制结果与数据请求状态分开报告；`control_status: passed` 仅限定当前控制模型。源文件、symbol 和 reference 数随版本变化，不能用固定条数判断业务覆盖。

5 个源文件（`types.ahfl` / `intake.ahfl` / `decision.ahfl` / `response.ahfl` /
`main.ahfl`）通过 `module` 声明和 `import` 组装成一个 package，manifest 是
当前目录的 `ahfl.toml`。

看 workflow 的 DAG 结构：

```bash
"$AHFLC" emit execution-plan
```

```json
{
  "format": "ahfl.execution-plan",
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

先准备配置（模板在示例目录里，只需 endpoint、model 和凭证引用；token 预算、
JSON 模式有默认值，temperature 省略时采用模型服务端默认）：

```bash
mkdir -p ~/.ahfl
test -e ~/.ahfl/llm_config.json || cp llm_config.example.json ~/.ahfl/llm_config.json
# 编辑 ~/.ahfl/llm_config.json：填 endpoint、model，
# 并 export AHFL_LLM_API_KEY='你的 key'
```

`endpoint` 是 base URL，`/chat/completions` 会被追加在后面；OpenAI-compatible
relay 通常需要带 `/v1` 后缀。凭证用 `api_key_secret: "env:AHFL_LLM_API_KEY"`
引用环境变量，不要把 key 写进文件。完整配置说明见
[execution.zh.md](./execution.zh.md) 和示例目录的
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

运行结果包含节点状态、外部调用、token / 成本和备用服务切换。使用 `human` 查看摘要，使用 JSON 报告或 JSONL 事件保存与处理结果。

## 第 7 步：持久化与跨进程恢复

`examples/durable-resume/` 展示挂起任务的保存与恢复：一个
capability 返回 `PENDING`，workflow **挂起**并持久化恢复记录，之后在一个
**全新进程**里冷启动恢复到确定性结果。

这个示例**不需要真实 LLM**——`--suspend-capability` 强制 capability 返回
PENDING，恢复时注入结果。完整配方（示例目录 `README.md` 里也有）：

```bash
cd "$REPO_ROOT/examples/durable-resume"
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
- **跨进程**：两次运行之间共享磁盘上的 snapshot，格式为
  `ahfl.workflow-recovery`，恢复时严格校验完整当前结构。
- **复用已完成调用**：恢复时使用快照中的结果，不重新发起已完成调用；
  `DraftReply` 声明为 `durable_write`，dispatch 前会写一条 write-ahead intent
  （幂等键），加 `--intent-log /tmp/intent.jsonl` 可以看到它只在挂起那次记录
  一次，恢复时不重复记录。外部服务仍需实现幂等和事务保障；这不保证任意崩溃下的外部副作用只发生一次。

## 第 8 步：复杂编排 —— 贷款发放

`examples/loan-origination/` 是项目级展示示例：6 个模块、5 节点 DAG，覆盖
并行分支与汇合、范围约束类型，以及不同外部影响的 capability 声明。

```bash
cd "$REPO_ROOT/examples/loan-origination"
"$AHFLC" check
"$AHFLC" emit summary
"$AHFLC" emit execution-plan
```

`check` 默认开启控制模型验证，数据目标默认 not_requested。观察 generated/user temporal 的分类结果及 conditional/unsupported，而不是把性质总数理解为完整业务证明。显式数据请求不支持的贷款业务条款会失败；本教程不声称贷款规则或 provider 实现已全部获证。

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
> `Disbursed` 与 `underwrite` 的 `Approved` 涉及多终态选择，模型会保留相应控制关系及假设。阅读报告中的 user_temporal_conditional/unsupported 和 environment_assumptions，不能将抽象或条件目标计作无条件证明，也不能用固定规格数宣称全部业务规则成立。

## 下一步

读完本教程，你已经跑通了 AHFL 的完整工具链：建模 → 类型检查 → 形式化验证
→ 执行 → 持久化恢复。实际调用模型需要完成第 6 步的服务和凭证配置。按主题深入：

| 你想做什么 | 读哪里 |
|------------|--------|
| 写自己的 Agent workflow | [建模指南](./authoring.zh.md) |
| 掌握全部 CLI 动作和 artifact | [CLI 工作流](./cli.zh.md)、[CLI 命令参考](../reference/cli-commands.zh.md) |
| 配置 package 运行、provider、secret | [执行与包指南](./execution.zh.md) |
| 检查高风险调用与验证结果 | [行为约束与验证](./assurance.zh.md) |
| 排查编译 / 配置 / 运行失败 | [调试指南](./debugging.zh.md)、[错误码参考](../reference/error-codes.zh.md) |
| 理解语言规范 | [core-language.zh.md](../spec/core-language.zh.md)、[assurance.zh.md](../spec/assurance.zh.md) |
| 用 VS Code 写 AHFL | [LSP / VS Code 扩展](../reference/lsp-vscode-extension.zh.md) |
| 嵌入自己的 host runtime | [host-abi.zh.md](../reference/host-abi.zh.md)、[native-handoff.zh.md](./native-handoff.zh.md) |

示例索引：

| 示例 | 展示什么 |
|------|----------|
| `examples/refund/` | 状态机、通用控制验证、环境 predicate 声明、quota |
| `examples/refund-rules/` | 按需数据合同蕴含，金额规则正例和公式反例 |
| `examples/execution-demo/` | 多文件 package、3-Agent DAG、真实 LLM capability、run profile |
| `examples/durable-resume/` | PENDING 挂起、跨进程恢复、复用已完成调用 |
| `examples/loan-origination/` | fan-out/fan-in、refinement 类型、effect 阶梯、financial_write |
