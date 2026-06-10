# AHFL Type Checker 下一阶段架构任务计划

| 项目 | 内容 |
|------|------|
| 文档类型 | plan |
| 状态 | proposed |
| 版本 | v0.1 |
| 目标模块 | `include/ahfl/compiler/semantics/`、`src/compiler/semantics/`、`src/compiler/ir/`、语义 / IR / golden / integration 测试 |
| 关联规范 | [core-language-v0.1.zh.md](../spec/core-language-v0.1.zh.md) |
| 关联设计 | [semantics-architecture-v0.2.zh.md](../design/semantics-architecture-v0.2.zh.md)、[compiler-phase-boundaries-v0.2.zh.md](../design/compiler-phase-boundaries-v0.2.zh.md)、[diagnostics-architecture-v0.2.zh.md](../design/diagnostics-architecture-v0.2.zh.md)、[testing-strategy-v0.5.zh.md](../design/testing-strategy-v0.5.zh.md) |

## 一、背景

当前 AHFL type checker 已经完成了核心现代化：semantic `Type` 以 `types::Payload` 作为唯一真相，类型对象通过 interning canonicalize，类型关系与 IR lowering 已迁移到 payload visitor / accessor，表达式检查和 IR lowering 共用 AST visitor，诊断系统具备 related notes，增量编译已经开始保存 `TypeEnvironment` signature fingerprint，Optional narrowing 也具备保守的 then-block 收窄能力。

从强类型 DSL 编译器角度看，当前实现已经达到较高工业质量。若继续向世界级通用语言编译器架构靠拢，剩余差距主要集中在五个方向：Typed HIR、完整 flow-sensitive typing、类型关系 / constraint 框架、解释型诊断、TypeContext 生命周期与并发模型。

本文档把这五个方向拆成可独立立项、可验收、可渐进落地的 task list。

## 二、总览

| 编号 | 主题 | 优先级 | 推荐阶段 | 目标 |
|---|---:|---:|---:|---|
| T1 | 引入 Typed HIR | P0 | Phase 1-4 | 消除 IR lowering 对 expression type map 的核心依赖，让类型、符号、effect、constness 绑定到 typed 中间表示 |
| T2 | 完整 Flow-Sensitive Typing / CFA-lite | P1 | Phase 2-5 | 把 Optional narrowing 从局部 AST pattern 升级为 facts 驱动的控制流类型精化 |
| T3 | Type Relation / Constraint Framework | P2 | Phase 4-6 | 为泛型、union、effect polymorphism、schema variance 等未来特性预留系统化类型关系框架 |
| T4 | 解释型诊断系统 | P1 | Phase 1-4 | 让核心类型错误能说明 expected / found 的来源，并给出 declaration notes / suggestions |
| T5 | TypeContext 生命周期与并发模型 | P1 | Phase 1-3 | 从进程级 singleton 迈向 session-scoped interning，为并发、daemon、长期运行服务做准备 |

## 三、T1：引入 Typed HIR

### 3.1 背景问题

当前 pipeline 更接近：

```text
AST -> Resolver -> TypeCheckResult + ExpressionTypeInfo map -> IR
```

`ExpressionTypeInfo` 已经通过 `node_id + source_id` 提升了稳定性，但它仍是外部 lookup map。更理想的编译器分层是：

```text
AST -> Resolved AST/HIR -> Typed HIR -> IR
```

Typed HIR 应直接携带 resolved symbol、semantic type、effect、constness、source range、stable node id，使后续 pass 消费 typed tree，而不是通过 range / node id 回查类型。

### 3.2 目标

引入 `TypedProgram` / `TypedDecl` / `TypedExpr` 层，让 type checker 产出正式 typed HIR，并让 IR lowering 逐步迁移到 typed HIR 输入。

### 3.3 子任务

#### T1.1 定义 Typed HIR 数据模型

**建议文件：**

- 新增：`include/ahfl/compiler/semantics/typed_hir.hpp`
- 新增：`src/compiler/semantics/typed_hir.cpp`

**任务内容：**

- 定义 `TypedProgram`、`TypedDecl`、`TypedExpr`、typed path / typed call / typed literal / typed binary / typed member access 等节点。
- 每个 typed expr 必须携带：
  - `SourceRange range`
  - `std::optional<SourceId> source_id`
  - `std::uint64_t node_id`
  - `TypePtr type`
  - `ExprEffect effect`
  - `bool is_pure`
