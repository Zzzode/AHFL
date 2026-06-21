# AHFL 语言规范结构重写方案

> 本文档不是规范本身，而是规范的"重构施工图"。它诊断当前 spec 组织的可读性与维护问题，给出新的分册结构设计、core/native 边界划定原则、章节迁移映射表，以及分阶段重写路线图。
>
> 范围：仅涉及 `docs/spec/` 下的规范文本组织。`docs/design/`、`docs/reference/`、`docs/plans/` 不在本方案的重构边界内，仅在迁移映射表中标注它们的引用关系。

## 1. 现状诊断

### 1.1 单文件规模

| 文件 | 行数 | 职责 |
| --- | --- | --- |
| `docs/spec/core-language.zh.md` | 1149 | 词法 + EBNF 全集 + 类型系统 + 静态语义 + 完整示例 |
| `docs/spec/assurance.zh.md` | 214 | capability effect profile + production gate + formal verification gate + obligation + bundle |

core-language 是绝对瓶颈。它一个文件同时承担了三个性质完全不同的子领域：

1. **词法与文法**：纯语法描述，需要与 `grammar/AHFL.g4` 严格 1:1。
2. **类型系统**：形式化规则、子类型闭包、effect 分级，需要与 `type_relations.cpp`、`effects.cpp` 对齐。
3. **静态语义约束**：声明级、agent/contract/flow/workflow 各级检查清单，需要与 `validate.cpp`、`typecheck*.cpp` 对齐。

这三类读者关注点完全不同：parser 维护者只读 §2-§3，类型系统维护者只读 §4，checker 维护者只读 §3 语义约束 + §5。把它们塞进同一个 1149 行文件，导致任何局部修订都需要在巨型文件里翻页定位，diff review 成本高，章节编号一旦插入就要全文重排。

### 1.2 P0 漂移缺口的根因

本轮已修复的 4 项 P0 缺口（见 `core-language.zh.md` §3.4 effect block、§4.1 Any/Never/ErrorT、§4.6.8 ExprEffect；`assurance.zh.md` 命令别名）有一个共同根因：

**单文件结构让"哪些章节必须同步"变得不可见。**

- effect block EBNF 缺失：因为 capability effect block 的"语法归属"在 core spec，"语义归属"在 assurance spec，单文件 core 没有给跨册同步留位置。
- ExprEffect 6 级未文档化：因为类型系统与"effect 静态事实"被装在同一个 §4 里，但实现侧它们是两个独立文件（`type_relations.cpp` / `effects.cpp`），文档结构没有反映这一事实。
- Any/Never/ErrorT 关系规则缺失：因为它们是"内部辅助类型"，单文件 §4.1 只有一段文字提及名字，没有为"内部类型关系"留独立小节。

结构重写的目的，正是让**每个实现文件**都能在 spec 中找到**对应且唯一**的归属位置，使后续同步变得机械可查（已有门禁脚本 `tools/spec/check_grammar_coverage.py` 覆盖 grammar↔spec 一侧）。

### 1.3 与其他文档的耦合

`docs/spec/core-language.zh.md` 当前被以下文档引用：

- `docs/reference/user-guide-authoring.zh.md`、`user-guide-overview.zh.md`：面向用户，引用 spec 章节号。
- `docs/design/semantics-architecture.zh.md`、`frontend-lowering-architecture.zh.md`：面向 contributor，引用 §3/§4。
- `docs/design/formal-backend.zh.md`：引用 §3.11 时序表达式实现说明。

重写后这些引用会失效，需要一次性更新（见 §5 路线图第 4 阶段）。

## 2. 新分册结构设计

### 2.1 拆分原则

1. **按读者关注点拆，不按文件大小拆**。每个分册对应一类维护者：parser / type / semantics / assurance。
2. **每份分册 ≤ 400 行**为目标，超出说明职责边界没划清。
3. **EBNF 与语义约束就近**。一条规则的 EBNF 与它的语义约束应在同一份文档，不跨册跳转。
4. **core 与 native 物理分册**。core 是规范冻结区，native 是演进区，混在一起会让读者无法判断哪些是稳定契约。
5. **reference 独立成系列**。reference 是"如何使用"，spec 是"语言是什么"，混在 spec/ 下会让规范层级模糊。

### 2.2 新目录树

