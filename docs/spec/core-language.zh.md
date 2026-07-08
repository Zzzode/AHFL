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
6. 高阶类型（HKT）、union 类型与 intersection 类型——这三类抽象在本规范覆盖的 Core 范围内不提供，且未在演进路径上

### 1.1 语言演进路径

历史上本规范将"通用高阶函数与用户自定义泛型"列为 out-of-scope。该条目已被撤销：函数抽象（`fn`）、用户泛型（单态化实现）、一等闭包、`trait` / typeclass、ADT（`enum` 带 payload）以及统一 effect 系统不再由本规范排除，而是由 [corelib RFC](../design/corelib-rfc.zh.md) 定义的可验证演进路径统一承载。该 RFC 把上述能力的语法形态、类型规则、stdlib 库化（容器从关键字迁移为库类型）、以及进入 `contract` / `invariant` / `safety` / `liveness` / `flow guard` 的可验证子集（`effect Pure` + `decreases` 终止度量 + bounded refinement）一并规定，并给出 P0–P7 分阶段迁移计划。函数抽象 / 泛型 / 闭包 / `trait` / corelib 边界的最终定义以该 RFC 及其四份 detail 附件为准；本规范不再各自重复。

### 1.2 语言演进原则

> **语言表达力与可验证性正交——用可验证子集连接二者，不用降级语言换可判定性。**

这是本规范后续一切类型系统与 effect 演进的指导原则。具体含义：

1. 引入新的表达力（`fn`、泛型、闭包、`trait`、ADT）本身不削弱可验证性；可判定性由调用是否落入可验证子集（`effect Pure` + `decreases` + bounded）静态判定
2. 子集之外的代码（IO、非确定、无度量的递归）合法存在，服务于 runtime / capability / workflow 路径，仅不进入 `contract` / `invariant` / temporal 公式
3. 不得通过"砍掉 trait / 闭包 / ADT"来换取可判定性——这与上述正交原则相悖

## 2. 词法定义

### 2.1 注释与空白

- 单行注释：`// ...`
- 文档注释：`/// ...`
- 多行注释：`/* ... */`
- 多行注释**不支持嵌套**
- 空白字符包括空格、制表符、换行符、回车符

### 2.2 保留关键字

```text
module import use as pub
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
set map
```

说明：`Optional`、`List`、`Set`、`Map` 不属于 language primitive prelude。
它们在源码中只是普通名义类型路径，必须通过 package dependency 与显式
`import` 进入作用域。语言层面永远可见的 primitive 只有
`Unit`、`Bool`、`Int`、`Float`、`String`、`UUID`、`Timestamp`、
`Duration`、`Decimal`；用户顶层声明不得覆盖这些 primitive 名称。

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

Visibility      ::= "pub" ;

TopLevelDecl    ::= [ Visibility ] ConstDecl
                  | [ Visibility ] TypeAliasDecl
                  | [ Visibility ] StructDecl
                  | [ Visibility ] EnumDecl
                  | [ Visibility ] CapabilityDecl
                  | [ Visibility ] PredicateDecl
                  | [ Visibility ] AgentDecl
                  | ContractDecl
                  | FlowDecl
                  | [ Visibility ] WorkflowDecl
                  | [ Visibility ] FnDecl
                  | [ Visibility ] TraitDecl
                  | UseDecl ;

ModuleDecl      ::= "module" QualifiedIdent ";" ;
ImportDecl      ::= "import" QualifiedIdent [ "as" Ident ] ";" ;
UseDecl         ::= [ Visibility ] "use" QualifiedIdent [ "as" Ident ] ";" ;
```

### 3.1.1 符号可见性

顶层命名 declaration 默认是 package-internal；同 package 内可按正常
`import`/name lookup 使用，跨 package 不可见。`pub` 表示 source-level public
intent，但跨 package 使用还必须同时满足 dependency、module export 或
API-reachable `pub use` facade、当前 source 显式 import、以及 public signature
well-formedness。

`pub use` 是独立的 public alias declaration。它拥有自己的 source range 与
navigation identity，但类型等价、method receiver identity 和 trait matching
仍使用 target declaration identity。`pub use` 不支持 enum variant、impl item、
associated item 或 trait conformance re-export。

位于非 exported module 且未被 API-reachable facade 或 handoff artifact export
触达的 `pub` symbol 会产生 `visibility.UNREACHABLE_PUBLIC` warning。handoff
target export 只能引用 `pub` source symbol；否则必须产生
`visibility.HANDOFF_EXPORT_PRIVATE_SYMBOL`。

### 3.2 类型定义

```ebnf
Type            ::= PrimitiveType
                  | FnType
                  | NamedType ;

