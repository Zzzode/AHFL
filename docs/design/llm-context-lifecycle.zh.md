# LLM 默认配置与上下文生命周期

## 当前架构

普通 workflow 传递强类型 input、output 和 Agent state，各 LLM capability 有独立
会话。每次调用从声明和完整参数生成 system/user prompt，工具对话只在该调用内
积累，节点之间通过强类型结果交接，不共享不断增长的全局聊天历史。

workflow checkpoint 保存执行坐标与已完成 capability 的 memo；LLM session 保存
调用内部的对话、摘要、请求用量与工具回执。两层通过 Pending frontier 接合，
既有 `ahfl.workflow-recovery` 格式保持不变。

## 普通用户配置

最小配置只包含 endpoint、model、api_key_secret。JSON 输出默认开启，返回结果
必须通过 capability 类型校验。temperature 缺省不发送，由模型服务端决定。
三个应用 token 预算省略时由容量解析器自动分配；显式预算继续作为应用上限。

容量来自官方 endpoint 的精确模型 profile、OpenRouter 模型 metadata 或显式物理
容量覆盖。未知服务采用 4096 总量、1024 输出和 3072 prompt 的保守应用预算，
该预算不代表识别到了模型容量。fallback 逐个独立解析，不能继承主模型窗口。

## 请求预算与自动 compact

完整 typed input、system 规则、工具 schema、tool arguments/results 和请求封装
在每次 HTTP 前计入预算，并预留响应额度。token 计数目前仍为启发式估计，
不是模型 tokenizer 的精确计数；服务端仍可能拒绝估计误差造成的超限请求。

工具历史达到可用 prompt 容量的 80% 时触发 semantic compact（语义压缩）：
模型总结较旧的完整工具交换，保留完整初始输入、规则和最近完整交换。压缩请求
使用同一 HTTP/预算/持久用量路径，本身会产生模型用量。原始历史不可变并以数值
history ID 保存；压缩后的对话可调用只读 `ahfl_session_history_get` 分块找回原文。
摘要只作提示材料，不是授权、工具回执或业务状态。

摘要格式非法、没有严格缩小提示体积、压缩请求自身超限或输入/工具 schema 已经
无法容纳时明确停止。不会用截断 UTF-8/JSON、重启 capability 或重跑工具来压缩。

## 持久会话与恢复

Provider 先保存 Prepared session 并返回 Pending；Wasm host 再持久化外层 frontier。
CLI 内部自动续跑，只有已存在的 frontier resolver 能开始 HTTP 和执行工具。
请求发送前保存用量预留，响应保存后才能执行其工具；每个工具有 Prepared、
InFlight、Completed 回执。Completed 重用原始结果；崩溃留下 InFlight 时提交状态
未知，恢复停止而不重发非幂等工具。请求中断留下的预留保守计入累计预算，恢复
不会重置轮次或额度。工具执行器还收到稳定的数值坐标和内部幂等键。

默认 CLI 使用 `~/.ahfl/runs` 下与程序、workflow、输入、公共配置及工具描述绑定的
私有 active namespace，文件锁拒绝并发续跑；成功标记完成后清理该 namespace，
再次独立运行创建新 namespace。显式 recovery path 的行为保持原有恢复契约。

会话使用数值 ID flat index、单一当前结构、完整绑定和本地 HMAC（带密钥的消息
完整性校验）。目录权限 0700、文件 0600，拒绝不安全权限、损坏记录、缺失旧密钥
和绑定不匹配。HMAC 不加密内容，也不防持有同一账户与密钥的攻击者。当前持久
会话与文件锁支持 POSIX；非支持平台明确拒绝，不声称具备恢复保证。

## Decision History

- 2026-10-12：独立默认策略代理选择保留现有应用 token 预算、最小配置优先，
  同次修复完整输入截断与工具轮次预算；拒绝盲目增大窗口和自动发现模型目录，
  因当前没有可信容量来源。初步温度方案保留 0.1；官方参数兼容证据表明推理模型
  可能拒绝该参数，当时交给另一专职决策代理复议，尚未据此修改生产温度行为。

