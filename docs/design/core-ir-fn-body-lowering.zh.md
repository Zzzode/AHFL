# Core-IR Fn-Body & Closure-Value Lowering — Design

> Status: **DRAFT rev 1** (RFC 0026 P6 / KR6.6 子片设计门;文档编号
> CORE-FNBODY-DESIGN)。本文只做决策,不含开放问题;无生产代码。落地按 §8
> 的 FB-1 → FB-5 实现阶梯逐片交付,每片一个可评审 Conventional Commit。
> 上游事实锚点均为当前 `develop` HEAD(`11e57d32`)源码 `file:line`。

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
6. WASM 编码器没有 Table(4)/Element(9) 段:段常量仅有
   Type/Import/Function/Memory/Global/Export/Code
   (`src/compiler/backends/infra/core_wasm_codegen.cpp:114-121`),
   `encode_module` 也只发射这七段(`core_wasm_codegen.cpp:5174-5305`)。
   opcode 有 `call`(`kOpCall = 0x10`,`:163`)但无 `call_indirect`
   (0x11)。函数索引空间 = 可选 capability import 占低位 + 7 个固定 ABI
   函数(alloc/dealloc/current_state/is_final/step/run/run2,
   `:240-242`)+ P6-2 起的 computed handler(`kDefinedHandlerBase = 7`,
   `:246`;`FunctionTable` 投影规则 `:361-394`)。

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
  f64 值(与现有 `RawP6Frame` 门控同一界线,见 §6.4)。

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
    /// dispatch descriptor(concrete params/ret)从该实例的 dispatch_types
    /// 经 CoreVtFn 读取,本 decl 不复制签名(无第二份签名 SSOT)。
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
  all_definitions/visible 中,类型为 signature 具体化后的
  `CoreValueTypeId`(从 `CoreInstanceDecl::dispatch_types` 的 Fn 形读取,
  不重新派生)。

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
/// result_type 为 interned CoreVtClosure{ signature=该 fn 实例的 CoreVtFn,
/// captures = 每个捕获值的逻辑类型 + ByValue },与 captures 同槽序。
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
5. **本地闭包值调用(新)**:`callee` 是一个解析为本地 SSA 值(其逻辑类型
   为 `CoreVtClosure`)的标识符时,lower 为 `CoreCallClosureExpr`
   (evaluator 的对应分派:`ctx.get_local(callee)` 命中 `CallableValue`,
   `evaluator.cpp:1776-1799`)。
6. 其余:维持 fail-closed(原型无体、未解析、intrinsic 未覆盖)。

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
  `core_wasm_codegen.cpp:163`);编译期唯一目标,无间接分发。
- arity/类型完全静态:verifier 用 callee 实例 `dispatch_types` 物化的
  `CoreVtFn` 逐参核对并确定 result_type。

### 5.2 `CoreCallClosureExpr`:闭包值间接调用

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
  **FB-4** 引入有序语句形态的 `CoreCallStmt { result, callee, args }`
  (与 `CoreCapabilityCallStmt` 同构的 ordered statement + resume
  checkpoint),codegen 为 outlined 效果函数的 `call`,其内部 cap-call 的
  不重放/恢复语义随 wire checkpoint 框架一并落地。把它放在 FB-4 是因为
  pending 结果的帧/恢复身份依赖 P6-7 与 B2 系列的帧决策,提前做只会产生
  第二套帧 SSOT。
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
  `std/collections.ahfl:120-130`,decreases =
  `list_raw_length(source) - source_i`)。
- WASM 上递归用**原生 `call`**(MVP 即支持函数自调用,无需
  return_call/tail-call 提案);不做 state-thunk trampoline。上界由 §8
  FB-2 的编译期深度预算 + P4-D bounded capacity 给出(列表容量编译期已知,
  `std/collections.ahfl:56` 的固定 8 元 `list_from_array` 即典型有界形)。
- 不使用 `return_call`(0x12):MVP 引擎兼容优先;单层 wasm 调用栈深度由
  预算静态封死(§8 FB-2),不依赖尾调用优化保栈。

## 6. WASM codegen

### 6.1 函数索引空间扩展(在现有 PROJECT 规则上追加,不改写既有字节)

现状:import 占低位 → 固定 7 ABI 函数(import_count+0..6)→ P6-2
computed handler(import_count+7..,`core_wasm_codegen.cpp:240-246`,
`:361-394`)。**保持 E1-E3/P6 既有模块字节不变**的前提下追加:

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
  `handler(index)` 同形);workflow 模块的 `WorkflowFunctionTable`
  (`:396-423`)在其 runner/run/run2 之后做相同追加。
