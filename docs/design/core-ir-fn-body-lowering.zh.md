# Core-IR Fn-Body & Closure-Value Lowering — Design

> Status: **DRAFT rev 2** (RFC 0026 P6 / KR6.6 子片设计门;文档编号
> CORE-FNBODY-DESIGN)。本文只做决策,不含开放问题;无生产代码。落地按 §8
> 的 FB-1 → FB-5 实现阶梯逐片交付,每片一个可评审 Conventional Commit。
> 上游事实锚点均为**已提交**的 `develop` HEAD(`11e57d32`)源码
> `file:line`;评审发现的在飞(P6 conformance/WASM WIP)未提交改动**不作
> 锚点**,其计数另在 §7.2 标注。
>
> rev 2 修订(评审 fix-forward,纯文档):(1) 新增 §3.1.1 的 FnT/Closure
> 物理二分决策与 §8.4 FB-3 的 lambda-lift 变换——rev 1 在该点上自相矛盾
> (把 9 个 lambda 程序算作 unlocked,却未规定 FnT 四字节形参如何接受八字节
> 闭包,也未规定 lambda 如何提升为 Fn 实例);(2) §6.3 闭包 env 改为运行时
> checked bump heap(`alloc`)分配,与 P6-4 编译期静态 scratch 明确脱钩,
> 新增 agent-lane heap_base 落位与 env 容量预算决策;(3) §7.2/§9 的
> conformance 计数改为 committed HEAD 口径(8 例 = 5 runnable / 3 blocked),
> 10 个 p6 用例标注为在飞未提交;RFC 0026 Decision History 同步更正;全部
> dirty worktree 上的 `core_wasm_codegen*` 锚点重锚到 HEAD;(4) §8.1-6 补
> FB-2 递归预算格的形式化规则与 fail-closed 形状清单;(5) §2.1/§2.3 签名
> 来源改为对 base FnTypeInfo 做 P2d 替换,纠正"从 dispatch_types 经
> CoreVtFn 读取"的错误表述;(6) §6.2 wasm 段序改为规范固定位置表述;
> (7) §5.4/§7.1 行号与 stdlib 锚点更正。

## 0. Problem 与当前事实

KR6.6/P6 的表达式、match、coercion、aggregate、bounded collection 已在
**编排 lane** 落地(P6-0..P6-6、P6-8a),但函数体与闭包值整体缺席:

1. `LambdaExpr` 不被 Core lowerer 识别。它落入 `lower_value` 的 generic
   兜底臂,记录 `CoreUnsupportedExpr{"LambdaExpr", range}` 并产出 Error
   (`src/compiler/ir/core_lower.cpp:1927-1931`;节点定义
   `include/ahfl/compiler/ir/expr.hpp:330-338`,携带显式 `captures`
   capture list)。
2. 一般 `CallExpr`(自由 fn 调用)只解析三类:capability 调用、内部
   collection hook(`list_raw_*`)、enum variant 构造;其余一律
   `lower_unsupported_value(..., "CallExpr", ...)`
   (`core_lower.cpp:2757-2836`,兜底在 `:2832-2836`)。
3. `MethodCallExpr` 在 Core 层完全没有 value-lowering 臂(只在
   capability 子树探测里出现,`core_lower.cpp:2980-2991`);它经 generic
   兜底记为 `CoreUnsupportedExpr{"MethodCallExpr"}`。
4. 单态化实例表里的 Fn 实例是空壳:`CoreFnInstance` 无任何字段
   (`include/ahfl/compiler/ir/core_ir.hpp:1297-1303`),Pass 6 对
   `InstanceKind::Fn` 只放一个空 payload(`core_lower.cpp:4004-4006`)。
   实例注册本身已可用:Pass 6 消费全部 AHFL-IR `InstanceDecl`
   (`core_lower.cpp:3880-4016`),Fn 实例由 typed-HIR lowering 在
   `src/compiler/ir/typed_hir_lower.cpp:4462` 产出,其 mangled key 字节
   透传为 `CoreInstanceDecl::instance_key`。
5. 逻辑值类型层已有闭包的**类型形状** `CoreVtClosure`(signature + 有序
   capture slots,`core_ir.hpp:1490-1514`),物理布局层已有
   `(func_index:i32, env_ptr:i32)` 八字定型 + P4-D indirect env 边
   (`include/ahfl/compiler/ir/core_layout.hpp:55-78`,
   `src/compiler/ir/core_layout.cpp:259-284`,P6-8a 已落地),但
   `lower_value_type` 今日**永不产出** `CoreVtClosure`
   (`core_ir.hpp:1505-1508` 明记 "P4-A has no closure consumer"),Core 也
   没有构造闭包值、调用闭包值的 expr/stmt 节点。
6. WASM 编码器没有 Table(4)/Element(9) 段常量:已有的七个**有编号**段常量
   是 Type(1)/Import(2)/Function(3)/Memory(5)/Global(6)/Export(7)/Code(10)
   (`src/compiler/backends/wasm/core_wasm_codegen.cpp:111-117`);同一常量块
   另有 `kSectionCustom = 0`(`:110`),它不是编号段,用于可选的 wire-schema
   custom 段。agent `encode_module`(`core_wasm_codegen.cpp:5157-5299`)按序
   追加上述七个有编号段(`:5172-5279`),并在带 wire schema 时追加**第八个**
   Custom 段(`:5288-5297`);段 id 4(Table)与 9(Element)无常量、无发射。
   opcode 有 `call`(`kOpCall = 0x10`,`:164`)但无 `call_indirect`
   (0x11)。函数索引空间 = 可选 capability import 占低位 + 7 个固定 ABI
   函数(alloc/dealloc/current_state/is_final/step/run/run2,
   `kDefinedAlloc = 0` .. `kDefinedRun2 = 6`,`:237-243`)+ P6-2 起的
   computed handler(`kDefinedHandlerBase = 7`,`:247`;`FunctionTable` 结构与
   投影规则 `:354-377`,`WorkflowFunctionTable` 为 `:379-398`)。

后果:任何使用闭包、泛型实例化 fn、impl 方法真实函数体的程序在 Core
lowering 即不可执行,WASM 无法消费。这是完整 KR6.6 的文档化前置,也是
KR6.8 evaluator 退役必须消灭的最大一整块执行语义(evaluator 今日的
first-class 函数面见 §7)。

### Non-goals(本设计门)

- 不引入 GC / 不引入 STG/CPS 层(RFC 0026 已定:AHFL 闭包受限,无需 GHC
  式 STG,`docs/rfcs/0026-ir-tower-and-execution-model.zh.md:141`)。
- 不做 by-reference capture、不做 partial application / currying(理由见
  §3.4、§3.5,均为当前语言事实而非推迟)。
- 不重新打开 P4 值表示/P4-D 布局决策:闭包八字形 + indirect env 已由
  P6-8a 落地,本设计只消费它。
- P6-7 wire frame 决策未落地前,跨 fn 边界不传递 PtrLen(String)/bytes/
  f64 值(与现有单词 P6 门控同一界线——投影叶 `place_is_p6_value`,
  见 §6.3/§6.4)。

## 1. 设计原则与总体形状

全部遵循 CLAUDE.md 五原则与 RFC 0026 层不变式:

- **Principle 2(索引身份)**:fn 身份是 `CoreFnId`(flat per-program
  table 的稠密下标),绝不是 mangled 字符串;mangled `instance_key` 继续
  只做 dispatch label/global 唯一键。闭包持有的函数身份是 `CoreFnId`,
  不是名字。
- **Principle 3(flat store)**:`CoreProgram::fns` 是 `vector<CoreFnDecl>`;
  fn 体复用现有 ANF arena 机器,不另建第二套 expr/stmt 模型。
- **Principle 4(variant + visitor)**:新节点一律进 X-macro 节点表
  (`core_expr_nodes.def`;需要时 stmt 表),穷举 visitor 编译期强制覆盖。
- **层擦除不变式**:fn 的 `decreases` 度量、effect grade、temporal/contract
  在 Core 不存在。effect grade 在 `CoreVtFn` 已构造时擦除
  (`core_ir.hpp:1480-1488`);终止性已在 AHFL-IR 层消费完毕。递归的资源
  上界在 Core 以**结构性**事实表达(§8 FB-2),不重新承载 decreases 项。

总体新增(完整字段见 §2):

```text
CoreProgram
  + fns: vector<CoreFnDecl>                 // index == CoreFnId(新 flat store)
CoreFnInstance { CoreFnId body }            // 由空壳变 1:1 反向链接
CoreExprNode  + CoreCallExpr                // 静态解析的纯 fn/builtin 直调(call)
              + CoreClosureExpr             // 构造 (fn, env) 闭包值
              + CoreCallClosureExpr         // 闭包值间接调用(call_indirect)
```

## 2. 数据模型:CoreFnDecl / CoreFnId 与可复用 ANF body

### 2.1 决策:fn 表按单态化实例 1:1 建立,独立于 instance 注册表

新增 typed id 与程序级 flat store:

```cpp
struct CoreFnId {
    static constexpr std::uint32_t kInvalid = UINT32_MAX;
    std::uint32_t value{kInvalid};
    [[nodiscard]] friend bool operator==(CoreFnId, CoreFnId) noexcept = default;
};

// 拥有一个函数体的全部 ANF 资产(从 CoreFlowDecl/CoreWorkflowDecl 抽出的
// 共享 owner,见 §2.3)。
struct CoreBodyStorage {
    std::vector<CoreExpr> exprs;              // 纯表达式 arena
    std::uint32_t value_count{0};             // CoreValueId 分配计数
    std::vector<CoreValueTypeId> value_types; // dense,size()==value_count
    std::vector<CoreCoercionPlanNode> coercion_plans;
    std::vector<CorePattern> patterns;        // match pattern arena
};

struct CoreFnDecl {
    CoreFnId id{};
    /// 单态化身份:1:1 指向 CoreProgram::instances 中的 Fn-kind 实例。
    /// 注意:实例上的 `dispatch_types` 装的是**类型实参**(如
    /// map<Int,Int> 的 [Int,Int]),不是 fn 签名,上面没有也读不到
    /// CoreVtFn。具体 (params,ret) 签名由保证 pass 把该实例的
    /// dispatch_types 经 P2d Substs 替换进 base `FnTypeInfo.params/return_type`
    /// 后 intern 得到(见下文"签名来源"),物化在 body 的预绑定参数/返回
    /// 类型上;本 decl 不另存第二份签名字段。
    CoreInstanceId instance{};
    ir::SymbolRef origin;                     // provenance/display only
    /// 参数在 body SSA 域中的预绑定值(declaration order)。其逻辑类型在
    /// body.value_types[param] 中,与 CorePatternBinding 同构(P4-B 规则)。
    std::vector<CoreValueId> params;
    CoreBodyStorage storage;                  // ANF arena
    CoreRegion body;                          // 单一入口 region,必以 return 完成
    std::string name;                         // display only(mangled 实例名)
    SourceRangeOpt source_range;
};
```

**签名来源(决策,纠正 rev 1 的错误表述)。** 不能"从实例的
dispatch_types 经 CoreVtFn 读签名":Pass 6 里
`CoreInstanceDecl::dispatch_types` 直接 intern 自
`ir::InstanceDecl.type_args`(`core_lower.cpp:3907-3920`),对
`map<Int,Int>` 该向量是 `[Int,Int]` 两个**类型实参**;`CoreFnInstance`
是空壳,任何地方都不存 CoreVtFn。`ir::InstanceDecl.params` 虽由 typed-HIR
从 `FnTypeInfo.params` 填充(`typed_hir_lower.cpp:4463-4467`),但那是带泛型
TypeVar 的声明形参,Pass 6 不读它。决策:保证 pass 对每个有体 Fn 实例,用
**该实例自己的 dispatch_types 作为 Substs**,对 base `FnTypeInfo` 的泛型
`params`/`return_type`(`declaration_info.hpp:374-396`)逐形参替换、intern 为
concrete `CoreValueTypeId` 序列——与 typed 侧 body clone 用的 P2d 同一替换
(`monomorphization.hpp:101-106` 的 `body_block_index`,消费点
`typed_hir_lower.cpp:3959-3963`),不存在第二套替换/第二份签名 SSOT。产出的
concrete 形参类型按 declaration order 预绑定进 `body.value_types[param]`,
concrete 返回类型在 verifier/codegen 需要时由同一替换重新物化(纯函数,
interned 结果确定),不新增持久字段。

**为什么是独立 `CoreFnId` 而不是直接复用 `CoreInstanceId`:**

1. 生命周期不同。instance 注册表覆盖 Capability/Predicate/Agent/
   Workflow/Fn 五类(`core_instance_payload.def`),其中 Capability 实例有
   实例而**无函数体**(它是 import),prototype/`@builtin` fn 同理
   (`ir::FnDecl::has_body=false`,`include/ahfl/compiler/ir/decl.hpp:411`;
   std raw hooks `std/collections.ahfl:16-56` 全是无体 prototype)。代码生成
   的"可发射函数"单位必须能独立回答"这个 id 有没有 wasm 函数体"。
2. 与既有约定一致:workflow node 有 `CoreWorkflowNodeId` 而不是直接用
   instance id(`core_ir.hpp:149-156`),node 表是编排/代码生成单位,实例
   是单态化注册单位。fn 表是同样的分工。
3. 二者在有体 fn 上严格 1:1:`CoreFnInstance` 由空壳改为
   `{ CoreFnId body }`(kInvalid 仅可能出现在 lowering-error 部分产物,
   verifier 只跑 error-free 候选,沿用 P4-B 既有约定)。

**非泛型 fn 也要进表。** Pass 6 今日只消费已存在的 `InstanceDecl`
(`core_lower.cpp:3897-3901`),而 `InstanceKind::Fn` 实例由 typed-HIR 在
单态化传播时产出(`typed_hir_lower.cpp:4462`)。决策:Core lowerer 新增
一个确定性 pass(置于 Pass 6 之前或并入),为**每个 `has_body` 的
`ir::FnDecl` 保证恰好一个 Fn-kind `CoreInstanceDecl` + 一个 `CoreFnDecl`
——非泛型 fn 以空 type-args 的规范实例存在(与其 mangled key 规则一致,
`mangle::mangle_instance` 空实参形态),泛型 fn 每个 concrete 实参组一个
副本(typed 侧 body clone 已由 P2d 提供,
`MonomorphizationInstance::body_block_index`,
`include/ahfl/compiler/semantics/monomorphization.hpp:101-106`,消费点
`typed_hir_lower.cpp:3959-3963`)。impl 合成方法同样已是 `FnDecl`
(`typed_hir_lower.cpp:3970` 的 `impl#<index>::<name>`),天然复用同一路径。

### 2.2 body 表示:复用、而不是复制 ANF 机器

lowerer 内部今日已有非拥有视图 `CoreBodyStorageRef`
(`core_lower.cpp:1673-1682`):expr arena、value_count、pattern arena、
dense `value_types`、coercion plans 五件资产的引用束,`ExprLowerer`
模板类以此为载体(`core_lower.cpp:1761-1778`),`FlowLowerer`
(`:3285`)与 `WorkflowLowerer`(`:3551`,`:3646`)各自把自己 decl 的字段
手工扎成该视图。

**决策(Principle 1 激进重构,不做最小补丁):**

1. 把拥有形 `CoreBodyStorage`(§2.1 五字段)提升进 `core_ir.hpp`。
2. `CoreFlowDecl`(`core_ir.hpp:1156-1173`)与 `CoreWorkflowDecl`
   (`:1213-1230`)各自的五个平行字段替换为一个 `CoreBodyStorage storage`
   成员(对外读取器保持等价;JSON 线格式不变——字段仍在 flow/workflow
   对象内,序列化路径不变)。这是 P9 wire 兼容的结构内聚,不产生第二份
   SSOT。
3. `core_lower.cpp` 的 `CoreBodyStorageRef` 保留为 lowerer 私有非拥有视图,
   改为扎在 `CoreBodyStorage&` 上;`ExprLowerer<RootPolicy>` 原样复用。fn
   体的 lowerer 是 `ExprLowerer` 的第三个 policy 实例化,**不新增表达式
   lowering 代码路径**。

### 2.3 fn body 的 RootPolicy 与 SSA 域

新增 `FnRootPolicy`(与 `FlowRootPolicy`/`WorkRootPolicy` 并列,
`core_lower.cpp:1685-1738`):

- fn 体内允许的 path root 只有 `Local` 与(若语言需要)`Identifier` 解析
  到的同域常量;**不允许** Input/Context/Workflow* root。fn 不看见 agent
  frame——它需要的值经参数 SSA 值进入。这让 fn 体与 agent 内存预留区
  (`core_wasm_abi_constants.hpp:54-56`)解耦。
- SSA 域**每 fn 独立**:`CoreValueId` 在一个 `CoreBodyStorage` 内稠密、
  flow-global 单定义的现有规则(`core_verify.cpp:1917-1935`)逐字适用,
  `verify_region` 的共享 `ArenaView`(`core_verify.cpp:600-619`)加第三种
  `OwnerKind::Fn`。跨 body 永不互引 SSA 值;边界只有参数、捕获、返回。
- 参数是 body 的**预绑定** SSA 值:进入 `verify_region` 时即在
  all_definitions/visible 中,类型为 §2.1"签名来源"经 P2d Substs 具体化
  后的 `CoreValueTypeId`(保证 pass 物化,不从 dispatch_types 直接读)。

## 3. 闭包值:构造节点、env 布局、捕获语义

### 3.1 决策:专用 `CoreClosureExpr`,不扩展 `CoreConstructExpr`

在 `CoreConstructExpr` 上做闭包是**错误抽象层的特判**,被明确否决:

- `CoreConstructExpr` 的身份是 nominal 的:`type_id`(必填 `CoreTypeId`)
  + 可选 `variant` + 按 `CoreFieldId` 字段身份绑定的 args
  (`core_ir.hpp:496-506`)。闭包的逻辑类型是 `CoreVtClosure`,**没有也不
  应有** `CoreTypeId`(它不是 nominal,value-type arena 与 nominal 表是
  两张表,`core_ir.hpp:1458-1471`);env 槽是**位置有序**而非字段身份。
  复用它只能塞 kInvalid type_id 并让每个消费者(construct codegen、
  verifier、JSON)加 "if closure" 臂——正是 CLAUDE.md Principle 1 禁止的
  special-case branch。