```
docs/spec/
├── README.zh.md                      # 分册索引 + 阅读路径 + 规范冻结策略
├── core/
│   ├── 01-lexical.zh.md              # 词法：注释、关键字、标识符、字面量
│   ├── 02-grammar-overview.zh.md     # 顶层结构 + EBNF 索引 + 与 grammar/AHFL.g4 对齐说明
│   ├── 03-declarations.zh.md         # 10 类顶层声明的 EBNF + 语义约束
│   ├── 04-statements-and-expressions.zh.md  # Block/Statement/Expr/TemporalExpr EBNF + 约束
│   ├── 05-type-system.zh.md          # 类型宇宙 + 子类型 + 内部辅助类型关系 + ExprEffect
│   ├── 06-static-checks.zh.md        # 声明/agent/contract/flow/workflow 检查清单
│   └── 07-reference-example.zh.md    # 最小一致性完整示例
├── core-native-boundary.zh.md        # core vs native 边界划定（见 §3）
├── native/
│   └── native-language.zh.md         # tool 实现体 / llm_config / observability / compliance
├── assurance/
│   ├── capability-effect-profile.zh.md   # effect block 字段语义 + obligation 合成
│   ├── production-gate.zh.md             # validate-assurance 规则
│   └── formal-verification-gate.zh.md    # verify-formal + SMV backend 契约
└── formal/
    └── temporal-semantics.zh.md      # 时序表达式语义 + SMV backend 映射（从 design/ 提升为规范）
```

### 2.3 每份文档的职责边界

| 文档 | 唯一职责 | 不应包含 |
| --- | --- | --- |
| `core/01-lexical.zh.md` | 词法层全部规则 | 任何语法或语义 |
| `core/02-grammar-overview.zh.md` | 顶层结构、模块/导入、EBNF 总索引 | 具体声明的 EBNF 细节 |
| `core/03-declarations.zh.md` | 10 类顶层声明的 EBNF + 就近语义约束 | 表达式细节、类型规则 |
| `core/04-statements-and-expressions.zh.md` | Block/Statement/Expr/TemporalExpr/ConstExpr | 顶层声明、类型系统 |
| `core/05-type-system.zh.md` | 类型宇宙、子类型、内部辅助类型、ExprEffect、表达式/合同/flow/workflow 类型规则 | EBNF、检查清单 |
| `core/06-static-checks.zh.md` | 静态检查清单（声明级到 workflow 级） | 类型规则细节、EBNF |
| `core/07-reference-example.zh.md` | 完整可运行示例 | 任何规则定义 |
| `core-native-boundary.zh.md` | core/native 边界划定原则 + 扩展守则 | 具体语法 |
| `native/native-language.zh.md` | native 扩展全部规范 | core 已定义的规则 |
| `assurance/capability-effect-profile.zh.md` | effect block 字段语义 + obligation 合成 | production gate 规则细节 |
| `assurance/production-gate.zh.md` | validate-assurance 的全部硬门禁规则 | formal verification |
| `assurance/formal-verification-gate.zh.md` | verify-formal + SMV backend 契约 | production gate |
| `formal/temporal-semantics.zh.md` | 时序表达式形式语义 + SMV 映射 | EBNF（在 core/04） |

## 3. Core vs Native 边界划定

### 3.1 划定原则

一条规则属于 **core** 还是 **native**，由以下三个判据共同决定：

1. **可静态判定性**：能在 parse + resolve + typecheck + 基础 validate 阶段完成的规则属于 core；依赖运行时（provider、外部系统、LLM）的规则属于 native。
2. **形式化可证明性**：能 lower 到有限控制模型并交给 model checker 的规则属于 core；涉及无限数据域或 provider 实现的属于 native。
3. **稳定性承诺**：core 是冻结契约，破坏性变更需要 spec 版本号升级；native 是演进区，可按版本迭代。

### 3.2 当前明确归属

| 能力 | 归属 | 依据 |
| --- | --- | --- |
| 数据 schema（const/type/struct/enum） | core | 纯静态 |
| 纯 predicate 声明 | core | 纯、确定、无副作用 |
| capability **声明**（含 effect block 语法） | core | 声明是静态事实 |
| capability **实现体** | native | 依赖 provider |
| agent 状态机 + contract + flow + workflow | core | 编排控制面可形式化 |
| `llm_config` | native | 依赖 LLM provider |
| `observability` / `compliance` | native | 运行时配置 |
| `main` 与服务启动 | native | 运行时入口 |
| CTL / 通用高阶函数 / 用户自定义泛型 | 暂不归属 | 未稳定，留在边界文档待定 |
| capability effect profile **语义**（effect kind 定义） | core/assurance | 静态分类 |
| capability effect profile **运行时执行**（receipt/audit 实际产生） | native | 运行时行为 |

### 3.3 边界守则

