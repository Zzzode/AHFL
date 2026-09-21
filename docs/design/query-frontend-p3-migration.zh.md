# RFC 0027 P3: 前端 resolve/typecheck 查询化与 CLI 切分边界 -- 设计

> Status: **IMPLEMENTED (P3 / KR6.11-S4 + S4B)**。本文记录 RFC 0027 P3 切片
> (`resolve(module)` / `typecheck(module)` / `type_of(expr)` 查询化,以及 CLI 经
> QueryEngine 求值的**切分边界**)的受追踪设计决定。RFC 0027 把前端逐级搬上 query
> 图,每一级都必须证明"与直接流水线逐位等价"
> (`docs/rfcs/0027-query-frontend-and-ir-ssot.zh.md` §Implementation Plan P3、
> §Test Plan "等价回归")。P2 (`parse(file)`) 已由 S3 落地并确立方法论;本文定 P3 的
> 语义级 stage 如何进图、以及 CLI 切换的边界。
>
> **修订 (S4B)**:§3 原先的"package 路径不可 query 化"边界**已被撤销**。结构性障碍
> (SourceGraph 不可比较、SourceUnit 的 AST 不是其文本的函数)由新的 **`ProjectInputModel`
> 值语义输入 + `parse_project` 纯函数化**消除,故 CLI 的**两个** `run_analysis` 实例现
> 在都经引擎求值。§3 保留原判定的记录,并给出撤销它所需的确切重构。
>
> 本文只记录 P3 的**新**决定。KR6.9-B0 的分层 Core-IR JSON 投影契约是
> `docs/design/core-ir-p9-layered-json.zh.md`(已由 `fef7c322` 落地),与本文无关,
> 不在此重复。

## 0. 问题、范围、非目标

RFC 0027 的 P2/P3 把前端表述为 query 图(`parse(file)` → `resolve(module)` →
`type_of(expr)`)。P2 只搬了 `parse`,并把"query 结果 == 直接结果"的等价方法论在
最便宜的 stage 上跑通。P3 要搬语义级 stage,并让 driver 经 QueryEngine 求值。

**范围:** 前端语义 stage(`resolve` / `typecheck`)与其派生视图(`type_of`)的
query 化;以及 CLI driver 经 QueryEngine 的切换边界。

**非目标:**

- **不改语言语义**,不改 IR 塔分层(那是 RFC 0026)。
- **不翻转默认路由。** S4B 把 `parse_project` / `SourceGraph` 搬上了 query 图(§3.1
  —3.3),故 package 路径**可** query 化、且 opt-in 路径**已**覆盖它;但默认路径仍走
  直接流水线(§3.3),翻转默认是独立可后置的一步。
- **不删手工 incremental 子系统**(`src/tooling/incremental/`,RFC 0027 P5 / KR6.12)。
- **不做 LSP 切换**(P4)。

## 1. query 粒度:分级,对齐 RFC §Open Questions Q2

RFC 0027 Q2 决策"分级粒度,对齐 rust-analyzer":文件级 `parse(file)`、符号/模块级
`resolve(module)`、表达式级 `type_of(expr_id)`。P3 的实现遵循该分级:

| query | key | 语义 |
|---|---|---|
| `source_text(FileId)` | 文件槽 | 输入:一条 `(display_name, text)` 原子事实 |
| `parse(FileId)` | 文件槽 | 派生:`Frontend::parse_text`(P2 已落地) |
| `resolve(ModuleId)` | 模块槽 | 派生:`Resolver::resolve`,缓存 `ResolveResult` |
| `typecheck(ModuleId)` | 模块槽 | 派生:`TypeChecker::check`,缓存 `TypeCheckResult` |
| `type_of(ModuleId, node_id, source_id)` | —— | **派生视图**:读 typecheck memo + `TypedProgram::find_expr` 反查表 |

**`ModuleId` 与 `FileId` 是不同 phantom tag 的索引 key**(CLAUDE.md Principle 2)。
单文件前端图里"一个分析单元 == 一个文件",所以 `ModuleId` 槽 i 与 `FileId` 槽 i 指
同一单元;分开两个 tag 是为了让 RFC 图里两个粒度(文件级 parse、模块级 resolve)在
调用点不可互相误传,而非引入第二套身份。

