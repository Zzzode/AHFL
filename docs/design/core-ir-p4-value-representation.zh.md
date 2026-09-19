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

**Substitution model (P4-C member templates).** `type_param_count` alone can't
materialize a payload. Each generic nominal's field/variant-slot type is a root
in a declaration-owned, postordered, compositional template arena:

```cpp
enum class CoreMemberTypeTemplateKind { Concrete, Param, Nominal, Fn };
struct CoreMemberTypeTemplateNode {
    CoreMemberTypeTemplateKind kind;
    CoreValueTypeId concrete;                     // Concrete
    uint32 param_index;                           // Param
    CoreTypeId nominal;                           // Nominal
    std::optional<uint64> capacity;               // Nominal collection refinement
    std::vector<CoreMemberTypeTemplateNodeId> children; // Nominal args / Fn params
    CoreMemberTypeTemplateNodeId fn_return;       // Fn
};
```

`Option::Some`'s slot is `Param{0}`; materializing `Option<User>` substitutes
`args[0] = Vt(User)`. `Nominal` and `Fn` make `Option<T>`, `Map<String,T>` and
`fn(T)->List<T>(4)` representable without reconstructing types from erased
`ir::TypeRef`s. Builtin descriptors and user generics use the identical arena;
`instantiate_member_template` is the single substitution/materialization path.

**Interning.** Mirror `TypeContext`: `vector<CoreValueType> storage_` +
`unordered_map<StructuralKey, CoreValueTypeId> pool_`. First *interned* Core-IR
arena (expr/pattern arenas are append-only); the RFC anticipates hand-written
hash-consing here. The lowerer's interner guarantees canonical ids; the
standalone verifier (§"arena verifier") re-proves canonicity at the
deserialization boundary.

## 3. Layout (P4-D D0 contract)

Greenfield (the WASM backend is still a WAT skeleton and the evaluator uses a
tagged `std::variant`). Layout is a deterministic, target-specific **side
artifact**. It is never embedded in or used to mutate `CoreProgram`:

```cpp
enum class TargetDataLayoutId { Wasm32 };
struct TargetDataLayout {
    TargetDataLayoutId id{TargetDataLayoutId::Wasm32};
    uint32 pointer_size{4};
    uint32 pointer_align{4};
    uint32 function_index_size{4};
    uint32 function_index_align{4};
};

struct CoreLayoutId { uint32 value{kInvalid}; };
struct CoreLayoutTable {
    TargetDataLayout target;
    std::vector<CoreLayout> layouts;       // index == CoreLayoutId
    std::vector<CoreLayoutId> value_layouts; // parallel to CoreProgram::value_types
};

enum class CoreScalarRepr { I32, I64, F64 };

struct CoreLayout {
    uint64 size;
    uint32 align;
    bool is_zero_sized;
    CoreLayoutShape shape;
};
struct CoreLayoutPending {}; // builder-only reserved slot; forbidden in a successful table
struct CoreLayoutScalar { CoreScalarRepr repr; };
struct CoreLayoutBytes { uint64 byte_count; }; // UUID: opaque inline bytes
struct CoreLayoutPtrLen {};                   // (i32 ptr, i32 len)
struct CoreLayoutFnRef {};                    // wasm32 table index i32
struct CoreLayoutClosure {                    // (func_index:i32, env_ptr:i32), always 8/4
    std::optional<CoreLayoutId> environment;  // INDIRECT env struct; absent iff no captures
};
struct CoreLayoutStruct {
    std::vector<uint64> field_offsets;         // declaration order
    std::vector<CoreLayoutId> field_layouts;   // INLINE edges
};
struct CoreLayoutEnum {
    uint32 tag_size;
    uint64 payload_offset;
    std::vector<CoreLayoutId> variant_payload_layouts; // INLINE aggregate edges
    std::vector<uint64> variant_payload_sizes;
};
struct CoreLayoutContainer {
    CoreLayoutId element;                      // INDIRECT backing edge
    std::optional<CoreLayoutId> value;         // Map value, INDIRECT backing edge
    uint64 capacity;
    uint64 stride;
    uint64 value_offset;                       // 0 except Map entry layout
    uint64 backing_size;                       // checked stride * capacity
};
struct CoreLayoutUninhabited {};              // Never: not a loadable ZST
```

