# Core-IR P4 Coercion: `CoreCoerceExpr` + proof-plan tree — 窄设计 (rev 4, APPROVED)

> RFC 0026 P4 后续片。rev 1(Core 端 classify=第二套 subtype engine)、rev 2(Composite 不自洽 / variance 无桥 / witness lifetime / Identity re-label / Fn·contra 无证明 / IR verifier 缺失)、rev 3(VariantToEnum 漏 / Visiting cycle / 冗余 variance 字段 / op 唯一性+完整 IR 校验 / real·synthetic builtin 分流)均 Request-changed。
> **rev 4 = Codex 终审 APPROVED,可进入实现。** 首个真实 consumer = annotated `let x: T = e`。两 commit review series。
> 参考:Rust MIR `Rvalue::Cast` 层次 / Swift SIL cast。

## 0. 已锁定不变(Codex approved,不再动)

- Sema-owned witness(关系求解产出,不在 Core 重判 subtyping)。
- statement-boundary fact(挂 `TypedStatement`,非通用 `TypedExpr` 字段)。
- Core 产 **fresh** CoreValueId(result_type=target),source id/类型不动。
- physical 语义 layout-dependent(P4-D 才判 no-op vs 实体转换,本 RFC 不声明 no-op)。
- `collect_expr_uses` 收 CoreCoerceExpr.operand。
- 两 commit review series。

## 1. Proof-plan tree(P0-1 定案:tree 非 sequence)

一个 plan node 拥有完整 source/result 对;多个 op 是**正交分量**;所有未被 op 命名的维度必须相等。**无 invented 中间类型。**

### 1.1 Core 侧(`core_ir.hpp`)

```cpp
enum class CoreCoercionOpKind { IntWiden, StringWiden, CapacityWiden, TypeArg, FnParam, FnReturn };
struct CoreCoercionOp {
    CoreCoercionOpKind kind{};
    std::uint32_t arg_index{0};              // TypeArg / FnParam:第几个投影位置(其它 kind 忽略)
    CoreCoercionPlanId child{};              // TypeArg/FnParam/FnReturn:子 plan(证明该投影)
    // IntWiden/StringWiden/CapacityWiden 无 child(叶 op,只验本 node 的对应投影)
    // 注(P1-1):TypeArg 无 variance 字段——方向由 verifier 从 CoreTypeDecl.variances[arg_index]
    // 决定(唯一 SSOT,不冗余存以免漂移)。
};
struct CoreCoercionPlanNode {
    CoreValueTypeId source{};                 // 该 node 覆盖的完整 source 值类型
    CoreValueTypeId result{};                 // 该 node 覆盖的完整 target 值类型
    std::vector<CoreCoercionOp> ops;          // 正交分量,canonical 顺序(见 §1.3)
};
// per-body flat arena: std::vector<CoreCoercionPlanNode> coercion_plans;(CoreFlowDecl/CoreWorkflowDecl)
struct CoreCoerceExpr {
    CoreValueId operand{};                    // 被提升的源值
    CoreCoercionPlanId plan{};                // 进 coercion_plans arena 的 root node
    [[nodiscard]] friend bool operator==(...) = default;
};
```

- `CoreExpr.result_type` = plan root 的 `result`;`body.value_types[operand]` 必须 == plan root 的 `source`。
- **op 语义(每个只验一个投影,其余维度必等)**:
  - `IntWiden`:node.source/result 都 CoreVtInt,source.bounds ⊆ result.bounds(result 无 bounds=全集)。node 无其它 op。
  - `StringWiden`:CoreVtString,length_bounds ⊆。
  - `CapacityWiden`:同 base CoreVtNominal、role∈{List,Set,Map},**仅** capacity 投影变(source.capacity ⊆ result.capacity)。元素/参数变化不归 CapacityWiden——由同 node 的 TypeArg op 负责;**未被任何 TypeArg 命名的 arg 位置必须 source==result**(见 List probe 解法)。
  - `TypeArg{index, child}`:node.source/result 同 base CoreVtNominal;child plan 的 source/result == source/result 在 args[index] 的类型;**方向由 verifier 从 `CoreTypeDecl.variances[index]` 读取**(§1.2);**其余 arg 位置 + capacity 必须相等**(未被本 op 命名)。
  - `FnParam{index, child}` / `FnReturn{child}`:node.source/result 都 CoreVtFn;见 §1.2。
