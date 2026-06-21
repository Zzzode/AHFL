# AHFL 语言规范

本文是 AHFL Core 的规范性定义，覆盖：

- 词法约束
- 规范化 EBNF 文法
- 类型系统
- 静态语义约束

若本文与其他设计说明冲突，以本文为准。

## 1. 范围

AHFL Core 是面向 agent 编排与控制的强类型 DSL。只覆盖能够稳定实现并可直接进入 parser/checker 阶段的核心能力：

1. 数据 schema 与基础类型
2. 纯谓词声明与外部 capability 声明
3. agent 状态机
4. 合同约束
5. 受限的 per-state flow
6. DAG 形式的 workflow

不包含以下能力：

1. capability/tool 实现体
2. 原生 `llm_config`
3. `observability` / `compliance`
4. `main` 与服务启动
5. CTL
6. 通用高阶函数与用户自定义泛型

## 2. 词法定义

### 2.1 注释与空白

- 单行注释：`// ...`
- 文档注释：`/// ...`
- 多行注释：`/* ... */`
- 多行注释**不支持嵌套**
- 空白字符包括空格、制表符、换行符、回车符

### 2.2 保留关键字

```text
module import as
const type struct enum
capability predicate
agent contract flow workflow for
input context output
states initial final capabilities quota transition
state with retry retry_on timeout
max_tool_calls max_execution_time
node after return let if else goto assert
requires ensures invariant forbid
safety liveness
always eventually next until
called in_state running completed
true false none some
and or not
Unit Bool Int Float String UUID Timestamp Duration Decimal
Optional List Set Map
set map
```

### 2.3 标识符

```ebnf
Ident         ::= Letter { Letter | Digit | "_" } ;
QualifiedIdent ::= Ident { "::" Ident } ;
Letter        ::= "A"…"Z" | "a"…"z" ;
Digit         ::= "0"…"9" ;
```

说明：

- 标识符必须以字母开头
- `QualifiedIdent` 只用于模块路径、类型路径、顶层声明引用和枚举变体引用
- 字段访问与局部路径访问使用 `.`，例如 `input.order_id`
- `input` 与 `output` 是保留关键字，但在表达式位置可作为内建作用域根出现在 `PathExpr` 中

### 2.4 字面量

```ebnf
BoolLiteral     ::= "true" | "false" ;
IntLiteral      ::= [ "-" | "+" ] Digit { Digit } ;
FloatLiteral    ::= [ "-" | "+" ] Digit { Digit } "." Digit { Digit } [ Exponent ] ;
Exponent        ::= ( "e" | "E" ) [ "-" | "+" ] Digit { Digit } ;
DecimalLiteral  ::= [ "-" | "+" ] Digit { Digit } "." Digit { Digit } "d" ;
StringLiteral   ::= "\"" { StringChar } "\"" ;
DurationLiteral ::= IntLiteral ( "ms" | "s" | "m" | "h" ) ;
StringChar      ::= ? any unicode scalar except " and \ ? | EscapeSequence ;
EscapeSequence  ::= "\\" ( "\"" | "\\" | "n" | "r" | "t" ) ;
```

约束：

1. `DecimalLiteral` 的类型为 `Decimal(p)`，其中 `p` 等于小数部分位数
2. `DurationLiteral` 的类型为 `Duration`
3. `UUID` 与 `Timestamp` 通过标准库构造函数获得，不定义专用字面量

## 3. 语法总览

### 3.1 顶层结构

```ebnf
Program         ::= { ModuleDecl | ImportDecl | TopLevelDecl } EOF ;

TopLevelDecl    ::= ConstDecl
                  | TypeAliasDecl
                  | StructDecl
                  | EnumDecl
                  | CapabilityDecl
                  | PredicateDecl
                  | AgentDecl
                  | ContractDecl
                  | FlowDecl
                  | WorkflowDecl ;

ModuleDecl      ::= "module" QualifiedIdent ";" ;
ImportDecl      ::= "import" QualifiedIdent [ "as" Ident ] ";" ;
```

### 3.2 类型定义

```ebnf
Type            ::= PrimitiveType
                  | NamedType
                  | OptionalType
                  | ListType
                  | SetType
                  | MapType ;

PrimitiveType   ::= "Unit"
                  | "Bool"
                  | "Int"
                  | "Float"
                  | "String"
                  | "String" "(" IntLiteral "," IntLiteral ")"
                  | "UUID"
                  | "Timestamp"
                  | "Duration"
                  | "Decimal" "(" IntLiteral ")" ;

NamedType       ::= QualifiedIdent ;
OptionalType    ::= "Optional" "<" Type ">" ;
ListType        ::= "List" "<" Type ">" ;
SetType         ::= "Set" "<" Type ">" ;
MapType         ::= "Map" "<" Type "," Type ">" ;
```

### 3.3 常量、结构体、枚举、别名