`CoreLayoutShape` is the variant of the shapes above. `CoreLayoutPending` exists
only so the builder can reserve a stable `CoreLayoutId` before descending; both
successful construction and standalone verification reject a table containing
one. Every `CoreValueTypeId` in the input program has exactly one entry in
`value_layouts`. Layout roots are deliberately **not hash-consed**: distinct
logical types may have different ids while still being physically equivalent.

### 3.1 Representation table and arithmetic

- Unit is a size-0/alignment-1 struct; `Never` is `CoreLayoutUninhabited`, not a
  loadable size-0 value. Bool and a bounded Int wholly contained in signed i32
  use i32; every other Int uses i64; Float uses f64.
- `Decimal(scale)` is a signed i64 **unscaled mantissa** representing
  `mantissa / 10^scale`; `scale` is logical type metadata and is not stored in
  each value. This is not an arbitrary-precision decimal or a handle. Literal /
  const conversion must fail closed when the mantissa is not representable;
  dynamic construction, rescaling and arithmetic use checked i64 operations and
  trap/report a runtime error on overflow. Silent truncation and wraparound are
  forbidden. The evaluator's spelling-backed `DecimalValue` is a reference
  representation, not the Core ABI; its `decimal_raw_*` path already parses an
  i64 mantissa and reports rescale/arithmetic overflow.
- Duration and Timestamp use i64. UUID is 16 opaque inline bytes with alignment
  1: Core never loads it as an i32 lane, so stronger alignment would introduce
  padding without an ABI or access requirement.
- String is `(ptr:i32,len:i32)` (size/alignment 8/4). `CoreVtFn` is a wasm32
  table index i32 (size/alignment 4/4). `CoreVtClosure` is the D2
  `(func_index:i32, env_ptr:i32)` word pair (size/alignment 8/4) — a function
  reference word plus the address of its captured environment — so its size is
  independent of how much it captured. The environment is a separate
  `CoreLayoutStruct` over the capture slots in canonical env-slot order, reached
  through an **Indirect** edge (design §3.3), absent iff the closure captures
  nothing. Because that edge is Indirect, a closure that captures its own
  enclosing struct type finalises (word pair + separate env) rather than
  reporting a spurious `core.layout.INFINITE_RECURSION`. D1's explicit
  `core.layout.UNSUPPORTED` is therefore gone.
- Tuple elements and struct fields are inline in index/declaration order (never
  evaluator `FieldMap` name order). Enum is `(tag:i32,payload)` with one
  deterministic payload aggregate per variant and payload aligned to the
  maximum variant alignment.
- Bounded List/Set/Map is an inline `(ptr,len)` header. The element/entry backing
  storage is indirect bump-arena memory, but the shape records element/value
  layouts, Map value offset, stride, capacity and checked `backing_size` so
  representation comparison cannot confuse equal headers with different
  backing contracts. An unbounded collection fails `core.layout.UNBOUNDED` for
  this bounded target. All align-up, aggregate addition and
  `stride * capacity` operations are checked; overflow fails
  `core.layout.OVERFLOW` rather than wrapping or invoking host UB.

### 3.2 Pure type closure and deterministic publication

`compute_core_layouts(const CoreProgram&, TargetDataLayout)` is pure: no
wall-clock, pid, host path, allocator address or mutation of `CoreProgram`
enters the result. D1 accepts only the exact wasm32 constants shown above; an
unknown target id or inconsistent target fields fails `core.layout.UNSUPPORTED`
rather than silently inventing an ABI. P4-C's
`instantiate_member_template(CoreProgram&,...)` currently interns into the
program arena, so D1 factors its implementation over a supplied
`ValueTypeArena` and reuses that **same** template evaluator in two wrappers:

1. the existing mutating P4-C API, preserving its behavior; and
2. a layout-private arena seeded with a copy of `program.value_types`.

The second wrapper computes the concrete member-type closure without publishing
new logical types back into `CoreProgram` and without implementing a second
template/subtyping engine. Layouts for closure-only materialized types may be
referenced by layout nodes but are not appended to `value_layouts`, whose domain
remains exactly the original program arena.

