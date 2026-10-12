# RFC 0027 P3: 前端 resolve/typecheck 查询化与 CLI 切分边界 -- 设计

> Status: **IMPLEMENTED (P3 / KR6.11-S4 + S4B + S4C, P5 / KR6.12 收口)**。
> 本文描述前端 `resolve(module)` / `typecheck(module)` / `type_of(expr)` 的查询化
> 与 CLI 的当前边界。单文件和项目输入均通过 QueryEngine 求值，分析尾部消费
> 引擎持有的 AST / SourceGraph 和对应 stage 结果。查询失败立即返回编译错误，
> 不转入第二条直接流水线。
>
> `ProjectInputModel` 将项目配置和完整源码快照提为可比较输入；`parse_project`
> 是其纯函数。单元测试用直接调用语义组件作为参考，生产 CLI 只有 Query 路由。
> 进程级门禁检查该路由和重复执行确定性，不再维护历史环境开关或双路径对照。
>
> P3 的历史决策见 RFC 0027；分层 Core-IR JSON 契约见
> [core-ir-p9-layered-json.zh.md](./core-ir-p9-layered-json.zh.md)。

## 0. 问题、范围、非目标

前端查询图按 `source_text` → `parse` → `resolve` → `typecheck` 表达依赖，
`type_of` 从类型检查结果提供派生视图。目标是使 CLI 与工具复用同一套 stage
计算和 revision 失效规则，并保持语言语义、诊断及 IR 输出一致。

**范围：** 语义 stage 的 QueryEngine 输入、结果身份、生命周期与 CLI 消费边界。
单文件和 package/workspace 输入都必须进入引擎，不能因输入形状不同绕过查询层。

**非目标：** 本文不修改语言语义和 IR 塔，也不另建缓存或备用前端。LSP 使用同一
查询架构；手工 incremental 子系统已由 RFC 0027 P5 删除，持久缓存归
`src/tooling/cache/`。

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
   stage 的短路行为可以发生分歧，而诊断字节仍相同。

`ResolveResult` / `TypeCheckResult` 显式可拷贝/可移动(`resolver.hpp:187-193`),故可
作为 memo 值或存于 query 对象持有的 store 中。typed program 的投影复用既有
`serialize_typed_program_json`——**单一 SSOT**,不引入第二套 typed-tree 拼写。

## 3. CLI 边界：两种输入，同一查询路由

`run_analysis<InputT>` 有 `ast::Program` 与 `SourceGraph` 两个实例，分别服务
单文件与 package/workspace。它们共享同一分析边界，均通过 FrontendQueries
取得 resolve/typecheck 结果。

项目输入不能直接把 SourceGraph 放入 query input：SourceUnit 持有不可拷贝、
不可比较的 Owned AST；深比较 AST 会把派生产物错误地当成输入身份。项目 parse
还依赖 module root、prelude 配置、overlay 和 import 文件存在性。正确边界是将
这些配置与可达文本冻结成 ProjectInputModel，再在 query compute 中构造图。
这解决输入比较问题，不需要临时绕过项目查询。

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
  不是文件系统魔法;它们的 `exists_on_disk` 保留 import 解析语义(解析只在
  `std::filesystem::exists` 判定候选**之后**才用 overlay 替换文本,所以一个不在盘上的
  overlay 路径**不能**仅凭自己被 import 解析选中)。
- `parse_project(const Frontend &, const ProjectInputModel &)` 是**纯函数**:预导入
  注入、module root 选择、import 边解析全部在 compute body 内对该 model 完成,不再探盘
  (`resolve_import_path` 的存在性判定改读 model 快照)。`parse_project(frontend, const ProjectInput &)` 将读盘冻结与 model parse
  组合为 `parse_project(frontend, resolve_project_input(input))`。它复用同一 parse
  函数体，是源码装载的组合入口，不是 CLI 的备用分析路由。

SourceUnit 的 AST 是 model(含**完整文本快照**)的纯函数 ——
"一个单元的 AST 不是其文本的函数"之所以成立,是因为 prelude 注入等发生在 parse 之后且
依赖盘上其他文件;把那些文件**也**纳入快照后它就成立了。model 里只有
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

### 3.3 Driver 消费引擎持有的输入

`run_analysis<InputT>` 对两个实例都创建 FrontendQueries：

- 文件实例设置 `source_text(FileId)` 为 `(display_name, bytes)`。
- 项目实例设置 `set_project_input(ProjectId, model)`；model 由项目输入冻结获得。

分析尾部的 `analysis_input` 指向引擎持有的 AST / SourceGraph，resolve 与
typecheck 指针也借自同一引擎。引擎对象活到所有借用消费结束，IR lowering
不能拿入口原始 AST 配上另一份 query stage 结果。