- **List probe = 一个 root node**:`source=List<Int(0,0)>(4)`,`result=List<Int>(8)`,`ops=[ CapacityWiden, TypeArg{0, child=IntWiden-plan(Int(0,0)->Int)} ]`。CapacityWiden 只验 capacity 4⊆8 且 args 相等——但 args 不相等!**解法**:CapacityWiden 的"args 相等"改为"**除被同 node 其它 TypeArg op 命名的位置外**,args 相等"。即:node 级校验 = 收集所有 TypeArg 命名的 arg index 集合 S;CapacityWiden 要求 arg∈S 的位置由对应 TypeArg 验、arg∉S 的位置 source==result;capacity 4⊆8。这样 capacity+element 在**同一 node** 正交共存,无中间类型。(§4 verifier 精确表述。)

### 1.2 方向(variance,含 Fn — P0-2/P1-1 定案:方向由 CoreTypeDecl.variances 决定,不冗余存)

TypeArg 的方向由 verifier 从被指 nominal 的 `CoreTypeDecl.variances[arg_index]` 读取(唯一 SSOT):
- variance == `Covariant`:child 证 `arg_source <: arg_target`(child.source=arg_source, child.result=arg_target)。
- variance == `Contravariant`:child 证 `arg_target <: arg_source`(**方向反转**:child.source=arg_target, child.result=arg_source)。verifier 精确锁 child 的 source/result 方向,不得当普通 covariant。
- variance == `Invariant`:该位置**不得**有 TypeArg op(必须 source==result);出现即 `COERCION_VARIANCE_INVALID`。故合法 TypeArg 只落 Covariant/Contravariant 位置。
- `FnParam{index, child}`:Fn 参数**逆变**——child 证 `target.param[index] <: source.param[index]`(child.source=target 的该 param, child.result=source 的该 param)。
- `FnReturn{child}`:返回**协变**——child 证 `source.ret <: target.ret`。
- Fn 其余 param(未被 FnParam 命名)source==target;param 数必须相等。

### 1.3 canonical 顺序

node.ops 的顺序 = capacity 先、再 TypeArg 按 arg index 升序;Fn 的 FnParam 按 index 升序、FnReturn 最后。lower 桥接保序,verifier 不重排(但校验不依赖顺序——ops 是正交集合,顺序仅为确定性/序列化稳定)。

## 2. Witness lifetime(P0-3 定案:statement-owned 自包含 arena;P0-2 定案:Visiting fail-closed)

- Sema 关系求解可建**临时 memoized proof DAG**,但**返回前必须 materialize** 一个自包含、reachable、`TypedStatement` 拥有的 flat plan arena:
  ```cpp
  enum class TypedAdjustmentOpKind {
      IntWiden, StringWiden, CapacityWiden, TypeArg, FnParam, FnReturn,
      VariantToEnum,   // P0-1:EnumVariantT <: EnumT(见下)
  };
  struct TypedAdjustmentOp { TypedAdjustmentOpKind kind; uint32 arg_index; uint32 child; };
  // 注:无 variance 字段(P1-1)——方向在 Core verifier 从 CoreTypeDecl.variances 读;
  //     Typed 侧 TypeArg 的方向由 Sema witness 构造时已按推断 variance 决定 child 方向。
  struct TypedAdjustmentNode { TypePtr source; TypePtr target; vector<TypedAdjustmentOp> ops; };
  struct TypedAdjustmentPlan {
      TypePtr source; TypePtr target;
      vector<TypedAdjustmentNode> nodes;   // flat arena,index 引用
      uint32 root;                          // nodes 内的 root index
  };
  ```