```ebnf
ConstDecl       ::= "const" Ident ":" Type "=" ConstExpr ";" ;

TypeAliasDecl   ::= "type" Ident "=" Type ";" ;

StructDecl      ::= "struct" Ident "{" { StructFieldDecl } "}" ;
StructFieldDecl ::= Ident ":" Type [ "=" ConstExpr ] ";" ;

EnumDecl        ::= "enum" Ident "{" EnumVariant { "," EnumVariant } [ "," ] "}" ;
EnumVariant     ::= Ident ;
```

约束：

1. `struct` 字段名在同一结构体内唯一
2. `enum` 变体名在同一枚举内唯一
3. 若某个 `struct` 被用作 agent `context` 类型，则其所有字段必须提供默认值

### 3.4 capability 与 predicate 声明

```ebnf
CapabilityDecl  ::= "capability" Ident "(" [ ParamList ] ")" "->" Type
                     ( ";" | CapabilityEffectBlock ) ;

CapabilityEffectBlock ::= "{" { CapabilityEffectItem } "}" ;

CapabilityEffectItem  ::= "effect" ":" CapabilityEffectKind ";"
                        | "domain" ":" QualifiedIdent ";"
                        | "idempotency" ":" PathExpr ";"
                        | "receipt" ":" CapabilityReceiptMode ";"
                        | "retry" ":" CapabilityRetryMode ";"
                        | "timeout" ":" DurationLiteral ";"
                        | "compensation" ":" QualifiedIdent ";"
                        | "policy" ":" "[" QualifiedIdentListOpt "]" ";" ;

CapabilityEffectKind  ::= "read"
                        | "external_side_effect"
                        | "durable_write"
                        | "financial_write"
                        | "unknown" ;

CapabilityReceiptMode ::= "required" | "optional" | "none" ;
CapabilityRetryMode   ::= "safe" | "safe_if_idempotent" | "unsafe" ;

PredicateDecl   ::= "predicate" Ident "(" [ ParamList ] ")" "->" "Bool" ";" ;

ParamList       ::= Param { "," Param } [ "," ] ;
Param           ::= Ident ":" Type ;
```

语义约束：

1. `capability` 表示外部 effectful 调用点
2. `predicate` 表示纯、确定、无副作用布尔函数
3. `predicate` 不得调用 `capability`
4. `CapabilityEffectBlock` 为可选；缺省时该 capability 语法合法，但无法通过 assurance production gate（见 `assurance.zh.md`）。effect block 中各字段的语义定义、production gate 规则与 obligation 合成见 `assurance.zh.md` 的 "Capability Effect Profile"、"Production Gate 规则"、"Obligation Synthesis" 章节，本规范不重复
5. `CapabilityEffectItem` 各字段在 effect block 内至多出现一次；字段顺序任意
6. `domain`、`compensation`、`policy` 中的 `QualifiedIdent` 在前端只校验词法合法性，不将其解析为强制存在的符号引用；其真实可用性由 assurance profile 在后续阶段校验

### 3.5 agent 定义

```ebnf
AgentDecl           ::= "agent" Ident "{"
                        InputDecl
                        ContextDecl
                        OutputDecl
                        StatesDecl
                        InitialDecl
                        FinalDecl
                        CapabilitiesDecl
                        [ QuotaDecl ]
                        { TransitionDecl }
                       "}" ;

InputDecl           ::= "input" ":" Type ";" ;
ContextDecl         ::= "context" ":" Type ";" ;
OutputDecl          ::= "output" ":" Type ";" ;
StatesDecl          ::= "states" ":" "[" IdentList "]" ";" ;
InitialDecl         ::= "initial" ":" Ident ";" ;
FinalDecl           ::= "final" ":" "[" IdentList "]" ";" ;
CapabilitiesDecl    ::= "capabilities" ":" "[" IdentListOpt "]" ";" ;
TransitionDecl      ::= "transition" Ident "->" Ident ";" ;

QuotaDecl           ::= "quota" ":" "{"
                        [ "max_tool_calls" ":" IntLiteral ";" ]
                        [ "max_execution_time" ":" DurationLiteral ";" ]
                       "}" ;

IdentList           ::= Ident { "," Ident } [ "," ] ;
IdentListOpt        ::= [ IdentList ] ;
```

语义约束：

1. `input`、`context`、`output` 必须为具名 `struct` 类型或其别名
2. `states` 非空，且元素唯一
3. `initial` 必须属于 `states`
4. `final` 必须是 `states` 的非空子集
5. `transition` 的源状态与目标状态必须都在 `states` 中
6. `final` 状态不得作为任何 `transition` 的源状态
7. `capabilities` 中每个名字必须引用已声明的 `capability`
8. 所有从 `initial` 可达的非终态必须至少有一条出边
9. 所有状态必须从 `initial` 可达

### 3.6 contract 定义

```ebnf
ContractDecl        ::= "contract" "for" QualifiedIdent "{"
                        { ContractItem }
                       "}" ;

ContractItem        ::= RequiresDecl
                      | EnsuresDecl
                      | InvariantDecl
                      | ForbidDecl ;

RequiresDecl        ::= "requires" ":" Expr ";" ;
EnsuresDecl         ::= "ensures" ":" Expr ";" ;
InvariantDecl       ::= "invariant" ":" TemporalExpr ";" ;
ForbidDecl          ::= "forbid" ":" TemporalExpr ";" ;
```

