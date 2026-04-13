# AHFL Design Audit

## Executive Summary

The current AHFL design has a strong product direction:

- it treats agent governance as a first-class language concern
- it models agents as bounded state machines instead of free-form scripts
- it moves safety, schema, and audit concerns to the specification layer

The main problem is not the direction. The main problem is **scope and semantic layering**.

The current spec mixes:

1. declarative specification
2. executable business logic
3. temporal logic
4. type theory
5. runtime platform configuration
6. observability/compliance authoring

That makes the language hard to parse, hard to type-check, and hard to implement faithfully.

## Severity Legend

- `Critical`: must fix before parser/type-checker work starts
- `High`: should fix before locking the language surface
- `Medium`: can be deferred, but should be tracked explicitly

## Grammar Audit

### G1. `->` / `→` Is Overloaded

Severity: `Critical`

Current uses include:

- state transition
- workflow dependency
- logical implication
- compensation mapping

This is a grammar and semantics problem. Even if parsing is forced by context, the notation trains users to think these are the same relation when they are not.

Recommendation:

- reserve `->` for transition and edge declarations only
- use `=>` for logical implication
- use a keyword or dedicated syntax for compensation mappings

### G2. `||` Is Overloaded

Severity: `Critical`

Current uses include:

- boolean disjunction
- workflow parallelism

That creates ambiguity in both parsing and mental model.

Recommendation:

- keep `or` or `||` for boolean logic
- use `par` or an explicit workflow combinator for parallel branches

### G3. `StructType` Is Referenced But Not Defined

Severity: `Critical`

The grammar uses `StructType` in multiple places, but it is not defined in the EBNF excerpt.

Recommendation:

- define `StructType` explicitly
- or remove inline anonymous structs from V0.1 and require named `struct`

### G4. `EnumDecl` And `StructDecl` Are Not In `TopLevelDecl`

Severity: `Critical`

You define these productions, but `TopLevelDecl` does not reference them directly. `TypeDecl` only covers type alias syntax in the given grammar.

Recommendation:

- include `EnumDecl` and `StructDecl` in top-level declarations
- or make `TypeDecl` a union over alias/struct/enum

### G5. `Enum["approve", "reject"]` Exists In Examples But Not In Grammar

Severity: `Critical`

The examples use inline string enums, while the grammar only supports named `enum` declarations.

Recommendation:

- pick one representation
- for V0.1, prefer named nominal enums only

### G6. Unit-Suffixed Literals Are Not Defined

Severity: `Critical`

Examples use:

- `30s`
- `5min`
- `512MB`
- `0.5core`
- `365days`

These have no lexical or grammar definition.

Recommendation:

- remove them from V0.1
- or define a dedicated `Duration` and `ResourceQuantity` literal family

### G7. `{}` Is Used For Set, Map, And Struct-Like Constructs

Severity: `High`

Current syntax overloads braces for:

- set literals
- map literals
- struct-like schema blocks
- resource/compliance objects

This is parseable in some cases, but not cleanly. Empty literals are especially problematic.

Recommendation:

- keep `{}` for blocks and named record initializers
- use `#{}` for sets or `set[...]`
- use `map[...]` or require a colon-containing form only

### G8. Nested Block Comments Increase Lexer Complexity

Severity: `High`

You state that multiline comments support nesting. That is possible, but not trivial in ANTLR4.

Recommendation:

- drop nested comments in V0.1
- or implement custom lexer state logic and accept the added cost

### G9. Unicode Operators Hurt Tooling

Severity: `High`

Operators like `→`, `∧`, and `∨` look elegant, but they create friction for:

- editor input
- diff readability
- parser implementation
- formatter consistency
- LSP support

Recommendation:

- make ASCII the canonical syntax
- optionally allow Unicode as non-canonical aliases later

### G10. Postfix Call Syntax Is Too Permissive

Severity: `High`

This production:

```ebnf
<PrimaryExpr> ::= <PrimaryExpr> "(" <ExprList> ")"
```

allows any primary expression to become callable.