- **P0-1 `VariantToEnum` op**:`subtype_impl` 有独立 accepted 规则 `EnumVariantT <: EnumT`(`type_relations.cpp:779-807`),不经 constructor expected-type override 也可达:
  ```
  let v = Maybe::Some(input.id);   // inferred binding 保持 EnumVariantT
  let m: Maybe = v;                // path source 是 variant,target 是 owner enum
  ```
  第一个值 Core-lowerable(P4-A 已把 EnumVariant TypeRef 归一到 owner `CoreVtNominal`)。故第二个边界 **Core 无需物理 coercion**,但 AHFL-IR 层 initializer TypeRef 仍带 `variant_name`,`type_refs_equal` 判其 != target enum。加 `VariantToEnum`(同 resolved nominal id + args 逐位置 equivalent + 变体真实存在):Typed→AHFL 保留为语义 provenance(IR 边界类型不同,不能缺 plan 否则 §7 拒);**AHFL→Core normalization 时擦除**(两端 intern 到**同一** CoreValueTypeId),elision 在 §3 P0-4 规则下合法(Core source_ty==target_ty)。**此 op 不进 `CoreCoercionOp`**(与 effect-only 同类,是 IR 边界的语义证明、Core 无对应转换)。加两-let real frontend probe。
- **P0-2 Visiting/coinductive(定死,不留开放)**:witness-producing assignability query **在 `Visiting` 命中时 fail-closed**(不走 coinductive assumption),因为 Typed/AHFL/Core plan verifier 都要求 3-color 无环,真正 revisit 会产 cyclic witness,序列化/verifier 必拒——placeholder-finalize 解决不了 final-plan 无环要求。本片 let 用到的 source Type 图是**有限的**结构 args / Fn params / ret(subtype **不**遍历 nominal 字段递归),故 witness query 不会 revisit;文档 + probe 明确这点。普通 bool relation 行为**不变**(其它分析若依赖 coinduction 照旧)。将来若有 equirecursive coercion,单独设计显式有限 `RecursiveAssumption` proof node,**不序列化 cyclic plan**。
- **memo 结果携 witness**:`RelationDecision { bool accepted; TypedAdjustmentNodeId witness; }`(witness 指向**本次 materialize 的 arena**,不是 solver-local id);memo entry `{RelationState state; TypedAdjustmentNodeId witness;}`。Proven cache hit 连 witness 一起 return。public `is_subtype_of`/`is_assignable_to` 只投影 `.accepted`(现有 caller 不变);annotated-let caller 取 witness + materialize 进 statement。若全返回类型重构过大 → 抽 `BoolPolicy`/`WitnessPolicy` 复用**同一组 rule case 定义**,不留两套遍历。
- **纯 equiv/identity 子树**:witness node 无 op(Identity)。materialize 时保留结构位置(证明是哪一维,虽无 op),但 lower 到 Core 时规范化掉(§3)。

## 3. Core lower(P0-4 定案:绝不 re-label)

`FlowLowerer::lower_let`:
```
value       = lower_value(initializer)               // actual 值 + interned 类型 A(source)
source_ty   = value_type_of(value)
target_ty   = intern_value_type(s.type_ref)          // T
if !s.adjustment:
    if source_ty == target_ty:  scope[name] = {value, source_ty}   // 精确/inferred,无 coercion
    else:                       error(core.MISSING_ADJUSTMENT); 不可执行   // 缺 plan 但类型不同 = fail closed
else:
    plan_root = lower_adjustment_plan(s.adjustment)  // 镜像 ir plan -> CoreCoercionPlanNode arena,intern 每层类型
    # 规范化:一个 node 的所有 op 递归后若无任何真实转换(全 Identity 子树)-> 该 node 是 no-op
    if normalized_plan_is_identity(plan_root):
        # 仅当 interned Core source_ty == target_ty 才允许 elide(P0-4 死规则)
        if source_ty == target_ty:  scope[name] = {value, source_ty}
        else:                       error(core.INVALID_COERCION); 不可执行   # 空 plan 但 Core id 不同
    else:
        coerced = bind_pure(CoreCoerceExpr{value, plan_root}, target_ty, range)  // 新 id
        scope[name] = {coerced, target_ty}
```