语义约束：

1. `requires` 与 `ensures` 的表达式必须是**纯表达式**且类型为 `Bool`
2. `invariant` 与 `forbid` 必须是 `TemporalExpr`
3. 合同中禁止直接出现 `capability` 调用
4. `contract for A` 中对状态名的引用必须属于 agent `A`

### 3.7 flow 定义

```ebnf
FlowDecl            ::= "flow" "for" QualifiedIdent "{"
                        { StateHandler }
                       "}" ;

StateHandler        ::= "state" Ident [ StatePolicy ] Block ;

StatePolicy         ::= "with" "{"
                        { StatePolicyItem }
                       "}" ;

StatePolicyItem     ::= "retry" ":" IntLiteral ";"
                      | "retry_on" ":" "[" QualifiedIdentListOpt "]" ";"
                      | "timeout" ":" DurationLiteral ";" ;

QualifiedIdentListOpt ::= [ QualifiedIdent { "," QualifiedIdent } [ "," ] ] ;
```

语义约束：

1. `flow for A` 必须绑定到已声明的 agent `A`
2. 每个 agent 状态至多有一个 `state` handler
3. 所有非终态必须定义 handler
4. 终态若需要产出结果，必须定义 handler 且以 `return` 结束
5. `retry_on` 中的名字是运行时错误模型中的符号标识；前端只校验其词法合法性，不将其解析为普通类型声明

### 3.8 workflow 定义

```ebnf
WorkflowDecl        ::= "workflow" Ident "{"
                        WorkflowInputDecl
                        WorkflowOutputDecl
                        { WorkflowItem }
                        WorkflowReturnDecl
                       "}" ;

WorkflowInputDecl   ::= "input" ":" Type ";" ;
WorkflowOutputDecl  ::= "output" ":" Type ";" ;

WorkflowItem        ::= WorkflowNodeDecl
                      | WorkflowSafetyDecl
                      | WorkflowLivenessDecl ;

WorkflowNodeDecl    ::= "node" Ident ":" QualifiedIdent "(" Expr ")" [ "after" "[" IdentListOpt "]" ] ";" ;

WorkflowSafetyDecl  ::= "safety" ":" WorkflowTemporalExpr ";" ;
WorkflowLivenessDecl ::= "liveness" ":" WorkflowTemporalExpr ";" ;

WorkflowReturnDecl  ::= "return" ":" Expr ";" ;
```

语义约束：

1. `node n : A(e)` 中 `A` 必须引用已声明的 agent
2. `e` 的类型必须与 `A.input` 完全匹配
3. `after` 中的节点必须已声明
4. `workflow` 依赖图必须是 DAG
5. `return` 表达式的类型必须与 workflow `output` 完全匹配

### 3.9 代码块与语句

```ebnf
Block               ::= "{" { Statement } "}" ;

Statement           ::= LetStmt
                      | AssignStmt
                      | IfStmt
                      | GotoStmt
                      | ReturnStmt
                      | AssertStmt
                      | ExprStmt ;

LetStmt             ::= "let" Ident [ ":" Type ] "=" Expr ";" ;
AssignStmt          ::= LValue "=" Expr ";" ;
IfStmt              ::= "if" Expr Block [ "else" Block ] ;
GotoStmt            ::= "goto" Ident ";" ;
ReturnStmt          ::= "return" Expr ";" ;
AssertStmt          ::= "assert" Expr ";" ;
ExprStmt            ::= Expr ";" ;

LValue              ::= PathExpr ;
```

语义约束：

1. `if` 与 `assert` 的条件表达式必须是纯表达式且类型为 `Bool`
2. `ExprStmt` 只允许：
   - `capability` 调用
   - `predicate` 调用
   - 纯函数/标准库调用
3. `goto` 只能跳转到当前 agent 的合法后继状态
4. `return` 只能出现在终态 handler 中

### 3.10 表达式

