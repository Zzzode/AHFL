# Core-IR P4: Value Representation & Memory Layout — Design

> Status: **APPROVED** (RFC 0026 P4 / KR6.4). Design acceptance point `dc1b38a1`
> (@Codex final sign-off). Absorbs the deferred "monomorphization Slice 2" as
> P4's first consumer-driven vertical slice. Implementation follows the P4-A→D
> migration series (§6).

## 0. Problem & non-goals

Core-IR today carries **no logical value type and no physical layout**. Every
per-value type is either absent or a `kInvalid` `CoreTypeId` placeholder marked
"P4 deferred" (§7 inventory). `CoreTypeId` denotes only a *nominal declaration*
(`CoreProgram::types[]`); it cannot express a primitive, a refined scalar, a
bounded container, a `Fn`/closure, or an instantiated generic (`Option<User>`).
Match projection already works (via `matched_enum`), so this is **not** a
correctness gap today — it is the prerequisite for WASM codegen (KR6.5): a
backend must know each value's concrete type and its byte layout.

RFC 0026 §"值表示" (lines 184-199, 278) prescribes the target model. This design
implements that table; it does not invent a new value model.

**Non-goals (first slice):** wiring `run_monomorphization`; a program-wide type
closure; migrating every `kInvalid` field at once; any `CoreLayout`; real WASM
emission (KR6.5, the eventual layout consumer).

## 1. Three-layer identity split (Codex boundary 1 & 2 — APPROVED)

| Id | Denotes | Lives in | Equality |
|----|---------|----------|----------|
| `CoreTypeId` (**unchanged**) | a *nominal declaration* (`struct Foo` / `enum Bar`) | `CoreProgram::types` | index |
| `CoreValueTypeId` (**new**) | a *concrete logical value type* (post-mono; no TypeVar) | `CoreProgram::value_types` (interned) | **index ⇔ structural equality** (hash-consed) |
| layout | a *physical layout* under a target data-layout | side `CoreLayoutTable` (§3), **NOT** `CoreProgram.layouts` | index within a `(TargetDataLayoutId)` |

- **Logical type is target-independent.** `CoreValueType` never holds a wasm32
  constant. `Int` bounded `[0,255]` is a *logical* `Int{bounds}`; "i32 vs i64" is
  a *layout* decision (§3, §"repr precision").
- **Layout is a projection**, computed only in P4-D, keyed by
  `(CoreValueTypeId, TargetDataLayoutId)`. The first slice makes **no** decision
  about whether `CoreProgram` is a target-specialized artifact.

## 2. `CoreValueType` node set (Codex boundary 1 & 5)