- 2026-10-12（温度修订）：另一专职决策代理选择 `std::optional<double>`，缺省不
  发送 temperature，显式数值保持覆盖，在普通、工具和 fallback 请求中一致生效。
  [OpenAI 参数兼容文档](https://developers.openai.com/api/docs/guides/latest-model?model=gpt-5.4)
  明确部分推理模式拒绝 temperature，因此固定 0.1 会使合法模型调用失败。拒绝
  按模型名称分支（任意兼容服务下能力会漂移）和为单字段引入能力目录（维护成本
  不成比例）。代价是未覆盖时采用各服务的采样默认值，不能承诺输出确定性；已有
  显式配置语义保持不变，显式温度须为 [0, 2] 的有限数值。验收要求默认不带该字段、
  显式 0.2 在普通/工具/fallback 请求保留、最小示例能加载、原生回归通过。

## 2026-10-12：自动容量与持久会话实施决策

用户已授权继续实现模型容量、自动 compact 和 LLM 会话恢复；前述“尚未实现”是
上一阶段状态。本阶段由两个独立专职决策代理分别确定容量策略与恢复协议，
独立审查要求以下约束在实现前纳入验收。

### 容量决策

配置中的三个 `max_*` 改为 optional：省略为自动预算，显式为应用上限。
容量来源为受控显式覆盖、官方 OpenRouter metadata、版本化官方 OpenAI 精确
目录、Unknown；每个 candidate/fallback 独立解析，不按模型名子串猜测。
OpenAI 标准 models 接口不提供窗口，不能把它冒充容量发现接口。
官方目录仅匹配官方 endpoint 与精确模型 ID；初版 `gpt-6-astra`、`gpt-6.1-sol`、`gpt-6-luna` 三个 profile 的窗口
1,050,000、最大输出 128,000，来源与版本日期跟随记录。

已知窗口 W，默认输出 O=min(1024, 模型最大输出)，预留 max(O, ceil(W*10%))，
自动 prompt 为 W 减预留。显式 total/prompt 只能收紧；显式输出超物理限额明确
拒绝。未知容量继续 4096/1024/3072 保守应用预算，不能称为模型容量。
请求的 completion-limit 字段由可信 profile/metadata 决定；现代官方 OpenAI
使用 max_completion_tokens，未知兼容服务使用 max_tokens。
通用 HTTP request transport 同时承载 metadata GET 与聊天 POST，替换旧专用接口。

选择此方案而非模型名猜测、运行时网页抓取或盲目增大窗口，是为了容量有可核对
来源、测试覆盖同一生产路径，并保持未知服务的保守行为。目录维护是本次新增
成本；后续加入 provider profile 必须附官方证据。token 计数边界仍须如实说明。

参考：[OpenAI 模型目录](https://developers.openai.com/api/docs/models/compare)、
[Models retrieve](https://developers.openai.com/api/reference/python/resources/models/methods/retrieve)、
[Chat 请求参数](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)、
[OpenRouter metadata](https://openrouter.ai/docs/api/api-reference/models/get-model)。

### 恢复决策

Provider 持有独立 session artifact，使用当前 workflow-recovery 契约。
canonical session 与工具身份均为 numeric ID；外层坐标为 workflow/node/capability/
per-node ordinal，外部模型名、tool name/call id 仅是配置或协议字段。
绑定覆盖完整 IR、workflow、input、非 secret 模型配置、工具描述、initial prompts、
参数和编译模块。恢复时按当前 provider 重新解析容量，不保存未消费的容量副本；
动态容量变化不破坏身份，窗口缩小必须在下一 HTTP 前 compact 或拒绝。

采用两层握手：先持久 Prepared session、返回 Pending、外层保存 frontier，随后
才允许 resolver 发 HTTP/工具。CLI 自动继续匹配的 LLM frontier，用户不必提供模型
最终结果。显式 native/raw pending result 保持原有类型校验与优先级；无匹配模型
session 的其他 pending 保持现有人工恢复行为。

每次 HTTP 前存 request digest 与保守用量预留；完整模型 response/tool_calls 在
任何工具执行前持久化。工具执行前存 InFlight，结果返回后存完整 Completed
receipt，已完成结果直接复用。崩溃留在 InFlight 时未知提交状态，fail closed，
不自动重复副作用；不能声称普通外部工具已获得 exactly-once。模型请求可重试，
未确认用量的预留保留以免跨恢复绕过预算，可能保守过计。

Store 独立随机本地 key、0700 目录、0600 文件、HMAC-SHA256 完整性校验和 durable
atomic writes；不从模型凭证派生 key，以便凭证轮换。它不加密，也不防能修改 key
的同用户攻击者，不能宣传为本机敌手模型下的认证存储。

显式 recovery path 衍生 `.llm` session root；默认以完整 public binding digest
定位私有 managed active manifest，排他锁防并发误续跑。进程死亡后同绑定自动恢复，
不同绑定不复用；completed 状态先终止再清理，新的 run 使用新 namespace。
无 outer 坐标的直接 invoke 使用同一状态机的临时同步 session，不伪造持久恢复身份。

### Compact 决策

保留 system、完整 typed input、近期完整工具交换；只压缩最老封闭 exchange prefix。
原始 history 与 receipts 持久保存，摘要只是派生提示。无工具 summarizer 的完整
请求本身须入预算，用量记入 session ledger；摘要覆盖 numeric history IDs，保存后
必须严格降低下一主请求估计。无进展、无法容纳完整 exchange、超过尝试上限明确
停止，绝不字节截断。内置只读 history_get 按 numeric history/chunk ID 读取完整、
校验过的分块旧记录，避免摘要成为唯一事实来源。

拒绝直接重启 Agent、把历史塞入 workflow checkpoint、未知提交工具自动重试、全局扫描 latest run
等替代方案：它们分别丢语义/扩大稳定格式/重复副作用/混淆运行身份。新增会话
状态、文件与摘要请求的维护成本由真实恢复和免调参需求承担。

### 独立审查的硬验收

- Prepared -> outer frontier durable -> 外部动作；孤立 Prepared 无副作用。
- Frontier 成功注入（包括人工值）立即记录完整 memo，PostFrontier 成功也记录；
  第二次挂起可冷启动恢复，不能重发前序调用。
- 完整 assistant calls 先持久，再工具 InFlight，再 exact receipt；未知提交停止。
- 跨进程完整性/绑定失败零外部调用；使用量不因重启清零。
- compact 确实调用 summarizer、降低请求，原始旧事实可按只读 history 工具找回。
- 默认恢复、新运行、并发 busy、手工恢复与临时 invoke 均有原生/CLI 回归。

### 实施审查补充

独立审查发现 frontier 注入及 PostFrontier 成功结果必须加入外层 memo，
否则第二次暂停后的重放会重复已完成动作；本次统一记录两种成功路径。
resolver 返回 optional result：不存在 provider session 时保留通用 pending 的
手工结果要求，不误报 LLM 绑定错误。手工 pending 注入有纯校验 guard：任何匹配
的持久 LLM session（包括内部已 Completed 而外层 frontier 尚未推进的崩溃窗口）
都只能从 canonical session 续跑，拒绝手工替换结果。没有 LLM session 的旧
`--suspend-capability` 示例继续使用原有手工注入流程。

### 2026-10-12：完成调用的报告元数据

第三次专职决策审查多次内部续跑与冷启动后的最终报告，选择独立纯读取
`CompletedCapabilityMetadataReader`。MemoHit 先核对数值坐标、参数和类型，再从
HMAC 会话取得 Completed 记录；canonical 输出必须与 memo 严格一致。只恢复
attempts、cache hit、usage/notices 和 provider 标记，通过既有事件投影生成报告，
输出仍由 memo 提供。该读取不执行 HTTP、工具、容量发现、持久化写入或用量累加。
不存在会话时保留通用 memo 行为；存在未完成或不一致记录时 fail closed。
费用估算标记来自会话持久化的 `cost_estimated`：只有实际使用的 candidate 配置了
非零费率才置为 true，实时与冷恢复一致；未配置费率时保持 false。

拒绝只在 runtime 内存缓存（不能冷恢复）、CLI 拼接事件/直接改 report（错误层级
且破坏事件投影真相）及把 LLM 会话数据复制到 workflow recovery（职责重复）。新增成本
限于一个中性读取回调和新 LLM 会话中的完成元数据。验收要求最终两节点报告保留
两次 completion/usage，缓存标记 `[false,true]` 与冷缓存 `[true,true]` 正确，冷启动
不增加 HTTP/工具执行，不重复累计用量；无读取回调的既有手工恢复不变。

### 2026-10-12：纯编译门禁与完成清理

另一专职决策选择同一 Wasm runtime 两阶段配置：CLI 先完成纯编译并保留编译
产物，失败立即呈现诊断，成功才打开恢复目录、认证记录、解析凭证和 attach
执行配置。配置只允许在首次 run 前附加一次。拒绝先 lease 后编译（纯失败有目录
副作用与错误 busy）、重编译及预跑执行；没有第二套 compiler-owner。
纯计算及完全由标准能力/显式 binding 执行的 workflow 不创建 LLM run namespace。

显式恢复路径只清理命令启动时没有外层 snapshot、正常完成的新任务内部生成的
checkpoint/session；加载既有 snapshot 的手工/自动恢复保留原有文件语义。
私有 Completed 标记先 durable 保存，再清理多个文件，保留锁 inode；冷启动见
该标记只完成清理。拒绝改变所有旧手工恢复成功后的文件行为。恢复使用唯一当前结构，不维护历史读端。

工具幂等键还绑定会话 namespace：持久会话使用本地随机 key 的公共摘要，冷启动
稳定；独立新任务创建不同随机 key。直接调用使用本次数值 session ID。这样同一
输入的新任务不会被支持幂等的外部工具当作上次调用，而恢复不会生成新调用键。

### 2026-10-12：终止失败的恢复语义

独立专职决策确认仅保存错误文本不足以恢复失败：实时重试耗尽、预算诊断及调用
计数会在冷启动后退化成通用 Error。采用单一可选 `SessionTerminalFailure`，保存
最终 status、可选 failure_kind、错误文本、诊断代码及 usage 存在标记；复用既有
attempts、用量 ledger、notices 和 provider 元数据，不复制整个 capability result。
终止入口先确定最终类别，再一次持久化；冷恢复纯重建，不查询容量、不发 HTTP、
不执行工具、不重复记账。已发生调用的失败同时报告累计用量与未确认的预留。

拒绝仅保存文本、序列化包含 Value/Pending 的通用结果、复制 LLM 数据到 workflow-recovery
及从日志猜测权威结果。成本限于新会话格式、codec、终止路径和原生测试；损坏、
绑定或 I/O 失败仍报告当前恢复错误，不冒充已恢复原始失败。验收覆盖重试耗尽、
请求前后预算拒绝、已完成工具后的失败、解析失败及非法终止类别/成功失败冲突；
冷恢复结果元数据一致且零新增 HTTP/工具，既有成功恢复继续通过。