- **P0-4 死规则(Codex 点名)**:任何 node 只有在 **interned Core source_ty == target_ty** 时才可 elide,elide 时绑 `{value, source_ty}`(不改 value_types[value])。**绝不**在 source_ty != target_ty 时把已有 value id 绑到 target_ty(那正是本片要消灭的 re-label bug,会让 LocalBinding 与 dense SSA 表成两 SSOT)。
- **MISSING_ADJUSTMENT**:`s.adjustment` 缺失但 `source_ty != target_ty` → fail closed,绝不当 inferred/exact。
- `lower_adjustment_plan` 纯镜像+intern+Identity 规范化,不判 subtyping。
- 核心不变量:`let y: Int = x`(x bounded Int)→ `%y = coerce %x`,`%x` id/`value_types[%x]` 不变(bind_pure 产新 id)。

## 4. Core verifier(`core_verify.cpp`)

1. **operand 收集(P1)**:`collect_expr_uses`(1130-1150)**加 CoreCoerceExpr → 收 operand**;`verify_expr_arena` 加 CoreCoerceExpr 分支查 operand 在界。
2. **plan arena**:每个 CoreCoercionPlanId 在界;plan 图 3-color 无环(TypeArg/Fn* child 引用);op.arg_index 在被指 nominal arity / Fn param 数内。
3. **每 node source/result 有效**:在界/非 Never。
4. **CoreExpr.result_type == root.result;value_types[operand] == root.source。**
5. **node 级正交校验**(不重跑 Sema,只结构验;据持久化 `CoreTypeDecl.variances`):
   - **op 唯一性(P1-2)**:同 node 拒重复维度声明——至多一个 CapacityWiden、每个 index 至多一个 TypeArg、每个 index 至多一个 FnParam、至多一个 FnReturn;标量 node(Int/String)在非-identity 时恰好一个匹配的标量 op。否则两个 op 可对同一维度歧义命名而 unchanged-dimension 检查仍通过 → `COERCION_KIND_MISMATCH`。
   - 收集 node.ops:标量 op(IntWiden/StringWiden/CapacityWiden)+ 投影 op(TypeArg/FnParam/FnReturn)。
   - **同 node 至多一个标量 op**(Int/String 二选一,或 CapacityWiden;不能既 IntWiden 又是 Nominal)。
   - 每 op 校验自己那一维;**未被任何 op 命名的维度必须 source==result**:
     - CoreVtInt node:IntWiden 验 bounds⊆;无 op 则 source==result(但那样 node 是 Identity,不该出现在非规范化 plan)。
     - CoreVtNominal node:CapacityWiden 验 capacity(role∈{List,Set,Map});每 TypeArg{i} 验 args[i](按该位置 `CoreTypeDecl.variances[i]` 的 variance,方向见 §1.2);**未被 TypeArg 命名的 arg 位置 + (无 CapacityWiden 时的)capacity 必须相等**;base 必须相等。
     - CoreVtFn node:每 FnParam{i} 逆变验、FnReturn 协变验;未命名 param source==target;param 数相等。
   - 方向与 `CoreTypeDecl.variances[arg_index]` 不符(如对 Invariant 位置发 TypeArg,或 Map key 位置有 TypeArg)→ `COERCION_VARIANCE_INVALID`。
   - op kind 与 node 形状不符(IntWiden 但 node 是 Nominal)→ `COERCION_KIND_MISMATCH`;关系方向/bounds 非法 → `COERCION_INVALID`。
6. **拒恒等**:规范化后的 CoreCoerceExpr 的 root plan 必含至少一个真实转换 op(直接或经 child);全 Identity → `COERCION_IDENTITY`(lower 不该产)。
7. **Map**:key 位置 variances==Invariant → 该位置无 TypeArg(args 相等);value 位置 Covariant。据 `CoreTypeDecl.variances` 锁。