The builder first reserves one `Pending` root slot for each input value type in
source order (`value_layouts[i]` is therefore stable), then visits roots in that
same order. Layout-private materialized types and enum payload aggregates append
in deterministic discovery/declaration/variant order. A temporary table is
published only after every reserved or appended placeholder has finalized;
failure returns a structured diagnostic with source provenance and no partial
table. Recomputing a fixed `(program,target)` yields structurally equal tables.

### 3.3 Recursive layout and physical equivalence

Layout dependencies are classified by the shape field that carries them:

- struct fields and enum variant payload aggregates are **Inline** size edges;
- collection element/key/value references are **Indirect** backing edges;
- the closure environment reference is Indirect.

On first visit the builder reserves a stable placeholder id. Re-entering a
`Visiting` value through an Inline edge is
`core.layout.INFINITE_RECURSION` (`struct A { a: A }`, a direct enum payload, or
mutual equivalents). Re-entry through an Indirect edge returns the stable
placeholder, so `struct Node { children: List<Node>(N) }` is finite. The final
verifier runs three-color cycle detection over Inline edges only, permits cycles
only when every cycle is broken by an Indirect edge, and separately rejects any
dangling/unfinalized placeholder.

Raw `CoreLayoutId` equality is not physical-layout equality. The public query is
`value_layouts_equivalent(table, source_type, target_type)`: equal
`CoreValueTypeId`s are the fast path; otherwise it calls cycle-safe
`layouts_equivalent` on their roots. The latter memoizes pairs with
`Visiting/Equivalent/Different`, compares size/alignment/shape and all scalar
metadata, offsets, capacity/stride/backing facts, then recursively compares both
Inline and Indirect child pairs. A re-entered Visiting **pair** is provisionally
equal; any later local mismatch makes the enclosing comparison different. This
is structural bisimulation of two finalized finite layout graphs, not cyclic
layout hash-consing.

### 3.4 D1 diagnostics and probes

D1 introduces `core.layout.UNSUPPORTED`, `core.layout.UNBOUNDED`,
`core.layout.OVERFLOW`, `core.layout.INFINITE_RECURSION` and
`core.layout.INVALID`. Focused probes cover every scalar/atom representation;
declaration-order struct offsets; enum tag/payload alignment; bounded
List/Set/Map stride and backing size; generic/nested member materialization;
same-id and distinct-id physical equivalence; indirect `Node` recursion;
deterministic second computation; and fail-closed unbounded collection,
overflow, direct struct/enum recursion and deliberately unfinished placeholder.
D2 (P6-8a) adds the closure representation: the fixed `(func_index, env_ptr)`
word pair plus its Indirect environment aggregate, including the finite
self-capture case — so the D1 fail-closed Closure probe is replaced by a
positive D2 probe.

The D2 layout is a *representation* fact only. The P6 value model has no
closure value (no function-table index value, no env-field walk, no
`call_indirect`), so a closure-typed edge is deliberately NOT a single-word P6
value: codegen's `place_is_scalar_leaf` / `place_is_aggregate_leaf` both reject
it and a closure projection leaf fails closed exactly like a `PtrLen` / bytes /
f64 leaf. Closure VALUE codegen is a later slice.

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
- **P4-C — nominal member templates (landed).** Declaration-owned flat
  `CoreMemberTypeTemplateNode{Concrete|Param|Nominal|Fn}` arena on struct fields /
  enum payload slots; `instantiate_member_template` substitutes through the
  shared `CoreValueType` hash-cons arena. Primitive/generic slot kInvalid holes
  are gone; `field_nominal_types` remains explicitly navigation-only.
- **P4-D — layout pass.** `TargetDataLayout` + side `CoreLayoutTable`; scalar /
  struct / enum / bounded-container D1, then closure env D2 (landed, P6-8a). D1
  uses stable placeholders and explicit Inline/Indirect dependency semantics
  (§3.3): direct inline recursion is infinite layout and fails closed;
  collection backing and the closure environment (both Indirect edges) break a
  cycle. The pass uses a layout-private value-type closure backed by
  the P4-C materializer and never mutates `CoreProgram` (§3.2).
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