- typed decl 至少携带：
  - declaration kind
  - `SymbolId symbol`
  - source range
  - declaration-level type metadata

**验收标准：**

- Typed HIR 能表达所有当前 `ExprSyntaxKind`。
- 不丢失 source range、source id、node id、type、effect、purity。
- 编译通过。
- 对 typed HIR 的基本构造和 move-only payload 写单元测试。

#### T1.2 TypeCheckResult 增加 typed HIR 输出

**修改文件：**

- `include/ahfl/compiler/semantics/typecheck.hpp`
- `src/compiler/semantics/typecheck.cpp`
- `src/compiler/semantics/typecheck_decls.cpp`

**任务内容：**

- 在 `TypeCheckResult` 中加入 typed HIR 产物，例如：

```cpp
TypedProgram typed_program;
```

- 在 `TypeCheckPass::check_expr()` / declaration 检查路径中同步构建 typed expr / typed decl。
- 保留 `expression_types()` 作为迁移期辅助数据，避免一次性破坏 LSP / debug / lowering。

**验收标准：**

- Typed HIR 与现有 `ExpressionTypeInfo` 对同一表达式记录的 type/effect/purity 一致。
- 新增测试覆盖 literal、path、call、struct literal、list/set/map literal、unary/binary、member/index access。
- 现有 golden 输出不变化。

#### T1.3 IR lowering 支持 Typed HIR 输入

**建议文件：**

- 新增：`include/ahfl/compiler/ir/typed_hir_lower.hpp`
- 新增：`src/compiler/ir/typed_hir_lower.cpp`
- 修改：`src/compiler/ir/ir_lower.cpp`

**任务内容：**

- 新增 lowering 入口：

```cpp
ir::Program lower_typed_program(const TypedProgram& program);
```

- 初期与旧 AST lowering 并存，不直接删除旧路径。
- 对核心表达式、声明、agent、workflow、contract、temporal expression 做等价 lowering。

**验收标准：**

- 对核心 golden case，Typed HIR lowering 与旧 AST lowering 输出一致。
- 增加 property / snapshot test 对比两条 lowering 路径。
- 不改变现有 CLI 默认行为，除非显式启用新路径或完成切换。

#### T1.4 逐步移除 IR lowering 对 expression type lookup 的核心依赖

**修改文件：**

- `src/compiler/ir/ir_lower.cpp`
- `include/ahfl/compiler/semantics/typecheck.hpp`
- LSP / debug 相关读取路径按需调整

**任务内容：**

- 把 IR lowering 中的 `find_expression_type_by_node(...)` / `find_expression_type(...)` 依赖迁移为 typed HIR 节点上直接读取。
- `ExpressionTypeInfo` 降级为 LSP / debug / compatibility 辅助结构。

**验收标准：**

- IR lowering 主路径不再依赖 expression type map。
- `ExpressionTypeInfo` 的职责在注释 / API 中明确为辅助索引。
- 全量测试通过。

### 3.4 T1 总体验收

- Type checker 产出正式 Typed HIR。
- IR lowering 可消费 Typed HIR。
- 旧 golden IR 不变化。
- expression type lookup 不再是 lowering 的核心数据通道。
- 全量测试通过，编译时间无明显回退。

## 四、T2：完整 Flow-Sensitive Typing / CFA-lite

### 4.1 背景问题

当前 Optional narrowing 支持 `x != none` 和 `x != none && y != none`，实现保守且 sound，但仍是局部 AST pattern matching。它不支持对称表达式、else 分支、member path、assignment fact invalidation，也没有统一 facts / flow environment。

### 4.2 目标

引入 `TypeFact` / `FlowFacts` / `ConditionFacts`，将 Optional narrowing 从局部特殊逻辑升级为事实驱动的控制流类型精化。

### 4.3 子任务

#### T2.1 定义 TypeFact / FlowFacts

**建议文件：**

- 新增：`include/ahfl/compiler/semantics/flow_facts.hpp`
- 新增：`src/compiler/semantics/flow_facts.cpp`

**任务内容：**

定义：