新码:`core.MISSING_ADJUSTMENT` / `core.INVALID_COERCION`(lower);`core.verify.COERCION_INVALID` / `COERCION_KIND_MISMATCH` / `COERCION_VARIANCE_INVALID` / `COERCION_IDENTITY`(verify)。

## 5. Variance 声明桥(P0-2 定案:必须现在补,唯一 SSOT 物化)

Codex 锁:把推断结果**物化成唯一 SSOT**,不再开第二条 query。

1. **Sema 物化**:`StructTypeInfo`/`EnumTypeInfo`(`declaration_info.hpp:94/144`)加声明序 `std::vector<Variance> type_param_variances`(与 `type_param_names` 平行)。
2. **一次性计算**:`environment` 建完后一次性算 nominal variance table(内部按 SymbolId 存;canonical 只留现有 Sema 兼容入口),**同一张表**同时:(a) 喂给 TypeRelationContext 的 variance provider(替换现在临时 closure cache——provider 从物化表查),(b) 写回 TypedProgram declaration payload/update 的 `type_param_variances`。**实现稿需明确 declaration_updates 的写回时点**(DeclarationSema 复制 payload 在 provider 安装前,故物化必须在 environment 完成后、且写回 declaration payload,不能指望 lower 时查 provider)。
3. **AHFL-IR 桥**:`ir::StructDecl`/`ir::EnumDecl`(`decl.hpp:69/98`)各加 `std::uint32_t type_param_count` + `std::vector<ir::Variance> type_param_variances`(数值/vector,**无 canonical-name lookup**;镜像 FnDecl/TraitDecl 已有 type_param_names 的先例)。`typed_hir_lower::lower_typed_struct`(3385)/`lower_typed_enum`(3410)从 TypedDecl payload 投影 count + variance(现在 drop 了)。
4. **sweep**(调研已列点):`ir_json.cpp` struct/enum serialize(1508-1552 / 1553-)+ deserialize(3081-3098 / enum_decl)加性 r/w;`verify.cpp` BackendReady verify_decl(255-263 struct / 265- enum)锁 `type_param_variances.size()==type_param_count`;`ir_print.cpp` 可选显示;`ir_equal.cpp` 无 decl 相等(不需);visitor/program_view 标量字段无 expr(不需)。
5. **消费进 Core(P1-3:区分 synthetic vs real vs user)**:`core_lower.cpp` `add_struct`(213)/`add_enum`(230)从 ir decl 填 `CoreTypeDecl.type_param_count` + 新 `CoreTypeDecl.variances`(`std::vector<CoreVariance>`)。`decorate_from_builtin_ssot`(499)**不再 one-size-fits-all**,按来源三分:
   - **synthetic builtin**(`add_builtins()` 造的基座,起始 default arity/variance):直接从 descriptor **stamp** kind/arity/variance/role。
   - **real builtin**(从 AHFL-IR 来的真实 std 声明):要求 incoming metadata(count+variance)与 descriptor **精确相等**,drift → fail-closed。
   - **ordinary user nominal**:原样 consume incoming count/variance(不碰 descriptor)。
   SSOT 加 per-param variance:Option/List/Set=[Cov],Result=[Cov,Cov],Map=[Inv,Cov]。`CoreVariance {Invariant,Covariant,Contravariant}`。
6. **verifier**:`core_verify.cpp` `verify_nominal_role`(184-)锁 stdlib nominal 的 variances == SSOT;所有 nominal 锁 `variances.size()==type_param_count` + 枚举合法。
7. **Result 补进 builtin variance**:两 payload 参数 Covariant(user-enum variance 路径下 Ok/Err payload 协变)。

这是 **P4-C 的窄 metadata 前置**,但对 user-covariant/contravariant scope 是 mandatory(否则 `Box<T>` 被 P4-A arity check 在 coercion 前就拒)。

## 6. Fn effect normalization boundary(P1-1 定案)