- 闭包构造没有"字段顺序无关"这一 aggregate 语义;它的操作数顺序是运行时
  env 内存布局本身(§3.3),混淆二者会让 `Pair{b,a}` 式重排安全规则错误
  外溢到闭包。

新增 expr 节点(追加在 `core_expr_nodes.def` 末尾,variant index 只增):

```cpp
/// 构造一个闭包值 (fn, env)。PURE:捕获的是已绑定 SSA 值,无效果。
/// result_type 为 interned CoreVtClosure{ signature=该 fn 实例经 §2.1
/// P2d Substs 物化的 concrete CoreVtFn,captures = 每个捕获值的逻辑类型
/// + ByValue },与 captures 同槽序。
struct CoreClosureExpr {
    CoreFnId fn{};                         // 被闭包化的单态化函数体
    std::vector<CoreValueId> captures;     // canonical env-slot 序(见 §3.2)
};

/// 对闭包值的间接调用(见 §5.2)。
struct CoreCallClosureExpr {
    CoreValueId closure{};                 // CoreVtClosure 类型的 SSA 值
    std::vector<CoreValueId> args;         // 实参,左→右求值序
};
```

零捕获的一等函数值(把一个静态 fn 名当值传递,如 `map(list, f)`)用同一
`CoreClosureExpr{fn, {}}` 表示,运行时即 `(func_index, env_ptr=0)`;不另
设 "fn ref" 节点(节点形状不随捕获数变化,与 P4-D "closure 大小与捕获数
无关" 的定型一致,`core_layout.hpp:59-73`)。

### 3.1.1 决策(rev 2 补):FnT 形参与闭包值的物理表示统一 + lambda-lift

rev 1 在此留有未声明的硬冲突,本节将其定死。

**问题(实测)。** 源 `Fn(P)->R` 经 `lower_fn` intern 成 `CoreVtFn`
(`core_lower.cpp:1032-1057`),其物理布局是**四字节**
`CoreLayoutFnRef`(`core_layout.cpp:259-260`);闭包是**八字节**
`CoreLayoutClosure`(`core_layout.cpp:262-284`),而
`layouts_equivalent` 明确不跨 FnRef↔Closure(`core_layout.cpp:991-1005`)。
于是:(1) 把 lambda(八字节)传给 `f: Fn(T)->U` 形参(四字节槽)直接
撞结构性类型/布局检查——本文 counted as unlocked 的 9 个 lambda 程序
**全部**是这一形状(如 `map<Int,Int>(xs, \x -> x*2)`,
`tests/integration/stdlib_units/list_ut.ahfl:30`);(2) §5.1 的
`CoreCallExpr` 要求静态解析到 callee **实例**,本地 FnT 形参既不是实例、
四字节字也不能当闭包间接调用,所以 `f(...)` 在函数体内无法表达——
FB-2 目标 helper `list_map_into` 自身就在 FnT 形参上做
`f(list_raw_get(...))`(`std/collections.ahfl:141-153`),按 rev 1 不可
实现;(3) lambda 今日不产生任何 `ir::FnDecl`/`InstanceDecl`(只有 call/
method 站点产 Fn 实例,`typed_hir_lower.cpp:4145-4253`;monomorphization
中零处 LambdaExpr),而 rev 1 的保证 pass 只覆盖"每个 has_body
ir::FnDecl",不含嵌套 `LambdaExpr`,lift 变换从未规定。

**决策 D-FNREP(修订 P4/P6-8a 的字宽决策)。** 所有一等可调用**值**只有
一种物理表示:八字节 `(func_index:i32, env_ptr:i32)`,即 P6-8a 的
`CoreLayoutClosure` 形。这**修订** P4 值表示文档"`CoreVtFn` 是 i32 table
index(4/4)"的决策(`docs/design/core-ir-p4-value-representation.zh.md:233-236`)
与 RFC 0026 Decision History 的同义表述(`docs/rfcs/0026...:519`):四字节
`CoreLayoutFnRef` 布局删除(其唯一布局单测
`tests/unit/compiler/ir/core_layout.cpp:169` 由 FB-1 同步改为八字节断言),
`CoreVtFn` 不再是独立的值表示类型、不再拥有自己的 layout。逻辑层分工:

- `CoreVtFn{params,ret}` 保留为纯**签名**(effect 已擦除):源 FnT lower 到
  它,`CoreVtClosure.signature` 引用它;可调用**形参/绑定位**(捕获集在该
  点静态未知)的逻辑类型即签名型 `CoreVtFn`。
- `CoreVtClosure{signature,captures}` 只在闭包**构造点**(提升后的 lambda、
  取静态 fn 为值)赋类型,该点静态知道 env 槽,用于构造 env 聚合。
- **兼容规则(verifier 结构规则,零运行时代码)**:`CoreVtClosure{S,caps}`
  是 `CoreVtFn{S}` 的子类型——interned 签名 `S` 相同即可在任何 `CoreVtFn`
  位使用,captures 与被调方无关(它只经八字节对调用)。因为二者字宽/布局
  完全相同,"FnRef→Closure 拓宽适配"退化为 no-op,故评审建议的
  coercion/adapter 对(b)被明确否决:不发射任何转换序列、不留 FnRef 节点。
  反向(把签名型当已知捕获的 `CoreVtClosure`,或任何 Closure→四字节
  FnRef 收窄)拒绝:env 无法重建。新码 `FN_CALLABLE_TYPE_INCOMPATIBLE`。
- **调用规则**:对可调用**值**(形参、本地绑定、零捕获 fn 引用、捕获
  lambda)的调用**只有** `CoreCallClosureExpr`(`call_indirect`)一条路,
  不区分该值来自静态 fn 还是捕获 lambda;`CoreCallExpr`(直接 `call`)只用于
  resolver 静态解析到具体 Fn **实例**的名字调用(递归、按名调用 stdlib
  helper)。把静态 fn 名当值传递时,构造点物化
  `CoreClosureExpr{fn=instance, captures={}}` → `(table_slot, env_ptr=0)`,
  其 `CoreVtClosure{S,[]}` 按上面的子类型规则进入 `CoreVtFn{S}` 形参槽。
  table 中**每个**函数统一 `(i32 env, args...) -> ret`(§6.3),故零捕获
  fn 被间接调用时签名天然成立,同一 fn 直调/间调只有一份代码。

**决策 D-LIFT(FB-3 lambda 提升变换)。** 在单态化之后的 typed-HIR 上(P2d
克隆体已是具体类型,故提升产物一律单态、不重新泛型化)、Core lower 外层体
之前,跑确定性 lambda-lift:

1. 对计算体(flow/workflow computation region、已提升 fn 体)中的每个
   `LambdaExpr`,按源码序合成一个新的单态 Fn 符号 + Fn-kind 实例 +
   `CoreFnDecl`;其签名 = lambda 形参(具体类型)+ body 推导返回类型。
2. env = §3.2 的捕获(显式表按源码序;隐式按 first-use DFS)。提升 fn 的
   SSA 域:统一 env 首参(一个 i32 SSA 值),捕获名在体内改写为
   `i32.load(env_ptr + 槽偏移)`(§6.3),lambda 形参随后按普通预绑定 SSA
   参数进入;捕获槽不进 fn 的逻辑参数表。
3. 原构造点 lower 为
   `CoreClosureExpr{fn=提升实例, captures=[外层 SSA 值(规范序)]}`;外层体
   从此只见 `CoreClosureExpr`/`CoreCallClosureExpr`,不再有嵌套 lambda。
4. 表槽位(§6.2)按提升站点源码序确定性分配。递归 lambda(自引用匿名闭包/
   fixpoint 风格)不在当前语法/corpus 内,FB-3 对其 fail-closed
   (`core.UNSUPPORTED_RECURSIVE_LAMBDA`),不造 Y 组合子。
5. 提升产生的实例同样满足 §2.1 的有体 Fn 实例 1:1 保证与 §8.1 全部不变式。

> **实现落点(FB-3a2 落地修订):lift 在 Core lowerer 内,而非 typed-HIR。**
> 设计原拟在"单态化之后的 typed-HIR"上跑 lift;FB-3a2 实现时对照源码
> 核实了三个事实——(a) `monomorphization.cpp` 对 lambda **零处理**(它只按
> `body_block_index` 克隆顶层 fn 体,lambda 从不成为 `ir::FnDecl`);(b)
> `typed_hir_lower.cpp` 仅**誊写** `ir::LambdaExpr`(`visit_lambda`),既不产
> Fn 实例也不做自由变量分析;(c) 捕获必须主导的"构造点外层 SSA 值"在 Core
> lowerer A-规范化构造体之前**尚不存在**。故提升身份(每构造点一个确定性
> 单态 fn)、§3.2 有序捕获表、以及主导构造点的捕获值,三者最早只能在
> Core lowerer 内同时得出。等价 lift 因此落在
> `src/compiler/ir/core_lower.cpp`:遇 `LambdaExpr` 即声明 lifted
> `CoreFnDecl`+Fn 实例、经第三个 body owner(`FnRootPolicy`)降其体(逻辑参数
> 与 env 槽在 Core SSA 层同为**预绑定值**,槽进入 `CoreFnDecl.captures` /
> `env_bindings` 而非逻辑参数表),构造点改写为 `CoreClosureExpr`。§3.2 rule 4
> 的 frame 投影在构造点物化一次并按值捕获,经 `FnRootPolicy` 的 frame
> redirect 在体内读到预绑定槽。wasm 的 env 指针/`i32.load` 槽读取仍属 FB-3b
> codegen:Core SSA 层不引入 env 指针形参,捕获即普通预绑定值。

### 3.2 有序捕获(ordering)——确定性规则

env 槽序 = `CoreVtClosure.captures` 的向量序,该序在**构造点**与**类型
intern**点必须一致(P4 设计已锁此契约,
`docs/design/core-ir-p4-value-representation.zh.md:108-117`):

1. **显式 capture list**(语法 `\[a, b] -> ...`,`grammar/AHFL.g4:357-361`)
   按**源码书写序**。resolver 已把显式表持久化到
   `captured_names_by_expr`(`src/compiler/semantics/resolver.cpp:3307-3309`,
   未知名即报错 `:3311-3325`),typed-HIR 透传到 `LambdaExpr.captures`
   (`typed_hir_lower.cpp:2238-2248`)。
2. **隐式捕获**(无 `[...]`):今日编译器**没有** free-variable 分析——
   grep 全仓无 `free_var`/capture analysis;resolver 只解析 lambda 体
   (`resolver.cpp:3302-3305`)不计算自由变量,`captures` 为空。语义层
   typecheck 把外层 bindings **复制**进子 ValueContext
   (`src/compiler/semantics/typecheck_expr.cpp:2040-2047`,
   `insert_or_assign` 以支持参数遮蔽 `:2090`)。
   **决策:Core lowerer 新增确定性隐式捕获分析**:对 lambda body 表达式做
   单次 DFS(语法序/左→右),收集被引用、且绑定来自构造点外层 value 作用域
   的局部名,按**首次引用序**去重;lambda 参数与 body 内新绑定遮蔽外层同
   名(与 typecheck 遮蔽规则一致)。捕获名不进入逻辑类型身份
   (`CoreClosureCapture` 只有 value_type+mode,`core_ir.hpp:1495-1503`)。
   这把 typecheck 今日"复制整个 bindings map"的粗粒度动作收敛为最小精确
   槽集,且完全确定(无 hash 序)。
3. 捕获值在**构造点**物化:lambda 出现处,每个捕获槽对应一个当时已绑定
   的 SSA 值。lambda 体内对该名字的引用被改写为对 env 槽的读取
   (`env_ptr + P4-D 槽偏移`,§6.3),不再是外层 SSA 域的直接 use——否则
   会跨 SSA 域引用,违反 §2.3。
4. 对 `input.x` / `ctx.y` 这类 frame-root 读取:不把 frame 指针入 env(fn
   看不见 agent frame,§2.3);在**构造点**把该投影提升为一个普通 SSA 值
   (现有 path lowering 已能产出),再按值捕获。语义等价性见 §3.4。

### 3.3 env 布局:直接消费 P4-D indirect 边

不新增任何布局决策。P6-8a 已规定:闭包自身永远是
`(func_index:i32@0, env_ptr:i32@4)`(size 8 / align 4),env 是经
**Indirect 边**到达的独立聚合体,一槽一字段,槽序 = capture 序;无捕获时
无边(`core_layout.hpp:59-78`)。布局器已实现:为每个 `CoreVtClosure`
保留 internal layout 并登记 `PendingClosure`,在 root 之后 finalize
(`core_layout.cpp:262-284`,env 字段物化 `:475-510`)。

lowerer 的义务只是让 `CoreClosureExpr.captures[i]` 的逻辑类型与该 fn
`CoreVtClosure` 类型的 `captures[i].value_type` 逐槽相等(verifier
§8.1 强检),codegen 即可从 `CoreLayoutTable` 读出每槽偏移/大小,自身零
推导(与 P6 aggregate/container 纪律一致)。

### 3.4 捕获方式:仅 ByValue(当前语义事实)

决策:**只支持 ByValue**,与 `CoreCaptureMode` 今日唯一枚举值一致
(`core_ir.hpp:1490-1493`:`Only ByValue is accepted today`)。这是对当前
 evaluator 语义的忠实固化而非新增限制:

- 闭包构造时 `eval_lambda_expr` 以 `std::make_shared<EvalContext>(ctx)`
  **深拷贝整个上下文**(`src/runtime/evaluator/evaluator.cpp:249-257`);
  `EvalContext` 拷贝构造对 input/ctx/local 每个 scope 做 `clone_value`
  深克隆(`src/runtime/evaluator/eval_context.cpp:32-44`),getter 也返回
  克隆值。即:**构造点快照**,闭包内修改不影响外层。
- 调用入参同样是 by-value:`invoke_callable_value` 对每个实参 `clone_value`
  后绑定(`evaluator.cpp:270-274`);顶层 fn 调用同构
  (`evaluator.cpp:2071-2073`)。

ByRef 未来若引入,需要 region/lifetime 提升规则,但**不改节点形状**
(`CoreCaptureMode` 已留扩展位),是独立 RFC,不在本设计内。

### 3.5 无 partial application

决策:本设计不支持也不编码 partial application。当前语言严格定元:

- callable 调用:`params.size() != args.size()` 即错误
  (`evaluator.cpp:265-268`);
- 顶层 fn 调用同样硬等元(`evaluator.cpp:2046-2050`);
- 仓内无 curry/partial-apply 节点或语法。

Core verifier 对两类调用节点都做精确 arity 检查(§8.1)。未来若要偏应用,
实现方式是编译期生成"参数收集适配器新 fn + 闭包",是新节点/新片,不影响
本文节点形状。

## 4. AHFL-IR → Core 的调用解析

`lower_value` 的 `CallExpr` 臂(`core_lower.cpp:2757`)在现有三类解析之
后、unsupported 兜底之前,按以下**确定性身份优先**顺序解析;
`MethodCallExpr` 新增专用臂(经 §2.2 的同一 ExprLowerer 机制):

1. **Capability**(现有,`core_lower.cpp:2759-2789`):不变,继续是有序
   `CoreCapabilityCallStmt`。
2. **内部 collection hook**(现有,`:2798-2800`,`:2853-2865`):不变,
   继续 lower 为 typed `CoreCollectionExpr`。
3. **Enum variant 构造**(现有,`:2809-2831`):不变。
4. **静态 fn/方法直调(新)**:当 `callee_ref` 解析到 Function symbol
   (`MethodCallExpr` 用 `method_ref`,receiver 作第 0 实参——与 evaluator
   的 receiver-first 扁平化逐字一致,`evaluator.cpp:1930-1947`),且解析到
   一个有体 Fn 实例时,lower 为 `CoreCallExpr`(§5.1)。目标实例的身份
   解析复用 Pass 7 的 symbol-id 优先表手法(`core_lower.cpp:4018-4054`
   为 agent shell 建索引;fn 建 `(origin SymbolId, dispatch_types) ->
   CoreInstanceId` 索引),mangled key 永不重新派生。
5. **一等可调用值调用(新)**:`callee` 解析为一个本地 SSA 值/形参,其逻辑
   类型为可调用型(`CoreVtFn` 签名位,或构造点赋的 `CoreVtClosure`,
   §3.1.1),lower 为 `CoreCallClosureExpr`——包括 FnT 形参在 fn 体内被调用
   (`list_map_into` 的 `f(...)`),不再有"FnT 参数无法调用"的洞。evaluator
   的对应分派:`ctx.get_local(callee)` 命中 `CallableValue`
   (`evaluator.cpp:1776-1799`)。
6. **静态 fn 名作为值传递(新)**:不是调用而是把 fn 名出现在值位置时,
   lower 为零捕获 `CoreClosureExpr{fn=解析到的实例, captures={}}`
   (`(table_slot, 0)`),按 §3.1.1 子类型规则进入 `CoreVtFn` 形参。
7. 其余:维持 fail-closed(原型无体、未解析、intrinsic 未覆盖)。

`@builtin` facade / 无体 prototype(`has_body=false`)不进 fn 表:已映射为
typed Core 节点的(raw hooks → `CoreCollectionExpr`)继续;未映射的
intrinsic 在其被 P6 纯节点覆盖前继续 unsupported-fail-closed,**不**为它们
发明 wasm 函数体(计算留宿主/编译器内建,不产生用户可见 ABI)。

## 5. 调用节点:直调 vs 间接调用;效果与纯计算的分层

### 5.1 `CoreCallExpr`:静态解析的纯直调

```cpp
/// 对一个静态解析的、Pure 的单态化 fn/有体方法的直接调用(call 指令)。
/// PURE:仅当 callee 体不含 CoreCapabilityCallStmt 时合法(§5.3)。
struct CoreCallExpr {
    CoreInstanceId callee{};               // 必为 payload=CoreFnInstance 的实例
    std::vector<CoreValueId> args;         // ANF,左→右;含方法 receiver(第 0 个)
};
```

- 是 **CoreExpr**(纯 ANF 表达式),结果由外层 `CoreLetStmt` 绑定,参与
  match arm / 构造嵌套等一切纯位置。
- codegen 为普通 `call <funcidx>`(`kOpCall` 已存在,
  `core_wasm_codegen.cpp:164`);编译期唯一目标,无间接分发。被直调函数的
  wasm 签名按 §6.3 统一为 `(i32 env, args...) -> ret`,直调恒传 `env=0`。
- arity/类型完全静态:verifier 用 §2.1"签名来源"对 callee 实例做 P2d
  Substs 后物化的 concrete `CoreVtFn{params,ret}` 逐参核对并确定
  result_type;**不**从 `dispatch_types`(类型实参向量)直接读签名。

### 5.2 `CoreCallClosureExpr`:闭包值间接调用

- 按 §3.1.1 D-FNREP,**一切一等可调用值的调用**(FnT 形参、callable 本地
  绑定、零捕获 fn 引用、捕获 lambda)都走本节点,不按值的来源分叉。
- 同为 **CoreExpr**(闭包在类型系统里是 Pure-only:
  `typecheck_expr.cpp:2020-2025` 明确 "current closure model supports Pure
  closures only",lambda 体以 `CallContext::PureOnly` 检查
  `:2045`)。因此闭包调用不可能藏效果,无需 stmt 形态。
- codegen:从闭包 SSA 值的地址 load 两个 i32
  (`func_index` @0、`env_ptr` @4,P4-D 定型),压入 `env_ptr` 与实参,执行
  `call_indirect <typeidx> <tableidx>`(0x11,新增 opcode,§6.2)。
- table 的 functype 异构是合法的 wasm;`call_indirect` 携带**期望**
  functype,运行时签名不符会 trap。该 trap 在良构程序中不可达:verifier
  静态证明闭包值的 `CoreVtFn` 与调用点期望逐结构相等(interned 同 id)。

### 5.3 效果调用保持有序语句;含效果的 callee 推迟

不可把 capability 调用塞进纯 expr——这是 ANF 层的立身事实
(`core_ir.hpp:350-365`)。规则:

- 直接写在 handler 里的 capability 调用:现状不变,
  `CoreCapabilityCallStmt`。
- 对**体含 capability 调用**的 fn 的调用:不是纯操作(它会排序、会
  pending/suspend),不能进 `CoreCallExpr`。决策:FB-1..FB-3 **fail-closed**
  (新 lowerer 诊断,见 §8.2),要求用户/库把该调用直接写在 handler;
  **FB-4(已落地)** 引入有序语句形态的 `CoreCallStmt { result, callee, args }`
  (与 `CoreCapabilityCallStmt` 同构的 ordered statement + resume
  checkpoint),codegen 为 outlined 效果函数的 `call`;效果分类由 Core-ANF
  上的结构最小不动点 `analyze_fn_effects`(core_recursion.hpp)给出——含
  transitive 效果的包装 fn 也被分类为效果 fn,不读任何 effect 子句拼写。
  效果 fn 内部的 cap-call 经与 handler capability 语句相同的 ahfl_cap
  import 序号发射;本次只实现**单次 run 的有序执行**,跨进程 memo/replay/
  不重放语义随 wire checkpoint 框架(D2b)一并落地,flow-handler 直接
  resume 行为保持不变。把完整 checkpoint 放在 FB-4 之外是因为 pending 结果的
  帧/恢复身份依赖 P6-7 与 B2 系列的帧决策,提前做只会产生第二套帧 SSOT。
- `effect Nondet` fn:编排层的 Nondet 语义是跨 run 的 goto 非确定性
  (agent state 机),值级 Nondet 表达式在单值 WASM 计算 lane 没有对应物。
  决策:fn 体/闭包内出现 Nondet 调用,FB-1..FB-4 fail-closed;待值级
  nondet 语义在 RFC 0026 后续片定义后再开。当前仅少量负向/单元测试使用
  (`tests/unit/compiler/semantics/effects.cpp:608` 等),不阻塞 corpus。

### 5.4 控制流:无循环;递归只经 fn 调用

语法层没有任何循环语句:`statement` 仅有
let/assign/if/ifLet/goto/return/assert/unwrap/requires/unreachable/expr
(`grammar/AHFL.g4:438-450`)。AHFL 的迭代是 **state-machine goto**(P6
已以 `block`+`br` + computed handler 函数落地),fn 体内没有 goto
目标状态(verifier 禁止 fn 体出现 `CoreGotoStmt`,§8.1)。因此:

- fn 体内的重复计算**只有递归一条路**,且语言已用 `decreases` 度量在
  AHFL-IR 层保证终止(如 std 的有界复制 `list_copy_into`,
  `std/collections.ahfl:128-139`,decreases =
  `list_raw_length(source) - source_i`,递归调用在 `:138`;同族还有
  `list_map_into` `:141-153` 等 `*_from` helper)。
- WASM 上递归用**原生 `call`**(MVP 即支持函数自调用,无需
  return_call/tail-call 提案);不做 state-thunk trampoline。上界由 §8
  FB-2 的编译期深度预算 + P4-D bounded capacity 给出(列表容量编译期已知,
  `std/collections.ahfl:56` 的固定 8 元 `list_from_array` 即典型有界形)。
- 不使用 `return_call`(0x12):MVP 引擎兼容优先;单层 wasm 调用栈深度由
  预算静态封死(§8 FB-2),不依赖尾调用优化保栈。

## 6. WASM codegen

### 6.1 函数索引空间扩展(在现有 PROJECT 规则上追加,不改写既有字节)

现状:import 占低位 → 固定 7 ABI 函数(import_count+0..6,
`kDefinedAlloc..kDefinedRun2`,`core_wasm_codegen.cpp:237-243`)→ P6-2
computed handler(import_count+7..,`kDefinedHandlerBase = 7` 在 `:247`,
`FunctionTable` `:354-377`)。**保持 E1-E3/P6 既有模块字节不变**的前提下
追加:

```text
[0, import_count)                    capability imports(ahfl_cap)
[import_count+0 .. +6]               固定 ABI(alloc/dealloc/current_state/
                                     is_final/step/run/run2)
[import_count+7 .. +7+H)             P6-2 computed handlers(H=handler_count)
[import_count+7+H .. +7+H+F)         FB-1:outlined fn bodies(F=被 reachability
                                     触达的 CoreFnDecl 数,确定性排序)
[... 其中闭包可寻址子集)             同时进入 §6.2 的 funcref table
```

- `FunctionTable` 增加 `fn_count` 与 `fn(ordinal)` 投影(与
  `handler(index)` 同形,后者在 `:371-373`);workflow 模块的
  `WorkflowFunctionTable`(`:379-398`)在其 runner/run2 投影之后做相同追加。
- fn 函数的 codegen 复用 P6 的 scalar/aggregate/coerce/match emitter
  (`emit_expr` 起于 `core_wasm_codegen.cpp:3049`;fn 体是第三类 body owner,
  与 agent plan 共享 emitter,不写第二套表达式编译器)。
- Reachability:只发射从入口 handler / workflow region 的直接调用闭包可达
 的 fn(确定性定点),与当前 capability import reachability 同一纪律
 (`WorkflowPlan::imports` 注释,`:347-350`)。

### 6.2 Table(4)/Element(9)——标记为后续 codegen 片(FB-3)

编码器当前没有 Table(4)/Element(9) 段常量或发射(七个有编号段常量在
`core_wasm_codegen.cpp:111-117`,段 id 4 与 9 缺如;agent `encode_module`
为 `:5157-5299`,只发七个有编号段 + 可选 Custom(0))。FB-3 决策:

- 恰好声明 **1 个** `funcref` table,`min = N`(N = 可达的、被取闭包值的
  fn 数;零闭包模块不声明 table/element,其 Table/Element **段与字节整体
  省略**,E1-E3/P6 模块逐字节不变)。
- wasm 段序必须按段 id 非递减,故插入位置**唯一**,没有"在 Memory 前后
  任选"的自由:**Table(4) 恰好插在 Function(3) 与 Memory(5) 之间**,
  **Element(9) 恰好插在 Export(7) 与 Code(10) 之间**;Element(9) 以
  active element 初始化 `table[0..N)` 到对应 wasm 函数索引。
- 闭包值里的 `func_index` 字是**稠密 table 下标**(0..N-1),不是 wasm
  函数索引——编译器维护 `CoreFnId -> table slot` 与 `table slot -> wasm
  funcidx` 两张确定性映射(前者写入闭包构造点常量,后者进 Element 段);
  这与"身份是索引而非名字"一致,也让重排内部函数不改变闭包值语义。
- 无 Data(11)段需求:env 在 bump 堆运行时构造,不做静态数据初始化。

### 6.3 调用约定(env、参数/返回字)

- **统一 env 首参约定**:table 中每个函数的 wasm 签名为
  `(i32 env, <args...>) -> <ret>`;无捕获静态 fn 的 thunk 忽略 env(传入
  0)。直调路径(`CoreCallExpr`)直接 call 真函数并传 `env=0`;被取闭包的
  同一 fn 仅此一份代码,无双实现。
- 参数/返回在 FB-1/FB-3 限定为 **P6 单词值集合**:i32/i64 标量、aggregate
  i32 地址、bounded collection i32(头部地址)。String(PtrLen 双字)、
  bytes、f64 跨 fn 边界 fail-closed——与现有单词 P6 门控同界:投影叶必须
  是单词 P6 值(`place_is_p6_value` 失败即 reject,
  `core_wasm_codegen.cpp:3117-3123`,reject 在 `:3122`),workflow 跨帧的
  fail-closed 诊断码是 `wasm.UNSUPPORTED_WORKFLOW_FRAME` /
  `wasm.UNSUPPORTED_CAPABILITY_FRAME`(`core_wasm_codegen.hpp:28-31`,
  发射点如 `core_wasm_codegen.cpp:4232-4242` 及 `:4442-4566` 一带)。
  注意 HEAD 上**不存在** `CoreWasmFrameContract`/`RawP6Frame` 符号(它是
  在飞未提交的 P6 conformance WIP 引入的),本文不引用未提交符号;多字值跨
  边界随待落地的 P6-7 wire-frame 决策统一以"地址化"解除,不在此发明私帧。
- **env 内存(rev 2 改):env 从运行时 checked bump heap 分配,不用 P6-4
  construct scratch**。P6-4 的 scratch 槽是**计划期单调游标**
  `scratch_addr_cursor_`(`core_wasm_codegen.cpp:1391-1396`,消费点
  `:2054-2060`/`:2607-2613`)在编译期切出的**固定地址**,handler 各次
  激活复用同一槽;闭包构造是**动态**的(递归/迭代下每次应用都需要新鲜 env
  快照以忠实 §3.4 ByValue 深拷贝语义),放进静态 scratch 会在不同激活间
  别名。运行时分配器是 `alloc` ABI bump heap:unchecked
  `make_alloc_body`(`core_wasm_codegen.cpp:4858-4873`)与 capability lane
  的 checked 版 `make_checked_alloc_body`(`:4878` 起,超出 64 KiB 单页
  返回保留空指针 0 且不推进 heap_next)。构造点发
  `i32.const size` + `call alloc` 得 env 基址,按
  `CoreLayoutClosure.environment` 的槽偏移写入捕获值;`alloc` 返回 0 即
  RESOURCE fail-closed(不向空指针写)。env 生命周期为模块实例级(arena,
  不回收——RFC 0026 Q1 已定无 GC)。
- **heap_base 落位决策**:agent 模块 heap global 今日以 `1024` 初始化
  (`core_wasm_codegen.cpp:5225` 的 `append_global(..., 1024)`),与
  `kP6AggregateInputBase=1024`/context/scratch 预留区
  (`core_wasm_abi_constants.hpp:54-56`)**重叠**;今天安全仅因生成代码从不
  发 `call alloc`(只有 ABI handler 体/import 内出现 `kOpCall`,见
  `:4964`/`:4995`/`:5007`/`:5067`,用户表达式路径无 alloc)。FB-3 启用
  alloc 前必须把 agent-lane `heap_base` 放到全部预留帧/backing 区**之后**,
  镜像 capability-workflow 的两阶段做法(`compute_event_layout`
  `:5320` 起算 `heap_base`,消费 `:5876-5896`):新增 agent 侧
  `compute_agent_heap_base(input/ctx/scratch/backing 极值 + env 预算)`,
  checked wasm32 算术 → `BINARY_OVERFLOW`,对 64 KiB 页 →
  `RESOURCE_EXHAUSTED`;workflow lane 同样把 env 预算并入其 event/heap
  sizing。**单页总预算一处权威**:env 容量预算 = §8.1-6 的每实例深度上界 ×
  该实例 env 聚合 layout size(沿 SCC 求和,静态上界),并入现有预留区容量
  fail-closed(`core_wasm_abi_constants.hpp:73-103` 的 capacity 体系),
  不新增第二套预算闸门。
- fn 体内读取捕获槽:把 `env_ptr` 物化为该次调用的一个 i32 local,槽读取
  = `i32.load offset=<P4-D 槽偏移>`;写回不存在(ByValue)。

### 6.4 与现有 frame 门控的衔接

直调纯 fn 不跨越 run2 边界(同一 wasm 调用栈内完成),因此不引入
wire-JSON 封送;fn 内部若产生 aggregate,地址在模块私有线性内存内,沿用
P6 raw-frame 证据规则。只有 §5.3 FB-4 的效果 fn 调用触及 run2/帧,故与
P6-7 绑定。

## 7. 与 evaluator 退役(KR6.8)及 WASM conformance 的关系

### 7.1 本片消灭的 evaluator 执行面

KR6.8 要原子删除 `src/runtime/evaluator/`
(`docs/rfcs/0026...:285` P8;`:282` 是 P6 WASM codegen 条目,勿混;roadmap
KR6.8
`docs/plans/q4-2026-roadmap.zh.md:199`)。今日 evaluator 的函数面正好是本
设计覆盖的四块:

1. 顶层 fn 直调:`find_function` 名字扫描 + 实参克隆 + `exec_block`
   (`evaluator.cpp:2037-2097`);
2. callable 值本地分派:`eval_call_expr` 的 `ctx.get_local(callee)` →
   `CallableValue` 路径(`evaluator.cpp:1776-1799`);
3. lambda/闭包:`eval_lambda_expr` 整上下文深快照 + `invoke_callable_value`
   (`evaluator.cpp:249-275`、`:1949-1952`);
4. 方法调用:`MethodCallExpr` receiver-first 扁平化后复用调用机制
   (`evaluator.cpp:1930-1948`)。

FB-1 + FB-3 落地后,这四块在 Core/WASM 有一一对应节点;FB-4 关闭含效果
callee 后,evaluator 函数面即无剩余独占语义(其 intrinsic 面随
KR6.6 各 typed 节点 / 宿主能力面另行退役)。

### 7.2 对 golden/conformance corpus WASM 合格率的影响(基于当前计数)

事实清点(**committed HEAD `11e57d32` 口径**;计数均对 git 索引实测,
不采用在飞未提交工作树):

- **committed** conformance 清单 `tests/conformance/cases/` 共 **8** 例:
  **5** 例 runnable orchestration(e1/e2/e3 x2/float),**3** 例
  `BlockedComputation`(`if_let_e2e`、`enum_variant_e2e`、`e2e_multi_agent`,
  机器钉死于
  `tests/unit/runtime/conformance/wasm_eligibility_test.cpp:196-217`;
  runnable 表在 `:172-177`,两表合起来恰好覆盖 committed 清单)。这 3 例的
  阻塞码是 `wasm.UNSUPPORTED_WORKFLOW_FRAME` /
  `wasm.UNSUPPORTED_CAPABILITY_FRAME`,阻塞构造是 workflow 多 agent
  packaging 的 computed-goto/frame 限制(computed-goto 拒绝
  `core_wasm_codegen.cpp:4175-4187`、"exactly one goto" 拒绝
  `:4232-4242`,frame 码定义于 `core_wasm_codegen.hpp:28-31`),**不是**
  fn/闭包。
- **在飞未提交**的 P6 conformance 切片另在工作树新增 10 个
  `p6_*.case.json`(aggregate/cascade/coerce/coerce_bounds/collection/
  match_enum/neg_compare/nested_depth3/nested_fallthrough/scalar_cond)及其
  observations,并改了 `wasm_eligibility_test.cpp` 的钉死表;它们**未进
  HEAD**,故 18/15/3 是"工作树口径"而非 pinned commit 口径。该片落地后
  本节随其 rebase 重新计数,本文不把未提交事实写成 HEAD 事实。
- `tests/golden/`(HEAD)共 **117** 个 `.ahfl`:**lambda 0 个**;含 `fn`
  声明的仅 3 个,且都是 resolver/typecheck **负向**用例(不执行)。
- `tests/integration/` + `examples/`(HEAD)共 **107** 个 `.ahfl`:含 fn
  体的 **23**、含 lambda 的 **9**、含方法调用的 **9**(对工作树与 HEAD
  的该子集文件一致)。9 个 lambda 程序的共同点正是把 lambda 传入 FnT 形参
  (§3.1.1 二分问题),仓内**零**处显式 `[capture]` 列表语法用例,现有
  lambda 词法上均不捕获外层变量,但隐式捕获分析仍按 §3.2 实现。
- stdlib(`std/*.ahfl`)大量能力以泛型 fn/impl 方法承载
  (`std/collections.ahfl:98-153` 的有体构造器/递归 helper——其中
  `list_copy_into` 在 `:128-139`、`list_map_into` 在 `:141-153`、
  `list_fold_right_from` `:195-206`、`list_copy_range_into` `:208-221`、
  `list_concat_flatten` `:224-237`;`:16-89` 的 raw/库 fn 原型),是
  fn-body lowering 落地后 WASM lane 的主要新增可执行面。

**结论(诚实,不夸大):**

- 本设计落地**不直接**改变 committed HEAD 的 8 例 / 5 runnable / 3 blocked
  数字——3 个阻塞例的闸门在 frame/packaging,不在 fn/闭包;没有任何当前
  committed conformance 例使用 lambda(0)。在飞 P6 conformance 切片的
  计数(工作树 18 例)以该片自己的提交为准,与本文的 HEAD 口径分开陈述。
- 它解除的是 corpus **扩面**的封锁:23 个用 fn 的 integration 程序、
  9 个 lambda 程序、stdlib 泛型函数体得以进入 WASM 差分 conformance,
  这是 KR6.7 把 evaluator e2e/golden 系统性迁向引擎无关用例、进而 KR6.8
  退役 evaluator 的必要条件(roadmap 依赖序
  KR6.4 → KR6.5 ∥ KR6.6 → KR6.7 → KR6.8,
  `docs/plans/q4-2026-roadmap.zh.md:319-323`)。
- 合格率的量化目标放在实现片:FB-1 后新增"fn 直调"conformance 组(从
  integration/stdlib 选有界纯函数程序),FB-3 后新增 closure 组(9 个
  lambda 程序中差分可跑的子集);每片由
  `tests/conformance/wasm_eligibility.cpp` 分类器机器裁定 lane,不手维护
  跳过清单。

## 8. 验证器不变式、擦除语义、实现阶梯

### 8.1 新增/强化的 Core verifier 不变式

沿用 `core_verify.cpp` 的独立结构验证(不依赖 lowerer):

1. **fn 表与实例 1:1**:`CoreFnDecl.instance` 指向存在且
   `holds_alternative<CoreFnInstance>` 的实例;每个有体链接的
   `CoreFnInstance.body` 反指有效;无重复链接;参数个数 ==
   实例 `CoreVtFn.params` 个数;body `value_types` 对每个参数预绑定为
   对应具体类型(新码 `core.verify.FN_INSTANCE_LINK_INVALID`、
   `FN_SIGNATURE_ARITY`)。
2. **fn 体 SSA**:`ArenaView` 加 `OwnerKind::Fn`;dense
   (`value_types.size()==value_count`)、单定义、def-before-use、yield
   非法、goto 非法、每条路径以 `CoreReturnStmt` 完成
   (`core_region_exit` 的 diverges 规则 + 必须是 return 而非 goto/trap;
   新码 `FN_BODY_TERMINATION`)。
3. **直调合法**:`CoreCallExpr.callee` 是有体 Fn 实例;arity 精确;逐参
   逻辑类型与签名一致(同 interned id 或 P4 已证 layout 等价的 coercion
   路径);result_type == 签名 ret。**效果种类一致性(FB-4 已落地)**:
   纯 `CoreCallExpr` 只能调用 PURE fn;体(经 transitive 固定点)到达
   capability 的效果 fn 只能经有序 `CoreCallStmt` 调用。纯 expr 调效果
   fn 报 `FN_CALL_EFFECTFUL_CALLEE`,有序 stmt 调纯 fn 报
   `FN_CALL_EFFECT_KIND`;fn 经 transitive 效果图到达 agent 白名单之外
   的 capability 报 `FN_EFFECT_CAPABILITY_UNAUTHORIZED`。
4. **闭包构造合法**:`CoreClosureExpr.fn` 在表内;captures 个数/类型与该
   fn 闭包类型 `CoreVtClosure.captures` 逐槽相等;每个捕获 SSA 值在构造
   点可见、跨体不直接引用外层 SSA;捕获槽序与 P4-D env 间接边物化结果一
   致(新码 `CLOSURE_CAPTURE_ARITY`/`CLOSURE_CAPTURE_TYPE`/
   `CLOSURE_CAPTURE_ORDER`)。
5. **闭包调用 dispatch 类型**:`CoreCallClosureExpr.closure` 的逻辑类型
   是可调用型——签名型 `CoreVtFn`(形参/绑定位)或构造点赋的
   `CoreVtClosure`(§3.1.1),二者都解析到同一个 interned 签名 `CoreVtFn`;
   arity 与该签名精确匹配,实参/结果类型逐结构相符(保证 wasm
   `call_indirect` 期望类型不会 trap;新码
   `CLOSURE_DISPATCH_ARITY`/`CLOSURE_DISPATCH_TYPE`)。另检
   §3.1.1 D-FNREP 兼容规则:仅允许 `CoreVtClosure{S,_}` 流向
   `CoreVtFn{S}` 位(签名 interned id 相等),反向收窄拒绝
   (`FN_CALLABLE_TYPE_INCOMPATIBLE`)。
6. **调用图有界(FB-1)**:`CoreCallExpr` 直接调用图无环(新码
   `FN_RECURSION`,FB-1 拒绝一切递归)。**FB-2** 放开为"有静态深度预算
   的有界递归",预算**不**信任、也**不**读取 decreases 项(已在 Core
   擦除),而是在 Core ANF 上独立推导,规则如下:
   a. 在 fn 直调图上求 SCC(Tarjan);非平凡 SCC = 递归组。组内每条递归
      边的实参必须包含一个**秩参数 r**(i32 标量位置),verifier 在
      SSA 上证明沿该边 `r' = r - 1`(允许编译期常量加数)且 r 在静态上
      被某个**秩上界 B** 封死;不满足形状(秩经 memory/aggregate/未知函数
      传播、沿边不减、上界不是静态值)的 SCC 一律 fail-closed
      (`FN_RECURSION_UNBOUNDED`)。
   b. 秩上界 B 只接受两类来源:(i) 某个 `CoreVtNominal` bounded container
      的 capacity——秩初值是该容器 header 的容量字(P6 collection
      backing 已静态物化容量,`kP6CollectionBackingBase` 体系),verifier
      从入参 container 的 P4-D container 布局读 capacity;(ii) 编译期整数
      常量(常量传播可定值的 SSA)。refinement 上界只有在已被 lower 成
      container capacity/常量时才接受,Core 不重新解 refinement 公式。
   c. SCC 的静态深度上界 = max 沿入口边的 B(互递归组取组内最大上界并要求
      每边秩严格下降,保证组内总激活数有界);该上界乘 §6.3 的 env/帧大小
      后并入单页 RESOURCE 预算,超出 fail-closed(`FN_RECURSION_DEPTH`)。
   d. 接受形状的判定只看结构事实,不看 decreases 拼写;**FB-2 明确接受**
      的 stdlib 递归:`list_copy_into`(collections:128-139,
      r=source_i,B=source 容量)、`list_map_into`(:141-153,与 FB-3 合流
      后 f 经闭包间接调)、`list_filter_from`(:155-169)、
      `list_fold_from`(:171-182)、`list_copy_range_into`(:208-222,
      r=src_end-src_start)、`list_concat_flatten`(:224-235,r=n-i)、
      `map_values_step`/`filter_keys_step`(:695-703/:712-722,r=n-i)。
      `list_fold_right_from`(:195-206)decreases 写 `i+1`、递归实参
      `i-1`,秩方向是 i 由常量 n 向下计数——FB-2 接受当且仅当 n 静态绑定
      container 容量(规则 b-i),否则 fail-closed,不为它特判。**保持
      fail-closed 的形状**(不暗示"有 capacity 就够"):递归参数不沿边
      静态减 1、容量来自运行时 String(PtrLen,容量不在 P4-D container
      布局内)、深度依赖 aggregate 字段运行时值(规则 b 不认)、相互递归
      跨非平凡 SCC 但有边缺秩参数、lambda 自引用(§3.1.1 D-LIFT)。
