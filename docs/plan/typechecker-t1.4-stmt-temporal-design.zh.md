# AHFL T1.4 Statement / Temporal 层 Typed-Tree 迁移设计（延期至 T1.6）

> 文档版本：v0.1-draft（2026/06）
>
> **Scope 说明：本期（T1.4）不做实质迁移，全部内容留待 T1.6 阶段实现。** 本文件只给出接口骨架、接入点、迁移动线，作为 T1.6 实施的直接输入。

---

## 1. 背景与 ROI 判定

### 1.1 已完成的 P3 边界（T1.2/T1.4）

- **表达式层（T1.2 完成）**：`TypedProgram::expressions` 覆盖全部 `ExprSyntaxKind`；`typed_hir_lower.cpp` 通过 `typed_visit` 从 TypedExpr 降 IR，不再触碰 AST 表达式内部结构；payload 表达式通过 `lower_expr(ast_expr) -> typed_expr_for -> typed_visit` 的单向桥接进入 typed-tree。
- **声明层（T1.4 完成）**：`TypedProgram::declarations` 覆盖全部顶层 `NodeKind`（Module/Import 除外）；lowerer 从 `declarations` 迭代，结构细节（字段、状态、handler、quota 等）仍回查 AST。
- **Block / Statement / Temporal**：仍直接遍历 AST `BlockSyntax`、`StatementSyntax`、`TemporalExprSyntax`，typed-tree 在这三层没有对等节点。

### 1.2 ROI 评估（T1.4 时间点快照）

| 维度 | 数值 / 判断 |
|---|---|
| 语句 kind 数 | 7 种（Let / Assign / If / Goto / Return / Assert / Expr），未达到"≥ 10 种建议本轮迁移"阈值 |
| 时序 kind 数 | 7 种（EmbeddedExpr / Called / InState / Running / Completed / Unary / Binary） |
| payload expr 已入 expressions | 全部 `check_expr` 出口都会 `remember_expression_type` → **是**；`lower_expr` 桥接已令表达式子树完全 typed |
| statement 级重写需求（DCE / let 提升 / 常量传播） | 产品 backlog 中未出现，仅为 roadmap 层设想 |
| temporal 级重写需求（de-Morgan / redundancy 消除） | 同上，未排期；且 `FormalObservationCollector` 仍在 IR 层做索引，temporal 改写后需重跑观察收集 |
| typecheck 出口写 statements 风险 | **低**：`check_statement` 各分支独立，无互递归回环（递归仅在 `check_block` 外层容器） |
| 隐藏依赖：Block 级 typed 化 | **高影响**：If 的 then/else 是 `BlockSyntax`；FlowDecl 的 state handler body、WorkflowDecl 的 return body 也都是 Block；不把 Block 一起 typed 化的话，If 的控制结构仍得回查 AST，statement 级 typed-tree 的收益被打折扣 |
| T1.2 等价性测试 | 已有 `tests/unit/compiler/ir/identity_visitor.cpp` + `tests/unit/compiler/semantics/typed_hir.cpp` 两组基线；新增字段后只需要 fingerprint 的 assertion 补充 |

**结论**：
- statement 层 typed-tree 的增量收益（即"让表达式子树也走 typed-tree"）**已经由 `lower_expr` 桥接达成**。
- 新增收益仅剩"post-typecheck 重写能改 statement 级结构后被 lowering 看见"，而当前重写没有排期。
- Block 级依赖使得实际工作量翻倍（Block + container decl 的 body_block_index）。
- **综合判定：Medium ROI**。T1.4 不做实质迁移，设计落文档，接口骨架预留，T1.6 有明确 statement 级 rewrite 需求时再启动。

---

## 2. 数据结构设计（TypedProgram 扩展）

### 2.1 TypedStmtKind 枚举

与 `ast::StatementSyntaxKind` 一一对应（保持 switch 覆盖编译期检查）：

```cpp
enum class TypedStmtKind {
    Let,
    Assign,
    If,
    Goto,
    Return,
    Assert,
    Expr,
};
```

