# AHFL Core V0.1

> Note
>
> This file is a scope-and-design note.
> The normative grammar and type-system definition lives in
> [ahfl-spec-v0.1-zh.md](/Users/bytedance/Develop/AHFL/docs/ahfl-spec-v0.1-zh.md).

## Positioning

AHFL should start as a **control-plane DSL for agent execution governance**, not as a general-purpose programming language and not as a full business implementation language.

The core idea is:

- `agent` defines the executable boundary of an agent.
- `contract` defines the safety and correctness properties around that boundary.
- `workflow` defines composition between bounded agents.

Everything else is secondary in V0.1.

## Why Narrow The Scope

Your original design tries to solve five problems at once:

1. Agent specification
2. Agent implementation
3. Runtime policy enforcement
4. Workflow orchestration
5. Formal verification and compliance

That is directionally correct, but too much for a first language version. A usable V0.1 should solve the first three well enough to support the fourth, while leaving the fifth on a constrained subset.

## Core Design Goals

V0.1 should optimize for:

1. A small and unambiguous grammar
2. A small and sound type system
3. Static validation of state machines, I/O schemas, and capability policies
4. Runtime-checkable contracts
5. A clean lowering target for policy engines and model-check subsets

V0.1 should explicitly not optimize for:

1. Arbitrary imperative business logic
2. Embedded tool bodies
3. Embedded LLM prompt/config execution
4. Full observability/compliance authoring
5. A built-in HTTP/gRPC runtime launcher

## What Stays In Core

The minimal surface should contain only these declaration kinds:

- `module`
- `import`
- `struct`
- `enum`
- `type`
- `capability`
- `predicate`
- `agent`
- `contract`
- `workflow`

## What Moves Out Of Core

These should be deferred to `AHFL Native` or later versions:

- `tool { ... }` implementation bodies
- `llm_config`
- imperative `flow` blocks
- `lifecycle`
- `observability`
- `compliance`
- `main`
- built-in HTTP/gRPC server startup
- retry/backoff/timeout handlers as executable code
- saga compensation bodies

V0.1 can still reference these concepts through metadata, but should not execute them directly in the language.

## Semantic Model

### Agent

An `agent` is a finite-state controller with:

- typed input
- typed output
- a finite set of states
- a single initial state
- one or more final states
- an explicit transition relation
- an allow-list of capabilities
- an optional deny-list
- optional quotas as metadata

In V0.1, the `agent` declaration is **declarative only**. It does not contain arbitrary imperative code.

### Capability

A `capability` is an externally provided callable interface known to the runtime.

It has:

- a name
- a typed parameter list
- a typed return value

It does not contain an implementation body in Core V0.1.

### Predicate

A `predicate` is a pure boolean-valued function used by contracts and model-check lowering.

Restrictions:

- side-effect free
- deterministic
- terminating
- no tool calls
- no LLM calls

If a runtime wants to back a predicate with external data, that mapping happens outside the Core language.

### Contract

A `contract` attaches safety and correctness properties to an `agent`.

Core clauses:

- `require`: must hold before execution starts
- `ensure`: must hold on successful completion
- `always`: invariant over the execution trace
- `never`: forbidden event or state pattern

To keep verification tractable, contracts should use a separate expression layer from normal value expressions.

### Workflow

A `workflow` is a DAG of agent or service nodes with typed edges and global properties.

Core V0.1 supports:

- node declarations
- edge declarations
- entry conditions
- safety properties
- liveness properties

It does not support arbitrary inline code blocks.

## Expression Layers

One of the biggest problems in the current design is that everything is an `Expr`. V0.1 should split expressions into three layers.

### 1. Value Expressions

Used for data values only.

Examples:

- literals
- field access
- equality and ordering
- arithmetic
- constructor calls

### 2. Predicate Expressions

Used in `require` and some `ensure` clauses.

Examples:

- `order_exists(input.order_id)`
- `refund_amount_within_total(input.order_id, input.refund_amount)`
- `current_state == Approved`

### 3. Temporal Formulas

Used in `always`, `never`, workflow `safety`, and workflow `liveness`.

V0.1 should support **one temporal logic only**. Use LTL first. Do not mix LTL and CTL in the same grammar/version.

Recommended temporal operators for V0.1:

- `always`
- `eventually`
- `next`
- `until`

Do not expose raw `A` and `E` in Core V0.1.

## Operator Policy

The language should adopt one operator per meaning.

Recommended choices:

- `->` for state transitions and workflow edges
- `=>` for logical implication
- `or` for boolean disjunction
- `and` for boolean conjunction
- `par` as a keyword for parallel workflow branches

Unicode operators can be formatter sugar later, but should not be part of the canonical grammar in V0.1.

## Core Type System

V0.1 should prefer a small, explicit, largely nominal type system.

### Primitive Types

- `Unit`
- `Bool`
- `Int`
- `Decimal(scale)`
- `String`
- `UUID`
- `Timestamp`

### Composite Types

- `Optional<T>`
- `List<T>` as an immutable sequence
- `Set<T>` as an immutable set
- `Map<K, V>` as an immutable map
- named `struct`
- named `enum`

### Type Policy

V0.1 should use these rules:

1. No implicit numeric subtyping
2. No `Int <: Float`
3. No `Decimal <: Float`
4. No implicit `T -> Optional<T>` lifting in the type system
5. No structural subtyping for user-declared `enum`
6. No mutable collection covariance
7. Exact type equality by default, with a small number of explicit coercions

This keeps the checker implementable and avoids hidden complexity.

## Recommended Static Checks

### Agent Checks

The compiler should reject:

- empty state sets
- duplicate states
- an initial state not in `states`
- final states not in `states`
- transitions with unknown source or target states
- unreachable states
- non-final states with no outgoing transition
- capabilities mentioned in deny-list and allow-list at the same time
- duplicate capability declarations

### Contract Checks

The compiler should reject:

- non-boolean contract clauses
- temporal operators outside temporal positions
- predicate calls to undeclared predicates
- tool or LLM calls inside contracts
- references to unknown states, capabilities, or outputs

### Workflow Checks

The compiler should reject:

- cycles in the workflow DAG
- unknown node references
- edges connected through incompatible outcome labels
- undeclared agents/services
- safety/liveness properties that reference unknown workflow symbols

## Reduced EBNF

This is a narrowed grammar sketch, not yet a full ANTLR target.

```ebnf
Program         ::= { ModuleDecl | ImportDecl | TopLevelDecl }

TopLevelDecl    ::= TypeDecl
                  | CapabilityDecl
                  | PredicateDecl
                  | AgentDecl
                  | ContractDecl
                  | WorkflowDecl

ModuleDecl      ::= "module" Ident ";"
ImportDecl      ::= "import" Ident ";"

TypeDecl        ::= StructDecl | EnumDecl | AliasDecl
AliasDecl       ::= "type" Ident "=" Type ";"

StructDecl      ::= "struct" Ident "{" { FieldDecl } "}"
FieldDecl       ::= Ident ":" Type ";"

EnumDecl        ::= "enum" Ident "{" Ident { "," Ident } [ "," ] "}"

CapabilityDecl  ::= "capability" Ident "(" [ ParamList ] ")" "->" Type ";"
PredicateDecl   ::= "predicate" Ident "(" [ ParamList ] ")" ":" "Bool" ";"

ParamList       ::= Param { "," Param }
Param           ::= Ident ":" Type

AgentDecl       ::= "agent" Ident "{"
                    "input" ":" Type ";"
                    "output" ":" Type ";"
                    "states" ":" "[" Ident { "," Ident } "]" ";"
                    "initial" ":" Ident ";"
                    "final" ":" "[" Ident { "," Ident } "]" ";"
                    "allow" ":" "[" Ident { "," Ident } "]" ";"
                    [ "deny" ":" "[" Ident { "," Ident } "]" ";" ]
                    { TransitionDecl }
                    [ QuotaDecl ]
                   "}"

TransitionDecl  ::= "transition" Ident "->" Ident ";"
QuotaDecl       ::= "quota" ":" "{" { QuotaItem } "}" ";"
QuotaItem       ::= Ident ":" Literal ";"

ContractDecl    ::= "contract" Ident "for" Ident "{"
                    { ContractClause }
                   "}"

ContractClause  ::= "require" ":" PredExpr ";"
                  | "ensure" ":" TemporalExpr ";"
                  | "always" ":" TemporalExpr ";"
                  | "never" ":" TemporalExpr ";"

WorkflowDecl    ::= "workflow" Ident "{"
                    { NodeDecl | EdgeDecl | SafetyDecl | LivenessDecl }
                   "}"

NodeDecl        ::= "node" Ident ":" QualifiedIdent ";"
EdgeDecl        ::= "edge" OutcomeRef "->" OutcomeRef ";"
OutcomeRef      ::= Ident "." Ident
SafetyDecl      ::= "safety" ":" TemporalExpr ";"
LivenessDecl    ::= "liveness" ":" TemporalExpr ";"
```

## Example

```ahfl
module refund.audit;

struct RefundRequest {
    order_id: String;
    user_id: String;
    refund_amount: Decimal(2);
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

enum AuditResult {
    Approve,
    Reject,
}

capability OrderQuery(order_id: String) -> OrderInfo;
capability TicketCreate(order_id: String, reason: String) -> String;

predicate order_exists(order_id: String): Bool;
predicate order_belongs_to_user(order_id: String, user_id: String): Bool;
predicate refund_amount_within_total(order_id: String, amount: Decimal(2)): Bool;
predicate ticket_created(order_id: String): Bool;
predicate audit_log_complete(): Bool;
predicate output_contains_sensitive_info(): Bool;

agent RefundAudit {
    input: RefundRequest;
    output: RefundDecision;
    states: [Init, Auditing, Approved, Rejected, Terminated];
    initial: Init;
    final: [Terminated];
    allow: [OrderQuery, TicketCreate];
    deny: [RefundExecute, UserInfoModify];

    transition Init -> Auditing;
    transition Auditing -> Approved;
    transition Auditing -> Rejected;
    transition Approved -> Terminated;
    transition Rejected -> Terminated;
}

contract RefundAuditSpec for RefundAudit {
    require: order_exists(input.order_id);
    require: order_belongs_to_user(input.order_id, input.user_id);
    require: refund_amount_within_total(input.order_id, input.refund_amount);
    never: call(RefundExecute);
    never: output_contains_sensitive_info();
}
```

## Notes On The Example

This example is intentionally narrower than your original version:

- no tool implementation bodies
- no LLM config
- no imperative state handler blocks
- no inline retry/backoff logic
- no runtime launcher

That is not a downgrade. It is the boundary that makes the language coherent as a first release.

## Suggested Versioning

### V0.1

Focus on:

- parser
- name resolution
- type checker
- agent/contract/workflow validator
- JSON or IR lowering

### V0.2

Add:

- lifecycle metadata
- timeout/retry policy as declarative metadata
- typed event model
- a constrained observability section

### V0.3

Add:

- native tool binding
- native LLM binding
- executable flow handlers
- formal backend for a restricted pure subset

## Bottom Line

AHFL Core V0.1 should be a small, declarative, statically checked control language for agent boundaries and workflow policy.

If V0.1 succeeds at that, the native fusion story can be added later without collapsing the language under its own weight.