7. **无效果逃逸到纯位置**:闭包/fn 纯表达式子树中出现 capability 结果以外
   的效果构造,继续由既有 ANF 结构保证;新节点不引入例外。
8. P9 JSON:所有新节点/字段进 `core_expr_nodes.def` wire 名列与
   writer/reader 的 kind 表(RFC 0026 P9 §3 单一表规则),fn 体 arena 按
   既有 flow/workflow body 相同规则序列化,reader 重建后跑同一 verifier。

### 8.2 新增 lowerer 诊断码(挂 `core.*` 目录)

- `core.UNRESOLVED_FN_CALL`:fn/方法 callee 无法按身份解析到有体实例。
- `core.FN_BODY_UNLOWERED`:fn 体含本片尚未 lower 的语句/表达式
  (fail-closed,不用 Unknown 节点)。
- `core.FN_EFFECTFUL_CALLEE`(FB-4 前):被调 fn 体含 capability 调用。
- `core.NONDET_FN_VALUE`:Pure 计算 lane 遇到 Nondet fn 值调用。
- `core.CLOSURE_CAPTURE_INVALID`:隐式捕获分析遇到不可捕获引用
  (非局部、非可提升投影)。
- `core.FN_CROSS_BOUNDARY_TYPE`:PtrLen/f64 等跨 fn 边界类型(FB 帧门未开)。
- `core.FN_CALLABLE_TYPE_INCOMPATIBLE`:可调用值流向签名不符的形参位,或
  试图把签名型 `CoreVtFn` 收窄成已知捕获的 `CoreVtClosure`(§3.1.1)。