**为什么不复用 `ast::StatementSyntaxKind`**：与 `TypedExpr` 一致，typed 层拥有独立枚举，以便未来承载"合成语句"（synthetic-rewrite 注入的 let、goto 合并等）时不影响 AST 枚举。

### 2.2 TypedTemporalKind 枚举

与 `ast::TemporalExprSyntaxKind` 一一对应：

```cpp
enum class TypedTemporalKind {
    EmbeddedExpr,
    Called,
    InState,
    Running,
    Completed,
    Unary,
    Binary,
};
```

### 2.3 TypedStatement 结构

采用与 `TypedExpr` 一致的"扁平化索引 + children role"风格：

```cpp
struct TypedStatement {
    TypedStmtKind kind;
    SourceRange range;
    std::optional<SourceId> source_id;
    std::uint64_t node_id;  // 镜像 ast::StatementSyntax::node_id，缺省 0

    // 表达式子节点，按 role 区分。UINT32_MAX 表示空（对应 nullptr 情况）。
    //   Let:         [Initializer]             (TypedExprChildRole::Operand)
    //   Assign:      [Value]                   (TypedExprChildRole::RightOperand)
    //                    Assign 的 target 路径不进 children：path 是拼写细节，
    //                    通过 (range, kind) 回查 AST 的 assign_stmt->target 拿
    //                    PathSyntax；或者把 path_root + member_path 镜像到这里
    //                    （见 2.5 可选字段）。
    //   If:          [Condition]               (TypedExprChildRole::Condition, 新 role)
    //   Return:      [Value?]                  (TypedExprChildRole::Operand, 可选)
    //   Assert:      [Condition]               (TypedExprChildRole::Condition)
    //   Expr:        [Expr]                    (TypedExprChildRole::Operand)
    //   Goto:        无 children
    std::vector<TypedExprChild> children;

    // Block 引用（指向 TypedProgram::blocks 的扁平索引）。UINT32_MAX 表示不存在。
    //   If:    then_block_index / else_block_index（else 可空）
    //   其他 kind：均为 UINT32_MAX
    std::uint32_t then_block_index{UINT32_MAX};
    std::uint32_t else_block_index{UINT32_MAX};

    // ========================================================================
    // Primitive payload mirrors（与 TypedExpr 的 bool_value / unary_op 同思路）
    // 只放 lowering 不回查 AST 就能工作所需的最少字段。
    // ========================================================================

    // Let (T1.6 选项 A：只放 name，target 变量类型通过 children[Initializer].type 推断
    //               或从回查 AST 的 type 语法拿——因为 type 语法不总存在）
    std::string let_name;

    // Goto
    std::string goto_target_state;

    // Assign: 路径拼写镜像（2.5 详述）。不填的话 lowerer 回查 AST PathSyntax。
    std::string assign_path_root;
    std::vector<std::string> assign_member_path;
};
```

### 2.4 TypedTemporalExpr 结构

时序表达式树形结构，同样扁平索引（children 存 temporal 子节点索引，嵌入式布尔表达式仍通过 TypedExprChild 进 expressions）：

```cpp
enum class TypedTemporalChildRole {
    Operand,
    LeftOperand,
    RightOperand,
};

struct TypedTemporalChild {
    TypedTemporalChildRole role;
    std::uint32_t temporal_index{UINT32_MAX};  // 指向 TypedProgram::temporal_exprs
};

struct TypedTemporalExpr {
    TypedTemporalKind kind;
    SourceRange range;
    std::optional<SourceId> source_id;
    std::uint64_t node_id{0};

    // EmbeddedExpr：唯一指向 expressions 的桥
    std::optional<TypedExprChild> embedded_expr;  // role = Operand

    // 时序子节点
    //   Unary  -> [Operand]
    //   Binary -> [LeftOperand, RightOperand]
    //   叶子（Called/InState/Running/Completed/EmbeddedExpr）：空
    std::vector<TypedTemporalChild> children;

    // Primitive payload mirrors
    std::string name;                 // Called / InState / Running / Completed::node
    std::string state_name;           // Completed::state_name
    ast::TemporalUnaryOp unary_op;    // Unary
    ast::TemporalBinaryOp binary_op;  // Binary
};
```