**`resolve(module)` 是"整单元"查询,不是图级切分。** resolver 的 API 就是
`resolve(const ast::Program &)` / `resolve(const SourceGraph &)`,整个分析单元一次
求解。RFC 的 `resolve(module)` 命名的是**分析单元**,不是把一次图级 resolve 切成
逐模块。故 `ModuleId` 指向单元,而非图里的一个 module 子集。

**`type_of` 不注册独立 family。** 若为每个 `(module, node_id)` 注册 family,需要一套
key 注册表,而它唯一的用途是去读 typecheck memo 已经持有的那张表——纯开销。因此
`type_of` 的实现是:把 typecheck memo 驱到当前 revision(memo 命中即 0 重算),再经
`TypedProgram::find_expr`(既有反查索引)定位节点。**不承诺逐表达式重算**(RFC P3 原文
即如此)。类型指针经 `TypeContext` interning,身份稳定,视图比对廉价。

## 2. stage 结果的身份:快照覆盖整结果,不只是诊断

query memo 的**值身份**决定"什么算同一个 stage 结果"。P2 的教训(`ParseSnapshot`
覆盖 AST outline + 诊断 JSON + error flag)在 P3 被照搬,并为语义 stage 补上一处
关键修正:

| snapshot | 组成 |
|---|---|
| `ResolveSnapshot` | 符号表 outline(`dump_resolve_outline`)+ 诊断 JSON + `has_errors` + `ran` |
| `TypecheckSnapshot` | `serialize_typed_program_json` + 诊断 JSON + `has_errors` + `ran` |

两条规范性规则:

1. **身份必须覆盖符号表 / 类型化程序,不能只看诊断。** 两个**不同** AST 的 resolve
   可以产出**逐字节相同**的诊断,却携带不同符号;若 memo 身份只看诊断,下游
   typecheck 会复用一份过期的 typed program。把整个 stage 结果序列化,才让 memo 身份
   == stage 结果。这与 parse 用 AST outline 作为身份是同一条规则。
2. **`ran` 是身份的一部分。** CLI 流水线有短路:parse 报错则 resolve 不跑;resolve
   报错则 typecheck 不跑。"该 stage 被跳过"必须与"该 stage 跑了且无诊断"可区分,否则
   query 与直接流水线可以在"何时执行了某 stage"上分歧而字节仍相同。

`ResolveResult` / `TypeCheckResult` 显式可拷贝/可移动(`resolver.hpp:187-193`),故可
作为 memo 值或存于 query 对象持有的 store 中。typed program 的投影复用既有
`serialize_typed_program_json`——**单一 SSOT**,不引入第二套 typed-tree 拼写。

## 3. CLI 切分边界:S4B 撤销,S4 的两个实例现在都经引擎

**原判定 (S4):** CLI 的 query 化只对单文件(`ast::Program`)输入形状开放,且以 opt-in
环境开关(`AHFL_QUERY_ENGINE`)启用;package/workspace(`SourceGraph`)路径保留直接
流水线。理由(结构性的,不是工程保守):

1. **`run_analysis<InputT>` 是一个模板,两个实例。** driver 里
   `run_analysis<ast::Program>`(单文件 / CWD 发现)与
   `run_analysis<ahfl::SourceGraph>`(package / workspace)共享**同一份函数体**
   (`src/tooling/cli/cli_driver.cpp`,显式实例化两处)。只切一个实例会让**同一条 CLI
   命令**因到达路径不同(file 路径 vs package 路径)而行为不同——这正是"半切分"。
2. **`SourceGraph` 不能做 query 输入。** QueryEngine 的 input 要求
   `std::equality_comparable`(`query_engine.hpp` `register_input`)。`SourceGraph`
   含 `std::vector<SourceUnit>`,而 `SourceUnit` 持 `Owned<ast::Program>`——既不可拷贝
   也不可比较。它无法成为 input 槽。
3. **一个 SourceUnit 的 AST 不是其文本的函数。** `parse_project` 在 parse **之后**
   注入 `std::prelude` 为 import,并做 module root 发现、overlay 应用、文件系统 import
   边解析。因此"重新给一段文本就能重建该单元的 AST"不成立。

**修订 (S4B):** 上面 1 是**约束**(必须两个实例一起切),2 与 3 是**障碍**(需要重构
才能切)。S4B 消除了 2 与 3,于是 1 得以满足:**两个实例现在都在同一开关下经引擎求值。**