- `core.UNSUPPORTED_RECURSIVE_LAMBDA`:自引用/fixpoint 风格 lambda(FB-3
  fail-closed,§3.1.1 D-LIFT)。
- `core.FN_RECURSION_DEPTH`:§8.1-6c 推出的静态深度 × env/帧大小超出单页
  RESOURCE 预算。

### 8.3 擦除语义(明确记录,非隐式丢失)

| AHFL-IR 事实 | Core 处置 |
| --- | --- |
| `FnEffectClause.kind` / capability 列表 / `decreases`(`decl.hpp:385-398`) | 不进 `CoreFnDecl`;Pure 直调成 expr,效果 callee 成 FB-4 语句;终止上界由 §8.1-6 结构预算重述 |
| `FnTypeInfo` 的 type-param 名 / typed body | 单态化后消失;每实参组一份 `CoreFnDecl`,无类型参数(复用 P2d body 实例) |
| lambda 源参数名 / capture 名 | 仅 provenance;身份是 SSA 值与槽下标 |
| 契约运行时检查 | 若需要,只能作为普通 assert+分支(沿用 RFC 0026 降级步骤 3) |
| 闭包 effect grade | `CoreVtFn` 已构造时擦除;闭包 Pure-only 由 typecheck 与 Core 双侧保证 |
| `CoreVtFn` 的四字节 FnRef 物理表示(P4 旧决策) | rev 2 修订(§3.1.1):删除 `CoreLayoutFnRef`,一切可调用值统一八字节 `CoreLayoutClosure` 形;`CoreVtFn` 退为纯签名逻辑类型 |
| 嵌套 `LambdaExpr` | 不属于"擦除",而属 FB-3 的 lambda-lift 结构变换(§3.1.1 D-LIFT):提升为单态 Fn 实例 + `CoreFnDecl`,构造点改写为 `CoreClosureExpr` |