`core-native-boundary.zh.md` 必须显式回答三个问题，任何 native 扩展提案都要先过这三关：

1. 这条规则是否引入了 core 无法静态判定的依赖？
2. 这条规则是否会破坏现有 core 形式化模型的可证明性？
3. 这条规则的命名空间是否与 core 冲突？

三者任一为"是"，规则进入 native 分册，且必须在 `native-language.zh.md` 中声明对 core 的最小扩展点。

## 4. 章节迁移映射表

下表把当前 `core-language.zh.md` 与 `assurance.zh.md` 的章节映射到新分册。**同一行可能多对一**（多个旧章节合并到一份新文档）。

### 4.1 core-language.zh.md 迁移

| 旧章节 | 旧标题 | 新文档 | 新章节 |
| --- | --- | --- | --- |
| §1 | 范围 | `core/02-grammar-overview.zh.md` | §1 范围 |
| §2.1-§2.4 | 词法定义全部 | `core/01-lexical.zh.md` | §1-§4 |
| §3.1 | 顶层结构 | `core/02-grammar-overview.zh.md` | §2 顶层结构 |
| §3.2 | 类型定义 EBNF | `core/03-declarations.zh.md` | §1 类型定义（EBNF 部分） |
| §3.3 | 常量/结构体/枚举/别名 | `core/03-declarations.zh.md` | §2 |
| §3.4 | capability 与 predicate 声明 | `core/03-declarations.zh.md` | §3（effect block 语法；语义引用 assurance/） |
| §3.5 | agent 定义 | `core/03-declarations.zh.md` | §4 |
| §3.6 | contract 定义 | `core/03-declarations.zh.md` | §5 |
| §3.7 | flow 定义 | `core/03-declarations.zh.md` | §6 |
| §3.8 | workflow 定义 | `core/03-declarations.zh.md` | §7 |
| §3.9 | 代码块与语句 | `core/04-statements-and-expressions.zh.md` | §1 |
| §3.10 | 表达式 | `core/04-statements-and-expressions.zh.md` | §2 |
| §3.11 | 时序表达式 | `core/04-statements-and-expressions.zh.md` | §3（EBNF）；语义引用 formal/ |
| §3.12 | 常量表达式 | `core/04-statements-and-expressions.zh.md` | §4 |
| §4.1 | 类型宇宙 | `core/05-type-system.zh.md` | §1 |
| §4.2 | 符号环境 | `core/05-type-system.zh.md` | §2 |
| §4.3 | 类型等价与子类型 | `core/05-type-system.zh.md` | §3（含 Any/Never/ErrorT 关系） |
| §4.4-§4.5 | 结构体枚举 / none 与空容器 | `core/05-type-system.zh.md` | §4 |
| §4.6 | 表达式类型规则（含 §4.6.8 ExprEffect） | `core/05-type-system.zh.md` | §5 |
| §4.7 | 合同与时序公式类型规则 | `core/05-type-system.zh.md` | §6 |
| §4.8-§4.9 | flow / workflow 类型规则 | `core/05-type-system.zh.md` | §7-§8 |
| §4.10 | 运行时边界校验 | `core/05-type-system.zh.md` | §9 |
| §5 | 静态检查清单 | `core/06-static-checks.zh.md` | §1-§5 |
| §6 | 最小一致性示例 | `core/07-reference-example.zh.md` | §1 |
| §7 | 结论 | `core/02-grammar-overview.zh.md` | §3 结论（精简） |

### 4.2 assurance.zh.md 迁移

| 旧章节 | 旧标题 | 新文档 |
| --- | --- | --- |
| 命令名约定 | 命令别名 | `assurance/production-gate.zh.md` §0（被 production-gate 与 formal-gate 共享，置于前者） |
| 目标 | Assurance 目标 | `assurance/README`（新增，作为 assurance 子目录索引） |
| Capability Effect Profile | effect block 字段语义 | `assurance/capability-effect-profile.zh.md` |
| Production Gate 规则 | validate-assurance 门禁 | `assurance/production-gate.zh.md` |
| Formal Verification Gate | verify-formal + SMV 契约 | `assurance/formal-verification-gate.zh.md` |
| Obligation Synthesis | obligation 合成 | `assurance/capability-effect-profile.zh.md` §obligation |
| Assurance Bundle | bundle 格式 | `assurance/production-gate.zh.md` §bundle |
| Formal Model Profile | formal model 抽象层 | `formal/temporal-semantics.zh.md` §profile |

## 5. 分阶段重写路线图

### 5.1 本轮（已完成）