```cpp
struct Place {
    std::string root;
    std::vector<std::string> members;
};

enum class TypeFactKind {
    IsNone,
    IsNotNone,
};

struct TypeFact {
    Place place;
    TypeFactKind kind;
    SourceRange origin;
};

struct FlowFacts {
    std::vector<TypeFact> facts;
};
```

**验收标准：**

- 能表达 `x != none`、`ctx.field != none`、`input.field == none`。
- `Place` 支持 equality / hashing 或等价查找。
- 单测覆盖 root-only place、member place、不同 origin 的 fact 合并。

#### T2.2 从条件表达式提取 facts

**建议文件：**

- 新增：`src/compiler/semantics/condition_facts.cpp`
- 新增：`include/ahfl/compiler/semantics/condition_facts.hpp`
- 修改：`src/compiler/semantics/typecheck.cpp`

**任务内容：**

新增：

```cpp
struct ConditionFacts {
    FlowFacts when_true;
    FlowFacts when_false;
};
```

支持以下条件：

- `x != none`
- `none != x`
- `x == none`
- `none == x`
- `(x != none)`
- `x != none && y != none`

**验收标准：**

- `x != none`：true 分支产生 `x IsNotNone`，false 分支产生 `x IsNone`。
- `x == none`：true 分支产生 `x IsNone`，false 分支产生 `x IsNotNone`。
- `&&` 合并 true facts。
- 不确定表达式不产出 facts。
- 单测覆盖对称写法和 group expression。

#### T2.3 把 FlowFacts 应用到 ValueContext

**修改文件：**

- `src/compiler/semantics/typecheck_internal.hpp`
- `src/compiler/semantics/typecheck.cpp`

**任务内容：**

- `ValueContext` 增加 facts 或 narrow override 容器。
- 实现：

```cpp
ValueContext apply_facts(const ValueContext& context, const FlowFacts& facts);
```

- 对 root-only Optional binding，把 `Optional<T>` 收窄成 `T`。

**验收标准：**

- `if x != none { x }` 中 `x` 为 inner type。
- `if none != x { x }` 中 `x` 为 inner type。
- `if x == none { } else { x }` 的 else 分支中 `x` 为 inner type。
- 非 Optional 类型不会被收窄。
- 现有 narrowing case 不回归。

#### T2.4 支持 member-path narrowing

**修改文件：**

- `src/compiler/semantics/typecheck.cpp`
- `src/compiler/semantics/typecheck_internal.hpp`
- `include/ahfl/compiler/semantics/flow_facts.hpp`

**任务内容：**

- 让 `check_path()` consult `FlowFacts` 或 narrow override。
- 支持：

```ahfl
if ctx.foo != none {
    ctx.foo
}
```

- 对 `input.foo`、`ctx.foo` 等 member path 进行 Optional inner type override。

**验收标准：**

- then block 中 member path 被正确收窄。
- unknown path 不 narrow。
- 非 Optional member 不 narrow。
- nested path 可先支持一层，后续再扩展多层。

#### T2.5 引入 assignment fact invalidation 与 CFG-lite statement flow

**修改文件：**

- `src/compiler/semantics/typecheck.cpp`
- `include/ahfl/compiler/semantics/flow_facts.hpp`

**任务内容：**

- assignment 到某个 place 后 kill 对应 place 及其子路径 facts。
- block 顺序传播 facts。
- if then / else 分支各自应用 facts，退出 if 后不默认泄漏分支 facts。
- return 后 facts 不污染后续 outer context。

**验收标准：**

- `ctx.foo != none` 后对 `ctx.foo = none` 的后续访问不再保留 narrowed type。
- facts 不错误泄漏出 if。
- return 后路径 facts 不污染外部 block。
- 全量测试通过。

### 4.4 T2 总体验收

- Optional narrowing 覆盖对称条件和 else 分支。
- 支持 member path narrowing。
- assignment kill 正确。
- 不产生 unsound narrowing。
- pass / fail golden case 覆盖常见语义。

## 五、T3：Type Relation / Constraint Framework

### 5.1 背景问题

当前 `are_types_equivalent()` / `is_subtype_of()` 已经较好，但仍是直接函数风格。若未来加入泛型、union、effect polymorphism、schema variance，直接函数会逐渐难以解释、追踪和扩展。

### 5.2 目标

引入 `TypeRelationContext`，保留旧 bool API 作为 wrapper，并逐步增加 relation trace 和 constraint skeleton。

### 5.3 子任务