```ebnf
Expr                ::= ImpliesExpr ;

ImpliesExpr         ::= OrExpr { "=>" OrExpr } ;
OrExpr              ::= AndExpr { ( "or" | "||" ) AndExpr } ;
AndExpr             ::= EqualityExpr { ( "and" | "&&" ) EqualityExpr } ;
EqualityExpr        ::= CompareExpr { ( "==" | "!=" ) CompareExpr } ;
CompareExpr         ::= AddExpr { ( "<" | "<=" | ">" | ">=" ) AddExpr } ;
AddExpr             ::= MulExpr { ( "+" | "-" ) MulExpr } ;
MulExpr             ::= UnaryExpr { ( "*" | "/" | "%" ) UnaryExpr } ;

UnaryExpr           ::= ( "not" | "!" | "-" | "+" ) UnaryExpr
                      | PostfixExpr ;

PostfixExpr         ::= PrimaryExpr { "." Ident | "[" Expr "]" } ;

PrimaryExpr         ::= Literal
                      | QualifiedIdent
                      | PathExpr
                      | CallExpr
                      | StructLiteral
                      | ListLiteral
                      | SetLiteral
                      | MapLiteral
                      | "some" "(" Expr ")"
                      | "none"
                      | "(" Expr ")" ;

PathExpr            ::= PathRoot { "." Ident } ;
PathRoot            ::= Ident | "input" | "output" ;
CallExpr            ::= QualifiedIdent "(" [ ExprList ] ")" ;
ExprList            ::= Expr { "," Expr } [ "," ] ;

Literal             ::= BoolLiteral
                      | IntLiteral
                      | FloatLiteral
                      | DecimalLiteral
                      | StringLiteral
                      | DurationLiteral ;

ListLiteral         ::= "[" [ ExprList ] "]" ;
SetLiteral          ::= "set" "[" [ ExprList ] "]" ;
MapLiteral          ::= "map" "[" [ MapEntryList ] "]" ;
MapEntryList        ::= MapEntry { "," MapEntry } [ "," ] ;
MapEntry            ::= Expr ":" Expr ;

StructLiteral       ::= QualifiedIdent "{"
                        [ StructInitList ]
                       "}" ;

StructInitList      ::= StructInit { "," StructInit } [ "," ] ;
StructInit          ::= Ident ":" Expr ;

ConstExpr           ::= Expr ;
```

### 3.11 时序表达式

```ebnf
TemporalExpr        ::= TemporalImpliesExpr ;

TemporalImpliesExpr ::= TemporalOrExpr { "=>" TemporalOrExpr } ;
TemporalOrExpr      ::= TemporalAndExpr { ( "or" | "||" ) TemporalAndExpr } ;
TemporalAndExpr     ::= TemporalUntilExpr { ( "and" | "&&" ) TemporalUntilExpr } ;
TemporalUntilExpr   ::= TemporalUnaryExpr { "until" TemporalUnaryExpr } ;

TemporalUnaryExpr   ::= "always" TemporalUnaryExpr
                      | "eventually" TemporalUnaryExpr
                      | "next" TemporalUnaryExpr
                      | "not" TemporalUnaryExpr
                      | "!" TemporalUnaryExpr
                      | TemporalAtom ;

TemporalAtom        ::= "(" TemporalExpr ")"
                      | Expr
                      | "called" "(" Ident ")"
                      | "in_state" "(" Ident ")"
                      | "running" "(" Ident ")"
                      | "completed" "(" Ident [ "," Ident ] ")" ;

WorkflowTemporalExpr ::= TemporalExpr ;
```

语义约束：

1. `TemporalAtom` 中裸 `Expr` 必须是纯表达式且类型为 `Bool`
2. `called(x)` 只允许在 agent 合同的 `invariant` / `forbid` 中使用，且 `x` 必须引用已声明 capability
3. `in_state(s)` 只允许在 agent 合同中使用，且 `s` 必须属于绑定 agent 的状态集合
4. `running(n)`、`completed(n)`、`completed(n, s)` 只允许在 workflow `safety` / `liveness` 中使用
5. `completed(n, s)` 中 `s` 必须是节点 `n` 对应 agent 的终态

实现说明：

1. 当前仓库中的 `emit-smv` 只 lower validate 通过后的受限 formal subset
2. `called`、`in_state`、`running`、`completed` 的 backend 映射，以及 `TemporalAtom` 中裸 `Expr` 的 observation abstraction 规则，见 `../design/formal-backend.zh.md`

### 3.12 常量表达式

`ConstExpr` 的语法与 `Expr` 相同，但有更强的语义限制：

1. 只能引用 `const`、枚举变体、字面量
2. 不允许使用 `input`、`ctx`、`output`
3. 不允许调用 `capability` 或 `predicate`
4. 必须可在编译期求值

## 4. 类型系统

### 4.1 类型宇宙

AHFL Core 的**源码可写值类型集合**记为 `T_surface`：

```text
Unit
Bool
Int
Float
String
String(min, max)
UUID
Timestamp
Duration
Decimal(scale)
Optional<T>
List<T>
Set<T>
Map<K, V>
StructName
EnumName
```

其中：

1. `String(min, max)` 是 `String` 的 refinement 形式
2. `StructName` 与 `EnumName` 指代用户声明的具名类型
3. `type alias` 在类型检查阶段按别名透明处理，但它本身不是新的底层值类型
4. AHFL Core 没有 `readonly` 修饰符或 readonly 容器语法；容器 variance 是类型关系规则，不是源码类型构造子

编译器实现还可以维护少量**内部辅助类型**，例如：

```text
Any
Never
Error
```

约束：

1. `Any`、`Never` 与 `Error` 不是源码语法的一部分
2. 用户程序不得显式书写 `Any`、`Never` 或 `Error`
3. `Any` 仅可用于错误恢复、占位或未决类型状态
4. `Never` 仅可用于不可达表达式、终止控制流或类似内部语义位置
5. `Error` 仅作为诊断传播哨兵，标识一次确定失败的类型推导；不得作为正常值的类型
6. 规范后续若写 `T`，默认指源码可写值类型，而不是这些内部辅助类型

这三个内部辅助类型在类型关系求解器（`type_relations.cpp`）中按以下规则处理，用于错误恢复而不污染用户可见的子类型闭包：