引擎无法提供 stage 结果时，CLI 返回 CompileError；不会调用直接
Resolver/TypeChecker 来绕过查询失败。输入缺失或 query graph 的错误必须被
暴露，不能藏在成功回退中。

`AHFL_QUERY_ENGINE_TRACE=1` 仅开启观察：到达分析时 stderr 输出
`query-engine-route: engine`；stdout 产物不受影响。parse 失败在进入分析前返回，
因此不输出该行。此变量不改变路由，也不选择历史实现。

### 3.4 当前进程级门禁

CTest `ahfl.query.cli_default_engine_route` 调用
`tests/scripts/query_engine_cli_equiv.py`。同一输入、同一环境重复执行两次，比较
exit code、stdout 与移除 route trace 后的 stderr，检查运行确定性：

- 单文件 corpus：`tests/golden` 与 `examples` 中没有 package 祖先的 `.ahfl`，
  执行 `check`。
- 项目 corpus：integration package fixtures 中声明 workflow target 的 manifest，
  执行 `check` 与 `emit ir-json`。后者覆盖 query 输入、语义结果、IR lowering
  与 JSON printing 的完整消费链。

门禁必须发现非空的两类 corpus、实际比较命令，并至少观察到一次 engine 分析。
已观察到的 route 必须是 `engine`。重复执行相同只能证明确定性；正确的语义结果
仍由 native 单元参考、CLI golden 与增量/冷缓存等价测试共同守护。

## 4. 等价守护:单元 vs 进程两级

当前证据分两级，职责互补。直接 Resolver/TypeChecker 组合只用于单元参考，
不是可切换的 CLI 实现：

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
- **进程级 CLI 级**(§3 的 `ahfl.query.cli_default_engine_route`)：同一条命令经唯一
  Query 路由重复执行，比较确定性输出并观察 engine trace，覆盖 file 与 project
  两种输入。它不把自己当成语义参考实现；CLI golden 守护预期产物。

增量一致性也被守护:`ahfl.query.incremental_equiv_all`(S5)+
`frontend_queries_equiv.cpp` 的"增量 == 冷缓存"用例(注释-only 编辑不改 AST →
下游 stage 走 green 命中不重算;改 AST 的编辑 → 全链各重算一次)。

CLI golden 舰队使用同一当前路由，锁定诊断、IR 和用户可见产物。历史切换时的
验证记录留在 RFC Decision History；当前变更须以本次运行的门禁结果为准，
不能把历史全绿记录当作新工作树已经通过的证据。

## 5. 门禁失败条件

route token 不是 `engine`、corpus 为空、没有实际命令比较、没有任何运行到达分析，
或两次执行的 exit code/stdout/stderr 不一致，门禁都必须失败。这些条件防止
重复执行测试退化为空跑，也使已观察到的非 Query 路由无法被输出一致掩盖。

不要重新引入生产路由切换来提供负向对照。语义差异由进程内参考和 golden 捕获，
查询失效由编辑序列与冷缓存对拍捕获，进程级检查只负责它能直接观察的路由与
确定性。

## 6. 与其他查询能力的关系

- LSP 的 hover/completion/signatureHelp 通过同一 FrontendQueries 读取类型视图，
  编辑时使用 revision 失效与 memo 复用。
- P5 已删除手工 incremental 依赖/失效模拟；增量真相由 QueryEngine 管理。
- `src/tooling/cache/` 持久化当前 typed-HIR 产物；命中要求 producer、输入、配置
  与依赖身份一致。缓存 miss 触发当前查询计算，不加载历史前端实现。
- `type_of(module,node_id,source_id)` 是类型检查结果的派生视图，不承诺独立的
  表达式重算 family。

## 7. 被否决的备选

| 备选 | 否决原因 |
|---|---|
| 只让单文件进入 QueryEngine | 同一 CLI 命令因输入形状而走不同分析实现，项目缓存与失效规则无法统一 |
| 以环境开关或异常回退保留第二条直接流水线 | 掩盖 query graph 错误，扩大维护与验证面；当前 CLI 应暴露失败 |
| 将 package 伪造为单文件 query 输入 | 遗漏 module root、prelude 配置和 import 依赖，改变项目语义 |
| 在 SourceGraph 上深比较 AST | 将派生产物当成输入身份，且 Owned AST 不满足可拷贝值输入契约 |
| 为 type_of 注册独立表达式 family | 重复维护 key 注册表，只为读取 typecheck memo 已有索引，没有独立重算收益 |
| Resolve 结果身份只覆盖诊断 | 不同符号表可能产生相同诊断，使下游错误复用过期 TypedProgram |
| 仅用重复执行代替语义参考 | 同一种错误结果也能稳定重复；仍须保留 native 参考、golden 和增量/冷缓存回归 |