### 2.5 TypedBlock 结构

Block 是 statement 的容器，也是 If / FlowDecl handler / WorkflowDecl return body 的结构单元。不加 Block typed 化的话，If 控制结构仍得回查 AST，statement 层收益打折。

```cpp
struct TypedBlock {
    SourceRange range;
    std::optional<SourceId> source_id;
    // 扁平顺序，指向 TypedProgram::statements 的索引
    std::vector<std::uint32_t> statement_indexes;
};
```

### 2.6 TypedProgram 扩展字段

```cpp
struct TypedProgram {
    // ... 现有 declarations / expressions ...

    // T1.6 新增：
    std::vector<TypedBlock> blocks;
    std::vector<TypedStatement> statements;
    std::vector<TypedTemporalExpr> temporal_exprs;
};
```

`TypedDecl` 上也要加 body_block_index 字段（对 FlowDecl/AgentDecl/WorkflowDecl 等含 statement 体的 decl）：

```cpp
struct TypedDecl {
    // ... 现有字段 ...

    // T1.6：body 是 Block 的 decl 填这里；非 body decl 留 UINT32_MAX。
    //   FlowDecl:     state_handlers 每个 handler 都有一个 body —— 这里不能用单一
    //                 block_index。FlowDecl handler 的 body 索引改为存
    //                 handler_body_block_indexes 向量（与 AST handlers 对齐）。
    //   WorkflowDecl: return_value 是表达式不是 block，已有 expressions 覆盖；
    //                 nodes 是 AST 结构单独回查。
    //   AgentDecl:    没有 block body。
    //   Capability/Predicate/Const 没有 block。
    std::vector<std::uint32_t> handler_body_block_indexes;  // FlowDecl 专用
};
```

ContractDecl 的 clauses 中 temporal 公式的索引也需显式记录：

```cpp
struct TypedDecl {
    // ...
    // ContractDecl：每个 clause 要么是一个 expr_index（布尔）要么是 temporal_index。
    // 一对 vector，各自存 UINT32_MAX 代表该 clause 是另一种。
    std::vector<std::uint32_t> contract_clause_expr_indexes;      // UINT32_MAX = temporal 类
    std::vector<std::uint32_t> contract_clause_temporal_indexes;  // UINT32_MAX = expr 类

    // WorkflowDecl：safety / liveness 公式的 temporal 索引
    std::vector<std::uint32_t> workflow_safety_indexes;
    std::vector<std::uint32_t> workflow_liveness_indexes;
};
```

---

## 3. TypeCheckPass 接入点

原则：**在现有 check_* 函数的"每处理完一个节点"的出口处 push_back 一条记录，并返回（或就地记录）该记录的 flat index**。不改变 typecheck 的语义、递归顺序、诊断行为。

### 3.1 check_block → 写 `blocks[]`

当前位置：`typecheck.cpp:2116`，函数签名：

```cpp
void TypeCheckPass::check_block(const ast::BlockSyntax &block,
                                ValueContext &context,
                                MaybeCRef<Type> expected_return_type,
                                std::string_view state_name,
                                std::optional<SourceRange> expected_return_origin);
```

T1.6 改造：返回 `uint32_t`（新写入 blocks 的 index），尾部加：

```cpp
    // 在函数尾：
    result_.typed_program.blocks.push_back(TypedBlock{
        .range = block.range,
        .source_id = current_source_id_,
        .statement_indexes = /* 由 for 循环收集 */,
    });
    return static_cast<std::uint32_t>(result_.typed_program.blocks.size() - 1);
```

for 循环每次调用 `check_statement` 之后收集其返回的 stmt_index（见下）。

### 3.2 check_statement → 写 `statements[]`

返回 `uint32_t`（statements index）。每个 switch 分支尾部构建对应 `TypedStatement{}` 并 push_back：

