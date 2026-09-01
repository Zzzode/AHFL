# Core-IR KR6.5 E4-B0: Wire Schema and Durable-Resume Seam -- Design

> Status: **DRAFT rev 1** (design and research only).
>
> Scope: decide who decodes and type-checks RFC 0019/0021 `value_json`, define
> one canonical wire-schema projection, and define the durable record's control
> and sensitive-payload boundaries. This document does not add a resume export,
> opcode, import, Core node, codegen path, or production dependency.
>
> Priority: if the owner approves E4-A's pinned Wasmtime environment, E4-A
> implementation takes priority. E4-B0 stays design-only until this P0 boundary
> is reviewed.

## 0. Research result: the current checks cannot authorize Wasm resume

E2 deliberately forwards `AHFL_WIRE_VALUE_JSON` as opaque bytes. That is the
correct P5 boundary: wire JSON is not a P4-D object layout, and the module does
not contain a JSON evaluator. E4-B must not reverse that decision merely to add
resume.

The repository currently has two runtime checks, but neither is a sufficient
authority for E4-B:

1. `workflow_runtime.cpp::value_matches_return_type` checks only the outer
   `evaluator::Value` variant. A `StructValue` with the wrong nominal, fields,
   or nested values passes as any struct; an `EnumValue` with the wrong variant
   or payload passes as any enum. It also accepts `Never`, `Any`, and
   `Unresolved`, none of which may authorize a materialized E4-B result.
2. `response_schema_validator.cpp` recursively checks several AHFL `TypeRef`
   shapes, but it is a separate TypeRef-based relation. Generic declaration
   member TypeVars have already been erased from `FieldDecl.type_ref` in the
   Typed-to-AHFL bridge; P4-C had to persist member templates specifically to
   avoid reconstructing them from those TypeRefs. The validator also checks
   only enum variant existence, not positional/named payload arity and types.
3. `value_from_json` is intentionally schema-free. A JSON string could mean
   `String`, `Decimal`, or `Duration`; an array could mean `List` or `Set`; and
   `null` could mean `Unit`, `Option::None`, or a legacy `NoneValue`. Decoding
   first and checking the resulting untyped variant loses information needed
   to reconstruct the declared AHFL value.
4. The current `MapValue` JSON writer is canonical only for string-shaped keys.
   A non-string key cannot be emitted as a valid JSON object member without an
   additional wire encoding decision. E4-B0 therefore cannot claim all
   materialized `Map<K,V>` values are wire-decodable.

RFC 0022 also contains a stale implementation phrase: a persisted resume
record cannot validate an "interned TypeContext handle" by pointer equality in
a fresh process. Arena ids and pointers are owner-relative. Persisting either
as the type proof would be invalid.

These are representation gaps, not evidence-only gaps. A host type id, a
`CoreValueTypeId`, a `CoreLayoutId`, an argument hash, or a byte hash can bind a
record to other data, but none proves that decoded bytes have the declared
recursive logical type.

## 1. Layer decision: the trusted host owns wire decoding and validation

The **host adapter** is the only layer allowed to decode and type-check an
`ahfl_cap` wire frame.

- The Wasm module continues to treat `(ptr,len)` as opaque bytes. It does not
  parse JSON, inspect a type tag, calculate a layout, or call a test-only
  validation import.
- The host already owns the live capability implementation, target-memory
  access, result allocation, persistent store, and PENDING scheduling. Wire
  validation belongs at that boundary.
- The host is part of the trusted computing base. A hostile embedder can always
  violate an import contract; E4-B specifies and tests a conforming host rather
  than pretending the module can defend against its own imports.
- P4-D remains the sole in-memory layout authority, but it is not consulted to
  interpret wire JSON. The wire validator consumes a separate deterministic
  **projection** of verified logical types, not a second subtype or layout
  engine.

The same host-side rule engine validates all four ingress paths:

1. live capability arguments before invocation;
2. a live `AHFL_CAP_OK` result before it is returned to the module;
3. every memoized result loaded during replay; and
4. the host-supplied result for the previously pending call.

Parse failure, schema mismatch, unknown schema data, or coordinate drift is a
fatal resume error. There is no coercion and no fallback to a live capability.

## 2. Canonical `WireSchemaTable`