#### T3.1 引入 TypeRelationContext

**修改文件：**

- `include/ahfl/compiler/semantics/type_relations.hpp`
- `src/compiler/semantics/type_relations.cpp`
- `tests/unit/compiler/semantics/type_relations.cpp`

**任务内容：**

新增：

```cpp
struct TypeRelationOptions {
    bool allow_bounded_string_relaxation = true;
    bool allow_numeric_widening = false;
};

class TypeRelationContext {
public:
    explicit TypeRelationContext(TypeRelationOptions options = {});
    bool equivalent(const Type& lhs, const Type& rhs);
    bool subtype(const Type& source, const Type& target);
    bool assignable(const Type& source, const Type& target);
};
```

**验收标准：**

- 旧 API：`are_types_equivalent`、`is_subtype_of`、`is_assignable_to` 行为不变。
- 新 context API 单测覆盖等价、子类型、赋值兼容。
- 所有现有 type relation 测试通过。

#### T3.2 增加 RelationTrace

**修改文件：**

- `include/ahfl/compiler/semantics/type_relations.hpp`
- `src/compiler/semantics/type_relations.cpp`

**任务内容：**

新增：

```cpp
struct RelationTrace {
    bool ok;
    std::vector<std::string> notes;
};
```

支持：

- struct / enum nominal mismatch 解释。
- list / set element mismatch 解释。
- map key / value mismatch 解释。
- decimal scale mismatch 解释。
- bounded string bounds mismatch 解释。

**验收标准：**

- bool API 不受影响。
- trace API 对 mismatch 返回至少一条结构化 note。
- type mismatch diagnostics 可选择消费 trace。

#### T3.3 预留 TypeConstraint 数据结构

**建议文件：**

- 新增：`include/ahfl/compiler/semantics/type_constraints.hpp`
- 新增：`src/compiler/semantics/type_constraints.cpp`

**任务内容：**

定义：

```cpp
enum class ConstraintKind {
    Equal,
    Subtype,
    Assignable,
};

struct TypeConstraint {
    ConstraintKind kind;
    TypePtr lhs;
    TypePtr rhs;
    SourceRange origin;
};
```

先不引入完整 solver，仅提供 constraint collection 和逐条 relation evaluation。

**验收标准：**

- 可以收集 constraints。
- 可以用 `TypeRelationContext` 逐条求值。
- failure 可携带 origin 供诊断使用。
- 不改变现有 type checker 行为。

### 5.4 T3 总体验收

- 类型关系从直接函数升级为 context API。
- 旧行为完全兼容。
- relation trace 可服务诊断。
- constraint skeleton 为未来泛型、union、effect polymorphism 留接口。

## 六、T4：解释型诊断系统

### 6.1 背景问题

诊断系统已有 `Diagnostic::Related` 和 `DiagnosticBuilder::with_note()`，但 type checker 还没有系统性追踪 expected / found 的来源。很多 type mismatch 仍然只能说明“错了”，不能清晰解释“为什么期待这个类型”。

### 6.2 目标

让核心类型错误携带 expected origin、found origin、declaration note、candidate suggestion。

### 6.3 子任务

#### T4.1 定义 TypeExpectation

**建议文件：**

- 新增：`include/ahfl/compiler/semantics/type_expectation.hpp`

**任务内容：**

定义：

```cpp
enum class TypeExpectationOriginKind {
    Annotation,
    StructField,
    FunctionParameter,
    ReturnType,
    AssignmentTarget,
    SchemaBoundary,
};

struct TypeExpectation {
    TypePtr expected;
    TypeExpectationOriginKind origin_kind;
    SourceRange origin_range;
    std::string description;
};
```

逐步替换裸 `MaybeCRef<Type> expected_type` 的核心路径，保留轻量 wrapper 以降低迁移风险。

**验收标准：**

- `check_expr()` 可接收带 origin 的 expectation。
- 原有 expected type 调用路径保持兼容。
- 单测覆盖 annotation、struct field、return type、assignment target 四类 origin。

#### T4.2 增强 check_assignable() notes

**修改文件：**

- `src/compiler/semantics/typecheck.cpp`
- `include/ahfl/base/support/diagnostics.hpp`（仅在需要新增 helper 时修改）

**任务内容：**

当 assignment mismatch 时输出：

