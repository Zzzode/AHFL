# AHFL Core V0.1

> Note
>
> This file is a scope-and-design note, not the canonical syntax reference.
> The normative grammar and type-system definition lives in
> [core-language-v0.1.zh.md](../spec/core-language-v0.1.zh.md).

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
- `const`
- `struct`
- `enum`
- `type`
- `capability`
- `predicate`
- `agent`
- `contract`
- `flow`
- `workflow`

## What Moves Out Of Core

These should be deferred to `AHFL Native` or later versions:

- `tool { ... }` implementation bodies
- `llm_config`
- arbitrary host-integrated native execution bodies
- `lifecycle`
- `observability`
- `compliance`
- `main`
- built-in HTTP/gRPC server startup
- custom retry/backoff/timeout hook code in the host runtime
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

- `requires`: must hold before execution starts
- `ensures`: must hold on successful completion
- `invariant`: invariant over the execution trace
- `forbid`: forbidden event or state pattern

To keep verification tractable, contracts should use a separate expression layer from normal value expressions.

### Flow

A `flow` is a restricted, separately declared control block bound to an `agent`.

Core V0.1 supports:

- one handler per state
- local bindings
- capability calls
- assertions
- controlled `goto` transitions
- a terminal `return` from final-state handlers

It does not support arbitrary host-language execution, embedded tool bodies, or runtime lifecycle
hooks.

### Workflow

A `workflow` is a DAG of agent or service nodes with typed dependencies and global properties.

Core V0.1 supports:

- typed workflow input and output
- node declarations
- dependency declarations through node-local `after`
- safety properties
- liveness properties
- a typed workflow `return`

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

Used in `requires` and some `ensures` clauses.

Examples:

- `order_exists(input.order_id)`
- `refund_amount_within_total(input.order_id, input.refund_amount)`
- `current_state == Approved`

### 3. Temporal Formulas

Used in `invariant`, `forbid`, workflow `safety`, and workflow `liveness`.

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

- `->` for state transitions
- `=>` for logical implication
- `or` for boolean disjunction
- `and` for boolean conjunction
- reserve `par` for a future parallel workflow extension if it is added later

Unicode operators can be formatter sugar later, but should not be part of the canonical grammar in V0.1.

## Core Type System

V0.1 should prefer a small, explicit, largely nominal type system.

### Primitive Types

Source-level primitive types in Core V0.1 are:

- `Unit`
- `Bool`
- `Int`
- `Float`
- `Decimal(scale)`
- `String`
- `String(min, max)`
- `UUID`
- `Timestamp`
- `Duration`

### Composite Types

- `Optional<T>`
- `List<T>` as an immutable sequence
- `Set<T>` as an immutable set
- `Map<K, V>` as an immutable map
- named `struct`
- named `enum`

### Surface vs Internal Types

The source language should expose only the types listed above plus named aliases.

The implementation may still use a small number of checker-internal sentinel types, for example:

- `Any` for error recovery or unresolved placeholders
- `Never` for unreachable or non-returning control-flow positions

These are compiler-internal concepts. They are not part of the AHFL Core V0.1 source syntax and
must not appear in user-written programs.

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
- undeclared agents/services
- node inputs that reference unavailable dependencies
- workflow return type mismatches
- safety/liveness properties that reference unknown workflow symbols

## Normative Grammar And Example

This document no longer maintains a separate reduced EBNF or a shadow example.

Use these files as the implementation baseline:

- [core-language-v0.1.zh.md](../spec/core-language-v0.1.zh.md) for the
  normative grammar, type system, and static semantics
- [AHFL.g4](../../grammar/AHFL.g4) for the parser grammar
- [refund_audit_core_v0_1.ahfl](../../examples/refund_audit_core_v0_1.ahfl)
  for the minimal consistent example

## Suggested Versioning

### V0.1

Focus on:

- parser
- name resolution
- type checker
- agent/contract/workflow validator
- JSON or IR lowering
- a restricted formal backend over validated state machines and LTL formulas, including flow/workflow semantic lowering, stable observation abstraction, and shared backend dispatch for multiple emitters

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
- host-integrated native execution bodies

## Bottom Line

AHFL Core V0.1 should be a small, declarative, statically checked control language for agent boundaries and workflow policy.

If V0.1 succeeds at that, the native fusion story can be added later without collapsing the language under its own weight.
