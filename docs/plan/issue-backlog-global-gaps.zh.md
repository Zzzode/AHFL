# AHFL 全局未完成工作清单

本文档汇总截至 v0.56 的所有已识别但未实现的工作项，按领域分类并标注建议优先级与目标版本。

---

## 一、核心编译器管线

### 1.1 优化与分析 Pass 层（P1，v0.58+）

当前 IR 从 AST lowering 后直接进入后端发射，无中间优化/分析基础设施。

- [ ] 建立 Pass Manager 基础设施（Pass 注册、依赖声明、执行调度）。
- [ ] 实现 Dead State Elimination pass（移除不可达状态，减小 SMV 状态空间）。
- [ ] 实现 Contract Redundancy Analysis（检测冗余/矛盾约束）。
- [ ] 实现 Workflow DAG Simplification（线性链折叠、冗余依赖消除）。
- [ ] 实现 Expression Canonicalization（表达式规范化，避免 SMV 公式膨胀）。
- [ ] 实现 Capability Reachability Analysis（裁剪 dead capability 引用）。
- [ ] 实现 Temporal Formula Simplification（时序逻辑等价简化）。

### 1.2 增量编译（P3，v0.60+）

- [ ] 实现模块级依赖追踪（import graph + 文件修改时间戳）。
- [ ] 实现 IR 缓存机制（序列化 IR 到磁盘）。
- [ ] 实现增量 lowering（仅重编译变更模块及其依赖者）。
- [ ] 实现并行编译（多模块独立 lowering 可并行）。

### 1.3 错误恢复增强（P2，v0.58+）

- [ ] 自定义 ANTLR ErrorStrategy（结构性错误恢复：缺失 `}`、缺失 `;`）。
- [ ] 实现 "did you mean...?" 建议（编辑距离匹配已知符号）。
- [ ] 支持部分解析结果（即使有解析错误也尽量产出部分 AST）。

---

## 二、形式化验证层

### 2.1 反例诊断（P0，v0.57）

- [ ] 解析 NuSMV counterexample trace 为结构化数据（状态序列 + 变量赋值）。
- [ ] 实现 SMV 符号 → AHFL 源码位置的反向映射（利用已有 AHFL_MAP 注释）。
- [ ] 生成人类可读的违规路径描述（"在状态 X 调用 capability Y 后违反 invariant Z"）。
- [ ] 支持 counterexample 的 JSON 输出格式（供 IDE/工具消费）。
- [ ] 添加 `ahflc verify-formal --explain` 子命令。

### 2.2 有界模型检测 BMC（P2，v0.59+）

- [ ] 实现 SAT-based BMC 后端（浅层 bug 快速发现）。
- [ ] 实现 k-induction 验证（可扩展性优于 BDD）。
- [ ] 研究 CEGAR（抽象精化）对 Agent 状态机的适用性。

### 2.3 多模型检测器支持（P2，v0.59+）

- [ ] 支持 nuXmv 后端（IC3/PDR 无界验证）。
- [ ] 支持 SPIN/Promela 后端（并发协议验证）。
- [ ] 研究 TLA+/TLC 后端可行性（分布式系统验证）。

### 2.4 模型检测器集成方式改进（P2，v0.58+）

- [ ] 消除 `popen()` 依赖，改用跨平台进程管理（支持 Windows）。
- [ ] 研究 NuSMV/nuXmv library-mode 嵌入可行性。
- [ ] 支持增量验证（仅验证变更相关的 LTLSPEC）。
- [ ] 实现 SMV 状态空间大小预估（验证前预警状态爆炸）。

---

## 三、运行时与执行层

### 3.1 HTTP/gRPC Capability 真实实现（P0，v0.57）

- [ ] 实现 `make_http_capability()` 真实 HTTP 调用（替换当前 stub）。
- [ ] 实现 `make_grpc_capability()` 真实 gRPC 调用（替换当前 stub）。
- [ ] 实现请求/响应的 Value ↔ JSON/Protobuf 序列化。
- [ ] 实现连接池管理。
- [ ] 实现超时、重试、断路器策略。

### 3.2 并行工作流执行（P2，v0.58+）

- [ ] 实现线程池/协程调度器（DAG 独立节点并行执行）。
- [ ] 实现节点间数据流管道（前驱输出 → 后继输入）。
- [ ] 定义并行失败传播语义（fail-fast vs. wait-all）。
- [ ] 实现节点级 retry 与 failover。

### 3.3 分布式执行（P3，v0.61+）

- [ ] 定义节点远程调度协议（gRPC/HTTP）。
- [ ] 实现状态序列化/反序列化（Agent 状态快照 → 可恢复格式）。
- [ ] 实现 checkpoint/resume 真实 restore 逻辑。
- [ ] 实现故障转移策略（节点失败 → 重新调度）。
- [ ] 实现多 region 调度。