- **Let**：kind=Let，children[Initializer]=initializer_expr_index，let_name=statement.let_stmt->name
- **Assign**：kind=Assign，children[Value]=value_expr_index，assign_path_root / assign_member_path 从 PathSyntax 镜像
- **If**：kind=If，children[Condition]=cond_expr_index，then_block_index=调用 `check_block(then)` 的返回，else_block_index=调用 `check_block(else)` 的返回（可空）
- **Goto**：kind=Goto，goto_target_state=statement.goto_stmt->target_state，无 children
- **Return**：kind=Return，若 `return_stmt->value` 存在则 children[Operand]=expr_index，否则 children 空
- **Assert**：kind=Assert，children[Condition]=cond_expr_index
- **Expr**：kind=Expr，children[Operand]=expr_index

注意：**`check_statement` 内部对 then/else 的 `check_block` 调用**（typecheck.cpp:2283/2294）必须改为捕获返回值并存入当前 If 语句的 block_index 字段。

### 3.3 check_temporal → 写 `temporal_exprs[]`

当前 typecheck 侧只对 temporal 的嵌入式子表达式调 `check_temporal_embedded_exprs`（`typecheck.cpp:1921`）递归。T1.6 要新增一个对称的 `check_temporal_expr` 函数，**先递归子节点拿到 temporal_indexes，再构造并 push 当前节点**（后序）。

入口：
- ContractDecl clause 的 temporal 分支（typecheck.cpp:1883）—— 把返回值写入 `contract_clause_temporal_indexes[ci]`
- WorkflowDecl safety/liveness（typecheck.cpp:2085/2089）—— 把返回值写入 `workflow_safety_indexes` / `workflow_liveness_indexes`

### 3.4 Decl 出口写入 block/temporal 索引

- **FlowDecl**（typecheck.cpp:1985）：循环 `node.state_handlers`，每个 handler 调用 `check_block(handler->body)` 的返回值追加到 `handler_body_block_indexes`
- **ContractDecl**（typecheck.cpp:1883）：每 clause 结束写入 contract_clause_*_indexes 两个 vector 对应位置
- **WorkflowDecl**（typecheck.cpp:2085/2089）：safety/liveness 循环写入对应 vector

---

## 4. lower_statement / lower_temporal 迁移动线

### 4.1 核心思路（mirror T1.4 声明层做法）

- lowerer 入口在 TypedIrLowerer 中新增：
  - `lower_block(uint32_t block_index)` → 遍历 `typed_program_->blocks[block_index].statement_indexes`
  - `lower_statement(uint32_t stmt_index)` → 读取 `TypedProgram::statements[stmt_index]`，dispatch `TypedStmtKind`
  - `lower_temporal(uint32_t temporal_index)` → 读取 `TypedProgram::temporal_exprs[temporal_index]`，dispatch `TypedTemporalKind`
- **结构细节（let 的 name、assign 的 path、goto 的 target_state 等）从 TypedStatement 的 payload 字段直接读取**，不回查 AST。
- **表达式子节点全部走 `resolve_child(*typed_program_, child) -> lower_typed_expr`**，不再用 `typed_expr_for(ast_expr)` 桥接。这是 T1.6 迁移的唯一行为差异点：桥接被删除，改为直接从 TypedStatement 拿 children 的 expr_index。
- **控制结构（if 的 then/else）通过 block_index → `lower_block(idx)`**，也不碰 AST。
- 声明层含 body 的：FlowDecl 从 `typed_decl.handler_body_block_indexes` 取 block_index → `lower_block(idx)`；Contract/Workflow 同理用 temporal_index 向量。

### 4.2 AST 接触点最小化

迁移完成后 lower_statement/lower_temporal **完全不 dereference `ast::StatementSyntax` / `ast::TemporalExprSyntax` / `ast::BlockSyntax`**。FlowDecl handler 的非 body 字段（state_name、policy items 等）继续回查 AST，因为这些不是 typed-tree 的覆盖目标。

### 4.3 T1.2 等价性保障

迁移后的第一个 CI 任务：
1. `lower_program_ir`（AST 路径）产出 IR，序列化到字符串 / 指纹。
2. `lower_typed_program`（typed 路径）产出 IR，同样指纹。
3. `REQUIRE(identical)`。