1. **等价**：`Any`、`Never`、`Error` 各自与同种辅助类型等价；它们彼此之间不等价（`Any` ≠ `Never` ≠ `Error`），且不与任何源码可写类型 `T` 等价
2. **Error 双向通配**：只要 `Error` 出现在子类型关系 `S <: T` 的某一侧（无论源侧还是目标侧），关系即判定为成立。这样一次真实错误不会级联放大为数十条虚假二级诊断
3. **Any 作为 top**：对任意类型 `T`（包括源码可写类型与 `Never`），都有 `T <: Any`
4. **Never 作为 bottom**：对任意类型 `T`（包括源码可写类型与 `Any`），都有 `Never <: T`
5. **不传染源码关系**：除上述 `Error`/`Any`/`Never` 规则外，源码可写类型之间的子类型关系不受内部辅助类型影响；§4.3.2 列出的源码子类型闭包不因这些辅助类型而扩大

此外还定义两个语义层：

1. `Expr` 的类型判断：`Σ ; Γ ⊢ e : T`
2. `TemporalExpr` 的类型判断：`Σ ; Γ ; C ⊢ ψ : Formula`

其中：

- `Σ` 是全局符号环境
- `Γ` 是局部值环境
- `C` 是上下文环境，取值为 `AgentContract(A)` 或 `WorkflowContract(W)`

### 4.2 符号环境

`Σ` 至少包含下列命名空间：

```text
Σ.types         类型声明（struct / enum / alias）
Σ.consts        常量
Σ.capabilities  capability 声明
Σ.predicates    predicate 声明
Σ.agents        agent 声明
Σ.flows         flow 声明
Σ.workflows     workflow 声明
```

命名规则：

1. 同一命名空间内名称唯一
2. `struct`、`enum`、`type alias` 共用类型命名空间
3. `capability` 与 `predicate` 不共用命名空间
4. agent 内部状态名只在该 agent 的状态命名空间内唯一

### 4.3 类型等价与子类型

采用“**别名透明 + 极小子类型**”策略。

以下规则讨论的对象均为**源码可写类型**。`Any` 与 `Never` 若在实现中存在，仅作为编译器内部辅助类型处理，不参与用户可见的类型语法与常规子类型关系。

#### 4.3.1 别名透明

若：

```text
type A = B
```

则 `A` 与 `B` 在类型检查时等价。

#### 4.3.2 允许的子类型关系

仅允许以下真子类型：

1. `String(m1, n1) <: String(m2, n2)`，当且仅当 `m2 <= m1` 且 `n1 <= n2`
2. `String(m, n) <: String`

除此之外，还允许下列容器子类型关系：

1. `Optional<A> <: Optional<B>`，当且仅当 `A <: B`
2. `List<A> <: List<B>`，当且仅当 `A <: B`
3. `Set<A> <: Set<B>`，当且仅当 `A <: B`
4. `Map<K1, V1> <: Map<K2, V2>`，当且仅当 `K1` 与 `K2` 等价，且 `V1 <: V2`

其他隐式子类型关系不存在。

特别地：

1. `Int` 不是 `Float` 的子类型
2. `Decimal(p)` 不是 `Float` 的子类型
3. `Decimal(p1)` 不是 `Decimal(p2)` 的子类型
4. `T` 不是 `Optional<T>` 的子类型
5. `Map<K, V>` 的 key 位置保持不变；不能因为 `K1 <: K2` 就推出 `Map<K1, V> <: Map<K2, V>`
6. `readonly` 不是保留关键字，用户程序不得书写 `readonly List<T>`、`Readonly<T>` 或等价语法；若未来引入，需要单独修订本规范、grammar、AST、Typed HIR、typechecker 与 IR 表达

### 4.4 结构体与枚举

1. `struct` 使用名义类型
2. `enum` 使用名义类型
3. 结构体字面量必须字段完整、字段名精确匹配、不得包含额外字段
4. 枚举变体必须以 `QualifiedIdent` 形式引用，例如 `AuditResult::Approve`

### 4.5 `none` 与空容器

以下表达式采用**上下文定型**：

1. `none`
2. `[]`
3. `set[]`
4. `map[]`

规则：

1. 若缺少期望类型，编译器必须报错
2. 期望类型可来自变量标注、字段类型、参数类型、返回类型

### 4.6 表达式类型规则

#### 4.6.1 变量与常量

```text
Γ(x) = T
-------------
Σ ; Γ ⊢ x : T

Σ.consts(c) = T
-------------
Σ ; Γ ⊢ c : T
```

对于具名常量路径与枚举变体路径：

```text
Σ.consts(q) = T
----------------------
Σ ; Γ ⊢ q : T

q = EnumName::Variant
variant_type(q) = EnumName
-------------------------
Σ ; Γ ⊢ q : EnumName
```

#### 4.6.2 字段访问

若 `e` 的展开后类型是结构体 `S`，且 `S.f : T`，则：

```text
Σ ; Γ ⊢ e : S
field_type(S, f) = T
--------------------
Σ ; Γ ⊢ e.f : T
```