- primary：当前表达式位置。
- note：expected type 来源。
- note：actual type 来源或表达式描述。

**验收标准：**

示例诊断包含：

```text
type mismatch in struct field
expected String
found Int
note: field declared here
```

golden fail case 稳定。

#### T4.3 struct literal mismatch 增强

**修改文件：**

- `src/compiler/semantics/typecheck.cpp`
- 相关 golden fail case

**任务内容：**

对 struct literal 字段类型错误：

```ahfl
Foo { bar: 1 }
```

如果 `bar` 声明为 `String`，诊断需要同时指向：

- value expression。
- field declaration range。

**验收标准：**

- struct field mismatch 有 declaration note。
- missing field / unknown field 仍保持清晰主错误。
- golden diagnostics 更新并通过。

#### T4.4 enum variant / unknown field suggestion

**建议文件：**

- 新增：`src/compiler/semantics/name_suggestions.cpp`
- 新增：`include/ahfl/compiler/semantics/name_suggestions.hpp`
- 修改：`src/compiler/semantics/typecheck.cpp`

**任务内容：**

实现轻量候选建议：

```text
unknown field 'naem'
did you mean 'name'?
```

候选来源：

- struct fields。
- enum variants。
- 可选：visible local bindings。

**验收标准：**

- struct field typo 有 suggestion。
- enum variant typo 有 suggestion。
- 无足够相近候选时不提示。
- suggestion 算法 deterministic。

#### T4.5 narrowing 解释

**修改文件：**

- `src/compiler/semantics/typecheck.cpp`
- 可选：`include/ahfl/compiler/semantics/flow_facts.hpp`

**任务内容：**

- 当 verbose / debug diagnostics 打开时，解释 narrowing 是否发生及原因。
- 常规用户路径不增加噪声。

**验收标准：**

- debug 模式能说明 `x != none` 触发 narrowing。
- debug 模式能说明复杂表达式没有触发 narrowing 的原因。
- 默认模式不增加额外 warning / note 噪声。

### 6.4 T4 总体验收

- 核心 type mismatch 有 expected origin note。
- struct field mismatch 有 declaration note。
- unknown field / enum variant 有 suggestion。
- narrowing 行为可在 debug 模式解释。
- golden diagnostics 稳定。

## 七、T5：TypeContext 生命周期与并发模型

### 7.1 背景问题

当前 `TypeContext` 是进程级 singleton。虽然有 mutex，但长期看，它对多 workspace 编译、compiler daemon、并发 typecheck、测试隔离和 per-session memory reclaim 都不够理想。

### 7.2 目标

从 process-global `TypeContext` 迁移到 session-scoped interning context，同时保留 global compatibility wrapper，降低迁移风险。

### 7.3 子任务

#### T5.1 定义显式 TypeContext / TypeInterner 接口

**建议文件：**

- 新增：`include/ahfl/compiler/semantics/type_context.hpp`
- 修改：`src/compiler/semantics/types.cpp`

**任务内容：**

把当前 anonymous namespace 中的 interner 提升为显式类型：

```cpp
class TypeContext {
public:
    TypePtr make(TypeKind kind);
    TypePtr string();
    TypePtr bounded_string(std::int64_t minimum, std::int64_t maximum);
    TypePtr decimal(std::int64_t scale);
    TypePtr struct_type(std::string canonical_name, std::optional<SymbolId> symbol = std::nullopt);
    TypePtr enum_type(std::string canonical_name, std::optional<SymbolId> symbol = std::nullopt);
    TypePtr optional(TypePtr value_type);
    TypePtr list(TypePtr element_type);
    TypePtr set(TypePtr element_type);
    TypePtr map(TypePtr key_type, TypePtr value_type);
};
```

**验收标准：**

- 保留 `Type::make()` 等静态 factory 作为默认 global wrapper。
- 新 API 可被测试直接实例化。
- 同一 context 内相同 shape 返回同一 pointer。

#### T5.2 TypeCheckPass 持有 TypeContext

**修改文件：**

- `src/compiler/semantics/typecheck_internal.hpp`
- `src/compiler/semantics/typecheck.cpp`
- `src/compiler/semantics/typecheck_decls.cpp`

**任务内容：**

`TypeCheckPass` 构造时接收：

```cpp
TypeContext& types;
```

短期默认使用 global context，避免外部 API 一次性破坏。

**验收标准：**