`std::variant` (Principle 4), interned in a flat arena (Principle 3). It COVERS
`types::Type` (19 nodes) **minus TypeVar/Any/Error** (cannot survive
monomorphization → fail-closed verifier error if seen), but is NOT a strict 1:1
mirror: `EnumVariant` is normalized into its parent `CoreVtNominal` (an enum
value's type is the enum, not a per-variant type), and `CoreVtClosure` is a
Core-IR-only execution type with no `types::Type` counterpart (the typed layer
has only `FnT`).

```cpp
struct CoreValueTypeId { uint32 value{kInvalid}; };  // index into CoreProgram::value_types

// scalars & atoms
struct CoreVtUnit {};                       // ZST
struct CoreVtNever {};                       // uninhabited (see Q4)
struct CoreVtBool {};
struct CoreVtInt   { std::optional<std::pair<int64,int64>> bounds; };        // Int / BoundedInt
struct CoreVtFloat {};
struct CoreVtString { std::optional<std::pair<int64,int64>> length_bounds; };// String / BoundedString (len >= 0)
struct CoreVtDecimal { int64 scale; };
struct CoreVtDuration {};   struct CoreVtTimestamp {};   struct CoreVtUuid {};

// nominal instance — substitution by PARAM POSITION (Rust Substs; Codex boundary 5).
// `base` is the canonical in-Core CoreTypeId. The resolved nominal SymbolRef lives
// ONLY on the ir::TypeRef bridge (gap 1); lower_value_type resolves it to a
// CoreTypeId ONCE. A CoreValueType node never stores a SymbolRef — otherwise the
// three-layer identity table would itself bypass CoreTypeId and give one nominal
// two canonical representations.
struct CoreVtNominal {
    CoreTypeId base;                         // canonical in-Core nominal identity
    std::vector<CoreValueTypeId> args;       // concrete args by param position
    std::optional<uint64> capacity;          // ONLY legal on a bounded-collection nominal; else verifier-rejected
};

// anonymous structural
struct CoreVtTuple { std::vector<CoreValueTypeId> elements; };

// callable SIGNATURE (distinct from a closure — gap 2)
struct CoreVtFn { std::vector<CoreValueTypeId> params; CoreValueTypeId ret; };
// a closure: a signature PLUS its captured-environment slots (ordered = env slot order)
enum class CoreCaptureMode { ByValue /* future: ByRef */ };
struct CoreClosureCapture {
    CoreValueTypeId value_type;
    CoreCaptureMode mode{CoreCaptureMode::ByValue};
};
struct CoreVtClosure {
    CoreValueTypeId signature;                    // must be a CoreVtFn
    std::vector<CoreClosureCapture> captures;     // canonical env slot order (see below)
};

using CoreValueTypeNode = std::variant<
    CoreVtUnit, CoreVtNever, CoreVtBool, CoreVtInt, CoreVtFloat, CoreVtString,
    CoreVtDecimal, CoreVtDuration, CoreVtTimestamp, CoreVtUuid,
    CoreVtNominal, CoreVtTuple, CoreVtFn, CoreVtClosure>;

struct CoreValueType { CoreValueTypeNode node; };  // interned; index eq ⇔ structural eq
```

**Effect on `CoreVtFn`.** `types::FnT` carries an `EffectJudgement`. Core-IR is
the effect-LOWERED layer (RFC 0026: effects become explicit capability-call
nodes), so the effect grade is **erased** from `CoreVtFn` by construction — a
capability call is already an ordered `CoreCapabilityCallStmt`, not an effect on
a function value's type. This is stated explicitly so it is not a silent drop;
the verifier boundary is: no `CoreVtFn` carries an effect, and a function value's
callability/effect is expressed structurally by the call node, not the type.

**Closure capture order (gap 2).** `CoreVtClosure.captures` is an ordered slot
record — its vector index IS the environment slot order, and the concrete
captured `CoreValueId`s in a later closure-construction expr node are stored in
the SAME slot order. The order comes from Typed HIR with a stable rule: an
explicit capture list (RFC 0013 C-4) uses source order; an implicit capture uses
a deterministic first-use / lexical order (the existing Sema capture semantics,
tested). A capture's NAME does not enter the logical type identity (only its
`value_type` + `mode` do) — the name is closure-construction/debug provenance
only. The verifier accepts only `ByValue` today; `ByRef` (with its
lifetime/region-promotion rules) is a future addition that does NOT change the
node shape.

**Substitution model (gap-3 member templates).** `type_param_count` alone can't
materialize a payload. Each generic nominal's field/variant-slot type is a
*template*:

```cpp
struct CoreTypeTemplateRef {
    enum class Kind { Concrete, Param } kind;
    CoreValueTypeId concrete;   // Kind::Concrete
    uint32 param_index;         // Kind::Param  (position into CoreVtNominal.args)
};
```

`Option::Some`'s slot is `Param{0}`; materializing `Option<User>` substitutes
`args[0] = Vt(User)`. Builtin descriptors and user generics use the identical
representation — no builtin special-case. (Templates land in **P4-C**;
`type_param_count` lands in P4-A for the arity check.)