That is common in general-purpose languages, but your language needs stronger phase separation between values, predicates, tools, and workflow nodes.

Recommendation:

- restrict call targets by declaration kind during parsing or early name resolution
- or use separate syntactic forms for capability calls and predicate calls

### G11. `FlowExpr` Mixes Statement And Expression Worlds

Severity: `High`

`FlowExpr` currently includes:

- sequencing
- parallelism
- `if`
- node calls
- termination

This is closer to a workflow AST than a normal expression grammar, but it is embedded into the general expression style.

Recommendation:

- make workflow syntax statement-like and explicit
- prefer `node` and `edge` declarations over expression composition in V0.1

### G12. The Examples And Grammar Already Diverge

Severity: `High`

Examples include patterns such as:

- `on_state(Approved) | on_state(Rejected)`
- cross-state variables like `llm_result`

These do not line up cleanly with the written grammar or scope rules.

Recommendation:

- freeze a single canonical syntax
- reject any syntax not covered by the normative grammar

### G13. The Reserved Keyword List Is Incomplete

Severity: `Medium`

The grammar uses terms such as `fn`, `allow`, and `deny`, while the reserved list does not fully track the actual syntax surface.

Recommendation:

- generate the reserved word list from the grammar source
- avoid maintaining two unsynchronized lists

### G14. `main` Is Mandatory At Program Level

Severity: `Medium`

`Program ::= ... <MainDecl>` forces every source file to define a `main`.

That blocks:

- reusable modules
- libraries
- shared schema packages

Recommendation:

- make `main` optional or move launch configuration out of the language

## Semantic Audit

### S1. The Language Is Both Spec DSL And Implementation DSL

Severity: `Critical`

The current design wants to describe:

- what an agent is allowed to do
- what an agent does at runtime
- how tools are implemented
- how the runtime server boots

Those are different layers.

Recommendation:

- make V0.1 declarative
- move imperative runtime logic to a later native layer

### S2. Contracts Are Not Purely Spec-Level

Severity: `Critical`

Functions like `order_exists` and `ticket_created` are written as if they are pure predicates, but in practice they may depend on external data or side effects.

If contract predicates are not pure, model-check lowering becomes ill-defined.

Recommendation:

- define a `predicate` declaration class
- require predicates used in contracts to be pure and side-effect free
- treat runtime-backed predicates as external bindings with explicit semantics

### S3. LTL And CTL Are Mixed In One Surface

Severity: `Critical`

The spec claims native support for both LTL and CTL operators. That is too much for V0.1 and will complicate parsing, typing, and backend mapping.

Recommendation:

- choose LTL first
- add CTL only after the LTL subset is stable

### S4. “Direct Mapping To Spin/NuSMV” Is Overstated

Severity: `Critical`

Your spec currently includes:

- database queries
- tool calls
- LLM outputs
- timers
- retries
- external notifications

These do not map directly to finite-state model checkers without abstraction.

Recommendation:

- explicitly define a model-checkable subset
- require abstract environment models for tools, time, and external calls

### S5. `lifecycle` And `flow` Both Own Runtime Behavior

Severity: `High`

You currently have behavior attached in at least two places:

- lifecycle hooks
- flow `on_state` blocks

That raises ordering questions:

- does lifecycle run before flow?
- can both change state?
- can both call tools?
- which layer owns retries and exceptions?

Recommendation:

- choose one execution owner in V0.1
- keep the other as metadata only

### S6. Retry/Timeout/Exception Semantics Are Underspecified

Severity: `High`

Questions currently unanswered:

- does retry re-enter the same state or restart the handler?
- are tool side effects rolled back?
- what is the precise interaction with state transitions?
- does timeout fire during external tool calls?

Recommendation:

- defer executable retry/timeout semantics from V0.1
- reintroduce later as declarative runtime policy

### S7. Cross-State Variable Scope Is Undefined

Severity: `High`

In the example, `llm_result` is used in later states even though it is defined only inside a prior state block.

Recommendation:

- define `ctx` as the only cross-state mutable store
- reject implicit variable carry-over across state handlers

