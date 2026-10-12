# 当前契约与历史兼容清理

## 目标与范围

2026-10-12：用户明确要求删除项目自有的前向兼容、legacy 实现和 V1/V2/V3
版本分支。本次授权覆盖共享接口、持久化格式、编译缓存、CLI、registry、文档及
测试；旧生成产物不迁移、不提供兼容开关。保留工作区已有及并行任务的有效修改。

所属模块分别负责自己的当前结构，统一只有一个读写契约。格式身份用于识别
产物类别；内容摘要、类型、完整性与严格准入用于判断有效性。第三方协议要求的
版本、软件发布版本及包依赖语义版本不属于历史实现兼容，不能破坏。

## 阶段与验收

1. 盘点生产代码中的旧分支、版本命名、兼容 API、宽松缺省读取和死模块。
2. 专职决策代理确定无版本格式、缓存有效性及二进制结构；记录后分别实施。
3. runtime 删除旧恢复/旧 wire schema 准入；compiler 删除历史 layout/schema
   分支；主代理统一剩余格式、CLI/cache/frontend/registry 调用点与文档。
4. 更新原生回归与 golden，增加必要的旧结构拒绝、当前结构闭环和缓存证据。
5. 全 18 核构建与全仓 CTest，独立审查，修复相关失败并核对剩余匹配。

成功标准：项目自有产物不含代际版本号；无历史迁移读端、版本协商或 legacy
适配接口；唯一当前结构严格读写；自动 compact/checkpoint/resume、强类型验证
及无重复外部副作用继续成立。剩余外部协议匹配必须逐项解释。

## 实施与审查

已采用 [单一当前契约](../design/current-contract.zh.md) 的决策并完成全仓调用点迁移：

- 删除自有 JSON/binary 格式版本、manifest/resolver 选择器、ABI 协商模块和旧 registry 路径；读取端只接受完整当前结构。
- Wasm 使用语义入口 `run_frame` / `run_workflow`；恢复快照、精确 wire memo、HMAC 与节点 ID 范围校验保持严格。
- 编译与响应缓存用 SHA-256 内容身份；删除历史缺字段 fallback、字符串方法重解析与无生产调用的模块。
- 删除 106 个标准库 deprecated receiver 转发函数、sentinel unwrap 和占位 zip；迁移到当前方法及真实构造函数。
- 删除 bare assert 与未实现的 mut binding 语法，使用锁定 ANTLR 生成器同步 parser；更新消费者、教程及 native golden。
- 将 current-contract 检查纳入现有 architecture gate，阻止生产代码再次引入自有代际字段或已删除模块。

独立审查发现的标准库语法残留、方法 dispatch 缺失/无效、当前 IR writer effect 字段不完整、泛型定义 source ID 和 wildcard decreases 误判已修复。泛型 enum constructor 方法推断、限定名方法类型实参及恢复容量预检的8字节对齐遗漏也已修复并补原生回归。标准库原生18项测试、架构检查、IR文档同步、28份RFC检查及 Playground/VSCode TypeScript 构建已通过；最终原生全量与安装验证使用18核执行，结果以 CTest 输出为准。

最终只读审查进一步发现 Typed HIR capability 缺 `effect` 的空指针风险、Struct/Enum/Capability/Fn 的 `where_clause` 往返丢失及诊断报告宽松准入。已完成局部修复并补缺字段/错误字段拒绝与非空约束闭环回归；enum constructor 的 owner 也统一为 canonical `SymbolId`，删除声明名称扫描兜底。最终验证在所有源码和文档冻结后重跑全仓 CTest，安装与发布证据使用相同源码摘要。

## 边界与风险

这是已授权的破坏性契约修改：旧生成产物、cache、恢复文件、旧 manifest 字段与旧标准库转发函数必须重新生成或更新，不提供升级读端。第三方 API 路径、Wasm 标准头及包依赖/软件发布 SemVer 保留。

macOS 无法执行依赖 Linux 持久 payload-store 原语的4项测试，须在Linux运行对应CTest。C host header 为当前契约声明，没有独立生产 C 入口实现。未创建提交；工作区已有及并行任务有效修改均保留。