### 3.4 LLM Provider 增强（P1，v0.57–v0.58）

- [ ] 实现 Streaming 响应处理（SSE 流式接收）。
- [ ] 实现 Function Calling / Tool Use 协议支持。
- [ ] 实现多 Provider 路由与 fallback（主/备模型切换）。
- [ ] 实现 Token 用量统计与预算控制。
- [ ] 实现调用结果缓存（相同输入跳过 LLM 调用）。
- [ ] 实现重试与断路器策略。
- [ ] 实现 Prompt 模板自定义 / 用户覆写。

---

## 四、开发者工具链

### 4.1 Language Server Protocol — LSP（P1，v0.58）

- [ ] 实现 LSP 框架集成（基于 JSON-RPC）。
- [ ] 支持 `textDocument/diagnostic`（实时错误报告）。
- [ ] 支持 `textDocument/completion`（关键字 + 符号补全）。
- [ ] 支持 `textDocument/definition`（跳转到定义）。
- [ ] 支持 `textDocument/hover`（悬浮类型/文档信息）。
- [ ] 支持 `textDocument/references`（查找所有引用）。
- [ ] 支持 `textDocument/rename`（重命名符号）。
- [ ] 支持 `textDocument/documentSymbol`（文件大纲）。
- [ ] 支持 `workspace/symbol`（全局符号搜索）。

### 4.2 代码格式化器（P2，v0.58+）

- [ ] 定义 AHFL 官方格式化规则（缩进、换行、对齐）。
- [ ] 实现 `ahflc fmt` 命令（in-place 格式化）。
- [ ] 实现 `ahflc fmt --check`（CI 格式检查）。
- [ ] 支持 `.ahfl-format` 配置文件（可定制规则）。

### 4.3 IDE 扩展 — VS Code（P2，v0.58+）

- [ ] 实现 TextMate grammar（语法高亮）。
- [ ] 实现 snippet 模板（agent/workflow/capability 骨架）。
- [ ] 实现 LSP 客户端集成。
- [ ] 实现问题面板集成（诊断 → VS Code Problems）。
- [ ] 实现 CodeLens（agent 状态数 / capability 数）。

### 4.4 REPL / 交互式环境（P3，v0.59+）

- [ ] 实现表达式求值 REPL（快速验证类型推导结果）。
- [ ] 支持单 agent 状态转换模拟（交互式步进）。
- [ ] 支持 `:type` 命令（显示表达式类型）。
- [ ] 支持 `:verify` 命令（对当前定义运行形式化验证）。

### 4.5 调试器（P3，v0.60+）

- [ ] 实现 DAP (Debug Adapter Protocol) 集成。
- [ ] 支持状态断点（在指定 agent 状态暂停）。
- [ ] 支持 capability 调用断点。
- [ ] 支持变量检查（当前 context/input/output 值）。
- [ ] 支持步进执行（single state transition）。

---

## 五、安全与产品化

### 5.1 ABI 稳定性（P2，v0.59+）

当前 7+ 处声明 "ABI not yet promised"，需逐步稳定化：

- [ ] 定义 ABI 版本化策略（semver for IR / runtime protocol）。
- [ ] 稳定 durable store adapter ABI。
- [ ] 稳定 executor ABI。
- [ ] 稳定 persistence writer/recovery ABI。
- [ ] 添加 ABI 兼容性测试（golden binary 对比）。

### 5.2 Secret 管理（P1，v0.57+）

- [ ] 支持环境变量安全注入（不落盘明文）。
- [ ] 集成 Vault / Secret Manager 接口。
- [ ] 实现密钥轮换支持。
- [ ] 实现 mTLS / OAuth2 认证。
- [ ] 审计敏感配置访问日志。

### 5.3 沙箱隔离（P2，v0.59+）

- [ ] 实现 Capability 执行的进程级隔离。
- [ ] 实现资源配额真实强制（CPU/Memory/网络）。
- [ ] 实现网络策略（白名单出站域名）。
- [ ] 实现文件系统隔离（只读挂载 + 临时写入目录）。

---

## 六、可观测性与运维

### 6.1 OpenTelemetry 集成（P2，v0.58+）

- [ ] 实现 Trace 导出（编译/验证/执行全链路 span）。
- [ ] 实现 Metrics 导出（编译耗时、验证耗时、状态转换计数）。
- [ ] 实现结构化日志（JSON 格式，支持日志级别过滤）。
- [ ] 实现分布式追踪 context propagation（跨 capability 调用）。

### 6.2 性能 Profiling（P3，v0.59+）