- fn 函数的 codegen 复用 P6 的 scalar/aggregate/coerce/match emitter
  (`emit_expr`,`core_wasm_codegen.cpp:3085-3124`;fn 体是第三类 body owner,
  与 agent plan 共享 emitter,不写第二套表达式编译器)。
- Reachability:只发射从入口 handler / workflow region 的直接调用闭包可达
 的 fn(确定性定点),与当前 capability import reachability 同一纪律
 (`WorkflowPlan::imports` 注释,`:353-358`)。

### 6.2 Table(4)/Element(9)——标记为后续 codegen 片(FB-3)

编码器当前七段不含 Table(4)/Element(9)
(`core_wasm_codegen.cpp:114-121`,段 id 4 与 9 无常量,
`encode_module` 无发射)。FB-3 决策:

- 恰好声明 **1 个** `funcref` table,`min = N`(N = 可达的、被取闭包值的
  fn 数;零闭包模块不发射该段,E1-E3/P6 字节继续逐字节不变)。
- Element(9)段以 active element 初始化 `table[0..N)` 到对应 wasm 函数
  索引;段序按 wasm 规范置于 Code(10)之前(4 在 Memory 前后的规范位置;
  实现按规范固定)。
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
  bytes、f64 跨 fn 边界 fail-closed——与现有
  `CoreWasmFrameContract::RawP6Frame` / p6-7 门控同界
  (`core_wasm_codegen.hpp:92-103`;PtrLen/f64 leaf 拒绝
  `core_wasm_codegen.cpp:3160-3166`)。多字值跨边界在 P6-7 帧决策后统一
  以"地址化"解除,不在此发明私帧。
- env 内存:env 聚合从现有 checked bump 分配器分配(与 P6-4 construct
  scratch 同一分配器,区域常量 `kP6AggregateScratchBase`,
  `core_wasm_abi_constants.hpp:56`),按 `CoreLayoutClosure.environment`
  的槽偏移写入捕获值;env 生命周期为模块实例级(arena,不回收——RFC
  0026 Q1 已定无 GC)。单页预算由现有 RESOURCE 类 fail-closed 统一兜底
  (scratch/backing capacity,`core_wasm_abi_constants.hpp:76-106`),闭包
  env 计入 scratch/堆预算。
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
(`docs/rfcs/0026...:282` P8;roadmap KR6.8
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

事实清点(当前树实测):

- conformance 清单 `tests/conformance/cases/` 共 **18** 例:**15** 例
  `orchestration` 可发射,**3** 例标 `computation`
  (`if_let_e2e`、`enum_variant_e2e`、`e2e_multi_agent`,机器钉死于
  `tests/unit/runtime/conformance/wasm_eligibility_test.cpp:211-232`)。
  这 3 例的阻塞码是 `wasm.UNSUPPORTED_WORKFLOW_FRAME` /
  `wasm.UNSUPPORTED_CAPABILITY_FRAME`,阻塞构造是 workflow 多 agent
  packaging 的 computed-goto/frame 限制
  (`core_wasm_codegen.hpp:92-103`、`core_wasm_codegen.cpp:4205-4209`),
  **不是** fn/闭包。
- `tests/golden/` 共 **117** 个 `.ahfl`:**lambda 0 个**;含 `fn` 声明的
  仅 3 个,且都是 resolver/typecheck **负向**用例(不执行)。
- `tests/integration/` + `examples/` 共 **107** 个 `.ahfl`:含 fn 体的
  **23**、含 lambda 的 **9**、含方法调用的 **9**。
- stdlib(`std/*.ahfl`)大量能力以泛型 fn/impl 方法承载
  (`std/collections.ahfl:98-130` 的有体构造器/递归 helper;`:16-89`
  的 raw/库 fn 原型),是 fn-body lowering 落地后 WASM lane 的主要新增
  可执行面。

**结论(诚实,不夸大):**

- 本设计落地**不直接**改变 18 例 conformance 的 15/18 数字——3 个阻塞
  例的闸门在 frame/packaging,不在 fn/闭包;没有任何当前 committed
  conformance 例使用 lambda(0)。
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
   路径);result_type == 签名 ret;callee 体必须无
   `CoreCapabilityCallStmt`(新码 `FN_CALL_EFFECTFUL_CALLEE`,FB-4 前
   拒绝,FB-4 改为只允许经 `CoreCallStmt`)。
4. **闭包构造合法**:`CoreClosureExpr.fn` 在表内;captures 个数/类型与该
   fn 闭包类型 `CoreVtClosure.captures` 逐槽相等;每个捕获 SSA 值在构造
   点可见、跨体不直接引用外层 SSA;捕获槽序与 P4-D env 间接边物化结果一
   致(新码 `CLOSURE_CAPTURE_ARITY`/`CLOSURE_CAPTURE_TYPE`/
   `CLOSURE_CAPTURE_ORDER`)。