### 8.4 实现阶梯(每片独立可测、Conventional Commit)

- **FB-1 直调 fn 体(首个实现片)**:`CoreFnId`/`CoreFnDecl`/
  `CoreProgram::fns`;`CoreBodyStorage` 提升与 flow/workflow 内聚重构;
  保证有体 FnDecl 的实例+fn 表 pass(签名经 §2.1 P2d Substs 物化);
  **D-FNREP 布局修订同步落地**:删除 `CoreLayoutFnRef`,`CoreVtFn` 不再
  拥有 layout,可调用值统一八字节(改 `core_layout.cpp:259-260`/
  `991-1005` 与 `tests/unit/compiler/ir/core_layout.cpp:169` 的 FnRef
  断言),零消费者模块的其他布局字节不变;`CoreCallExpr` 纯直调(call
  opcode、import+7+H 之后函数索引、`(env=0, args...)` 约定 §6.3);
  MethodCallExpr receiver-first 解析;`OwnerKind::Fn` 验证 +
  §8.1-1/2/3/5(签名部分)/7 + 无环调用图(§8.1-6 的 FB-1 形态)。
  LambdaExpr 与可调用**值**调用仍 unsupported。
- **FB-2 有界原生递归**(已落地):纯分析 `analyze_fn_recursion`
  (`include/ahfl/compiler/ir/core_recursion.hpp`)对 Core-ANF 做 SCC
  划分 + 秩参数/上界/深度推导(§8.1-6 a-d);verifier 以
  `FN_RECURSION_UNBOUNDED`(无法导出有限秩上界)/ `FN_RECURSION_DEPTH`
  (超过 Core 层上限)fail-closed,取代 FB-1 的 blanket `FN_RECURSION`
  (该常量保留给读端兼容,不再发射)。`decreases` 在 Core 已擦除、
  分析绝不读取;归纳证据只来自编译期整数、bounded 容器
  (`CoreVtNominal.capacity`,List/Set/Map)的长度字、以及沿每条递归边
  原样透传且在每个入口边静态有界的不变量形参。wasm 后端用同一份分析
  (`max_native_fn_call_depth` 对 SCC 缩点 DAG 取最大加权路径)把原生
  `call` 栈深门控在 `kFnRecursionNativeStackDepthMax`(RESOURCE
  fail-closed,§6.3 单页预算的原生栈腿;env/heap 记账随 FB-3)。
  Node v22 真实引擎证据 `tests/golden/wasm/fb2_bounded_recursion.ahfl`
  + `tests/scripts/wasm_fb2_recursion_node_host.py`(容量 4 的有界列表
  递归求和)。注意 `list_map_into` 体内的 `f(...)` 是值调用,须等
  FB-3,FB-2 的递归验收集先取不含 FnT 形参的 helper(copy/copy_range/
  concat/map_values 等的非 f 递归路径)。