这两组已经在 `identity_visitor.cpp::typed_hir_lowering_preserves_ir_identity` 和 `typed_hir.cpp::T1.2 lowering produces equivalent IR via typed HIR entry` 中建立基线。T1.6 迁移时**不修改这两个测试的 source 文本**，只是观察它们不破；若破则 diff IR 文本定位偏差。

### 4.4 增量测试（T1.6 验收用）

补一个专门测试（留作 T1.6 的 AC 项）：

```
"typed_hir_stmt_lowering_preserves_let_assign_if_return_fingerprint"
  input: 一个只含 let / assign / if / return / goto / assert / expr-statement 的
         最小 FlowDecl
  断言：AST 路径和 typed 路径产出的 IR 指纹相同
```

---

## 5. T1.6 实施分阶段建议

| 阶段 | 内容 | 验收标准 |
|---|---|---|
| T1.6-P1 | 新增枚举与结构体（§2.1–2.6），不写 typecheck / lowerer；编译通过 | `cmake --build` 成功 |
| T1.6-P2 | `check_block` / `check_statement` 改造；typecheck 写 blocks + statements；断言 stat 数量 == AST block/statement 数量 | typed_hir.cpp 新增 coverage 测试 |
| T1.6-P3 | `lower_statement` / `lower_block` 改为读 TypedProgram::statements/blocks；保持 T1.2 基线不破 | identity_visitor.cpp 两组基线 fingerprint 等价 |
| T1.6-P4 | `check_temporal_expr` + `lower_temporal` 改造；Contract/Workflow 声明入口改写 | workflow temporal 相关 identity 测试不破 |
| T1.6-P5 | 删除 typed_hir_lower.cpp 中 `ast::StatementSyntax` / `ast::TemporalExprSyntax` / `ast::BlockSyntax` 的直接 deref；CI 全绿 | `grep 'ast::.*StatementSyntax\|ast::.*TemporalExprSyntax\|ast::.*BlockSyntax' typed_hir_lower.cpp` 在 lower_* 函数体内无命中 |

---

## 6. 风险与规避

| 风险 | 说明 | 规避 |
|---|---|---|
| Block 扁平索引错乱 | If 的 then/else 与 FlowDecl handler 混用同一 blocks 向量；index 回绕 / 未设置 | TypedBlock 构造统一在 `check_block` 尾部；所有 UINT32_MAX 的语义在 lowerer 侧做显式 null-check 并打 diagnostic |
| statement 顺序与 AST 不一致导致 T1.2 破 | typecheck 递归的 check_statement 顺序必须与 AST 遍历顺序严格相同 | 在 P2 阶段加一个测试：`(TypedStatement i).range.begin_offset` 按序严格单调不减 |
| temporal 子节点 role 不一致 | Unary/Binary 的 children role 写错会让 lowerer 读错位 | 单元测试每个 temporal kind 单独建节点，断言 children.size() + role 值 |
| payload 镜像字段漏填 | Let 名 / Goto target / Assign path 忘了填导致 lowering 出错 | P2 阶段 `check_statement` 出口做 `static_assert` + `REQUIRE(!field.empty())` 覆盖率测试 |

---

## 7. 给 T1.6 实现者的 Checklist

- [ ] `TypedStmtKind` / `TypedTemporalKind` 枚举覆盖所有 ast 侧 kind，同步更新 `-Wswitch` 无告警
- [ ] `TypedExprChildRole` 若不足则加 `Condition`（当前只有 Operand/LeftOperand…）
- [ ] `TypedStatement` 的所有 primitive payload 在 `check_statement` 的对应分支都有赋值路径
- [ ] `check_block` 返回值类型从 `void` 改 `uint32_t`；所有调用点（FlowDecl/If/Workflow）捕获返回值
- [ ] ContractDecl / WorkflowDecl 的 clause / formula 索引向量长度与 AST 侧对应数组同长
- [ ] `resolve_child` 家族新增 `resolve_temporal_child` 变体（对应 TypedTemporalChild）
- [ ] identity_visitor.cpp 中 `typed_hir_lowering_preserves_ir_identity` 基线不破
- [ ] typed_hir.cpp 中 `T1.2 lowering produces equivalent IR via typed HIR entry` 基线不破
- [ ] 编译时 + 运行时测试全部通过