### 2.1 One flat graph, projected from verified logical types

E4-B introduces a runtime-ABI data model, not a new language type system:

```cpp
struct WireSchemaNodeId {
    uint32_t value;
};

enum class WireSchemaKind {
    Unit,
    Bool,
    Int,
    Float,
    String,
    Decimal,
    Duration,
    Timestamp,
    Uuid,
    Option,
    List,
    Set,
    Map,
    Struct,
    Enum,
    Tuple
};

struct WireSchemaField {
    string wire_name;
    WireSchemaNodeId type;
};

struct WireSchemaVariant {
    string wire_name;
    PayloadKind payload_kind; // Unit, Tuple, or Struct
    vector<WireSchemaField> slots;
};

struct WireSchemaNode {
    WireSchemaKind kind;
    optional<pair<int64_t, int64_t>> int_bounds;
    optional<pair<int64_t, int64_t>> string_bounds;
    int64_t decimal_scale;
    optional<uint64_t> capacity;
    vector<WireSchemaNodeId> children;
    string nominal_wire_name;
    vector<WireSchemaField> fields;
    vector<WireSchemaVariant> variants;
};

struct WireCapabilitySchema {
    CoreCapabilityId capability;
    SymbolId source_symbol;
    vector<WireSchemaNodeId> params;
    WireSchemaNodeId result;
};

struct WireSchemaTable {
    uint32_t format_version;
    vector<WireSchemaNode> nodes;
    vector<WireCapabilitySchema> capabilities;
};
```

The final C++ spelling may split each node kind into a `std::variant`, as the
repository principles require. The normative properties are:

- ids and vector positions are canonical identity inside one table;
- strings exist only where `value_json` itself carries a nominal, field, or
  variant spelling and therefore must be compared on the wire;
- no display name, source path, SourceRange, allocator address, wall clock,
  pid, hostname, endpoint, credential, or provider configuration is included;
- capability entries are ordered by `CoreCapabilityId`; fields, variants, and
  payload slots retain declaration order;
- every reference is in range, every node is root-reachable, and duplicates or
  unknown enum values are rejected;
- recursive schemas are graph edges, not recursively serialized trees.

The compiler projects this table from a verified `CoreProgram` plus the sorted
set of capabilities actually imported by the selected artifact:

- capability roots come directly from the selected
  `CoreCapabilityDecl::param_types` and `return_type`; an unused declaration
  does not enlarge host authority or the serialized table;
- scalar/refinement facts come from `CoreValueType`;
- nominal fields and enum payloads come from P4-C
  `CoreMemberTypeTemplateNode` roots instantiated with the nominal arguments;
- nominal role comes from `CoreTypeDecl::role`, never a parsed canonical name;
  and
- P4-D is not queried and no size/align/offset/stride is emitted.

Only the transitive schema-node closure of those roots is published. This is a
structural projection. It never asks whether one type is a subtype of another
and never guesses a member type from two endpoints.

### 2.2 Recursion and determinism

Projection reserves a stable schema node before visiting its children. A
recursive nominal therefore refers back to that id. Nodes are first discovered
by program-global `CoreValueTypeId` order; members are discovered in declaration
order. Repeated projection of the same verified Core program must produce
byte-identical tables.

The schema verifier accepts a cycle only when every reference is valid and the
graph is root-reachable. Runtime validation terminates because it walks a finite
decoded `Value` tree together with the schema graph; each recursive descent
consumes a child value. A malformed cyclic runtime object cannot be produced by
the unique-ownership `evaluator::Value` representation.

`CoreVtNever`, `CoreVtFn`, and `CoreVtClosure` are not wire values in this
slice. `Any`, `Unresolved`, and TypeVars cannot occur in verified Core. Any such
root fails schema projection with no partial table.

### 2.3 Exact schema-guided decoding

The host parses JSON syntax once, then decodes each node under its expected
schema. It does not call schema-free `value_from_json` and try to repair the
result afterward.

Examples:

- a JSON string under `String`, `Decimal`, or `Duration` constructs the exact
  corresponding runtime variant;
- a JSON array under `List` or `Set` constructs that exact collection and
  validates every element;