**Interning.** Mirror `TypeContext`: `vector<CoreValueType> storage_` +
`unordered_map<StructuralKey, CoreValueTypeId> pool_`. First *interned* Core-IR
arena (expr/pattern arenas are append-only); the RFC anticipates hand-written
hash-consing here. The lowerer's interner guarantees canonical ids; the
standalone verifier (§"arena verifier") re-proves canonicity at the
deserialization boundary.

## 3. Layout (Codex boundary 2 — deferred to P4-D, shapes fixed now)

Greenfield (no existing layout machinery: WASM backend is a WAT skeleton,
evaluator is a tagged `std::variant`). **Not** in `CoreProgram`; a side artifact:

```cpp
struct TargetDataLayoutId { uint32 value; };     // initially one: wasm32
struct CoreLayoutId { uint32 value{kInvalid}; };
struct CoreLayoutTable {
    TargetDataLayoutId target;
    std::vector<CoreLayout> layouts;             // index == CoreLayoutId
    // key: (CoreValueTypeId, target) -> CoreLayoutId
};

enum class CoreScalarRepr { I32, I64, F64 };     // PtrLen is an aggregate, not a scalar (gap 3)

struct CoreLayout {
    uint32 size; uint32 align; bool is_zero_sized;
    CoreLayoutShape shape;
};
struct CoreLayoutScalar    { CoreScalarRepr repr; };
struct CoreLayoutPtrLen    { /* (i32 ptr, i32 len) aggregate: String / container header */ };
struct CoreLayoutStruct    { std::vector<uint32> field_offsets;      // index == CoreFieldId (decl order)
                             std::vector<CoreLayoutId> field_layouts; };  // SSOT: carried, not re-derived
struct CoreLayoutEnum      { uint32 tag_size; uint32 payload_offset;
                             // per-variant: index == variant id
                             std::vector<CoreLayoutId> variant_payload_layouts;
                             std::vector<uint32> variant_payload_sizes; }; // payload_offset aligned to max
struct CoreLayoutContainer { CoreLayoutId element;                    // List/Set element
                             std::optional<CoreLayoutId> value;       // Map value (element = key)
                             uint64 capacity; uint32 stride;          // bounded dynamic length (ptr,len<=cap)
                             /* header is inline PtrLen (8B); backing bytes = stride*capacity, bump-arena allocated */ };
struct CoreLayoutClosure   { CoreLayoutId env_layout; /* region class */ };  // (func_index i32, env_ptr i32) inline
struct CoreLayoutUninhabited {};   // Never: no runtime value, not a loadable size-0
```