PrimitiveType   ::= "Unit"
                  | "Bool"
                  | "Int"
                  | "Int" "(" SignedIntLiteral "," SignedIntLiteral ")"
                  | "Float"
                  | "String"
                  | "String" "(" IntLiteral "," IntLiteral ")"
                  | "UUID"
                  | "Timestamp"
                  | "Duration"
                  | "Decimal" "(" IntLiteral ")" ;

SignedIntLiteral ::= [ "-" ] IntLiteral ;

NamedType       ::= QualifiedIdent [ "<" Type { "," Type } [ "," ] ">" ] ;
```

### 3.3 常量、结构体、枚举、别名

```ebnf
ConstDecl       ::= "const" Ident ":" Type "=" ConstExpr ";" ;

TypeAliasDecl   ::= "type" Ident "=" Type ";" ;

StructDecl      ::= "struct" Ident "{" { StructFieldDecl } "}" ;
StructFieldDecl ::= Ident ":" Type [ "=" ConstExpr ] ";" ;

EnumDecl        ::= "enum" Ident [ TypeParams ] "{" EnumVariant { "," EnumVariant } [ "," ] "}" ;
EnumVariant     ::= Ident
                  | Ident "(" Type { "," Type } [ "," ] ")"
                  | Ident "{" EnumVariantField { "," EnumVariantField } [ "," ] "}" ;
EnumVariantField ::= Ident ":" Type [ "=" ConstExpr ] ;
```

约束：

1. `struct` 字段名在同一结构体内唯一
2. `enum` 变体名在同一枚举内唯一
3. enum variant payload shape 是语义身份的一部分：
   - `Name` 是 unit variant
   - `Name(T1, T2, ...)` 是 tuple variant
   - `Name { f1: T1, f2: T2, ... }` 是 struct variant
4. struct variant 字段名在同一 variant 内唯一；字段默认值必须是 const 表达式，且类型可赋给字段声明类型
5. 同一 module 内，variant 名不得与 `struct`、`enum` 或 `type alias` 的类型名冲突
6. pattern 与构造语法必须匹配 variant payload shape；tuple/struct/unit 之间不得互换使用
7. struct variant pattern 若未写 `..`，必须覆盖全部字段；构造表达式可省略有默认值的字段
8. 若某个 `struct` 被用作 agent `context` 类型，则其所有字段必须提供默认值

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
                      | IfLetStmt
                      | GotoStmt
                      | ReturnStmt
                      | AssertStmt
                      | ExprStmt ;

LetStmt             ::= "let" Ident [ ":" Type ] "=" Expr ";" ;
AssignStmt          ::= LValue "=" Expr ";" ;
IfStmt              ::= "if" Expr Block [ "else" Block ] ;
IfLetStmt           ::= "if" "let" Pattern "=" Expr Block [ "else" Block ] ;
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
5. `if let` scrutinee 必须是纯 enum 表达式；pattern 使用与 `match` arm 相同的 `Pattern` 语法，variant 必须存在，payload shape 必须与声明一致。
6. `if let Variant(x, y) = e` 匹配 tuple variant；`if let Variant { field, .. } = e` 匹配 struct variant payload；then block 内引入 pattern binding，binding 类型按 scrutinee enum 的泛型实参替换。
7. `if let` then block 获得匹配成功的局部 narrowing fact；else block 获得互补 fact。赋值到被窄化 path 或其后代 path 会失效这些 fact。

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

PostfixExpr         ::= PrimaryExpr { "." Ident | "." Ident "(" [ ExprList ] ")" | "[" Expr "]" } ;

PrimaryExpr         ::= Literal
                      | QualifiedIdent
                      | PathExpr
                      | CallExpr
                      | StructLiteral
                      | ListLiteral
                      | SetLiteral
                      | MapLiteral
                      | MatchExpr
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

MatchExpr           ::= "match" Expr "{" { MatchArm } "}" ;
MatchArm            ::= Pattern [ "if" Expr ] "=>" Expr [ "," ] ;

Pattern             ::= OrPattern ;
OrPattern           ::= PrimaryPattern { "|" PrimaryPattern } ;
PrimaryPattern      ::= "_"
                      | Literal
                      | VariantPattern
                      | BindingPattern
                      | TuplePattern ;
VariantPattern      ::= QualifiedIdent
                      | QualifiedIdent "(" PatternList ")"
                      | QualifiedIdent "{" [ PatternFieldList ] "}"
                      | Ident "(" PatternList ")"
                      | Ident "{" [ PatternFieldList ] "}" ;
PatternList         ::= Pattern { "," Pattern } [ "," ] ;
PatternFieldList    ::= PatternField { "," PatternField } [ "," ] ;
PatternField        ::= Ident ":" Pattern | Ident | ".." ;
BindingPattern      ::= [ "mut" ] Ident [ "@" Pattern ] ;
TuplePattern        ::= "(" [ PatternList ] ")" ;

ConstExpr           ::= Expr ;
```