- Typed `FnT` 的 effect 参与 subtype(`type_relations.cpp:846`),但 `ir::TypeRef` Fn **擦除 effect**(P4-A 已定:Core-IR 是 effect-lowered 层)。
- **normalization 规则**:**仅 effect 变化**的 witness 在 Typed→AHFL-IR bridge 时擦除。若擦除后 source/target 的 `ir::TypeRef`(及 intern 后的 `CoreValueTypeId`)相等 → **不携 plan、不产 CoreCoerceExpr**(effect-only 关系在 Core 是 identity)。
- param/return 有变化 → 保留结构 plan(FnParam/FnReturn)。
- **这解决 §7 IR 规则冲突**:"equal boundary 上带 plan 必拒" 与 effect-only 合法关系不冲突——effect-only 擦除后边界相等,本就不携 plan。

## 7. AHFL-IR verifier 规则(P1-2 定案,`verify.cpp` / BackendReady)

序列化本身不能让缺 plan fail closed;加 IR 层规则 + 落地文件:
- `ir::LetStatement` 加 `std::optional<ir::AdjustmentPlan> adjustment`(镜像 TypedAdjustmentPlan,TypePtr→TypeRef,flat nodes arena)。
- **plan 缺失合法 iff** `initializer.resolved_type` 在 IR identity/equality 层**等于** `let.type_ref`(用 `type_refs_equal`,id-first)。
- plan present 的 source/target 必**等于**这两个边界类型(source==initializer.resolved_type, target==let.type_ref)。
- plan arena root/children 在界、无环(3-color)、所有 TypeRef resolved(非 Unresolved)。
- **plan present 在 equal 边界(source==target 且无真实 op)→ 拒(identity)**;但 effect-only 已在 bridge 擦除(§6),不会到这。
- **BackendReady 结构完整校验(P1-2,不只 root/child bounds + resolved)**:用 TypeRef + 声明 variance metadata(ir decl 的 type_param_variances)对每 node/op 校验 kind/投影/方向/bounds/coverage——镜像 §4 Core verifier 的规则,但在 AHFL-IR 层跑:op 唯一性(每维度至多一 op)、op kind ↔ node TypeRef 形状、TypeArg 方向 == 声明 variance(逆变 child 方向反转)、未命名维度相等、bounds ⊆。**一个被篡改的 `IntWiden` 挂在 nominal node 上、或方向错的 contravariant child,必须在 AHFL-IR verification 就 fail(Core lowering 之前)**,不能只查 root/child bounds。
- BackendReady 对每个 annotated let 强制上述。
- **wrong/missing plan round-trip 测试**:断言结构相等(round-trip 保真)+ 篡改后 verifier 拒(丢 plan / source-target 不符 / child 越界 / equal 边界带 plan / IntWiden-on-nominal / contravariant child 方向错)。

## 8. 测试矩阵

正例(真实 frontend):
- scalar Int widen `let y: Int = x`(x bounded Int):`%y=coerce %x` root=[IntWiden],**断言 %x id/类型不变**。
- `let x: Int(0,2) = 1`(已有回归)。
- `Option<Int(0,0)> -> Option<Int>`:root=[TypeArg{0,IntWiden}](Option variance[0]==Cov,verifier 从 CoreTypeDecl.variances 读方向)。
- **同时 `List<Int(0,0)>(4) -> List<Int>(8)`**:root=[CapacityWiden, TypeArg{0,IntWiden}](rev 1/2 会回归的 case 现通过)。
- user covariant `Box<T>{value:T}`:`let b: Box<Int> = <Box<Int(0,0)>>` → [TypeArg{0,IntWiden}],CoreTypeDecl.variances[Box]==[Cov]。
- **VariantToEnum(P0-1)**:`let v = Maybe::Some(input.id); let m: Maybe = v;`(两-let)——第二个 let source 是 EnumVariantT、target 是 owner enum。Typed/AHFL 带 VariantToEnum witness op;AHFL→Core 擦除(两端 intern 同一 CoreVtNominal),**不产 CoreCoerceExpr**(Core source_ty==target_ty,§3 P0-4 合法 elide)。断言 lower 干净 + arena 无 CoreCoerceExpr。
- **Fn param 逆变 + return 协变**:capability 返回 `(Int) -> Int(0,0)`,`let f: (Int) -> Int = <that>` → root=[FnReturn{child: Int(0,0)<:Int}](param 无变则无 FnParam;若 param 变则 FnParam child 方向反转,按 §1.2)。
- **user contravariant nominal**(补 arity/variance 桥后):capability 返回 `Sink<Int>`,`let s: Sink<Int(0,0)> = <that>` → [TypeArg{0,child 方向反转}](Sink<T> 的 T 在 Fn param 位 → CoreTypeDecl.variances[Sink]==[Contra],verifier 据此锁 child 反向)。
- effect-only Fn 关系:擦除后边界相等 → **不产 CoreCoerceExpr**(断言 arena 无)。
- 精确/inferred:不产 CoreCoerceExpr。