- `null` under `Unit` or `Option::None` is interpreted only by that root;
- an Option non-null value is decoded under its `Some` child;
  - **Nullable-child restriction (C2b P0-9):** because `Option::None` encodes as
    `null` and `Some(x)` encodes as x's own bytes, an `Option` whose direct child
    itself encodes as `null` — a `Unit`, or a nested `Option` — would make `None`
    and `Some(child-null)` indistinguishable. Such a schema (`Option<Unit>`,
    `Option<Option<T>>`) is rejected up front by the wire-schema local verifier
    (`core.wire.UNSUPPORTED`), so it is neither projectable from Core nor
    admissible as a transported table. `Option` of any non-null-encoding shape,
    including a recursive `Struct` (`Node{next: Option<Node>}`), remains legal.
    Consequently E4-B0 does NOT promise an unconditional `Option` round-trip; it
    promises a round-trip for every *projectable* Option schema. The check is a
    single gate in `LocalSchemaVerifier::validate_node`, shared by both entry
    points, so no separate predicate can drift.
  - **Reserved wire-name restriction (C2b P0-11):** the same local verifier also
    rejects two writer-impossible reserved names with `core.wire.UNSUPPORTED`
    (after the structural checks, so a malformed table reports its structural
    error first): a Struct field named `_type` (collides with the value_json
    struct discriminator, producing a duplicate wire key) and an ORDINARY Enum
    whose `wire_name` is `std::option::Option` (collides with the value_json
    Option special-case, which would encode it as `null`/inner rather than an
    `_enum` object). A genuine Option is the distinct `CoreWireSchemaOption` shape
    and is unaffected. Normal source-derived Core never produces either; any
    synthetic (projector-API) or transported attempt is fail-closed by the same
    `LocalSchemaVerifier::validate_node` gate, so no such table is published or
    minted.
- a struct must carry the exact `_type`, exact field set, and recursively valid
  field values;
- an enum must carry the exact `_enum`, a declared `_variant`, the exact payload
  form/arity/field set, and recursively valid payload values;
- bounds, capacity, UUID spelling, timestamp range, decimal spelling/scale, and
  duration spelling are checked rather than merely checking the outer variant.

There is no implicit numeric widening at this boundary. A Float uses the
canonical float JSON spelling, while Decimal uses its canonical string spelling.
Accepting an Int as Float or Int/Float as Decimal and leaving the wrong runtime
variant, as the legacy response validator currently permits, is forbidden.

Until a separately reviewed map encoding exists, E4-B accepts only
`Map<String,V>`. Any other Map key schema fails projection with a stable
`wire.UNSUPPORTED_MAP_KEY` diagnostic. Silently stringifying an arbitrary key
would change the RFC 0019/0021 byte contract.

### 2.4 One rule engine, two input policies

Two inputs must be checked without duplicating schema semantics:

- raw JSON must be decoded into a typed `evaluator::Value`; and
- an already materialized native memo/injected `Value` must be validated without
  serializing it and thereby hiding an incorrect runtime variant.

The implementation uses one schema-kind dispatch with two policies:

```text
WireJsonDecodePolicy  -> consumes JSON node, returns typed Value
RuntimeValuePolicy    -> consumes Value node, returns validation only
```

Both policies use the same field/variant/child traversal and exact-kind rules.
The shallow `workflow_runtime.cpp::value_matches_return_type` is deleted. The
TypeRef-based response validator is migrated to the shared schema API rather
than becoming a third durable-resume authority.

### 2.5 Legacy native callers do not define durable schema

Durable Wasm resume accepts only the Core projection above. A standalone native
HTTP/gRPC binding that currently supplies a `TypeRef` may use a migration
projector into the same `WireSchemaTable`, but that projector must fail closed
when the available AHFL declaration has erased generic member information. It
cannot substitute `Any` or skip a payload check. The durable differential
harness gives both the native reference run and the Wasm host the table derived
from the same checked-frontend-to-Core fork, mapped by source `SymbolId` with no
name fallback.

This permits compatibility migration without making TypeRef traversal a second
durable schema authority. Projection-equivalence tests cover every closed shape
that both layers can represent; the shared decoder/validator remains the only
runtime rule engine.

## 3. Schema transport to a generic host

### 3.1 Deterministic Wasm custom section

A generic embedder receives module bytes, not a `CoreProgram`. E4-B therefore
serializes the verified table into one deterministic custom section:

```text
section name: ahfl.wire-schema.v1
payload: versioned, length-delimited, canonical LEB128/index-based flat table
```

This is the only new artifact metadata proposed by B0. It does not alter the
`ahfl_cap` import name or `(i32,i32)->(i32,i32,i32)` signature, the public
`run2` signature, or ABI version 1. Wasm code cannot observe the section; the
host extracts and verifies it before instantiation.

The earlier E1-E3 "no custom section" rule prevented accidental debug names,
paths, and nondeterministic metadata. It was not a promise that typed host
metadata could never be added. This named section is allowed only because its
contents are required semantic wire data and its encoder is deterministic.
The standard Wasm name section remains absent.

Identity-only E1/E3 artifacts need no schema section and remain byte-identical.
An E2 capability artifact gains the section only when the future E4-B transport
slice is explicitly enabled; B0 design itself changes no byte.

The host rejects:

- missing or duplicate schema sections for a resumable artifact;
- an unknown format version, node kind, or field;
- non-canonical integer/string encodings, out-of-range ids, duplicate roots,
  or unreachable nodes;
- a capability table whose `(CoreCapabilityId, SymbolId)` ordering does not
  match the module's canonical `cap_<SymbolId>` imports; and
- any schema root not covered by the table verifier.

### 3.2 Artifact binding is not type proof

The host records a SHA-256 digest of the complete module and of the exact schema
section. On restore, both must match the loaded artifact. These digests bind a
resume record to one artifact version; they do **not** prove a payload's type.
After the digest check, the host still performs full schema-guided decode and
validation.

Likewise, RFC 0022 `arg_hash` remains the established deterministic replay and
idempotency cross-check. Hash equality never substitutes for argument parsing,
schema validation, capability identity, or ordinal equality.

## 4. Durable record: deterministic control, protected payload

### 4.1 A persisted arena id is never authoritative

The expected result schema is derived after restore from:

```text
verified module/schema digest
  -> WireCapabilitySchema[CoreCapabilityId]
  -> result WireSchemaNodeId
```

The record does not persist a TypeContext pointer, `CoreValueTypeId`,
`CoreLayoutId`, host type id, or free-standing expected-schema id as authority.
A redundant root id, if retained for diagnostics, must equal the root derived
from the capability table or the record is rejected.

### 4.2 Canonical control record

The E4-B record is append-only, versioned, and index-based:

```text
schema = ahfl.wasm-resume.v1
module_sha256
wire_schema_sha256
entry = { kind, id }
workflow_node_id
node_input_slot
pending = { capability_id, ordinal, arg_hash }
memo = [ { ordinal, capability_id, arg_hash, result_slot }, ... ]
```

Memo entries are strictly ascending and dense below the pending ordinal. Every
coordinate is cross-checked on replay. Unknown, duplicate, missing, or reordered
entries fail before a module resumes. `arg_hash` remains the exact RFC 0022
algorithm for compatibility unless a future RFC versions it.

Serialization uses fixed field order and canonical numeric encoding. It has no
wall clock, pid, host path, allocator address, hostname, endpoint, credential,
or hash-map iteration. Given the same logical control state and slot assignment,
it is byte-reproducible.

### 4.3 Literal "secret-free resume state" is impossible

Node input and capability results are application data. They may legitimately
contain a user secret. A durable runtime cannot both persist enough state to
resume and promise that all persisted state is public/secret-free. Treating a
hash of low-entropy input as harmless would also be unsafe.

The correct boundary is:

- compiler artifacts, the schema section, conformance manifests, and logs stay
  secret-free;
- the control record contains no raw `value_json`, only deterministic
  `PayloadSlotId` references;
- payload slots are stored by a host-owned confidential and integrity-protected
  state store, keyed by workflow/checkpoint/slot identity;
- the host loads a slot into private memory, verifies its bytes against the
  schema, and never emits them in evidence or diagnostics;
- secret-provider credentials remain handles and are never copied into either
  the record or payload; application data returned by a capability is still
  classified as sensitive runtime state; and
- the existing plaintext native `ahfl.workflow-recovery.v2` JSON store is not
  silently relabeled as a production-safe Wasm resume store.

This distinguishes deterministic semantics from public artifact reproducibility.
The protected state store may use encryption nonces or provider-specific storage
identities; those are host operational state, not inputs to compiler artifact
identity or replay order.