语义约束：

1. `Enum::TupleVariant(a, b)` 使用 tuple constructor；实参数量与类型必须匹配 variant payload。
2. `Enum::StructVariant { field: value }` 使用 struct variant constructor；字段名必须存在，必填字段不得缺失，重复字段禁止。
3. `Enum::UnitVariant` 是 unit variant value；对非 unit variant 省略 payload 是错误。
4. match scrutinee 必须是 enum；match 必须由无 guard 的 arm 覆盖所有 variant，否则报告 `typecheck.MATCH_MISSING_PATTERNS`。
5. `_` 与 catch-all binding 覆盖所有 variant；具体 enum variant pattern 只覆盖对应 variant；or-pattern 覆盖其可识别分支的并集。
6. 带 guard 的 match arm 不贡献穷尽性覆盖，因为 guard 在运行时可能为 false；它仍可参与结构性 overlap warning。
7. 被前序无 guard arm 完全覆盖的 arm 报告 `typecheck.MATCH_UNREACHABLE_ARM` warning；与前序 arm 结构性相交的 arm 报告 `typecheck.MATCH_OVERLAP` warning。
8. variant pattern 的 payload shape 必须与声明一致；`Some(x)`、`Data { code }` 与 `Empty` 不能互换。
9. struct variant pattern 支持字段 shorthand、`field: pattern` 与 `..`；未使用 `..` 时必须覆盖全部字段。
10. `match` 的具体 top-level variant arm 会在 arm body 内窄化 scrutinee path；wildcard、catch-all binding 与 or-pattern 不产生唯一 variant fact。
11. `is_some`、`is_none`、`is_ok`、`is_err` 的零参数 method call 在 `if` 条件中会产生局部分支 narrowing fact；receiver 必须是可窄化 path，否则不产生 fact。

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
Int(min, max)
Float
String
String(min, max)
UUID
Timestamp
Duration
Decimal(scale)
StructName
EnumName
Qualified::Nominal<T...>
```

其中：

1. `Int(min, max)` 是 `Int` 的闭区间 refinement 形式，`String(min, max)` 是
   `String` 的 refinement 形式
2. `Unit`、`Bool`、`Int`、`Float`、`String`、`UUID`、`Timestamp`、
   `Duration`、`Decimal` 是 language primitive prelude；它们不经过 ordinary
   name lookup，也不需要 `import`
3. `StructName`、`EnumName` 与 `Qualified::Nominal<T...>` 指代当前 package
   或显式 import 后可见的名义类型
4. `type alias` 在类型检查阶段按别名透明处理，但它本身不是新的底层值类型
5. AHFL Core 没有 `readonly` 修饰符或 readonly 容器语法；容器 variance 是类型关系规则，不是源码类型构造子
6. `std::option::Option<T>`、`std::collections::List<T>`、
   `std::collections::Set<T>`、`std::collections::Map<K, V>` 是 standard-library
   nominal types，不是 primitive。用户 package 必须声明
   `std = { source = "sysroot" }` dependency，并显式 import 对应 module
   或 alias 后才能使用这些类型；detached 单文件模式不会隐式注入它们。

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

1. `Int(m1, n1) <: Int(m2, n2)`，当且仅当 `m2 <= m1` 且 `n1 <= n2`
2. `Int(m, n) <: Int`
3. `String(m1, n1) <: String(m2, n2)`，当且仅当 `m2 <= m1` 且 `n1 <= n2`
4. `String(m, n) <: String`

除此之外，所有**名义泛型类型**（`struct<T...>` / `enum<T...>`）的子类型关系按其类型参数上声明的 **variance** 决定。variance 的完整设计（通过 trait 约束对每个泛型参数声明 `covariant` / `contravariant` / `invariant`）在 **PHASE B** 中落地。在当前阶段，以下 variance 对 stdlib 容器按名称硬编码生效，其余用户定义的名义泛型一律视为 invariant：

| 名义类型 | 参数 0 | 参数 1 |
| --- | --- | --- |
| `std::option::Option<T>` | covariant | — |
| `std::result::Result<T, E>` | （注：当前保持 invariant，待 PHASE B 与 `Option` 对齐） | — |
| `std::collections::List<T>` | covariant | — |
| `std::collections::Set<T>` | covariant | — |
| `std::collections::Map<K, V>` | invariant（K） | covariant（V） |
| 其它用户 `struct` / `enum<T...>` | invariant | invariant |

对当前实现的说明（parenthetical）：截至本版本，`Result` 的 variance 尚未与 `Option` 对齐；容器级别的 covariance 实现位于 `src/compiler/semantics/type_relations.cpp` 中名义 struct/enum 的专用子类型分支，在 trait-based variance 系统交付前暂时以 canonical-name 分派。

其他隐式子类型关系不存在。

特别地：

1. `Int` 不是 `Float` 的子类型
2. `Decimal(p)` 不是 `Float` 的子类型
3. `Decimal(p1)` 不是 `Decimal(p2)` 的子类型
4. `T` 不是 `std::option::Option<T>` 的子类型
5. `std::collections::Map<K, V>` 的 key 位置保持不变；不能因为 `K1 <: K2`
   就推出 `std::collections::Map<K1, V> <: std::collections::Map<K2, V>`
6. `readonly` 不是保留关键字，用户程序不得书写 `readonly collections::List<T>`、
   `Readonly<T>` 或等价语法；若未来引入，需要单独修订本规范、grammar、AST、
   Typed HIR、typechecker 与 IR 表达

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

#### 4.5.1 Typed HIR 脱糖（desugaring）

上述空字面量在 Typed HIR 中均不再保留独立 AST 节点，而是被**脱糖**为对应名义容器的标准构造形式（以保持 typed tree 统一为 std-nominal）：

| 源码形式 | 期望类型 | 脱糖后的 typed HIR |
| --- | --- | --- |
| `none` | `std::option::Option<T>` | `std::option::Option::None`（带泛型实参 `T` 的枚举变体值） |
| `[]` | `std::collections::List<T>` | `std::collections::list_from_array<T>()` 或等价的名义空 List 构造子 |
| `set[]` | `std::collections::Set<T>` | `std::collections::set_from_array<T>()` 或等价的名义空 Set 构造子 |
| `map[]` | `std::collections::Map<K, V>` | `std::collections::map_from_entries<K, V>()` 或等价的名义空 Map 构造子 |

因此类型检查阶段不再为"空容器字面量"维护专用代码路径；所有空形态最终都进入名义 struct/enum 的构造与类型关系通用路径。

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

#### 4.6.4 `some()` 与 `none()`

源码语法保留 `some(e)` / `none` 作为表达式一级的语法糖（§3.10 PrimaryExpr）。**在 Typed HIR 中二者均被脱糖为名义 enum `std::option::Option` 的构造函数**：

| 源码形式 | Typed HIR 脱糖结果 |
| --- | --- |
| `some(e)` | `std::option::Option::Some(E)`，其中 `E` 是 `e` 的脱糖后 typed 表达式；推导类型为 `std::option::Option<T>`，`T = type_of(E)` |
| `none` | 参见 §4.5.1：脱糖为 `std::option::Option::None`，要求上下文提供 `std::option::Option<T>` 期望类型以补全泛型实参 `T` |

等价的推导规则可写作：

```text
Σ ; Γ ⊢ e : T
-----------------------------------------------------------
Σ ; Γ ⊢ some(e) : std::option::Option<T>
        (脱糖后值为 std::option::Option::Some(e))