Per RFC value-rep table: Bool/narrow-Int→i32, wide-Int→i64, Float→f64,
Decimal/Duration→i64, String→PtrLen, enum→(tag i32, payload aligned to largest
variant, per-variant layouts recorded), struct→**declaration-order** fields (NOT
the evaluator's name-sorted `FieldMap`), bounded container→PtrLen header + bump-
arena backing (`stride*capacity`), closure→(func_index,env_ptr) + separate env
layout, capability result→opaque `(ptr,len)` host frame.

## 4. Lowering boundary + resolved nominal identity (gap 1)

**One** entry point:

```cpp
CoreValueTypeId lower_value_type(const ir::TypeRef&, ValueTypeArena&);
```

**Nominal base must be a resolved SymbolRef, never a canonical string** (gap 1).
`ir::TypeRef` today has `canonical_name` but no nominal `SymbolRef`. Resolving a
base by string would violate index identity (Principle 2) and re-open cross-module
hijack. **Chosen (recommended by Codex): persist a resolved nominal `SymbolRef`
on Struct/Enum `ir::TypeRef`** across the full chain — `typed_hir_lower`
population, IR-JSON round-trip, clone, structural equality. `lower_value_type`
then resolves the base by id-first identity (same as the instance registry). This
is a P4-A prerequisite step.

`lower_value_type` consumes the concrete `ir::TypeRef` already at each use site
(bounds/scale/capacity/`params`/`first`/`second` in place); fail-closed on
`Unresolved`/`Any`/`Never`-as-value/unresolved-nominal (no kInvalid value type
ever produced). Discovery stays typed-side; the first slice needs no program-wide
closure (lower each consumer's own TypeRef on demand + intern). A
`TypeInstanceDecl` / typed-side closure is deferred to a consumer that needs
whole-program enumeration (e.g. P4-D emitting one layout per distinct type).

## 5. Worked examples

```
Option<User>        → Nominal{ base=CoreTypeId(Option), args=[Vt(User)], cap=∅ }
                       Some slot template = Param{0} → substitutes Vt(User)
Result<User,Err>    → Nominal{ base=CoreTypeId(Result), args=[Vt(User),Vt(Err)] }; Ok=Param{0}, Err=Param{1}
List<User>(4)       → Nominal{ base=CoreTypeId(List), args=[Vt(User)], cap=4 }
Map<Int,User>(8)    → Nominal{ base=CoreTypeId(Map), args=[Vt(Int),Vt(User)], cap=8 }
struct Pair{a:Int,b:String} → Nominal{ base=CoreTypeId(Pair), args=[] }
  layout (P4-D,wasm32) → struct: a i64 @0, b PtrLen @8; size=16 align=8; field_layouts=[i64, ptrlen]
(Int,String)        → Tuple{ elements=[Vt(Int),Vt(String)] }
fn(Int)->Bool       → Fn{ params=[Vt(Int)], ret=Vt(Bool) }               (signature; effect erased)
closure |x|…capturing y:User → Closure{ signature=Vt(fn…), captures=[{type=Vt(User), mode=ByValue}] }
  layout → (func_index i32, env_ptr i32) inline 8B; env_layout = struct{User}
id<Int> instance dispatch_types → [ Vt(Int) ]                            (P4-A first consumer)
id<Fn(Int)->Bool>   → [ Fn{[Vt(Int)],Vt(Bool)} ]
```

(The nominal `base` above is a `CoreTypeId`; the resolved nominal `SymbolRef`
lives only on the input `ir::TypeRef` bridge — `lower_value_type` resolves it to
the `CoreTypeId` once, per §1/§4.)

## 6. Migration series (Codex-locked)

Each slice lands its arena/consumer + fail-closed verifier + real e2e; no slice
leaves an unread table. **`CoreValueId` stays a pure index**; type is looked up
in a per-body `value_types[CoreValueId]` table (P4-B), never embedded in the id.

- **P4-A — logical types (FIRST slice).** (a) persist resolved nominal SymbolRef
  on Struct/Enum `ir::TypeRef` (full chain); **(a0, prereq)** the resolved
  `nominal_ref` must survive every `TypeRef` duplication — so consolidate the
  three divergent hand-rolled clones (`core_lower`, `opt_lower`, `workflow_run`)
  onto a single SSOT `ir::clone_type_ref` in `types.{hpp,cpp}` that copies ALL
  fields. This also forward-fixes two latent silent-drop bugs the divergence hid:
  `opt_lower` dropped `collection_capacity`; `workflow_run` dropped `params`
  (collection element types) → runtime response-schema element validation was
  silently skipped for `List/Set/Map` capabilities. **(a, closed at review round 2)**
  the bridge is FAIL-CLOSED, not best-effort: `nominal_ref_from` (typed_hir_lower)
  throws on a symbol id that fails to resolve / resolves to a non-Type kind /
  canonical-drifts from the type — a name-only ref is emitted ONLY when the source
  type carries no symbol at all. The BackendReady verifier (`verify_type_ref` →
  `verify_nominal_ref`) is the observable backend gate: Struct/Enum MUST carry a
  resolved Type identity with matching canonical (id-present refs cross-checked via
  the shared `verify_symbol_ref` identity map); every non-nominal ref MUST carry an
  empty/Unknown `nominal_ref` (no stray identity). Structural equality is a public
  `ir::type_refs_equal` SSOT (mirrors the clone; `core_lower`'s `type_ref_equal`
  delegates — no second field sweep). `opt_json::print_type_ref` also emits
  `nominal_ref` + `collection_capacity` (optimized-IR diagnostic dump keeps the two
  nominal identities distinguishable; write-only, no reader). Fidelity + fail-closed
  tests: all-field clone/equality fixture (nested + null param) + real-frontend
  BackendReady positive + 4 single-field tamper negatives. (b) `CoreProgram::value_types` arena
  + interner + `verify_value_types`; (c) unique `lower_value_type`; (d)
  `CoreTypeDecl.type_param_count` + arity check; (e) migrate
  **`CoreInstanceDecl.dispatch_types: vector<ir::TypeRef>` → `vector<CoreValueTypeId>`**
  (the chosen first consumer — it already holds accurate concrete TypeRefs
  covering primitive/refined/Fn/nominal-generic); (f) **std generic base coverage**:
  register as `CoreTypeDecl` shells every std nominal generic that can appear in a
  concrete dispatch TypeRef but is not necessarily inline-declared — at least
  Option(count=1), Result(2), List(1), Set(1), Map(2) — derived from the EXISTING
  std descriptor SSOT (`builtin_enum_descriptors` + the collection descriptors), NOT
  a new drift-prone table, so a legal `List<Int>` never fails for want of a
  CoreTypeId base; (g) **typed collection role on `CoreTypeDecl`**:
  `enum class CoreNominalRole { Ordinary, Option, Result, List, Set, Map }` (filled
  from the std descriptor metadata), so the verifier judges `capacity` by typed
  role — NOT by parsing `canonical_name`: Option/Result reject `capacity`;
  List/Set/Map permit it per the language's bounded-collection rules. e2e (real
  frontend): `id<Int>`, higher-order `Fn(Int)->Bool`, and `id<Option<User>>` if
  syntax allows; duplicate structural type → one id; nested unresolved →
  fail-closed. NO layout.
- **P4-B — per-`CoreValueId` value_types.** Add per-flow/workflow
  `value_types[CoreValueId]`; migrate a small closed loop (pattern binding + match
  result / call result / let). Requires a **TypedPattern → AHFL `MatchPattern`
  matched-TypeRef bridge** so a primitive binding (`Some(v)` on `Option<Int>`)
  recovers `Int`/bounds — matched_enum's nominal-only SymbolRef is insufficient
  (this is exactly why the binding is NOT the first consumer). `CoreExpr` may
  carry a result value type; verifier locks Let-result-table == expr-result.
  `CorePatternBinding.binding_type` is then DELETED.
- **P4-C — nominal member templates.** `CoreTypeTemplateRef{Concrete|Param}` on
  struct field / enum payload slots; materialize Option/Result/List/Map,
  eliminating the kInvalid `slot_types`.
- **P4-D — layout pass.** `TargetDataLayout` + side `CoreLayoutTable`; scalar /
  struct / enum / container golden first, then closure env. Recursion decision
  (gap 4): direct inline recursion (`struct A{a:A}`) is infinite layout →
  fail-closed; cycles break only through an explicit indirection boundary
  (String/container ptr, future Box, closure env ptr); layout DFS maintains a
  visiting set; logical `CoreVtNominal` shares via base+args (never expands decl
  fields into the type node).
- **Then** migrate projection result_type / construct / capability signatures /
  remaining kInvalid.

## 7. kInvalid inventory (shrinks monotonically per slice)

| # | Field | file | slice |
|---|-------|------|-------|
| 7 | `CoreInstanceDecl.dispatch_types` (ir::TypeRef → CoreValueTypeId) | core_ir.hpp | **P4-A** |
| 5 | `CorePatternBinding.binding_type` → per-CoreValueId table | core_ir.hpp | P4-B |
| — | let/call/match-result value types (new table) | core_lower.cpp | P4-B |
| 1 | builtin Option/Result `slot_types` (→ templates) | core_lower.cpp | P4-C |
| 2 | user enum variant `slot_types` (→ templates) | core_lower.cpp | P4-C |
| 3 | `CoreTypeDecl.field_types[i]` (primitive/collection field) | core_lower.cpp | P4-C |
| 4 | `CoreProjectionStep.result_type` (leaf) | core_ir.hpp | after P4-D |
| 6 | anonymous tuple element types (`CoreVtTuple`) | core_lower.cpp | P4-B/C |
| 8 | literal / int-range physical encoding | core_ir.hpp | P4-D / KR6.5 |

## 8. Verifiers

**`verify_value_types` (P4-A).** For every arena entry: child ids in range; type
graph acyclic + depth-bounded (iterative walk, total — mirrors the instance
dispatch-type walk); `CoreVtNominal.base` resolves to an in-range `CoreTypeId` of
matching kind (Struct/Enum) and `args.size() == type_param_count`; `capacity`
present ONLY on a bounded-collection nominal (else reject); `CoreVtFn` params/ret
valid + no effect; `CoreVtClosure.signature` is a Fn value type; structural
duplicates rejected (interning is canonical). No `TypeVar`/`Any`/`Unresolved`
node exists.

**Never (Q4).** `CoreVtNever` is legal in the arena but a *consumer-context*
check rejects it as a materialized value definition / dispatch type / shell;
allowed only as a diverging expression/region result marker.

**repr precision (gap 6, enforced at layout + arena):** `BoundedInt` whose range
fits signed i32 → i32, else i64; unbounded `Int` → i64 (formula, not ad-hoc).
`String`/`BoundedString` length bounds are non-negative; verifier rejects a
negative or reversed (min>max) bound. `capacity` only on collection nominals.

## 9. Candidates & rejected alternatives

- **(A) chosen:** separate interned `CoreValueTypeId` (logical, target-indep) +
  side per-target `CoreLayoutTable`. Matches Rust `Ty`/`Layout`, Swift
  `SILType`/`TypeLayout`, and the RFC two-layer intent.
- **(B) rejected:** widen `CoreTypeId` to also mean primitives/containers/Fn —
  conflates nominal-decl identity with structural value identity, re-opens the
  kInvalid hole, Codex-forbidden.
- **(C) rejected:** one node storing size/align inline — bakes wasm32 into the
  logical type, breaks target independence + hash-consing.

## 10. Resolved design decisions (were open questions)

1. **First consumer = `CoreInstanceDecl.dispatch_types`** (not PatternBinding —
   matched_enum lacks a full matched TypeRef; binding waits for the P4-B bridge).
2. **Layout not in first slice**; side `CoreLayoutTable` keyed by
   `(CoreValueTypeId, TargetDataLayoutId)`, never `CoreProgram.layouts`.
3. **`CoreTypeDecl.type_param_count` added** (mono=0/Option=1/Result=2/List·Set=1/
   Map=2; user generics filled by frontend) **plus** member templates
   (`Concrete|Param{index}`) for actual substitution.
4. **`CoreVtNever`** retained in the arena; rejected as a materialized value /
   dispatch / shell; allowed as a diverging marker; layout = `Uninhabited`.
5. **Nominal identity transport (re-review ①):** resolved nominal `SymbolRef`
   persisted on Struct/Enum `ir::TypeRef` (the lowering-input bridge) — id-first
   with canonical fallback, kind must be Type, `canonical_name` consistent,
   consumed by clone/IR-JSON/ir_equal/verify/fingerprint, fail-closed on
   missing/wrong-kind/inconsistent. `CoreVtNominal.base` stays a `CoreTypeId`
   (SymbolRef never enters a CoreValueType node — `lower_value_type` resolves it
   to a CoreTypeId once).
6. **Closure captures (re-review ②):** ordered `CoreClosureCapture{value_type,
   mode}` slots (vector index == env slot order); `ByValue` only today, `ByRef`
   deferred without a node-shape change.
7. **P4-A metadata (re-review A/B):** std generic base coverage (Option/Result/
   List/Set/Map shells from the existing std descriptor SSOT) + a typed
   `CoreNominalRole` so `capacity` is judged by role, never by canonical-name
   parsing.