#### 4.6.3 结构体字面量

```text
fields(S) = { f1:T1, ..., fn:Tn }
Σ ; Γ ⊢ e1 : U1    U1 <: T1
...
Σ ; Γ ⊢ en : Un    Un <: Tn
------------------------------------------------
Σ ; Γ ⊢ S { f1:e1, ..., fn:en } : S
```

要求：

1. 字段必须恰好覆盖 `S` 的全部字段
2. 字段顺序无关

#### 4.6.4 `some` 与 `none`

```text
Σ ; Γ ⊢ e : T
-------------------------
Σ ; Γ ⊢ some(e) : Optional<T>
```

`none` 没有独立可推导类型；它只能在期望类型为 `Optional<T>` 的上下文中成立。

#### 4.6.5 capability 调用

若：

```text
Σ.capabilities(k) = (T1, ..., Tn) -> R
```

则 capability 调用的类型规则是：

```text
Σ ; Γ ⊢ e1 : U1    U1 <: T1
...
Σ ; Γ ⊢ en : Un    Un <: Tn
--------------------------------------
Σ ; Γ ⊢ k(e1, ..., en) : R
```

但 capability 调用只允许出现在：

1. `flow` handler 中
2. 且调用 capability 必须在对应 agent 的 `capabilities` 白名单内

因此 capability 调用的完整合法性还需要额外上下文检查：

```text
k ∈ capabilities(agent_of_current_flow)
```

#### 4.6.6 predicate 调用

若：

```text
Σ.predicates(p) = (T1, ..., Tn) -> Bool
```

则：

```text
Σ ; Γ ⊢ e1 : U1    U1 <: T1
...
Σ ; Γ ⊢ en : Un    Un <: Tn
--------------------------------------
Σ ; Γ ⊢ p(e1, ..., en) : Bool
```

predicate 调用允许出现在：

1. 合同表达式
2. `assert`
3. `if` 条件
4. 其他纯布尔表达式上下文

#### 4.6.7 运算符

运算符规则如下：

1. `+ - * / %`：
   - `Int × Int -> Int`
   - `Float × Float -> Float`
   - `Decimal(p)` 仅允许与相同 `p` 的 `Decimal(p)` 做 `+`、`-`
   - `String + String -> String`
   - `Int` 与 `Float`、`Int` 与 `Decimal(p)`、不同 scale 的 `Decimal` 之间不存在隐式运算 promotion
   - `Decimal(p) * Decimal(p)`、`Decimal(p) / Decimal(p)` 未定义
2. 比较运算：
   - 两侧类型必须相同，或左侧为右侧子类型，或右侧为左侧子类型
3. 逻辑运算：
   - `and` / `or` / `not` 仅作用于 `Bool`
4. `=>`：
   - 仅作用于 `Bool`

测试要求：混合 numeric operator、不同 scale `Decimal` 运算、`Decimal` 乘除、以及 `Int < Float` 必须以稳定诊断
`typecheck.INVALID_OPERATION` 失败。`TypeRelationOptions::allow_numeric_widening` 仅可用于显式兼容或分析模式，
不得改变源码表达式类型规则。

#### 4.6.8 表达式 Effect 分级

除类型判断 `Σ ; Γ ⊢ e : T` 外，编译器对每个 `Expr` 同时维护一个 **effect 等级** `ε(e)`，用于在静态语义层区分纯/不纯表达式，是 §3.9、§4.6.5、§4.7.2 中"纯表达式"约束的判定依据（实现见 `src/compiler/semantics/effects.cpp`）。Effect 等级按副作用强度递增分为 6 级：

| 等级 | 名称 | 典型来源 | 是否纯 |
| --- | --- | --- | --- |
| 0 | `Pure` | 字面量、变量引用、字段访问、纯算术/比较/逻辑运算 | 是 |
| 1 | `ConstOnly` | 引用 `const` 常量或枚举变体，但表达式中不含运行时输入或调用 | 是 |
| 2 | `PredicateCall` | `predicate(...)` 调用及其组合（predicate 自身保证纯、确定、无副作用） | 是 |
| 3 | `CapabilityCall` | `capability(...)` 调用（外部 effectful 调用点） | 否 |
| 4 | `ExternalEffect` | 显式标注为 `external_side_effect`/`durable_write`/`financial_write` 的 capability 调用 | 否 |
| 5 | `Unknown` | 无法静态判定的 effect，或 effect profile 为 `unknown` 的 capability 调用 | 否 |

合成规则（`join_effects`）：

1. 子表达式 effect 通过取两侧等级的**较大者**汇合，即 `join(ε₁, ε₂) = ε₁` 当 `rank(ε₁) ≥ rank(ε₂)`，否则 `ε₂`
2. 因此 `if cond ...`、`assert cond`、`requires`、`ensures` 等"要求纯表达式"位置，校验条件等价于 `rank(ε(e)) ≤ 2`（即 `Pure`、`ConstOnly` 或 `PredicateCall`）
3. `ExprStmt`（§3.9）允许 `CapabilityCall`/`ExternalEffect`/`Unknown`，但仅在 `flow` handler 内、且 capability 在当前 agent 白名单中时合法（§4.6.5）
4. `predicate` 声明的函数体若出现 `rank(ε) ≥ 3`，违反"predicate 不得调用 capability"约束（§3.4 语义约束 3）