expected_type(Γ) = std::option::Option<T>
-----------------------------------------------------------
Σ ; Γ ⊢ none : std::option::Option<T>
        (脱糖后值为 std::option::Option::None)
```

因此 `std::option::Option<T>` 的子类型与 variance 行为完全由名义 enum 泛型规则（§4.3.2）驱动，`some(e)` 和 `none` 没有专用的子类型路径。

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
   - `Decimal(p) + Decimal(p) -> Decimal(p)`；`Decimal(p) - Decimal(p) -> Decimal(p)`
   - `Decimal(p) * Decimal(q) -> Decimal(p + q)`；乘法 scale 加法溢出时该表达式不成立
   - `String + String -> String`
   - `Int(min, max)` 参与 bounded Int range inference：`+`、`-`、`*`
     对两个 bounded operand 推导闭区间；`/` 仅在 divisor interval 静态排除 0
     时推导闭区间；`%` 对 singleton operand 推导精确 singleton，对非 singleton
     operand 推导保守 remainder 闭区间。溢出、除 0 可能性或无法证明的边界回退为
     普通 `Int`
   - `Int` 与 `Float`、`Int` 与 `Decimal(p)`、不同 scale 的 `Decimal` 之间不存在隐式运算 promotion
   - `Decimal(p) / Decimal(q)` 未定义；未来若支持，必须先定义 rounding / target scale policy
2. 比较运算：
   - 两侧类型必须相同，或左侧为右侧子类型，或右侧为左侧子类型
3. 逻辑运算：
   - `and` / `or` / `not` 仅作用于 `Bool`
4. `=>`：
   - 仅作用于 `Bool`

测试要求：混合 numeric operator、不同 scale `Decimal` 加减、`Decimal` 除法、以及 `Int < Float` 必须以稳定诊断
`typecheck.INVALID_OPERATION` 失败；`Decimal` 乘法必须以 product scale 参与 assignability 检查。
`TypeRelationOptions::allow_numeric_widening` 仅可用于显式兼容或分析模式，
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