### 3.1 `ProjectInputModel`:把 package 输入变成可比较的值

障碍 2 的根因是"输入形状里塞了不可比较的产物(parsed AST)"。修法不是在 `SourceGraph`
上贴一个比较器(那会**深比较 AST**,把一个阶段的输出当作下一阶段的输入身份,且 `Owned`
让拷贝都做不到),而是把输入提成**值**:

```cpp
struct ProjectSource {           // 一个源:身份(规范化路径)+ 冻结文本 + 盘上是否存在
    std::filesystem::path path;
    std::optional<std::string> text;   // nullopt = 盘上存在但读不出(渲染为 open 失败)
    bool exists_on_disk = false;
    friend bool operator==(const ProjectSource &, const ProjectSource &) = default;
};
struct ProjectInputModel {       // = 已解析项目配置 + 每个可达源的文本快照
    std::vector<std::filesystem::path> entry_files;
    std::vector<std::filesystem::path> search_roots;
    std::vector<ProjectInput::ModuleRoot> module_roots;   // 已 effective / 已规范化
    bool inject_prelude = false;
    bool enforce_package_dependencies = false;
    std::vector<ProjectSource> sources;                   // 按路径排序,去重
    friend bool operator==(const ProjectInputModel &, const ProjectInputModel &) = default;
};
```

- `resolve_project_input(const ProjectInput &) -> ProjectInputModel` 是**唯一**的不纯
  边界:它读盘,把可达的每个源(entry + search/module root 下所有 `.ahfl` + overlay /
  cache 文本)冻结进 `sources`,排序去重后返回。**overlay / cache 也是输入**(显式文本),
  不是文件系统魔法;它们的 `exists_on_disk` 保留直接流水线的语义(直接路径只在
  `std::filesystem::exists` 判定候选**之后**才用 overlay 替换文本,所以一个不在盘上的
  overlay 路径**不能**仅凭自己被 import 解析选中)。
- `parse_project(const Frontend &, const ProjectInputModel &)` 是**纯函数**:预导入
  注入、module root 选择、import 边解析全部在 compute body 内对该 model 完成,不再探盘
  (`resolve_import_path` 的存在性判定改读 model 快照)。原 `parse_project(frontend,
  const ProjectInput &)` **保留并委托**给 model 版本
  (`parse_project(frontend, resolve_project_input(input))`),故直接路径与查询路径是
  **同一份函数体**,不可能漂移。

这解决了障碍 3:A SourceUnit 的 AST 现在是 model(含**完整文本快照**)的纯函数 ——
"一个单元的 AST 不是其文本的函数"之所以成立,是因为 prelude 注入等发生在 parse 之后且
依赖盘上其他文件;把那些文件**也**纳入快照后它就成立了。这也解决了障碍 2:model 里只有
路径、文本、字符串列表,**全部可比较**。

### 3.2 项目 stage 的 query 图与身份

`FrontendQueries` 增一族项目槽(`ProjectId = QueryKey<ProjectTag>`,与 `FileId` /
`ModuleId` 同为 phantom tag 的索引 key):

| query | 值身份 |
|---|---|
| `parse_project(ProjectId)` | `ProjectParseSnapshot{graph_outline, diagnostics_json, has_errors}` |
| `project_resolve(ProjectId)` | `ResolveSnapshot`(复用文件级定义) |
| `project_typecheck(ProjectId)` | `TypecheckSnapshot`(复用文件级定义) |

`graph_outline` 来自 `dump_project_graph_outline`:`entry_sources`、每个 SourceUnit 的
`id` / `module_name` / `package_prefix` / exported+artifact+dependency 列表 /
intrinsics allow / `module_range` / imports、其 AST(经 `dump_program_outline`,**span-
与内容敏感**)、排序后的 `module_to_source`、以及全部 import 边。刻意**不含** `path`:路径
是显示产物,且同一逻辑项目拷到临时目录应相等。**含** `id`:它是图内所有引用的索引身份。

`SourceGraph` 自身(持 `Owned<ast::Program>`)不进引擎的类型擦除 memo,而存于
`FrontendQueries` 的 deque 存储,由 `project_graph(ProjectId)` 在 revision 校验后借出
(契约同 `program(FileId)`:只借 Clean/Verified 槽,model 一改槽即 Dirty)。短路语义与
文件级一致:项目 parse 错 → resolve 不跑;resolve 错 → typecheck 不跑,由 snapshot 的
`ran` 位记录。