effect 等级不改变表达式的**值类型** `T`，仅作为附加静态事实参与合法性判定。

### 4.7 合同与时序公式类型规则

#### 4.7.1 合同上下文

对于 `contract for A`：

- `requires` 的环境为：
  - `input : A.input`
- `ensures` 的环境为：
  - `input : A.input`
  - `output : A.output`
- `invariant` / `forbid` 的环境为：
  - `input : A.input`
  - 允许使用 `called(capability)` 与 `in_state(state)`

#### 4.7.2 snapshot 表达式

若 `Expr` 在合同上下文中：

1. 类型必须为 `Bool`
2. 必须是纯表达式
3. 不得包含 capability 调用

#### 4.7.3 时序原子

在 `AgentContract(A)` 中：

```text
s ∈ states(A)
----------------------------
Σ ; Γ ; AgentContract(A) ⊢ in_state(s) : Formula

k ∈ Σ.capabilities
----------------------------
Σ ; Γ ; AgentContract(A) ⊢ called(k) : Formula
```

在 `WorkflowContract(W)` 中：

```text
n ∈ nodes(W)
--------------------------------
Σ ; Γ ; WorkflowContract(W) ⊢ running(n) : Formula

n ∈ nodes(W)
--------------------------------
Σ ; Γ ; WorkflowContract(W) ⊢ completed(n) : Formula

n ∈ nodes(W)   s ∈ final_states(agent_of(n))
------------------------------------------------
Σ ; Γ ; WorkflowContract(W) ⊢ completed(n, s) : Formula
```

#### 4.7.4 时序连接词

若 `ψ1`、`ψ2` 为 `Formula`，则：

```text
always ψ1      : Formula
eventually ψ1  : Formula
next ψ1        : Formula
ψ1 until ψ2    : Formula
ψ1 and ψ2      : Formula
ψ1 or ψ2       : Formula
ψ1 => ψ2       : Formula
```

### 4.8 flow 类型规则

对于 `flow for A` 中绑定到状态 `q` 的 handler：

局部环境初始包含：

```text
input : A.input
ctx   : A.context
```

#### 4.8.1 赋值

```text
Σ ; Γ ⊢ l : T
Σ ; Γ ⊢ e : U
U <: T
----------------
Σ ; Γ ⊢ l = e ok
```

#### 4.8.2 `if`

```text
Σ ; Γ ⊢ cond : Bool
Σ ; Γ ⊢ then_block ok
Σ ; Γ ⊢ else_block ok
--------------------------------
Σ ; Γ ⊢ if cond ... else ... ok
```

#### 4.8.3 `goto`

```text
(q -> q') ∈ transitions(A)
--------------------------
Σ ; Γ ; A ; q ⊢ goto q' ok
```

#### 4.8.4 `return`

```text
q ∈ final_states(A)
Σ ; Γ ⊢ e : U
U <: A.output
--------------------------
Σ ; Γ ; A ; q ⊢ return e ok
```

#### 4.8.5 handler 完整性

静态检查器必须验证：

1. 非终态 handler 的所有控制流出口都以 `goto` 结束
2. 终态 handler 的所有控制流出口都以 `return` 结束
3. `return` 不得出现在非终态 handler 中
4. `goto` 不得跳转到非法状态

### 4.9 workflow 类型规则

对于：

```ahfl
workflow W {
    input: I;
    output: O;
    node n1: A1(e1);
    node n2: A2(e2) after [n1];
    return: r;
}
```

类型检查规则如下。

#### 4.9.1 节点输入

若 `Ai.input = Ti` 且：

```text
Σ ; ΓW ⊢ ei : Ui
Ui <: Ti
```

则节点输入类型合法。

#### 4.9.2 节点环境

在检查节点 `n` 的输入表达式时，环境包含：

1. `input : W.input`
2. `after` 中直接依赖节点的输出值

具体地，若 `after [a, b]`，且：

```text
agent_of(a).output = Ta
agent_of(b).output = Tb
```

则：

```text
ΓW = { input : W.input, a : Ta, b : Tb }
```

这意味着：

1. 一个节点只能直接引用 workflow 输入和其直接依赖节点的输出
2. 不允许引用未声明依赖节点的输出

#### 4.9.3 DAG 约束

workflow 的节点依赖图必须无环。

#### 4.9.4 `return`

检查 `return` 时，环境包含：

1. `input : W.input`
2. 所有节点名，且每个节点名绑定到其 agent 输出类型

若：

```text
Σ ; Γreturn ⊢ r : U
U <: W.output
```

则 `return` 合法。

### 4.10 运行时边界校验

以下边界采用**精确 schema 匹配**：

1. agent 输入
2. agent 输出
3. agent context 默认值
4. workflow 输入
5. workflow 输出
6. 节点调用实参

精确 schema 匹配的含义：