负例:
- MISSING_ADJUSTMENT(hand-built:source_ty!=target_ty 但无 plan)。
- INVALID_COERCION(空 plan 但 Core id 不同)。
- kind↔形状不符 / IntWiden 非法 bounds / TypeArg 落 Invariant 位(Map key)/ Contravariant 位 child 方向错(当 covariant 验)/ 恒等 plan / operand use-before-def / operand 越界 / 同 node 重复维度 op(两 CapacityWiden、两 TypeArg 同 index)。
- AHFL-IR(P1-2 结构校验):丢 plan(边界不等仍缺)/ source-target 不符 / child 越界 / equal 边界带非-identity plan / **IntWiden 挂 nominal node** / **contravariant child 方向错**;round-trip 篡改后 verifier 拒。
- **Visiting fail-closed(P0-2)**:若 witness query 命中 Visiting(本片 let 目标类型有限、理论不触发)→ fail-closed 而非产 cyclic plan(hand-built/内部一致性 probe)。
- numeric widening(opt-in off by default):首片不产(Sema 默认不开);若 hand-built 出现 NumericWiden op → Core 首片 fail-closed(defer,显式)。

## 9. 首片 Core-valid op 集 + 无回归证明

- **首片 Core-valid**:IntWiden, StringWiden, CapacityWiden, TypeArg(方向由声明 variance 决定,合法只落 Covariant/Contravariant 位;Invariant 位无 TypeArg), FnParam, FnReturn。覆盖 scalar refinement / string / capacity / 容器协变 / user 协变+逆变 / Fn 逆变+协变。VariantToEnum 是 Typed/AHFL 层 witness op(Core 擦除,不进 CoreCoercionOp)。
- **defer(显式,fail-closed 非静默)**:NumericWiden(Int→Float/Decimal,Sema `allow_numeric_widening` 默认 off,非默认合法 let 不产;hand-built 出现即拒)。
- **无回归证明**:Codex 已确认 Fn/contravariant + EnumVariant<:Enum 能到 Core-lowerable annotated let(capability return 任意 resolve、lower_call_value intern resolved_type、P4-A 支持 CoreVtFn + EnumVariant 归一 owner nominal;补 arity/variance 桥后 user generic 也能),故本片**支持它们**而非 defer;唯一 defer 的 NumericWiden 是 option-gated off by default,不属默认合法 let。故首片不回归任何默认合法 annotated let。

## 10. 落地边界(review 后实现,两 commit 同 review series)

**Commit 1(Sema witness + variance SSOT + AHFL-IR bridge)**:
- `type_relations.{hpp,cpp}`:RelationDecision + memo 携 witness + 每规则产 witness(或 BoolPolicy/WitnessPolicy 共享 rule);public API 投影 accepted 不变;materialize 自包含 arena。
- `declaration_info.hpp`:StructTypeInfo/EnumTypeInfo 加 type_param_variances;typecheck 一次性算 variance table + 写回 declaration payload(明确写回时点)+ 喂 provider。
- `typed_hir.hpp`:TypedAdjustmentPlan/Node/Op + TypedStatement::let_adjustment。
- `typecheck.cpp`:annotated-let 成功取 witness→materialize plan;mono(clone_stmt substitute_type plan TypePtr)+ 序列化 write/read(dispatch_target 嵌套 + array 模板)。
- `decl.hpp`:StructDecl/EnumDecl 加 type_param_count + type_param_variances;`expr.hpp` LetStatement::adjustment。
- `typed_hir_lower.cpp`:lower_typed_struct/enum 投影 count+variance;let 下降桥 adjustment(含 §6 effect-only 擦除)。
- `ir_json.cpp`:decl count/variance + LetStatement.adjustment 加性 r/w;`verify.cpp`:decl variance 长度锁 + §7 adjustment IR 规则;`ir_equal.cpp`:LetStatement.adjustment 若纳入 expr 相等(match_patterns 式)。