- [x] `core-language.zh.md` §3.4 补 capability effect block EBNF
- [x] `assurance.zh.md` 补命令别名约定
- [x] `core-language.zh.md` §4.6.8 补 ExprEffect 6 级分级表
- [x] `core-language.zh.md` §4.1 补 Any/Never/ErrorT 内部类型关系规则
- [x] `tools/spec/check_grammar_coverage.py` grammar↔spec 一致性门禁
- [x] 本结构重写方案文档

### 5.2 后续轮（follow-up）

> 这些任务**不**在本轮交付范围，仅作为路线图记录。每一轮开工前需重新核对 spec 当前状态。

**第 2 轮：core/ 分册物理拆分（高优先级）**

1. 创建 `docs/spec/core/` 目录与 7 份分册骨架
2. 按 §4.1 映射表把 `core-language.zh.md` 内容**机械迁移**（不改写规则，只搬运 + 重编号）
3. 创建 `docs/spec/README.zh.md` 作为新索引
4. 更新 `docs/README.md` 中对 `spec/core-language.zh.md` 的引用路径（仅路径，不动其他内容）
5. 保留 `core-language.zh.md` 为重定向占位（顶部一行指向 `core/` 新位置），3 个版本周期后删除

**第 3 轮：assurance/ 分册物理拆分**

1. 创建 `docs/spec/assurance/` 子目录与 3 份分册
2. 按 §4.2 映射表迁移
3. 创建 `assurance/README` 子目录索引

**第 4 轮：跨文档引用修复**

1. 全仓库搜索 `core-language.zh.md#§` 形式的锚点引用，更新到新分册
2. 重点更新：`docs/reference/user-guide-*.zh.md`、`docs/design/semantics-architecture.zh.md`、`docs/design/formal-backend.zh.md`、`docs/design/frontend-lowering-architecture.zh.md`

**第 5 轮：core-native-boundary 与 native 分册**

1. 新建 `docs/spec/core-native-boundary.zh.md`，写入 §3 的边界划定原则与守则
2. 把当前散落在 `docs/design/native-runtime-architecture.zh.md`、`docs/design/core-scope.en.md` 中的 native 语法规范部分，提炼为 `docs/spec/native/native-language.zh.md`
3. 注意：design 文档不动，只把其中"规范性"内容提升到 spec/

**第 6 轮：formal/ 分册提升**

1. 把 `docs/design/formal-backend.zh.md` 中"时序表达式形式语义 + SMV backend 映射"的规范性部分，提升为 `docs/spec/formal/temporal-semantics.zh.md`
2. design 文档保留"工程设计"部分，删除被提升的规范内容，改为引用 spec/

**第 7 轮：门禁接入 CI**

1. 在 CI 中接入 `tools/spec/check_grammar_coverage.py --fail-on-spec-only`，把当前 36 项 only_in_spec 中的真实聚合抽象（如 `AgentDecl`/`Type`/`Statement`/`PrimaryExpr`）加入 alias 白名单
2. 把剩余 2 项 only_in_grammar（`QualifiedIdentList`、`QualifiedValueExpr`）作为待修缺口跟踪，决定是补 spec 还是承认 grammar 内联

### 5.3 风险与回滚

- **风险**：分册后跨册跳转增加。**缓解**：每份分册顶部放"相关分册"链接块，关键术语（如 effect block）在 core 与 assurance 双向引用。
- **风险**：旧路径外部引用失效。**缓解**：第 2 轮保留 `core-language.zh.md` 重定向占位至少 3 个版本周期。
- **回滚**：每轮独立提交，任一轮发现回归可在该轮 revert，不影响前序已落地分册。

## 6. 与并行工作的边界

本方案不触碰以下范围（由其他工作流负责）：

- `docs/plans/*`：项目计划文档
- `docs/README.md`：docs 总索引（仅在第 2 轮做路径更新）
- `docs/design/corelib-rfc.zh.md`：另一 RFC 占位
- 任何 `src/` 代码、`grammar/`：本方案是文档重构，不动实现

## 7. 结论

本轮交付的是"施工图 + 第一铲"。结构重写的核心收益不是文件变多变小，而是**让每个实现文件都能在 spec 中找到唯一归属**，从而把 spec↔实现漂移从"靠人记忆"变成"靠脚本可查"。`tools/spec/check_grammar_coverage.py` 已经把 grammar↔spec 一侧机械化；后续轮可以仿照它再写 `check_type_coverage.py`、`check_semantics_coverage.py`，最终让整个 spec↔实现一致性可被 CI 守护。