5. **闭包调用 dispatch 类型**:`CoreCallClosureExpr.closure` 是
   `CoreVtClosure` 值;arity 与其内嵌 `CoreVtFn` 精确匹配;实参/结果类型
   逐结构相符(保证 wasm `call_indirect` 期望类型不会 trap;新码
   `CLOSURE_DISPATCH_ARITY`/`CLOSURE_DISPATCH_TYPE`)。
6. **调用图有界(FB-1)**:`CoreCallExpr` 直接调用图无环(新码
   `FN_RECURSION`,FB-1 拒绝一切递归);FB-2 放开为"有静态深度预算的有界
   递归":自递归/互斥递归必须由 verifier 从 bounded capacity(P4-D
   container capacity/refinement bounds)推出调用深度上界,超出预算/无法
   推出者 fail-closed(`FN_RECURSION_UNBOUNDED`)。**不**信任、也不读取
   decreases 项(已擦除)。
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

### 8.3 擦除语义(明确记录,非隐式丢失)

| AHFL-IR 事实 | Core 处置 |
| --- | --- |
| `FnEffectClause.kind` / capability 列表 / `decreases`(`decl.hpp:385-398`) | 不进 `CoreFnDecl`;Pure 直调成 expr,效果 callee 成 FB-4 语句;终止上界由 §8.1-6 结构预算重述 |
| `FnTypeInfo` 的 type-param 名 / typed body | 单态化后消失;每实参组一份 `CoreFnDecl`,无类型参数(复用 P2d body 实例) |
| lambda 源参数名 / capture 名 | 仅 provenance;身份是 SSA 值与槽下标 |
| 契约运行时检查 | 若需要,只能作为普通 assert+分支(沿用 RFC 0026 降级步骤 3) |
| 闭包 effect grade | `CoreVtFn` 已构造时擦除;闭包 Pure-only 由 typecheck 与 Core 双侧保证 |

### 8.4 实现阶梯(每片独立可测、Conventional Commit)

- **FB-1 直调 fn 体(首个实现片)**:`CoreFnId`/`CoreFnDecl`/
  `CoreProgram::fns`;`CoreBodyStorage` 提升与 flow/workflow 内聚重构;
  保证有体 FnDecl 的实例+fn 表 pass;`CoreCallExpr` 纯直调(call opcode、
  import+7+H 之后函数索引、单词调用约定 §6.3);MethodCallExpr receiver-
  first 解析;`OwnerKind::Fn` 验证 + §8.1-1/2/3/7 + 无环调用图
  (§8.1-6 的 FB-1 形态)。LambdaExpr 仍 unsupported。
- **FB-2 有界原生递归**:verifier 深度预算(§8.1-6),std
  `list_copy_into` 式 decreases 有界递归在 wasm 原生 `call` 下可跑;预算
  与 P4-D capacity/refinement 对齐的 RESOURCE fail-closed。
- **FB-3 闭包值 + 间接调用**:`CoreClosureExpr`/
  `CoreCallClosureExpr`;隐式捕获 DFS 分析(§3.2);`CoreVtClosure` 由
  lowerer 实际 intern(消费 P6-8a env 布局);Table(4)/Element(9)
  编码器 + env 首参约定 + `call_indirect`;零闭包模块字节不变;
  §8.1-4/5 验证。
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
5. 纯静态直调 = `CoreCallExpr`(expr,`call`);闭包值调用 =
   `CoreCallClosureExpr`(expr,`call_indirect`);capability 调用继续是
   ordered stmt;含效果 callee 推迟到 FB-4 的 `CoreCallStmt`。
6. 无循环语法,迭代是 state goto;递归是唯一重复机制,用原生 wasm
   `call`,FB-1 无环、FB-2 起有静态深度预算;不用 tail-call。
7. Table(4)/Element(9)随 FB-3 引入,零闭包模块保持字节恒等;闭包
   func_index 字是稠密 table 下标而非 wasm funcidx。
8. 跨 fn 边界限单词 P6 值,多字值随 P6-7 帧决策解除,不发明私帧。
9. 本设计不改变当前 18 例 conformance 的 15/18 数字(3 个阻塞例的闸门是
   frame/packaging,仓内 lambda 用例为 0);它解锁 23 个 fn / 9 个 lambda
   integration 程序与 stdlib 泛型体进入 WASM 差分,是 KR6.7/KR6.8 的前置。