### S8. `default_permission: allow` Weakens The Model

Severity: `High`

Allow-by-default contradicts your stated safety-left-shift philosophy.

Recommendation:

- remove `allow` as an option in V0.1
- keep deny-by-default only

### S9. Allow-List And Deny-List Precedence Is Not Defined

Severity: `High`

If a capability appears in both:

- does deny win?
- is it a compile error?

Recommendation:

- make overlap a compile-time error

### S10. Workflow Outcome Labels Are Not A Typed Concept

Severity: `High`

Examples use:

- `RefundAuditAgent.audit_passed`
- `RefundExecuteAgent.success`

These are not formally declared as typed outcomes.

Recommendation:

- add explicit node outcome declarations
- or derive allowed outcomes from agent final states only

### S11. `global_state` Atomicity Is Not Enoughly Defined

Severity: `High`

Statements like `global_state.refund_amount is atomic` are descriptive, not executable semantics.

Recommendation:

- define the concurrency model explicitly
- or remove shared mutable workflow state from V0.1

### S12. Saga Compensation With Parallel Steps Is Ambiguous

Severity: `Medium`

Compensation ordering is easy in linear flows, harder in parallel ones.

Recommendation:

- defer saga semantics from V0.1
- or require compensation order to be explicit and total

### S13. Observability And Compliance Are Over-Integrated

Severity: `Medium`

These are valuable concerns, but turning all telemetry and compliance concerns into core syntax will lock the language to one runtime platform too early.

Recommendation:

- move observability/compliance to optional metadata or generated config

### S14. `main` And Server Boot Belong To The Runtime Layer

Severity: `Medium`

`start_http_server` and `start_grpc_server` are runtime host concerns, not language-core concerns.

Recommendation:

- move launch concerns to CLI or deployment config

## Type System Audit

### T1. Mutable Collection Covariance Is Unsound

Severity: `Critical`

The spec says `List<T>` and `Map<K, V>` are covariant. That is only sound for immutable/read-only collections.

Recommendation:

- make collections immutable in V0.1
- or use invariance for mutable collections

### T2. `Int <: Float` Should Not Be A Subtype Rule

Severity: `Critical`

This is better modeled as an explicit numeric promotion or coercion, not subtyping.

Reason:

- it complicates overload resolution
- it breaks exactness expectations
- it interacts badly with generic inference

Recommendation:

- remove numeric subtyping
- add explicit or context-directed conversions

### T3. `Decimal(p) <: Float` Loses Exactness

Severity: `Critical`

`Decimal` is usually chosen precisely because `Float` is not exact enough for money.

Recommendation:

- do not make `Decimal` a subtype of `Float`
- require explicit conversion

### T4. `T <: Optional<T>` Creates Hidden Lifting

Severity: `High`

This seems ergonomic, but it causes:

- ambiguous inference
- surprising overload resolution
- nullability leakage

Recommendation:

- keep `Optional<T>` explicit
- allow only a narrow assignment coercion if needed

### T5. `Map<K, V>` Value Covariance Is Unsafe If Mutable

Severity: `High`

Same root issue as `List<T>`.

Recommendation:

- immutable maps only in V0.1
- or invariant type arguments

### T6. Structural Subtyping For `enum` Is Unusual And Costly

Severity: `High`

Enums participate in:

- exhaustiveness checks
- serialization
- pattern matching
- API compatibility

Structural enum subtyping complicates all of these.

Recommendation:

- use nominal enums only in V0.1

### T7. Structural Subtyping For `struct` Needs Mutability Rules

Severity: `High`

Width/depth subtyping can work for immutable records, but becomes complex once mutation exists or once schemas are externally visible.

Recommendation:

- prefer nominal `struct`
- allow structural compatibility only for schema matching as a separate rule if absolutely needed

### T8. `ContractType` Is Not Just `Bool`

Severity: `High`

A runtime boolean value and a temporal contract formula are not the same kind of thing.

Recommendation:

- separate:
  - `ValueExpr`
  - `PredExpr`
  - `TemporalExpr`