1. 结构体字段不得缺失
2. 结构体字段不得额外出现
3. 每个字段值必须满足字段类型与 refinement 约束

## 5. 静态检查清单

编译器前端至少应实现下列检查：

### 5.1 声明级检查

1. 重名检查
2. 未声明引用检查
3. 循环别名检查
4. 非法导入检查

### 5.2 agent 检查

1. 状态集一致性
2. 初始/终态合法性
3. 转移源目标合法性
4. 状态可达性
5. 终态无出边
6. capability 白名单合法性

### 5.3 contract 检查

1. `requires` / `ensures` 为纯布尔表达式
2. `invariant` / `forbid` 为合法时序公式
3. 状态引用合法
4. capability 引用合法
5. 合同上下文变量引用合法

### 5.4 flow 检查

1. `flow for A` 对应 agent 存在
2. handler 状态不重复
3. 非终态 handler 完整
4. 终态 handler 完整
5. `goto` 合法
6. capability 调用受白名单约束

### 5.5 workflow 检查

1. 节点名唯一
2. 节点绑定 agent 存在
3. `after` 依赖存在
4. 依赖图无环
5. 节点输入表达式只引用允许的依赖节点
6. `return` 类型匹配

## 6. 最小一致性示例

```ahfl
module refund::audit;

struct RefundRequest {
    order_id: String;
    user_id: String;
    refund_amount: Decimal(2);
}

struct RefundContext {
    result: AuditResult = AuditResult::Reject;
    reason: String = "pending";
    ticket_id: Optional<String> = none;
}

struct RefundDecision {
    result: AuditResult;
    reason: String;
    ticket_id: Optional<String>;
}

struct OrderInfo {
    order_id: String;
    user_id: String;
    total_amount: Decimal(2);
}

struct AuditReply {
    result: AuditResult;
    reason: String;
    need_ticket: Bool;
}

enum AuditResult {
    Approve,
    Reject,
}

capability OrderQuery(order_id: String) -> OrderInfo;
capability AuditDecision(order: OrderInfo, request: RefundRequest) -> AuditReply;
capability TicketCreate(order_id: String, reason: String) -> String;
capability RefundExecute(order_id: String, amount: Decimal(2)) -> Unit;
capability UserInfoModify(user_id: String) -> Unit;

predicate order_exists(order_id: String) -> Bool;
predicate order_belongs_to_user(order_id: String, user_id: String) -> Bool;
predicate refund_amount_within_total(order_id: String, amount: Decimal(2)) -> Bool;
predicate non_empty(value: String) -> Bool;

agent RefundAudit {
    input: RefundRequest;
    context: RefundContext;
    output: RefundDecision;
    states: [Init, Auditing, Approved, Rejected, Terminated];
    initial: Init;
    final: [Terminated];
    capabilities: [OrderQuery, AuditDecision, TicketCreate];
    quota: {
        max_tool_calls: 5;
        max_execution_time: 30s;
    }

    transition Init -> Auditing;
    transition Auditing -> Approved;
    transition Auditing -> Rejected;
    transition Approved -> Terminated;
    transition Rejected -> Terminated;
}

contract for RefundAudit {
    requires: order_exists(input.order_id);
    requires: order_belongs_to_user(input.order_id, input.user_id);
    requires: refund_amount_within_total(input.order_id, input.refund_amount);
    ensures: non_empty(output.reason);
    invariant: always not called(RefundExecute);
    forbid: always not called(UserInfoModify);
}

flow for RefundAudit {
    state Init {
        goto Auditing;
    }

    state Auditing with {
        retry: 2;
        retry_on: [TimeoutError, ToolError];
        timeout: 30s;
    } {
        let order = OrderQuery(input.order_id);
        let decision = AuditDecision(order, input);

        ctx.result = decision.result;
        ctx.reason = decision.reason;

        if decision.result == AuditResult::Approve {
            ctx.ticket_id = some(TicketCreate(input.order_id, decision.reason));
            goto Approved;
        } else {
            ctx.ticket_id = none;
            goto Rejected;
        }
    }

    state Approved {
        goto Terminated;
    }

    state Rejected {
        goto Terminated;
    }

    state Terminated {
        return RefundDecision {
            result: ctx.result,
            reason: ctx.reason,
            ticket_id: ctx.ticket_id,
        };
    }
}

workflow RefundAuditWorkflow {
    input: RefundRequest;
    output: RefundDecision;

    node audit: RefundAudit(input);

    safety: always not running(audit) or eventually completed(audit);
    liveness: eventually completed(audit, Terminated);

    return: audit;
}
```

## 7. 结论

这份规范刻意做了三件事：

1. 把语法面收窄到可实现、可检查、可生成 parser 的范围
2. 把 effectful capability、纯 predicate、时序公式三层语义拆开
3. 把类型系统收敛为“名义类型 + 极小 refinement + 精确边界匹配”

如果后续要扩展到 `AHFL Native`，建议只在这份 Core 规范之上追加：

1. `tool` 实现体
2. `llm_config`
3. `observability`
4. `compliance`
5. 更丰富的 workflow 条件分支与 saga 语义
