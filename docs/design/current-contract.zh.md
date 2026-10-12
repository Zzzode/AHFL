# 单一当前契约

## 2026-10-12 决策

用户明确授权删除历史兼容和代际版本号。专职决策代理选择一个当前契约，
不保留旧读端、迁移开关、兼容别名或并行格式。编译器、runtime、工具与产物在
同一变更内更新。此决策取代此前文档对项目自有格式的版本化与兼容承诺。

自描述 JSON 使用唯一 `format: "ahfl.<artifact>"` 类别标识，不含 vN，不另加
`schema_version` 或 `format_version`。binary magic、custom section、端点和路径
使用无版本名称；二进制结构只编码当前字段，不写格式版本字节。所有读取只接受
当前必填字段、规范编码、类型和完整结构；尾部必须读尽，不能用 EOF 缺省历史
扩展字段。零值、空容器和显式 null 表达当前结构中的不存在。

恢复使用 `format: "ahfl.workflow-recovery"`，固定包含 `suspended`（null 或对象）。
持久结果仅保存精确 `result_wire_json` 和 presence，不保留重复普通 JSON 投影。
有效的进程内 Value 与从磁盘读取的精确 bytes 是两种来源，使用 variant 表达；
磁盘结果必须按当前位置的强类型 binding 解码。摘要不成为业务状态，unknown
commit 仍明确停止，Completed 回执仍直接复用。

编译缓存不协商 schema 编号，以实际编译器文件与目标/配置的内容摘要隔离命名
空间。缓存不匹配直接 miss，不读取或升级旧缓存。AHFL registry 使用唯一
`/packages/...` 路径，不保留 `/v1` 或 `/api/v1` 路由别名。无生产调用者的 ABI
版本兼容模块删除；Wasm/C ABI 由固定当前 imports/exports/结构校验，不协商编号。

拒绝仅重命名旧版本、接受缺字段以自动升级、继续写旧字段便于旧读端理解、以及
用递增 cache 编号代替真实 build 身份：它们继续维护用户明确不要的历史契约。
维护成本是每次结构变化同步修改生产者、消费者和测试；旧生成产物必须重建。

第三方协议必须遵守第三方当前要求，Wasm 标准 header、OpenAI/Vault/Kubernetes
API 路径、JSON Schema 标准等保留。软件发布和包依赖的语义版本属于用户选择
依赖的业务数据，保留；它们不用于选择 AHFL 历史实现。

验收：项目自有版本标记与历史分支清零；当前产物严格读写闭环；旧结构拒绝；
编译器 build 改变导致 cache miss；自动 compact、session/checkpoint/resume 和
副作用保护回归通过；完整构建、全仓测试及独立审查记录真实结果。

## 当前入口与缓存身份

Agent 使用 `run_frame`，workflow 使用 `run_workflow`，名称表达实际执行语义。
删除无生产消费者的 pointer-only `run` 和 ABI 版本导出；控制与观测导出保留。
C host header 的 `struct_size` 只描述精确当前结构大小，不承诺追加字段兼容。
当前 C header 仍是契约声明，没有独立的 C 入口实现。

缓存 producer、source content、configuration、dependency 与 source graph 都使用
SHA-256 内容摘要。摘要不匹配或结构不完整直接 miss。恢复节点 ID 在转换前
必须位于 descriptor 的 u32 域，禁止高位 ID 折叠为已有节点。

Manifest 与 workspace 不再携带 `manifest_version` 或 resolver 版本字段，
依赖来源只由当前显式 dependency 配置表达。包的 SemVer 保持为依赖选择数据。

## 2026-10-12 声明符号身份决策

独立决策代理 `typed_decl_symbol_decision` 选择将 `TypedDecl::symbol` 改为
`std::optional<SymbolId>`，仅表示声明自身定义的符号。合法编号0不能表示缺失；
Module/Import/Impl/Contract/Flow 元数据没有自身符号，写为 `nullopt`，Flow 的
Agent 归属只写 `associated_agent_symbol`。builder 与 decider 分离。

拒绝在查询端增加 NodeKind 白名单：它仅遮住单个查找点，无法消除 lowering
判0为空、LSP 索引假匹配和磁盘快照的伪身份。也拒绝给元数据分配虚构符号，或
将 Resolver 改为从1编号；前者污染定义身份空间，后者保留无必要的哨兵。
Rust/Swift/Clang 的定义身份与可选语义信息分离原则支持显式 optional，AHFL
没有保留0哨兵的特定理由。

代价是一次迁移 compiler、LSP、monomorphization、序列化和原生测试的消费点；
当前 Typed HIR 的必填 `symbol` 字段写为 null 或整数，无旧0编码读端。验收为
首个真实符号0可正常构造与 dispatch、匿名记录无身份、Flow 归属唯一、当前
快照闭环且拒绝伪身份、LSP 细节与全仓回归通过。