- 测试中可以创建独立 `TypeContext` 并注入 type checker。
- 默认编译行为不变。
- 现有全量测试通过。

#### T5.3 消除 type checker 内部静态 global factory 依赖

**修改文件：**

- `src/compiler/semantics/typecheck.cpp`
- `src/compiler/semantics/typecheck_decls.cpp`
- `src/compiler/semantics/validate.cpp`（如需要）

**任务内容：**

逐步把 type checker 内部：

```cpp
Type::make(...)
Type::optional(...)
```

迁移为：

```cpp
types_.make(...)
types_.optional(...)
```

非 type checker 老代码可继续走 global wrapper。

**验收标准：**

- type checker 可用 session context 完成类型构造。
- 老 API 仍可兼容。
- pointer identity 仅在同一 context 内作为快速路径；跨 context 仍依赖 structural equivalence。

#### T5.4 并发与隔离测试

**修改文件：**

- `tests/unit/compiler/semantics/type_relations.cpp`
- 可新增：`tests/unit/compiler/semantics/type_context.cpp`

**任务内容：**

新增测试：

- 两个 `TypeContext` 独立构造同 shape 类型。
- 同 context 内 pointer identity 相同。
- 不同 context 内 pointer identity 可不同，但 structural equivalence 为 true。
- 多线程 intern 同 shape 类型不崩溃，且同 context 内 canonical pointer 稳定。

**验收标准：**

- 普通 unit test 通过。
- 如启用 TSAN / ASAN，测试不报 data race / memory error。
- session context 可用于未来 compiler daemon / parallel compilation。

### 7.4 T5 总体验收

- Type interning 生命周期可被 CompilerSession / TypeCheckPass 管理。
- global singleton 降级为兼容 wrapper。
- 支持未来 daemon / parallel compilation。
- 类型等价仍正确。
- 全量测试通过。

## 八、推荐实施顺序

### Phase 1：低风险高收益

1. T4.1 - T4.3：解释型诊断基础。
2. T5.1 - T5.2：TypeContext 显式化。
3. T2.1 - T2.3：FlowFacts 基础和 else narrowing。

理由：这三组改动收益高、风险相对可控，并且会给 Typed HIR、CFA-lite 和后续诊断解释铺路。

### Phase 2：中等架构升级

4. T1.1 - T1.2：Typed HIR 数据模型和 type checker 输出。
5. T2.4 - T2.5：member path narrowing 与 assignment fact invalidation。
6. T4.4 - T4.5：suggestion 与 narrowing explanation。

### Phase 3：核心架构切换

7. T1.3 - T1.4：IR lowering 消费 Typed HIR，移除主路径 expression type lookup 依赖。
8. T5.3 - T5.4：减少 global TypeContext 依赖并验证并发 / 隔离。

### Phase 4：未来语言特性基础

9. T3.1 - T3.3：TypeRelationContext、RelationTrace、Constraint skeleton。

## 九、最小 issue 切分

若需要转换成 issue tracker，建议建立以下 10 个独立 issue：

1. Add Typed HIR data model.
2. Emit Typed HIR from TypeCheckPass.
3. Lower IR from Typed HIR behind feature-equivalent path.
4. Remove IR lowering dependency on expression type lookup.
5. Add FlowFacts and condition fact extraction.
6. Support symmetric Optional narrowing and else-branch narrowing.
7. Support member-path narrowing with assignment fact invalidation.
8. Add TypeExpectation and diagnostic origin notes.
9. Make TypeContext session-scoped with global compatibility wrapper.
10. Introduce TypeRelationContext and RelationTrace.

## 十、完成定义

整组任务完成后，AHFL type checker 应满足：

1. type checker 正式产出 Typed HIR。
2. IR lowering 主路径消费 Typed HIR，而不是 expression type lookup map。
3. Optional narrowing 基于 FlowFacts，并支持对称条件、else 分支和 member path。
4. assignment 会正确 kill 受影响的 type facts。
5. 核心类型错误带 expected origin note / declaration note / suggestion。
6. TypeContext 可 session-scoped 注入，global context 仅作为兼容 wrapper。
7. TypeRelationContext / RelationTrace 为未来 constraint solver 预留接口。
8. 全量 `cmake --build --preset build-dev` 与 `ctest --preset test-dev --output-on-failure` 通过。