### 3.3 driver 的切换:同一开关,两个实例

`run_analysis<InputT>` 现在对**两个**实例在 `AHFL_QUERY_ENGINE` 下都尝试引擎路径:

- 文件实例:`source_text(FileId)` = (display_name, bytes)。
- 项目实例:`set_project_input(ProjectId, model)`;model 由
  `run_project_analysis(ahfl::ProjectInput)` 构造(冻结 → 直接 parse 渲染项目级 parse
  诊断 → 把 model 一并交给 `run_analysis`)。

关键是**分析输入本身**也随路由重绑:引擎路径下 tail 消费的是**引擎持有的** AST /
SourceGraph(`analysis_input` 指针),而不是直接流水线的产物 —— 否则借用到的 stage 结果
与 IR lowering 会指向两份不同的 AST,即"表层切换、底下半切换"。

**默认路径不动是对 golden 的诚实:** 本次切片**不**翻转默认,故 `tests/golden/ir` 与
`ahflc.*` 全舰队按构造不变;opt-in 路径由穷尽等价门禁守护。翻转默认(令引擎恒为默认)是
**独立的、可后置**的一步,需要在同一提交内重录/证明全 golden;S4B 的交付是"**两个**
实例都已在同一开关下经引擎求值且逐位等价",即切分边界的**结构障碍已清除**,而非把默认
翻掉。

### 3.4 交付的两条(不变)

- **默认路径逐字节不变。** 所有既有 golden(`ahflc.emit_ir*` 舰队、`tests/golden/ir`)
  按构造不变:它们走直接流水线。
- **opt-in 的引擎路径,由穷尽等价 ctest 守护。**
  `ahfl.query.cli_engine_equiv`(`tests/scripts/query_engine_cli_equiv.py`)对**两个**
  corpus 逐条跑两次(直接 vs `AHFL_QUERY_ENGINE=1`),断言 exit code / stdout / stderr
  逐字节相同:
  - 裸文件 corpus(`tests/golden` + `examples` 中无 package 祖先的 `.ahfl`),命令 `check`;
  - **项目 corpus**(`tests/integration/package_golden/**/ahfl.toml` 中声明 workflow
    target 的 fixture),命令 `check` **与** `emit ir-json`。`emit ir-json` 之所以纳入,
    是因为它把分析输入一路驱动到 IR lowering + printing —— 这是"用户实际看到的产物"层面
    最强的等价陈述。

**非真空性(vacuity)门禁。** 一个"每条输入都静默回退到直接流水线"的等价门禁会
平凡通过。故 driver 在**进入 `run_analysis` 时**在 stderr 打一行 route 迹
(`query-engine-route: engine|direct`,仅 `AHFL_QUERY_ENGINE_TRACE=1` 时;**两个实例都
打**,含项目路径);门禁断言:凡"到达分析"的运行,引擎运行必须报 `engine`,直接运行必须
报 `direct`,且 parse 失败(未到达分析)则两侧都无该行。把 route 开关人为关掉后门禁必须
**失败**——这是该门禁的自证(见 §5)。

## 4. 等价守护:单元 vs 进程两级

P3 的等价证据分两级,互补:

- **进程内单元级**(`tests/unit/compiler/query/frontend_queries_equiv.cpp`,`ctest`
  `ahfl.query.frontend_equiv_all`):对 `tests/golden` + `examples` 全 corpus,直接
  `Resolver` + `TypeChecker` + `lower_program_ir` + `print_program_ir_json` 与
  engine 驱动的 stage 结果/diagnostic JSON/typed-program 投影/IR JSON 逐字节比对;
  并断言 `type_of` 对每个 typed 表达式与 typed store 自身记录一致、未知名返回
  `found == false`。IR JSON 由**query 持有的 AST** + **借用到的 stage 结果**驱动,
  即 CLI 真实会传给 `lower_program_ir` 的那组对象。
  **S4B 扩了项目那一侧**(同一 ctest):对 `tests/integration/check_ok` 的
  workspace→package-graph→ProjectInput 路径,断言 `parse_project` /
  `project_resolve` / `project_typecheck` 与直接流水线逐字节等价(含由引擎持有的
  graph + 借用 stage 结果驱动的 IR JSON),并断言"改**非 entry** 模块的文本"会让整链
  重算一次(project parse compute 1→2、typecheck 1→2)——这是"修改依赖的文本是可观测
  的"这一性质,也是内容盲的 input model 会藏住的失败模式。