- [ ] 实现编译 pass 耗时统计（`ahflc --time-passes`）。
- [ ] 实现 SMV 状态空间大小报告（验证前预估）。
- [ ] 实现运行时执行热点分析（哪个 state handler 最耗时）。
- [ ] 实现内存使用追踪（IR 大小、AST 大小）。

---

## 七、编译目标扩展

### 7.1 WASM 编译目标（P3，v0.60+）

- [ ] 设计 AHFL → WASM 运行时模型（Agent 状态机 → WASM module）。
- [ ] 实现 WASM 后端代码生成。
- [ ] 实现浏览器端 Agent 执行引擎。
- [ ] 实现 WASI 接口映射（Capability → WASI import）。

### 7.2 其他生成目标（P3，v0.61+）

- [ ] 研究 Kubernetes CRD 生成（Agent → K8s Operator 定义）。
- [ ] 研究 OpenAPI Spec 生成（Capability → REST API 文档）。
- [ ] 研究 Terraform/Pulumi 生成（Workflow → IaC）。

---

## 八、测试与质量保证

### 8.1 Fuzzing（P2，v0.58+）

- [ ] 集成 libFuzzer 对 Parser（grammar-aware fuzzing）。
- [ ] 集成 libFuzzer 对 TypeChecker（随机 AST 输入）。
- [ ] 集成 libFuzzer 对 SMV emitter（恶意 IR 输入）。
- [ ] 建立 Fuzzing CI job（OSS-Fuzz 或自托管）。

### 8.2 属性测试（P3，v0.59+）

- [ ] 实现随机 AST 生成器（well-typed program generator）。
- [ ] 验证 lowering 前后语义等价性质。
- [ ] 验证 SMV 输出的语法正确性（自动调 NuSMV parse）。

### 8.3 性能回归测试（P2，v0.58+）

- [ ] 建立编译时间基准测试套件（bench/）。
- [ ] 建立内存使用基准。
- [ ] 建立 SMV 文件大小跟踪（防止输出膨胀）。
- [ ] CI 中集成性能回归检测（超阈值则 fail）。

### 8.4 突变测试（P3，v0.60+）

- [ ] 集成突变测试框架（如 Mull）。
- [ ] 量化测试套件的缺陷检测能力。
- [ ] 针对低突变分数模块补充测试。

---

## 九、文档与生态

### 9.1 Online Playground（P3，v0.60+）

- [ ] 基于 WASM 后端实现浏览器端编译器。
- [ ] 实现在线编辑器（Monaco Editor + AHFL 语法高亮）。
- [ ] 支持在线形式化验证（内嵌 SMV 模拟器或远程调用）。
- [ ] 提供示例库（可一键加载的 Agent/Workflow 模板）。

### 9.2 包管理与生态（P3，v0.61+）

- [ ] 设计包注册表协议（publish/resolve/fetch）。
- [ ] 实现 `ahflc publish` / `ahflc install` 命令。
- [ ] 支持语义化版本解析与依赖冲突检测。
- [ ] 建立官方包仓库。

---

## 优先级总览

| 优先级 | 工作项 | 建议版本 |
|--------|--------|----------|
| **P0** | 反例诊断（counterexample → 源码映射） | v0.57 |
| **P0** | HTTP/gRPC Capability 真实实现 | v0.57 |
| **P1** | LLM Provider 增强（Streaming + Function Calling） | v0.57–v0.58 |
| **P1** | LSP 语言服务器 | v0.58 |
| **P1** | Secret 管理 | v0.57+ |
| **P1** | 优化 Pass 层基础设施 | v0.58+ |
| **P2** | 并行工作流执行 | v0.58+ |
| **P2** | 代码格式化器 | v0.58+ |
| **P2** | nuXmv / BMC 集成 | v0.59+ |
| **P2** | OpenTelemetry 可观测性 | v0.58+ |
| **P2** | Fuzzing 基础设施 | v0.58+ |
| **P2** | IDE 扩展 (VS Code) | v0.58+ |
| **P2** | ABI 稳定性 | v0.59+ |
| **P2** | 沙箱隔离 | v0.59+ |
| **P2** | 性能回归测试 | v0.58+ |
| **P2** | 错误恢复增强 | v0.58+ |
| **P2** | 模型检测器集成改进 | v0.58+ |
| **P3** | WASM 后端 | v0.60+ |
| **P3** | 增量编译 | v0.60+ |
| **P3** | REPL 交互环境 | v0.59+ |
| **P3** | 调试器 (DAP) | v0.60+ |
| **P3** | 分布式执行 | v0.61+ |
| **P3** | 属性测试 | v0.59+ |
| **P3** | 突变测试 | v0.60+ |
| **P3** | Online Playground | v0.60+ |
| **P3** | 包管理与生态 | v0.61+ |
| **P3** | 其他编译目标（K8s/OpenAPI/Terraform） | v0.61+ |