- **FB-3 闭包值 + 间接调用 + lambda-lift**:`CoreClosureExpr`/
  `CoreCallClosureExpr`;§3.1.1 D-LIFT lambda 提升 pass(嵌套
  LambdaExpr + 外层实例 → 单态 Fn 实例/`CoreFnDecl`,env=捕获,SSA 改写);
  隐式捕获 first-use DFS(§3.2);`CoreVtClosure` 由 lowerer 实际 intern
  (消费 P6-8a env 布局)与 FnT 形参的兼容/子类型规则(§8.1-5);
  Table(4)/Element(9) 编码器(固定段位 §6.2)+ env 首参约定 +
  `call_indirect`;**env 走运行时 checked bump heap + agent-lane
  heap_base 重定位**(§6.3,含 RESOURCE 预算并入);零闭包模块字节不变;
  §8.1-4/5 全量验证。
- **FB-4 含效果 callee**:`CoreCallStmt` 有序语句 + outlined 效果函数 +
  cap-call checkpoint/恢复(依赖 P6-7/B2 帧决策);Nondet 值调用随语义
  决策另开。
- **FB-5 conformance 扩面与退役衔接**:把 integration/stdlib/lambda 程序
  系统性转为引擎无关 conformance 例,分类器 lane 全机器裁定;达成
  KR6.7 全绿前置,支撑 KR6.8 原子删除 evaluator(§7.1 四面对应齐)。