- **进程级 CLI 级**(§3 的 `ahfl.query.cli_engine_equiv`):同一条命令两条路径字节
  比对,**覆盖 file 与 project 两种到达**。这是"整个 CLI golden 舰队"的等价回归在
  opt-in 路径上的对应物。

增量一致性也被守护:`ahfl.query.incremental_equiv_all`(S5)+
`frontend_queries_equiv.cpp` 的"增量 == 冷缓存"用例(注释-only 编辑不改 AST →
下游 stage 走 green 命中不重算;改 AST 的编辑 → 全链各重算一次)。

**引擎路径下的全舰队核验(S4B 实测,非门禁):** `AHFL_QUERY_ENGINE=1 ctest -R
'ahflc\.'` 的 261 项全绿 —— 即在不改默认路径的前提下,整支 CLI golden 舰队在引擎路由
下也逐位通过。

## 5. 门禁自证(negative control)

`ahfl.query.cli_engine_equiv` 在**关闭路由开关**后必须失败(报
"reached analysis but did not engage the engine route" 并最终报
"vacuous gate")。此性质在落地时手工验证过:改 `query_engine_route_requested` 恒假、
重建 `ahflc`、跑门禁 → 失败;还原 → 通过。S4B 再次验证:恒假后,**file 与 project
两侧都**报 "fell back to the direct route" 且最终报 "vacuous gate"。这不是可选的美化,
而是防止门禁随代码演进退化为"两条路径其实是同一路径"。

## 6. 与后续切片的关系

- **P4(LSP 切 query)**:LSP 的 `hover`/`completion`/`signatureHelp` 可经
  `type_of(module, node_id, source_id)` 读类型——正是 P3 暴露的视图。LSP 有真实的
  单文件/多文件编辑序列,可用同一 QueryEngine。
- **翻转默认路由(令引擎恒为默认)**:S4B 已清除结构障碍(§3),故剩下的是纯工程步骤:
  删 `AHFL_QUERY_ENGINE` 开关、让引擎恒为默认、在同一提交内重录/证明全 golden。这是
  S4B 明确留出的下一步,不是遗漏。
- **P5 / KR6.12**:删 `src/tooling/incremental/` 时,P3 的 stage 缓存是其能力来源。

## 7. 被否决的备选

| 备选 | 否决原因 |
|---|---|
| ~~CLI 整体切换(两个 `run_analysis` 实例都经引擎)~~ | **原否决,已被 S4B 撤销**:原理由是"`SourceGraph` 不可作 input;伪造为单文件输入会改变语义"。S4B 没有伪造输入,而是引入 `ProjectInputModel` 值语义输入并让 `parse_project` 成为其纯函数,于是两个实例可在**同一开关**下都经引擎且逐位等价(§3.1–3.3)。原判定的**精神**仍成立:不允许"程序化地把 package 路径伪造为单文件输入"。 |
| 只切 `run_analysis<ast::Program>` 且**无** opt-in 开关 | 即在默认路径上改变行为,golden 必须在同一切片内重录/证明;且单一实例切换会让同一命令随到达路径行为不同(半切分)。opt-in + 穷尽等价 ctest 既拿到"driver 经 QueryEngine 求值"的交付,又让 golden 按构造不变 |
| 在 `SourceGraph` 上贴一个比较器(而非引入 model) | 会**深比较 AST**——把一个阶段的输出当作下一阶段的输入身份,且 `Owned<ast::Program>` 让拷贝都不可能;正确做法是把**输入**(文本 + 已解析配置)提成值,不是把**产物**提成值(§3.1) |
| `type_of` 注册 `(module, node_id)` 独立 family | 需要一套只为读 typecheck memo 已持有表的 key 注册表;纯开销,且不带来"逐表达式重算"能力(不承诺) |
| resolve 的身份只用诊断 JSON | 不同 AST 可产出相同诊断而携带不同符号 → 下游类型检查复用过期 typed program(§2.1) |
| 现在就为 package 路径写一版"能跑"的 query 化 | 需先 query 化 `parse_project`(prelude 注入 + 文件系统解析),属独立大切片;S4B 正是把那一步做成了**受追踪的 model 重构**(§3.1),而不是在错误抽象层加特例 |