**Commit 2(Core node + verifier + variance 消费)**:
- `core_ir.hpp`:CoreCoercionOp/Node/PlanId + CoreCoerceExpr + CoreExprNode variant + CoreFlowDecl/CoreWorkflowDecl::coercion_plans + CoreTypeDecl::variances/CoreVariance + SSOT per-param variance + 新码。
- `core_lower.cpp`:add_struct/add_enum 填 type_param_count+variances;decorate_from_builtin_ssot cross-check;lower_adjustment_plan + lower_let 插入点(§3 死规则)。
- `core_verify.cpp`:collect_expr_uses 收 operand + CoreCoerceExpr 校验(§4)+ verify_nominal_role variance cross-check + variances 长度锁。
- 测试:core_lower(真实 frontend 正例 + 不产断言 + %x 不变)、core_verify(hand-built 负例)。

ASCII-clean commit;每 commit roadmap evidence。

## 11. 取舍/被否(累积)

- Core 端 classify(rev1 否):第二套 subtype engine。
- 单 CoreCoercionKind / sequence + 中间类型(rev2 否):改 proof-plan tree,一 node 拥 source/result + 正交 ops。
- side-effect witness / 二次分类(rev2 否):memo 结果携 witness + materialize 自包含 arena。
- canonical-name variance provider in Core / 只 defer variance(rev2/rev3 否):物化唯一 SSOT + AHFL-IR 桥 + CoreTypeDecl.variances 自证。
- 声明 no-op(rev2 否):全 layout-dependent,P4-D 判。
- Identity elide 时 re-label(rev3 否,P0-4):仅 Core source_ty==target_ty 才 elide;否则 MISSING_ADJUSTMENT/INVALID_COERCION fail closed。
- Fn/contravariant defer(rev3 否,P1-1):能到 Core-lowerable let,现在支持。
- numeric widening 首片做:否,option-gated off,defer fail-closed。
- EnumVariant<:Enum 漏(rev3 否,P0-1):加 Typed/AHFL `VariantToEnum` witness op,Core 擦除(两端同 CoreVtNominal);两-let probe。
- Visiting placeholder cycle(rev3 否,P0-2):witness query 命中 Visiting fail-closed(本片 let 类型有限、subtype 不遍历字段递归,不触发);绝不序列化 cyclic plan;equirecursive 将来单独 RecursiveAssumption 节点。
- op 冗余 variance 字段(rev3 否,P1-1):删 CoreCoercionOp/TypedAdjustmentOp 的 variance;方向由 CoreTypeDecl.variances 单一 SSOT 读;合法 TypeArg 只落 Cov/Contra 位。
- op 维度歧义(rev3 否,P1-2):同 node 每维度至多一 op(至多一 CapacityWiden / 每 index 至多一 TypeArg·FnParam / 至多一 FnReturn / 标量 node 恰一标量 op)。
- IR verifier 只查 bounds(rev3 否,P1-2):BackendReady 用 TypeRef + 声明 variance 结构校验 kind/投影/方向/coverage,篡改的 IntWiden-on-nominal / 方向错 contravariant child 在 AHFL-IR 层就拒。
- builtin one-size-fits-all decorate(rev3 否,P1-3):synthetic builtin 从 descriptor stamp;real builtin 精确 match descriptor 否则 fail;user nominal 原样 consume。