Do not collapse all of them into `Bool`.

### T9. Full HM-Style Inference Is Too Ambitious

Severity: `High`

Hindley-Milner plus:

- subtyping
- temporal operators
- declaration kinds
- effects
- schemas

is not a small implementation.

Recommendation:

- do local inference only for `let`
- require explicit function, predicate, capability, and schema types

### T10. `Any` And `Never` Add Cost Without Immediate Value

Severity: `Medium`

They can be useful later, but they also interact with:

- subtyping
- control-flow typing
- coercions

Recommendation:

- keep `Never` internally for the checker if needed
- do not expose `Any` in V0.1 unless there is a hard requirement

### T11. `String(min, max)` Needs Exact Semantics

Severity: `Medium`

Questions to settle:

- measured in bytes or Unicode scalar values?
- checked at compile time for literals only, or also runtime?
- does concatenation refine the bounds?

Recommendation:

- define these as refinement constraints, not ordinary nominal types
- keep runtime enforcement explicit

### T12. `Decimal(p)` Needs Range And Rounding Semantics

Severity: `Medium`

Scale alone is not enough. You also need to define:

- range
- arithmetic promotion rules
- rounding mode
- division behavior

Recommendation:

- define a concrete decimal runtime model before finalizing the type

### T13. Capabilities Are Not Ordinary Functions

Severity: `Medium`

A capability has more semantics than a function:

- authorization
- audit logging
- runtime binding
- possible side effects

Recommendation:

- model capabilities as a separate declaration kind, not just as function values

### T14. `AgentType`, `WorkflowType`, And `SchemaType` Are Declaration Kinds, Not Regular Types

Severity: `Medium`

Treating them as ordinary first-class types may overcomplicate the checker.

Recommendation:

- keep them as compiler-internal declaration categories in V0.1
- only materialize explicit runtime types when needed

## Implementation Risks

### I1. ANTLR Grammar Is Not The Hard Part

The hardest parts are:

- name resolution across declaration kinds
- phase separation
- contract checking
- workflow validation
- backend lowering

Do not equate “parser generated successfully” with “language front-end done”.

### I2. Formal Verification Requires An Explicit Abstraction Boundary

You need a model for:

- external tools
- LLM non-determinism
- time
- retries
- environment predicates

Without that, “formal verification” stays aspirational.

### I3. Dual-Mode Runtime Doubles Product Scope

Supporting both:

- control-only mode
- native fusion mode

is valuable, but it should not be in the first compiler milestone.

### I4. IDE And LSP Support Will Be Harder Than Expected

Reasons:

- Unicode operators
- nested comments
- triple-quoted strings
- multiple expression layers
- declaration-kind-sensitive name resolution

### I5. Observability/Compliance Backends Can Swallow The Project

Those features touch:

- telemetry schemas
- retention policy
- redaction
- alert rules
- report generation

That is a platform in itself.

## Recommended Fix Order

### Phase 1: Lock The Core Language

Decide and freeze:

1. declaration kinds
2. canonical ASCII syntax
3. one temporal logic
4. one ownership model for runtime behavior
5. a minimal type system with no implicit subtyping magic

### Phase 2: Build The Front-End

Implement:

1. parser
2. AST
3. name resolver
4. type checker
5. agent and workflow validator

### Phase 3: Add A Restricted Backend

Target:

1. JSON IR
2. policy runtime config
3. optional model-checkable subset

### Phase 4: Add Native Extensions

Only after the core is stable:

1. native tool bindings
2. LLM bindings
3. lifecycle handlers
4. declarative retry/timeout policy
5. observability/compliance metadata

## Bottom Line

Your design intent is strong and differentiated. The current spec is not “wrong”; it is just trying to be a platform, a language, a verifier, and a runtime all at once.

The right move is not to throw it away. The right move is to split it into:

- a small, strict `AHFL Core`
- a later `AHFL Native`
- a formally checkable subset with explicit abstraction boundaries

That preserves the product idea while making the implementation actually achievable.