## 5. Resume transaction and ownership gate

No resume export is designed in B0. A later B2 may propose one only after the
schema and state-store foundations are implemented and verified.

The required host transaction for that later slice is already fixed:

1. load and verify the exact module plus schema digests;
2. parse and validate the control record and every memo coordinate;
3. retrieve the node input and memo result slots from protected storage;
4. schema-decode every frame before allocating it in target memory;
5. re-run ordinals below pending from validated memo bytes without invoking the
   live capability;
6. validate the injected pending result against the capability result root;
7. append the newly completed memo entry atomically; and
8. only then transfer the required frame ownership to a fresh module instance.

Any failure leaves the record unconsumed and invokes no live capability. E4-B
must separately define crash atomicity and last-use deallocation for capability
results routed across workflow nodes; B0 does not pre-approve an ownership
shortcut.

## 6. Implementation split after design approval

Implementation is intentionally split before any resume ABI:

1. **B0-C1 schema model/projector/verifier**: flat `WireSchemaTable`, Core
   projection through P4-C templates, recursion/map restrictions, deterministic
   serialization, and positive/negative structural tests. No Wasm byte change.
2. **B0-C2 shared host codec**: schema-guided JSON decode plus native-Value
   validation policies; delete the shallow resume checker; migrate the response
   validator and native replay checks to the shared rules. No resume export.
3. **B1 schema transport**: emit and parse `ahfl.wire-schema.v1`, strict import
   cross-check, deterministic bytes, and a conforming-host live E2 check. This is
   the first reviewed Wasm byte change.
4. **B2 durable resume**: separately design the append-only ABI, protected state
   store, atomic memo append, ownership, exact node-order observation, and
   execution evidence. B2 cannot start on an unchecked schema or plaintext
   payload shortcut.

E4-A remains independent and higher priority after owner approval.

## 7. Required probes

### 7.1 Schema projection and verification

- scalar/refinement roots, Option/Result, List/Set, `Map<String,V>`, tuple,
  generic user struct, enum tuple payload, enum struct payload, and an indirect
  recursive nominal;
- two projections of one Core program are byte-identical;
- field/payload TypeVars instantiate through the P4-C template SSOT;
- invalid/unknown root, orphan node, duplicate capability entry, wrong SymbolId,
  wrong declaration arity, Never/Fn/Closure, and non-string Map
  key fail closed with no partial table.

### 7.2 Shared decode and validation

- String/Decimal/Duration and List/Set are disambiguated by schema;
- Unit vs Option None and Option Some are decoded canonically;
- struct exact nominal/field set/nested generic field checks;
- enum exact nominal/variant/payload-kind/arity/field/nested-type checks;
- bounds/capacity/UUID/timestamp/decimal/duration malformed values fail;
- the raw-JSON and native-Value policies share the same accept/reject matrix;
- Int-as-Float, String-as-Decimal, and a wrong outer runtime variant fail;
- the old shallow helper is absent.

### 7.3 Transport, replay, and security boundaries

- custom-section parse/write round trip and byte-identical double emit;
- missing/duplicate/unknown/non-canonical schema section fails before
  instantiation;
- module/schema digest mismatch fails even when numeric ids happen to match;
- a matching digest or `arg_hash` with wrong typed bytes still fails schema
  validation;
- memo coordinate mismatch never falls back to a live call;
- control-record ordering is deterministic and contains no raw payload;
- payload bytes never appear in diagnostics, evidence manifests, or module
  custom sections; and
- a protected-store failure invokes no module or capability.

## 8. Explicit non-goals

B0 does not:

- define `resume2`, a resume opcode, or a clear-latch operation;
- change `ahfl_cap`, `run`, or `run2` bytes;
- claim arbitrary Map-key wire support;
- use P4-D layout as a wire schema;
- accept a host/core/layout id or hash as type proof;
- implement cross-node capability-result ownership or fan-out;
- expose an exact workflow runtime event stream;
- replace E4-A's required real-Wasmtime evidence lane; or
- claim KR6.5 execution-proven.

The next review decides only this wire-schema and state boundary. Production
resume remains fail-closed until B0, B1, and a separately reviewed B2 are all
implemented.