## 9. 关键决策小结(DECISIONS)

1. fn 身份 = `CoreFnId` flat 表;fn 表按单态化实例 1:1,与 instance
   注册表分工(代码生成单位 vs 单态化注册单位);非泛型有体 fn 也强制
   一实例一 decl。
2. fn 体复用 ANF:提升拥有形 `CoreBodyStorage`,flow/workflow/fn 三处内
   聚,lowerer 的 `CoreBodyStorageRef`/`ExprLowerer` 直接复用,不新增第二
   套 body 模型。
3. 闭包值用**专用** `CoreClosureExpr`(Principle 4/1),不扩展
   `CoreConstructExpr`(nominal/字段身份模型与闭包的位置槽模型不同构)。
4. 捕获仅 ByValue(固化 evaluator 深快照语义);显式表按源码序,隐式捕获
   由新确定性 first-use DFS 产生;无 ByRef、无 partial application。
5. 纯静态直调(名字解析到具体 Fn 实例)= `CoreCallExpr`(expr,`call`);
   **一切一等可调用值调用**(FnT 形参/本地绑定/零捕获 fn 引用/捕获
   lambda)= `CoreCallClosureExpr`(expr,`call_indirect`);capability 调用
   继续是 ordered stmt;含效果 callee 推迟到 FB-4 的 `CoreCallStmt`。
6. 无循环语法,迭代是 state goto;递归是唯一重复机制,用原生 wasm
   `call`,FB-1 无环、FB-2 起有 §8.1-6 的 SCC 秩参数静态深度预算(不读
   decreases);不用 tail-call。
7. Table(4)/Element(9)随 FB-3 引入,段位固定(4 在 Function/Memory 间,
   9 在 Export/Code 间),零闭包模块整体省略两段、字节恒等;闭包
   func_index 字是稠密 table 下标而非 wasm funcidx。
8. 跨 fn 边界限单词 P6 值,多字值随 P6-7 帧决策解除,不发明私帧。
9. 本设计不改变 committed HEAD 的 8 例 conformance 的 5 runnable/3
   blocked 数字(3 个阻塞例闸门是 frame/packaging,committed 仓内 lambda
   用例为 0;工作树在飞的 10 个 p6 例以其自己的提交计);它解锁 23 个 fn /
   9 个 lambda integration 程序与 stdlib 泛型体进入 WASM 差分,是
   KR6.7/KR6.8 的前置。
10. **(rev 2)**可调用值物理表示统一为八字节 closure 形:删除四字节
    `CoreLayoutFnRef`,`CoreVtFn` 退为纯签名型,`CoreVtClosure{S,caps}`
    是 `CoreVtFn{S}` 的结构子类型(零运行时适配);静态 fn 名当值即零捕获
    `CoreClosureExpr`。
11. **(rev 2)**嵌套 lambda 经 FB-3 确定性 lambda-lift 成单态 Fn 实例/
    `CoreFnDecl`(env=捕获、统一 env 首参、SSA 改写),递归 lambda
    fail-closed。
12. **(rev 2)**闭包 env 从运行时 checked bump heap(`alloc`)动态分配,
    不用 P6-4 编译期静态 scratch;FB-3 前把 agent heap_base 重定位到全部
    预留帧/backing 区之后(镜像 workflow `compute_event_layout`),env
    容量 = 静态深度上界 × env layout 大小,并入单页 RESOURCE 单一闸门。
13. **(rev 2)**fn concrete 签名由实例 dispatch_types(类型实参)经 P2d
    Substs 替换 base `FnTypeInfo` 泛型签名得到;dispatch_types 本身不是
    签名,不新增持久签名字段。
