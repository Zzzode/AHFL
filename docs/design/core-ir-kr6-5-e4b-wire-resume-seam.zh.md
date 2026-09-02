# Core-IR KR6.5 E4-B0: Wire Schema and Durable-Resume Seam -- Design

> Status: **E4-B1 IMPLEMENTED; B2-S / B2-A-pre / B2-A / B2-B / B2-C /
> B2-D1a-1..4 internal-authority FOUNDATION code/evidence slices landed**
> (schema-guided wire codec + durable-resume seam + full CLI/HTTP/gRPC/shim
> demotion + the wire-schema Wasm transport chain: C1 payload decoder
> `2d25aa3b`, C2 custom-section writer `a73a8991`, C3 runtime module inspector
> `b75fc8ec`; the legacy `response_schema_validator` is deleted; plus the B2-S
> base-support SHA-256/HMAC primitive below).
> B2 (the full durable-resume ABI/control-record, protected payload store,
> atomic/crash, ownership/last-use, and node-order evidence work) remains
> design/pending: its **B2-0 FOUNDATION DESIGN is locked** (the full-workflow
> replay ledger §4.2, execution manifest + resume state machine + node-event
> buffer §4.4, transaction/recovery §5.1, and resource contract §5.2); the
> B2-S / B2-A-pre / B2-A FOUNDATION slices have landed (record codec + runtime
> module-context below), and the B2-B integrity-only LOCAL durable-resume payload
> store FOUNDATION has since landed (B0 artifact codecs `d355171b`, B1a store
> `72a062e0`, B1b crash evidence `c710a997`) — NON-CONFORMING to the §4.3
> `ProtectedPayloadStore` (`guarantees` bit0 `rollback_protected` = 0 AND bit1
> `confidential_at_rest` = 0); the B2-C capability-workflow emitter has landed
> (`4224a52f`); NO production host code is implemented and B2 / KR6.5 stay false.
> The confidential
> `ProtectedPayloadStore`, a production key/KMS authority, real rollback
> protection, and the production host are all named future gates (§6). The
> B2-S security PRIMITIVE has landed as a base-support FOUNDATION — the NEW
> raw SHA-256 span input + typed 32-byte `Sha256Digest` API (`b0628f25`) and
> `hmac_sha256` (RFC 2104/FIPS 198-1, `147da993`); the new `hmac_sha256` API is
> now directly consumed by the A1 record codec below, while the existing
> `sha256_hex(string_view)` production callers remain with preserved in-domain
> behavior; there is still no production persistence/key caller. B2 and KR6.5
> stay false. The B2-A-pre shared verified-table
> authority has also landed in `compiler_ir` (`5c9dd67c`): an opaque, copy-only
> `VerifiedWireSchemaTable` that lets many bindings share one verified-once table
> without per-mint re-verification/copy. Its new public-in-header factories
> (`make_verified_wire_schema_table`, `make_wire_binding_from_verified_table`)
> are now directly consumed by the A2 module-context below (the existing
> `migrate_type_ref_to_wire_binding` / `make_wire_binding_from_transported_table`
> production paths continue to use the same shared backing internally); there is
> still no production host caller. B2 and KR6.5 stay false.
> The A1 record codec (`8a987ca1`) and the A2 runtime module-context
> (`5bd812b2`) have landed as FOUNDATION runtime slices (see §4.2, §4.4, §6);
> A1/A2 themselves add no payload store. The B2-B integrity-only LOCAL
> durable-resume payload store FOUNDATION has since landed (B0 artifact codecs
> `d355171b`, B1a store `72a062e0`, B1b crash evidence `c710a997`) —
> NON-CONFORMING to the §4.3 `ProtectedPayloadStore` (`guarantees` bit0
> `rollback_protected` = 0 AND bit1 `confidential_at_rest` = 0). The B2-C
> capability-workflow emitter has landed (`4224a52f`); no confidential/at-rest
> store, no production host, and no artifact-digest comparison exist — those remain
> future gates. The B2-D **D1a-1..4 host-independent internal authorities** have
> since landed as FOUNDATION code/evidence: the A1 `entry_input_slot` record field +
> the B1 exact-slot-set/store staged admission (`9b8053cc`), the A2 three typed
> artifact-digest getters (compute/expose only, no comparison) (`9a859224`), the
> runtime-owned node-event decoder (`c59a235d`), and the per-Verified-Result
> canonical wire-JSON size bound (`5fbd1a9b`, with the value-JSON integer
> locale-independence prerequisite `abf6bd23`). REMAINING for B2-D closure: the D1b
> host-independent replay controller, the TOTAL result-size reservation +
> one-page/u32-host verdict, the artifact-digest COMPARISON gate, the
> event<->manifest coordinate join, the D2a production VM/host adapter + non-test
> caller, the D2b durable-effect intent/result authority (which also owns the
> `IdempotencyToken` code — contract-only today), and the conforming
> confidential/KMS/rollback protected store. B2-E exact evidence also remains. B2
> and KR6.5 stay false.
>
> Scope: decide who decodes and type-checks RFC 0019/0021 `value_json`, define
> one canonical wire-schema projection, and define the durable record's control
> and sensitive-payload boundaries. The design is implemented through B1 plus the
> B2-S / B2-A-pre / B2-A / B2-B FOUNDATION slices (base-support HMAC, the shared
> verified-table authority, the `ahfl.wasm-resume.v1` record codec, the runtime
> module-context + exec-manifest decoder, the B2-B integrity-only LOCAL
> payload store FOUNDATION, the B2-C capability-workflow emitter `4224a52f`
> for the exec-manifest + node-event buffer + capability-status scheduling, and
> the B2-D1a-1..4 host-independent internal authorities
> `9b8053cc`/`9a859224`/`c59a235d`/`5fbd1a9b`). It
> does not
> by itself authorize the remaining
> confidential/at-rest store (the confidential/KMS/rollback protected store, a
> PREREQUISITE sub-slice of full B2-D closure) or the remaining B2-D artifacts (the
> D1b replay controller, the TOTAL result-size preflight + artifact-digest
> COMPARISON gate + event<->manifest join, the D2a/D2b production host +
> durable-effect authority) or B2-E exact events or any
> E4-A dependency/CI change; §3.2
> still DESIGNS the future production-host artifact-digest comparison/gate.
>
> Priority: the E4-B0 design gate is CLOSED and E4-B0-C1/C2 and E4-B1 are
> implemented (schema-guided wire codec + durable-resume seam + full ingress
> demotion + wire-schema transport; see the Status line above). E4-A remains
> independent; it becomes the higher priority ONLY after the owner approves its
> pinned Wasmtime environment. That approval has not been given, so this slice
> touches no CI or dependency wiring for E4-A.

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
   (Historical: RFC 0026 C2b G4d DELETED `response_schema_validator.cpp`
   entirely; the schema-guided `core_wire_codec` `validate_value` / `decode_json`
   against a verified wire binding is now the sole native-Value validation
   authority. This numbered item records the original motivation.)
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

Numeric provenance (C2b P0-10). The base JSON DOM records how each number was
parsed — `SignedInteger`, `UnsignedInteger` (a high-bit magnitude stored in a
dedicated `uint_val`, not a signed bit pattern), `FloatSyntax`, or
`IntegerFallback` (an integer token beyond both 64-bit integer ranges) — because
`Kind` alone cannot distinguish a legal `INT64_MIN` from a folded unsigned value,
nor a genuine float token from an over-large integer. `as_int` accepts only
`SignedInteger`; `as_uint` accepts `UnsignedInteger` and non-negative
`SignedInteger`; the schema-free `value_from_json` (both the string and the direct
`const JsonValue&` overload) constructs an `IntValue`/`TimestampValue` only from a
`SignedInteger` and a `FloatValue` only from a `FloatSyntax`, failing closed on
`UnsignedInteger`/`IntegerFallback` at any depth. Trust-boundary consumers that
already hold a parsed subtree (durable resume load, the CLI tool catalog) decode
that DOM directly rather than `serialize_json` then re-parse, because a generic
serialize→parse round-trip does NOT preserve `FloatSyntax`/`IntegerFallback`
provenance. The generic serializer's Float branch and every legal-producer
snapshot byte are unchanged (an integral `FloatSyntax` still emits bare `1`, so
v1/v2 snapshot bytes and `arg_hash`, which hashes the evaluator `value_to_json`
output directly, are unaffected); the only byte change the serializer makes is
that a formerly-corrupt high-bit unsigned integer now serializes as its correct
unsigned decimal instead of a negative. A fully provenance-preserving serializer
(option B) was considered and rejected as an out-of-scope persisted-format
change.

Until a separately reviewed map encoding exists, E4-B accepts only
`Map<String,V>`. Any other Map key schema fails projection with a stable
`wire.UNSUPPORTED_MAP_KEY` diagnostic. Silently stringifying an arbitrary key
would change the RFC 0019/0021 byte contract.

### 2.4 One rule engine, two input policies

Two inputs must be checked without duplicating schema semantics:

- raw JSON must be decoded into a typed `evaluator::Value`; and
- an already materialized native `Value` must be validated without serializing it
  and thereby hiding an incorrect runtime variant.

The implementation uses one schema-kind dispatch with two policies:

```text
WireJsonDecodePolicy  -> consumes JSON node, returns typed Value
RuntimeValuePolicy    -> consumes Value node, returns validation only
```

Both policies use the same field/variant/child traversal and exact-kind rules.

**Which policy applies is SOURCE-DEPENDENT, not "native memo/injected Value is
always native-validated"** (that earlier phrasing was wrong; see §9 for the landed
Stage-3 behavior). Concretely for durable resume:

- a **persisted** memo entry's authority is its JSON (LegacyV2 raw `result` substring
  or ExactSidecar sidecar wire), decoded by the `WireJsonDecodePolicy`
  (`decode_json` / the legacy variant) — its schema-free materialized `result` Value
  is a compat projection, NEVER the authority;
- only a **programmatic NativeOnly** memo entry and the **host-supplied native
  pending** result are checked by `RuntimeValuePolicy` on the ORIGINAL Value.

The shallow `workflow_runtime.cpp::value_matches_return_type` is deleted. The
TypeRef-based response validator is migrated to the shared schema API rather than
becoming a third durable-resume authority.

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
Only an E2 agent artifact whose `plan.imports` is non-empty appends exactly one
target `ahfl.wire-schema.v1` section after the Code section (at module EOF); E1,
E3, and no-import artifacts stay byte-identical. The runtime inspector extracts
and verifies the section before instantiation (E4-B1: writer `a73a8991`, runtime
inspector `b75fc8ec`).

The host rejects:

- missing or duplicate schema sections for a resumable artifact;
- an unknown format version, node kind, or field;
- non-canonical integer/string encodings, out-of-range ids, duplicate roots,
  or unreachable nodes;
- a capability table whose `(CoreCapabilityId, SymbolId)` ordering does not
  match the module's canonical `cap_<SymbolId>` imports; and
- any schema root not covered by the table verifier.

### 3.2 Artifact binding is not type proof

The host records three SHA-256 digests: the whole emitted module, the raw AHFLWS
table payload (no custom-name framing), and the raw AHFLXM section payload (no
custom-name framing). On restore, the B2-D digest gate compares all three against
the loaded artifact AFTER record HMAC authenticity succeeds (never digests-first,
per §5.1). The A2 module authority now computes these three digests during its
single framing pass and exposes them as three named typed getters returning a
header-local `using ArtifactDigest = std::array<uint8_t, 32>` by value (no
base_support header leak, no wrapper trio, no new dependency edge) — LANDED as
B2-D1a (`9a859224`). A2 only COMPUTES and EXPOSES the digests; the digest
COMPARISON gate (the actual restore-time match) remains B2-D and is NOT
implemented. A mismatch fails even when numeric ids coincide. These
digests bind a resume record to one artifact version; they do **not** prove a
payload's type. After the digest check, the host still performs full schema-guided
decode and validation.

Likewise, RFC 0022 `arg_hash` remains the established deterministic replay and
idempotency cross-check. Hash equality never substitutes for argument parsing,
schema validation, capability identity, or ordinal equality. The 64-bit `arg_hash`
is a ledger integrity comparator only — never a dedup or authenticity authority.

Cross-process exactly-once dedup (FUTURE; contract locked in B2-D D1a, code owner
D2b) uses an opaque 32-byte `IdempotencyToken = SHA-256(preimage)` over a FIXED,
canonical, fixed-width preimage (all integers little-endian, no ULEB/text):

```text
"AHFL-IDEMPOTENCY-v1"            (ASCII domain, no trailing NUL)
authority_id                    (IdempotencyAuthorityId: 16 raw bytes)
CoreWorkflowId                  (u32-LE)
ResumeCheckpointId              (u64-LE)
CoreWorkflowNodeId              (u32-LE)
InvocationOrdinal               (u64-LE)
CoreCapabilityId                (u32-LE)
source_symbol                   (u64-LE)
SHA-256(canonical typed Param bytes)   (32 raw bytes)
```

`generation` and `attempt` are EXCLUDED (they change across replay). `authority_id`
is a strong type `IdempotencyAuthorityId` — an opaque fixed 16-byte identity (the
preimage field `authority_id` is exactly its raw bytes; no wrapper trio, no
generalization). It is the dedup/intent backend's IMMUTABLE identity, supplied by
the typed backend authority or bound once at host/controller construction — NEVER
derived from `key_id`, a path, or a hostname, and never switched within a checkpoint
lifetime (a namespace swap on recovery would mint a new token and bypass dedup).
Within an `authority_id` scope, `(CoreWorkflowId, ResumeCheckpointId)` is the stable
checkpoint namespace. This token is SELECTED for Core-Wasm cross-process authority
INSTEAD OF reusing the native process-local `compute_idempotency_key`
(workflow-index + `RunId{0}`, not a stable instance identity); the native path is
NOT replaced and its `void` intent sink is unchanged. It is an identity/collision
authority only, never an authenticity or rollback authority; whether `authority_id`
is persistently bound into authenticated checkpoint metadata is a D2b gate, and
D1 does NOT claim exactly-once.

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
from the capability table or the record is rejected. A binding root is ALWAYS
derived from the verified table through a typed selector
(`CoreWireRootSelector{capability, expected_source_symbol, kind, param_index}`);
a bare `CoreWireSchemaNodeId` is never persisted or accepted as authority.

### 4.2 Canonical control record — full-workflow replay ledger

> Status: **B2-0 design LOCKED; the A1 record codec + authenticator LANDED
> (`8a987ca1`); the B2-B integrity-only LOCAL payload store LANDED (B0 codecs
> `d355171b`, B1a store `72a062e0`, B1b crash evidence `c710a997`).** The
> `encode_and_authenticate` / `decode_and_authenticate` codec, its two-pass HMAC
> admission, canonical re-encode, and record-internal invariants are implemented;
> integrity-only LOCAL slot/manifest/pointer persistence has landed as a
> §4.3-NON-CONFORMING FOUNDATION (`guarantees` bit0 = 0 AND bit1 = 0). The
> confidential/at-rest protected production store, the production host, and the
> artifact-digest comparison are NOT implemented and this does not close B2 or
> KR6.5. LAYERING: the original `8a987ca1` codec was the pre-`entry_input_slot`
> grammar; B2-D1a (`9b8053cc`) ADDED the REQUIRED `entry_input_slot` field to the
> A1 (`AHFLWR`) record as a no-production-caller INTERNAL pre-production grammar
> change. The new decoder's fixed field order + invariants are the admission
> authority, enforced by a PERMANENT old-golden reject gate (the
> pre-`entry_input_slot` golden is permanently rejected, not accepted by canonical
> re-encode alone). public C ABI and `ahfl.workflow-recovery.v1|v2` are unchanged;
> no persisted-data migration (no production persistence caller).

The grammar below is the LANDED D1a record shape (including `entry_input_slot`,
added by `9b8053cc`).

A fresh module instance re-runs the whole workflow from the start of its
deterministic (Kahn) schedule, so a single-node record cannot locate a prior
node's memo (invocation ordinals are per-node) nor avoid re-invoking earlier
capability nodes. Because the deterministic replay recomputes downstream inputs
but CANNOT recompute the workflow's own entry frame (it enters the module only as
`run2` arguments), that entry frame is persisted once as `entry_input_slot`; the
B2 record is therefore a FULL-WORKFLOW per-node replay
ledger — append-only, versioned, index-based, authenticated:

```text
record = body || auth_header || tag
body:
  magic = "AHFLWR"
  format_version = 1
  guarantees            (u32 bit flags: bit0 rollback_protected, bit1 confidential_at_rest)
  module_sha256         (64 lowercase hex; whole emitted module)
  wire_schema_sha256    (64 lowercase hex; raw AHFLWS table payload, no custom-name framing)
  exec_manifest_sha256  (64 lowercase hex; raw AHFLXM section payload, no custom-name framing)
  entry = { kind = Workflow, id }            (id: CoreWorkflowId)
  entry_input_slot      (PayloadSlotId; the caller-provided workflow ENTRY frame
                         for the fresh replay; non-invalid; DISTINCT from every
                         memo.result_slot; opaque bytes, NOT type-checked against
                         any transported schema — see below)
  suspended_node_id                          (CoreWorkflowNodeId)
  resume_state          (u8: 0 = Suspended, 1 = Injected)
  node_count
  nodes = [ {
    workflow_node_id      (CoreWorkflowNodeId; identity; globally UNIQUE
                           within the record; NOT numerically dense)
    schedule_pos          (dense 0..frontier; nodes[i].schedule_pos == i,
                           so nodes[] is strictly ascending and gap-free)
    node_kind             (u8: 0 = identity, 1 = capability)
    memo_count
    memo = [ {
      invocation_ordinal  (InvocationOrdinal)
      capability          (CoreCapabilityId)
      source_symbol
      arg_hash
      result_slot         (PayloadSlotId)
    }, ... ]
    pending?              (present ONLY for suspended_node_id when
                           resume_state = Suspended:
                           { invocation_ordinal (InvocationOrdinal),
                             capability (CoreCapabilityId),
                             source_symbol, arg_hash })
  }, ... ]
auth_header = { alg_version = 1 (HMAC-SHA256), key_id (16 bytes), generation (u64) }
tag         = 32 bytes (HMAC-SHA256)
```

Identity is `(workflow_node_id, invocation_ordinal)` — the invocation ordinal is
per node, so many nodes may share ordinal 0. `capability` is the canonical
`CoreCapabilityId` (the table-root selector authority); `source_symbol` is the
RFC 0022 SymbolId cross-check; both are matched strictly (0 legal; presence via
`has_value()`, never `!= 0`). The capability IMPORT ordinal (position in the
module import table / manifest) is a DISTINCT concept from this per-node
INVOCATION ordinal and never shares a field or diagnostic.

Every persisted identity is a distinct strong type in the C++/semantic model
(`CoreWorkflowId`, `CoreWorkflowNodeId`, `CoreCapabilityId`, `PayloadSlotId`, and a
distinct `InvocationOrdinal` separate from `CapabilityImportOrdinal` and the A2
`ManifestCallSiteIndex`); the model never exposes interchangeable bare integer
identities. Their ON-WIRE representation remains the canonical ULEB widths already
specified. Admission rejects the `UINT32_MAX` invalid sentinel of `CoreWorkflowId`
/ `CoreWorkflowNodeId` / `CoreCapabilityId` and the defined invalid sentinel of
`PayloadSlotId`; `CoreCapabilityId{0}` and `SymbolId{0}` stay legal and
`source_symbol` has no invented invalid sentinel; every ULEB-to-index/`size_t`
conversion is checked and `pending.invocation_ordinal == frontier.memo.size()` is
compared without narrowing or overflow.

The `entry_input_slot` is REQUIRED and its `PayloadSlotId` must differ from every
`memo.result_slot` in every node (input provenance never shares slot authority
with result provenance); admission rejects the invalid sentinel and any collision.
It is the ONLY entry-frame authority: a fresh instance's `run2(ptr, len)` is fed
these exact opaque bytes verbatim. AHFLWS carries only capability Param/Result
roots (no workflow-entry schema), so the entry frame is admitted OPAQUE (slot
artifact authenticity + length + cap only) and is NOT type-checked against a
transported schema; each capability import Param IS still decoded/type-checked
through its Verified binding before use.

Per-node memo entries are strictly ascending and dense by `invocation_ordinal`.
`pending` (Suspended state) belongs to exactly `suspended_node_id` on its highest
ordinal and its `(node, ordinal)` is NOT yet in that node's memo; in the Injected
state there is no `pending` and that ordinal is already the last memo entry — no
carried `(workflow_node_id, invocation_ordinal)` may appear in both memo and
pending (the enumerated invariant list below is the authority). The control
record's `resume_state` has only two values
(0 = Suspended, 1 = Injected); a completed workflow is NOT a third record state —
completion is recorded as a Consumed tombstone in the commit manifest (§5.1), not
as `resume_state = 2`. Every coordinate is cross-checked on replay; unknown,
duplicate, missing, reordered, non-canonical, out-of-bounds, or trailing bytes
fail before a module resumes, and a canonical re-encode-equality gate rejects any
non-shortest encoding.

Record-internal structural invariants (A1 admission, all fail-closed): `nodes` is
non-empty; `nodes[i].schedule_pos == i` (dense, gap-free); every
`workflow_node_id` is globally unique within the record;
`nodes.back().workflow_node_id == suspended_node_id` and that frontier node is a
capability node; an identity node has an empty memo and never a pending; each
node's memo `invocation_ordinal`s are exactly `0..memo_count-1`. A
`(workflow_node_id, invocation_ordinal)` LEDGER COORDINATE that the record carries
appears in EXACTLY ONE of memo or pending — a no-overlap rule over the coordinates
actually present, NOT a requirement that every possible ordinal appear. Suspended:
the whole record has EXACTLY ONE pending, only on the frontier, with
`pending.invocation_ordinal == frontier.memo.size()` and not present in memo.
Injected: the record has NO pending, the frontier memo is non-empty, and its last
entry is the committed injected ordinal (the semantics `resume_state` gives that
last memo entry). A non-frontier node never carries a pending. A1 verifies ONLY
these record-internal properties; consistency with the compiled plan/manifest is
NOT an A1 concern (see the ownership chain in §4.4).

`arg_hash` is the exact RFC 0022 `evaluator::hash_values`
algorithm and `pending.arg_hash` is REQUIRED (the host holds schema-decoded args
at suspend and persists it immediately; no `pending_args_slot` is forced). The
record's own trust root is the HMAC-SHA256 tag over the exact on-wire record
prefix `[0, tag)` — i.e. `body || auth_header`, the tag excluded. The prefix's
FIRST authenticated bytes are `magic` (`AHFLWR`) then `format_version`: a record
of a different artifact class or version has a different prefix, so its tag fails
verification under this class/version unless the MAC itself is forged.
(Implementation benefit: that contiguous prefix is passed directly to the
one-span `hmac_sha256` — no scatter/gather MAC API and no pre-authentication copy
of the whole record.) The artifact digests are integrity of the module and
schema, not of the record.

Admission is TWO-PASS. Layout `body || auth_header || tag` with a FIXED 57-byte
tail (`auth_header` 25 bytes = {`alg_version` u8, `key_id` 16 bytes, `generation`
u64 little-endian} + `tag` 32 bytes), located from EOF. PASS 1 (untrusted) checks
ONLY: minimum total length, `magic` + `format_version`, the fixed tail offsets,
`alg_version` in its allowed set, and equality of the record `key_id` against the
caller-supplied `expected_key_id` — it NEVER reads a body count and NEVER
reserves or allocates on any attacker-supplied count. `key_id` and `tag` are
compared with a fixed-work, no-early-exit BEST-EFFORT routine (portable C++
cannot prove constant-time). Only AFTER the tag verifies does PASS 2 decode the
body's ULEB fields/counts and enforce the structural invariants. The caller
explicitly provides the candidate `expected_key_id` and borrowed key bytes; this
slice has NO keyring, KMS, or key resolver. Canonical admission does NOT re-run
HMAC: pass 2 still validates the header (`alg_version` allowed set; `key_id` /
`generation` fixed shape), then the decoder re-encodes the parsed, validated
model's canonical `body || auth_header` and compares it byte-for-byte against the
already-authenticated prefix `[0, tag)` (the fixed-width `auth_header` / `tag`
have no alternative canonical form), so no second HMAC runs.

Node identity-only nodes carry no output slot: on
resume every node output and downstream node input is DETERMINISTICALLY
RECOMPUTED from validated memo results plus pure (Goto/Identity) runners, so a
`node_input_slot` is neither authority nor required and is omitted from this
Approach-A ledger.

Serialization uses fixed field order (one order — `invocation_ordinal` first in
both memo entries and `pending`) and canonical numeric encoding. Integer encoding
(single rule): every tag / discriminant / presence / version field is an EXACT u8
with an enumerated allowed set — `format_version` u8 ∈ {1}, `entry.kind` u8 ∈
{0=Workflow}, `resume_state` u8 ∈ {0=Suspended, 1=Injected}, per-node `node_kind`
u8 ∈ {0=identity, 1=capability}, and pending presence is implied by
`resume_state` (present iff Suspended), not a separate byte; the
`auth_header.alg_version` u8 ∈ {1=HMAC-SHA256}; every OTHER `body`
integer (`guarantees` u32; all counts, ids, ordinals, slots, `arg_hash`) is a
canonical unsigned LEB128; and the fixed-width fields are exact — each SHA-256 is
64 lowercase-hex ASCII bytes, `auth_header.key_id` is 16 raw bytes,
`auth_header.generation` is a fixed u64 little-endian, and `tag` is 32 raw bytes.
An out-of-set u8, an unknown `guarantees` bit, or a non-canonical LEB is rejected.
It has no wall clock, pid, host path, allocator address, hostname, endpoint,
credential, or hash-map iteration. Given the same logical control state and slot
assignment, it is byte-reproducible.

### 4.3 Literal "secret-free resume state" is impossible

> Status (current state): the boundary below is the TARGET confidential +
> integrity-protected contract; it is NOT yet implemented. The landed B1a
> `IntegrityPayloadStore` (`72a062e0`) is a §4.3-NON-CONFORMING integrity-only
> LOCAL store — it authenticates bytes but provides NO at-rest confidentiality
> (`guarantees` bit1 `confidential_at_rest` = 0) and NO rollback protection
> (bit0 `rollback_protected` = 0), so it must never be described as a conforming
> `ProtectedPayloadStore`. The existing plaintext native
> `ahfl.workflow-recovery.v1|v2` JSON store is unchanged and is NOT relabeled.

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

### 4.4 Execution manifest, resume state machine, and node-event buffer (Approach A)

> Status: **B2-0 design LOCKED; the A2 exec-manifest decoder + runtime
> module-context LANDED (`5bd812b2`); the B2-C capability-workflow emitter LANDED
> (`4224a52f`).** The `ahfl.wasm-exec-manifest.v1` decoder,
> the module framing + import/schema/manifest set-equality cross-check, and the
> eager Param/Result call-site bindings are implemented; the compiler-side
> manifest EMITTER (`4224a52f`), the module-side capability-status scheduling with
> import-time `PENDING` propagation, and the node-event buffer have LANDED as a
> FOUNDATION capability-workflow emitter; the production host + artifact-digest
> COMPARISON gate are NOT implemented and this does not close B2 or KR6.5. The
> resume STATE MACHINE's HOST side — fresh-instance replay/injection — remains
> B2-D, which is laddered: D1a host-INDEPENDENT internal authorities have LANDED
> (entry_input_slot record field + B1 exact-slot-set/store staged admission
> `9b8053cc`, A2 typed artifact digests `9a859224`, a runtime-owned node-event
> decoder `c59a235d`, and a per-Verified-Result canonical size bound `5fbd1a9b`;
> the 32B IdempotencyToken remains contract-only, code owner D2b), D1b a
> host-independent replay controller proving
> only deterministic state transitions (FOUNDATION — not correctness-complete, no
> real VM, no durable-effect intent/result authority, no power-loss/exactly-once
> closure), D2a a production embeddable VM/host adapter + non-test caller (the
> production-host step), and D2b a durable-effect intent/result authority; the
> conforming confidential/KMS/rollback store is a PREREQUISITE sub-slice of full
> B2-D closure. The emitter provides only module-side manifest + event bytes.

The chosen resume approach adds NO new module ABI SYMBOL or SIGNATURE and reuses
the existing PROJECT `import_count + base` function-index RULE unchanged (imported
functions occupy the low indices; each defined function's index is
`import_count + base`). Precisely: the existing `run` / `run2` export and
`ahfl_cap` import Wasm ABI signatures are unchanged, and the no-capability E1/E3
fixtures keep `import_count == 0` so their bytes AND their function/export index
NUMBERS stay frozen; the E2 agent artifact is not rewritten. The new
capability-bearing WORKFLOW artifact is the first capability-workflow byte change
and a NEW baseline: once it carries `ahfl_cap` imports its defined/export function
indices (runners / `run` / `run2`) NECESSARILY SHIFT by `import_count` UNDER THE
SAME RULE — that shift is expected, not a new ABI. Its `run2` BODY also
necessarily changes (capability `PENDING` propagation + node-event writes). Only
the ABI symbols/signatures and the index-computation rule are frozen — not the
concrete index numbers of the capability-workflow baseline, and not that body.
The capability-bearing workflow module carries a
deterministic EXECUTION MANIFEST (a compiler-emitted custom section) and a
module-written node-event buffer; the host drives a fresh instance and answers
the existing `ahfl_cap` imports.

Write-safety and re-entry contract (capability-workflow baseline). Legacy `run`
is pointer-only (the v1 `(ptr, len)` ABI carries no status), so on a
capability-workflow it MUST trap BEFORE any state mutation, input read, or
capability effect — a `PENDING` must never leak through `run`. `run2` returns
`OK` only after complete success; among non-OK import statuses it propagates only
`PENDING` as `(PENDING, 0, 0)`, and `ERROR` or any unknown/unexpected status
returns a fail-closed `(ERROR, 0, 0)` (never a fabricated status, never
trap-as-success); a non-`OK` status writes no completion event and does not
increment `event_count`. When `run2` returns `PENDING` it sets the private
`pending_latched` global; the FIRST instruction of any subsequent same-instance
`run2` checks that latch and traps BEFORE it reads or transfers a new input
frame, clears the event-log header, or re-runs any import — otherwise a caller
could re-invoke a suspended instance and repeat live effects. This latch is a
genuine safety gate, independent of the event buffer: the `event_count` bound
only limits count authority and cannot substitute for it. The `pending_latched`
global is emitted ONLY on the capability-workflow baseline; no-capability E1/E3
modules keep their existing global section and bytes.

Execution manifest — `ahfl.wasm-exec-manifest.v1`, compiler-emitted, carrying NO
key material and NO self-HMAC (a runtime deployment key must never enter the
build). Its integrity is anchored by `exec_manifest_sha256` inside the authenticated
control record. Grammar: magic `AHFLXM`, version u8 `1`, `entry{kind: u8, id: u32}`,
`node_count: u32`, and per node `{ workflow_node_id: u32, schedule_pos: u32,
cap_call_count: u8 (statically 0 or 1 today), and if 1: capability: u32,
source_symbol: u64 }`. It carries NO Param/Result kind and NO bare
`CoreWireSchemaNodeId`: a capability call always needs BOTH a Param and a Result
binding. The runtime module-context is the CALL-SITE AUTHORITY: a caller cannot
submit an arbitrary `(capability, source_symbol)` to mint. The context's
`resolve(ManifestCallSiteIndex)` returns a privately-constructed
`VerifiedCoreWasmCallSite` token that shares the module authority's immutable
payload; the token exposes only
the narrow host coordinates (`workflow_node_id`, `schedule_pos`,
`invocation_ordinal`, `capability`, `source_symbol`, `import_ordinal`) and mints
Param{param_index 0} / Result from its OWN authority. `ManifestCallSiteIndex`,
`CapabilityImportOrdinal`, and `InvocationOrdinal` are three DISTINCT strong types
that never share a variable or diagnostic. A token is bound to its source module
(not usable against another module), stays valid after the module handle is
destroyed (shared immutable payload), and exposes no raw table,
`CoreWireSchemaNodeId`, or manifest internals. The manifest never
stores a root, a kind, or a bare node id. `schedule_pos` is dense and
`workflow_node_id` is unique WITHIN the manifest; the manifest is NOT cross-checked
against a plan here (the runtime holds no verified plan — see the ownership chain
below). Wire encoding
(same discipline as the control record): `version` / `entry.kind` /
`cap_call_count` are exact u8 with enumerated allowed sets, every other integer
(`entry.id`, `node_count`, `workflow_node_id`, `schedule_pos`, `capability`,
`source_symbol`) is a canonical unsigned LEB128; `node_count` is gated by
remaining / min-entry-bytes + checked arithmetic BEFORE any reserve or growth;
decode requires exact EOF and a canonical re-encode byte-equality gate; unknown,
out-of-set, non-canonical, or trailing bytes fail closed. Thus
`exec_manifest_sha256` anchors a single canonical payload.

Consistency ownership chain (no single stage over-claims): A1 verifies ONLY the
record's INTERNAL uniqueness/structure (it has no plan or manifest input). B2-C's
emitter generates the `ahfl.wasm-exec-manifest.v1` section from the verified
canonical workflow plan. A2 verifies ONLY the manifest's OWN canonical invariants
(`schedule_pos` dense, `workflow_node_id` unique within the manifest,
`cap_call_count` in {0,1}, `entry.kind == Workflow`, `entry.id` a non-invalid
`CoreWorkflowId`) plus manifest capability-identity consistency against the schema
capability table and the Wasm import ordinals, and BINDS the typed manifest into
the module authority; A2 holds no verified plan and no second entry/node mapping,
so it does NOT validate the manifest against a plan and takes NO expected-entry
parameter (it only checks `entry.kind == Workflow` and a non-invalid
`CoreWorkflowId`, and binds that typed entry into the authority). B2-D — only after
record authentication + artifact-digest match — compares coordinate-by-coordinate
that `record.entry == module_context.entry == host-selected entry` and the
manifest-provable identity components (entry / node / schedule_pos / call-site /
invocation_ordinal / capability / source_symbol) of each record ledger coordinate;
`arg_hash` is instead recomputed in the import callback from the module-produced
Param frame and compared to the ledger, and `result_slot` / source-state / payload
go through their own later gates (the manifest carries NO `arg_hash`, `result_slot`,
or memo payload).

Canonical section placement (capability-workflow artifact): the custom section
NAMED `ahfl.wasm-exec-manifest.v1` (whose raw payload begins with the magic
`AHFLXM`) appears EXACTLY ONCE, IMMEDIATELY BEFORE the custom section NAMED
`ahfl.wire-schema.v1` (whose raw payload begins with `AHFLWS`), which remains the
module's FINAL section at EOF (the E4-B1 landed invariant is preserved). The
runtime module-context frames the module and admits both sections in this fixed
order; a missing, duplicated, misordered, name-framing-malformed, or
payload-magic-wrong manifest/schema section fails closed. No other custom section
may follow `ahfl.wire-schema.v1`.

Manifest determinism (why Approach A is total, not a menu): the schedule is a
deterministic Kahn order over node ids, and each node's capability call sites are
statically enumerated in plan order (cardinality 0 or 1 today), so the
`(workflow_node_id, invocation_ordinal)` call-site sequence is a pure function of
the plan and is identical across the suspend run and the resume run. Approach B (a
new resume export/opcode/host symbol) is a rejected alternative recorded for
history, never an implementation-time fork.

Resume state machine (fresh instance, from `schedule_pos` 0):
- the host keeps a deterministic manifest call-site CURSOR starting at 0. On each
  `ahfl_cap` import callback (which precedes any completion-event write), the
  cursor's next entry is the pre-import LOCATOR
  `(workflow_node_id, invocation_ordinal, capability, source_symbol)`;
- the FINAL identity gate is the full coordinate `(workflow_node_id +
  capability + source_symbol + invocation_ordinal + arg_hash)`, where `arg_hash`
  is recomputed from the MODULE-produced Param frame (the module owns the
  replayed argument frame; the host borrows it read-only during the call);
- for a `(node, ordinal)` in that node's memo the host returns the recorded
  result slot bytes — NEVER a live call; for the suspended node's pending ordinal
  it returns the injected result (validated against the capability Result root),
  appends the new memo entry atomically, then transfers frame ownership; beyond
  the frontier it performs the genuine live call;
- an unknown/out-of-order node, a call at a node whose manifest cardinality is 0,
  a duplicate `(node, ordinal)`, a missing expected below-frontier `(node,
  ordinal)` by run end, or an extra call beyond cardinality each fail closed,
  never live-fallback.

Node-event buffer — the module's own observable completion evidence (not
counters, not host inference). It is a fixed region in linear memory and needs
NO new export and NO new global of its own (it does not itself shift export or
global indices). Independently, the capability-workflow baseline DOES add one new
PRIVATE global — a `pending_latched` flag (see the write-safety and re-entry
contract above) — emitted ONLY on the capability-workflow lane; the
no-capability E1/E3 global sections and bytes are unchanged (a function import
never shifts global indices, and this global is emitted only when the module has
imports). Layout for the
capability-workflow artifact: a fixed HEADER at `event_log_base = 1024` holds
`event_count` as a u32 little-endian at offset [0..3] of the region (so the host
reads the valid record count DIRECTLY at byte 1024, with no inference); the
header's `pad[4..7]` MUST be 0; records start at `records_base = event_log_base +
8` (a u32 count + 4 reserved-zero pad bytes, 8-byte aligned); `event_bytes =
checked(8 + node_count * 40)`; `heap_base = align_up(event_log_base + event_bytes,
8)`, with `heap_base <= linear_memory_bytes` verified in preflight so the header +
records + heap never overlap. Before reading any record the host fails closed
unless `event_count <= node_count` AND the header `pad[4..7] == 0` AND the checked
bound `records_base + event_count * 40 <= heap_base (<= linear_memory_bytes)`
holds — a corrupted count can never steer the host outside the event region. Each
node completion that returns `AHFL_CAP_OK` writes ONE fixed 40-byte little-endian
TAGGED record at `records_base + event_count * 40`, THEN stores the incremented
`event_count` in the header (bumped only AFTER the full 40-byte body store, so the
host never reads a partial record). A `PENDING`, `ERROR`, or unknown status writes
NO record and does NOT increment `event_count` (only successful completions are
evidence). The
region is STATICALLY sized to exactly `node_count` records (one completion per
scheduled node), so the normal path cannot structurally overflow; a defensive
guard against a would-be write past `node_count` fails `run2` closed as
`AHFL_CAP_ERROR` (no fabricated Wasm status, no out-of-bounds trap). The record is
a tagged union: tag 0 = identity-completed, tag 1 = capability-completed. Exact
little-endian offsets (record_size = 40): `tag` u8 [0], pad [1..3], `workflow_node_id`
u32 [4..7], `schedule_pos` u32 [8..11], `capability` u32 [12..15], `source_symbol`
u64 [16..23], `invocation_ordinal` u64 [24..31], `status` u32 [32..35], reserved
[36..39]. `pad[1..3]` MUST be 0 in every record and the `reserved[36..39]` bytes
MUST be 0 in every record (one logical event has exactly one byte representation);
an unknown `tag`, a nonzero `pad`, or a nonzero `reserved` is fail-closed on the
host read. For tag 0 (identity) the `capability` / `source_symbol` /
`invocation_ordinal` bytes are ALL zero and `status` = `AHFL_CAP_OK` (an identity
node never forges capability identity); the host ignores those fields for tag 0.
The reserved [36..39] bytes stay 0: the module cannot honestly attest whether a
call was memo-replayed, injected, or live, because its `ahfl_cap` ABI receives
only `(status, ptr, len)`. `source_state` (memo / injected / live) is recorded by
the PRODUCTION HOST at each import callback and, at the run2 boundary, strictly
JOINED with the module event log by manifest coordinate into an HMAC-authenticated
host event envelope; NO-REINVOKE is proven by that authenticated envelope plus the
ledger/manifest gate, not by the module event alone. The header `event_count`
resets to 0 at every fresh instance entry.

## 5. Resume transaction and ownership gate

No resume export is designed in B0. A later B2 may propose one only after the
schema and state-store foundations are implemented and verified.

The required host transaction for that later slice follows the SINGLE admission
order defined in §5.1 (framing -> record HMAC/canonical -> artifact digests ->
coordinates -> staged exact-slot/typed payload [entry frame opaque] -> transition
eligibility -> TOTAL result-size preflight -> effect-free replay of below-frontier
imports to the frontier -> then CONDITIONAL on the frontier state: an Injected
frontier is a ReturnMemo of its already-committed injected memo (NO CAS), while a
Suspended frontier emits a PublishInjected command whose adapter CAS + explicit
success ACK precede the pending ownership transfer (a Suspended record with no
supplied injected frame stops at NeedsInjectedResult, before the preflight) ->
then ReadyForLive command(s) for imports strictly after the frontier; the
effect-free replay is BEFORE any CAS so a trap during replay never leaves a
mutation, and every CAS/mutation/live effect is AFTER the preflight); the ordered
steps below are its resume-side view:

1. minimal-frame the control record, verify its HMAC authenticity, then compare
   the module + schema (+ exec_manifest) digests — a mismatch fails even when
   numeric ids coincide;
2. parse + validate the full-workflow ledger and every per-node memo coordinate,
   and cross-check it against the module + AHFLXM manifest with the A2-BASELINE
   coordinate gate (the module cannot witness a record richer than its manifest, so
   fail closed on anything it cannot produce — this is a baseline gate, NOT an
   A1-grammar relaxation): record prefix node_count <= module node_count; an
   identity node has empty memo + no pending; every NON-frontier capability node has
   EXACTLY one memo at ordinal 0; a Suspended frontier has empty memo + exactly one
   pending at ordinal 0; an Injected frontier has exactly one memo at ordinal 0 + no
   pending; any capability node with memo_count > 1 or any memo/pending ordinal != 0
   fails closed (host code `resume.coordinate.mismatch`; an internal typed reason may
   distinguish it). A repeated result_slot across DIFFERENT nodes/occurrences stays
   legal — each node keeps its own ordinal-0 memo counted per occurrence;
3. retrieve the ledger-referenced slots from protected storage: the single
   `entry_input_slot` (the workflow ENTRY frame) plus every memo result slot.
   Approach A persists NO PER-NODE input slot — downstream node inputs are
   reconstructed by the fresh replay from the entry frame + validated memo results
   + pure runners; only the workflow entry frame slot is restored, never recomputed;
4. admit the `entry_input_slot` as OPAQUE bytes (slot-artifact authenticity +
   length + cap only — it carries no transported schema — replayed VERBATIM, never
   decoded); Verified-decode each store-loaded memo RESULT against its coordinate's
   Verified Result binding for TYPE authority. A committed memo (and an Injected
   record's committed injected memo) is replayed as its EXACT authenticated bytes
   and reserved by ACTUAL length — it is NOT re-canonicalized (that would change
   B2-C's opaque output spelling). A Suspended record's externally-supplied
   injected frame, though a NEW input, is Verified-decoded against the FRONTIER
   Result binding but published/replayed as its EXACT bytes and reserved by ACTUAL
   length, NOT via the canonical bound. Only an UNKNOWN FUTURE LIVE result relies on
   the D1a-4 canonical upper bound: after the live call returns it MUST be
   Verified-decoded against that call-site's Result binding, canonically
   re-encoded, and its canonical byte length checked <= that call-site's
   already-reserved bound BEFORE the adapter allocs / transfers / persists it (a
   schema-invalid decode or a length exceeding the reserved bound fails closed with
   no transfer/persist). A module-produced Param frame is instead
   Verified-Param-decoded + arg_hash-recomputed inside its import callback (§4.4);
5. validate transition ELIGIBILITY only (Suspended re-offers pending; Injected has
   the committed injected ordinal as last memo; no live mutation yet);
6. run the TOTAL result-size preflight (§5.2) BEFORE any commit, ownership
   transfer, or live call, reserving one fresh instance's actual bump allocations
   exactly once: checked_total = the event layout's heap_base (which already covers
   the [0,1024) baseline, the event header + all node-record slots, and 8-byte
   align) + the entry_input_slot's ACTUAL bytes + every memo occurrence's ACTUAL
   slot length (with multiplicity) + (Suspended only) the Verified-decoded injected
   frame's ACTUAL bytes + the future UNKNOWN live Result bounds for call sites
   STRICTLY AFTER the frontier; require checked_total <= the linear-memory capacity.
   Identity nodes add zero; the frontier is counted once (Suspended actual injected,
   or Injected committed memo), never both actual and bound; there is NO per-alloc
   framing/alignment overhead (the cap-private checked bump advances heap_next by
   exactly len). If it does not fit, fail closed with no effect;
7. run the fresh instance from schedule_pos 0. COMMON to EVERY import
   (below-frontier, frontier, after-frontier): verify the observed call-site +
   cursor (the import maps to the current expected coordinate) and the current
   node-event prefix (decode + manifest join, event_count == the CURRENT expected
   call-site's schedule_pos); Verified-Param-decode the module frame via
   `param_binding()`; form the EXACT arity-1 `std::vector<Value>` (A2 mints exactly
   one Param; `hash_values` mixes the vector length first, so the arity MUST be 1);
   COMPUTE arg_hash via `evaluator::hash_values`; and match capability +
   source_symbol + invocation_ordinal against the A2 manifest call-site. Then, by
   coordinate:
   - a below-frontier capability import, AND an Injected record's frontier import:
     ReturnMemo — additionally compare the COMPUTED arg_hash against that memo
     entry's arg_hash (ledger equality); return the OLD committed memo-result frame
     as EXACT authenticated bytes (independent already-committed authority,
     transferred per import), invoking no live capability and performing no CAS (the
     Injected frontier is ReturnMemo of its already-committed injected memo — NO
     second CAS);
   - a Suspended record's frontier import: compare the COMPUTED arg_hash against the
     PENDING entry's arg_hash (ledger equality); only if the event prefix satisfies
     event_count == frontier.schedule_pos, produce a PublishInjected command; the
     D2a adapter CAS-appends the NEW pending memo entry atomically and, ONLY after an
     explicit CAS-success acknowledgement, transfers the NEW pending-result frame's
     ownership to the instance (only this new frame is append-gated); a CAS failure
     transfers nothing and leaves the generation Available / record unconsumed;
   - imports STRICTLY AFTER the frontier: ReadyForLive — the record holds NO expected
     arg_hash for these coordinates, so there is NO record-hash equality. The
     computed 64-bit arg_hash is NOT the D2b IdempotencyToken (D0 locks that token as
     SHA-256 of the canonical typed Param bytes, NOT arg_hash); it is retained only
     so that IF this live call later forms a NEW pending/memo ledger coordinate, that
     new entry's arg_hash field can be written/checked from it. ReadyForLive is
     ONLY the typed "adapter MAY initiate a live call" command; it does NOT imply the
     next step is terminal OK. The real live call returns OK / PENDING / ERROR: OK ->
     the Result is Verified-decoded + canonically re-encoded + its canonical length
     checked <= the reserved bound before alloc/transfer/persist, then execution
     continues; PENDING -> no Result frame is fabricated, a follow-on typed
     suspend/persist decision forms a new pending coordinate carrying this step's
     computed arg_hash (its durable publication/authority is D2b); ERROR / unknown /
     trap -> fail closed, no transfer/persist/live-success claim. This OK/PENDING/
     ERROR response API is a D1b/D2b FOLLOW-ON, not defined complete in this slice —
     the contract does NOT narrate straight from ReadyForLive to terminal OK.
   Only when `run2` finally returns OK is the FULL node-event prefix verified (all
   node_count events present, dense, joined). Any trap / extra / out-of-order
   import-or-event / gate mismatch prevents the FAILING step from emitting any
   additional frame / publish / live command and fails the controller closed; it
   does NOT erase an earlier acknowledged PublishInjected command or an earlier
   ReadyForLive / live effect — the newest authenticated generation remains
   Available / unconsumed. D1b DECIDES these steps (the typed
   commands + the terminal verdict); VM instantiation, Param-frame production, CAS
   execution, and the live call are D2a/D2b adapter actions — D1b never executes
   them.

Failure handling is bounded by WHEN it occurs, not a blanket "no live capability":
a gate that fails BEFORE the current step emits any command produces no frame
transfer / CAS / live command for that step; a failure BEFORE the first
PublishInjected or ReadyForLive leaves the current authenticated generation
Available with no new CAS or live effect; a failure AFTER a PublishInjected CAS has
succeeded never rolls back or masquerades as the old generation — the newest
authenticated generation stays Available/unconsumed; and a failure AFTER one or
more live calls have already happened only fails closed (no fabricated result
transfer/persist, no success claim) and CANNOT claim "no live capability was
invoked" — durable-effect recovery / exactly-once for those live calls still
depends on D2b. §5.3
defines the six frame lifetimes; §5.1 defines crash atomicity and §5.2 the
resource contract. B0 does not pre-approve an ownership shortcut.

### 5.1 Transaction, concurrency, and recovery (FUTURE production gate)

> Status (layered): **B2-0 FOUNDATION DESIGN LOCKED; the integrity-only LOCAL
> tier has LANDED, the KMS/cross-host/rollback/confidential tier is NOT
> implemented.** LANDED: the B0 artifact codecs (`d355171b`; distinct-magic
> integrity-only slot `AHFLPS` / commit-manifest `AHFLCM` Available+Consumed /
> pointer `AHFLGP`, each `body || auth_header || HMAC` tag), the B1a
> `IntegrityPayloadStore` (`72a062e0`; Linux-only immutable generation-dir
> publish + atomic pointer swap, `flock(LOCK_EX)` writer, lock-free single-pointer
> reader, `expected_current_generation` CAS, fsync-ordered `renameat2`, EXT-family
> /XFS/Btrfs allowlist), and B1b cross-process crash evidence (`c710a997`; SIGKILL
> then fresh-process reopen proving old-or-new atomic visibility + rebuild-not-
> adopt + single-winner CAS). Honesty limits carried by that landed tier: SIGKILL
> proves process-death visibility ONLY, NOT power-loss durability (the page cache
> survives SIGKILL); the caller-supplied root prefix / parent path is TRUSTED
> input resolved by the normal kernel path resolver — only the final root (opened
> `O_NOFOLLOW`, then pinned as a root fd) and the `openat`/`O_NOFOLLOW` traversal
> BELOW that pinned fd are hardened, and parent-of-root is NOT protected; a
> pre-existing root directory is required, so a missing / non-openable root
> currently maps to `UnsupportedFilesystem` (not a distinct NotFound) — an
> observable behavior of this landed header-public internal API; the nlink==0
> read race and an `st_dev` submount under the root are code-path-review-only, not
> test-covered. NOT implemented: any KMS, key authority, at-rest confidential
> store, real rollback protection, or production caller; `atomic_file` remains
> rename-only (no fsync/flock/CAS — the store carries its OWN flock/fsync/
> renameat2, `atomic_file` is untouched). Everything KMS/cross-host/confidential
> below is the target production contract, not an existing capability.

Admission order (replaces any "digests first" and the earlier
"invariants -> identity -> binding -> source-state -> typed payload -> atomic
append" sketch): (1) minimal canonical framing of the record; (2) authenticity
admission — verify the record HMAC tag under the host key + canonical re-encode,
yielding TRUSTED expected digests and coordinates; (3) compare `module_sha256` +
`wire_schema_sha256` + `exec_manifest_sha256` — a mismatch fails even when numeric
ids coincide; (4) full record invariants + record/module/manifest coordinate
cross-check INCLUDING the A2-baseline coordinate gate (record prefix node_count <=
module node_count; identity node empty memo + no pending; every non-frontier
capability node exactly one memo ordinal 0; Suspended frontier empty memo + exactly
one pending ordinal 0; Injected frontier exactly one memo ordinal 0 + no pending;
any capability memo_count > 1 or memo/pending ordinal != 0 fails closed as
`resume.coordinate.mismatch` — a baseline gate on what the current cap_call_count in
{0,1} / invocation_ordinal()==0 module can witness, NOT an A1-grammar relaxation; a
repeated result_slot across different nodes/occurrences stays legal); (5) staged
exact-slot admission + typed payload OWNER SPLIT: the
ResumeSnapshot phase 2 / payload store authenticates and admits ONLY the exact
distinct STORED slot bytes (`{entry_input_slot}` U memo result slots) on the pinned
generation snapshot — it does NO schema decode; the `entry_input_slot` is OPAQUE
(slot auth/length/cap only, replayed verbatim, never decoded). THEN the D1b
controller Verified-decodes each STORED memo occurrence against that coordinate's
Verified Result binding (a repeated result_slot reuses the same authenticated
bytes, but every referencing coordinate's binding/type gate must still hold) — the
Verified-binding dependency lives in the controller, never in the payload store.
The Suspended record's EXTERNAL
injected frame is NOT part of the staged slot set: D1b PREPARE Verified-decodes it
against the FRONTIER Result binding only when it has been supplied (else the plan
stops at NeedsInjectedResult). A module-produced Param is Verified-Param-decoded +
arg_hash-recomputed in its IMPORT CALLBACK (arity-1 vector). A future LIVE Result
does not exist at admission: it is Verified-decoded + canonically re-encoded only
AFTER the live call returns, its canonical length checked <= its reserved bound
before the adapter allocs/transfers/persists it; (6) transition ELIGIBILITY
validation only (no mutation
yet); (7) the TOTAL result-size preflight (§5.2); (8) effect-free replay of the
below-frontier imports from schedule_pos 0 to the frontier, returning
each OLD committed memo-result frame with no live call; (9) CONDITIONAL on the
frontier state: an Injected frontier is a ReturnMemo of its already-committed
injected memo (NO CAS); at a Suspended frontier D1b produces a PublishInjected
command and the adapter CAS-publishes the new pending memo entry and, only after an
explicit CAS-success ACK, transfers the pending-result
frame ownership (a Suspended record with no supplied injected frame stops at
NeedsInjectedResult, before the preflight); (10) ReadyForLive command(s) for
imports strictly after the frontier (the live OK/PENDING/ERROR response API is a
D1b/D2b follow-on, not a straight line to terminal OK). The effect-free replay (8)
is
BEFORE the CAS (9) so a trap during replay never leaves a mutation, and every CAS,
mutation, Injected publish, or live effect happens strictly AFTER the preflight
(7). A failure fails the controller / loader CLOSED and leaves the newest
authenticated generation Available / unconsumed (an already-acknowledged CAS or an
already-emitted live effect is NOT rolled back) — it does
NOT inherit the native recovery "load error -> fresh run" downgrade.

Staged admission (B2-D1a, LANDED `9b8053cc`; the minimal store change that makes
the order above
implementable). The landed `IntegrityPayloadStore::load()` performs pointer +
record + manifest + slot admission ATOMICALLY and returns the fully decoded record
AND all authenticated slot payloads together, so the ruled order (record HMAC ->
digests -> coordinates -> slot set/payload) cannot be expressed on it. D1a-1 added a
store-owned STAGED admission via an additive `open_snapshot` returning a move-only
one-shot `ResumeSnapshot` (its `admit_slots(...)` consumes the pin so a second call
fails closed), WITHOUT changing `load()` (its API and observable semantics are
preserved; existing callers keep it):

- phase 1 opens an opaque move-only RAII `ResumeSnapshot` handle that PINS the
  verified immutable-generation directory fd AND HOLDS the NON-secret authenticated
  pointer / manifest / record metadata (never any key bytes; the single
  authenticated pointer read is the linearization point). Phase 1 auth/canonical-
  checks the pointer and the manifest THEMSELVES, and authenticates + cross-checks
  the A1 record (record len / digest / namespace / generation against the
  manifest); it does NOT admit the expected slot set or any slot payload. Phase 2
  never reopens the generation by
  name (no replacement window).
- the handle NEVER stores key bytes. Phase 1 borrows the key; phase 2 is a store
  API that RE-BORROWS `(expected_key_id, key)`, cross-checks it against the
  snapshot's NON-secret `key_id` / `generation` authority, and only then admits the
  exact expected DISTINCT slot set (`{entry_input_slot}` U memo result slots) and
  their payloads on that SAME pinned snapshot. The immutable generation-dir + no-GC
  give no TOCTOU between the two phases.
- the B1 expected DISTINCT slot set becomes `{entry_input_slot}` U memo result
  slots (the `entry_input_slot` must not collide with any memo `result_slot`), and
  the manifest-cap upper bound is raised by a CHECKED +1 (overflow fails closed).
  The A1 `entry_input_slot` record field and this B1 set / staged-admission change
  LANDED in ONE code commit (`9b8053cc`) under ONE verification gate, so a record
  can never reference a slot the store rejects.

Error SSOT and priority (all codes range-less, null/no-echo; a host admission /
controller / gate failure NEVER masquerades as a Wasm status — it terminates host
execution and leaves the record unconsumed). The 16 `PayloadStoreError` variants
map to host codes as: `NotFound` -> `resume.store.not_found`; `Consumed` ->
`resume.store.consumed`; `IntegrityFailed` -> `resume.auth.integrity_failed`
(record integrity is phase 1, slot integrity is phase 2 — not a single "top of
admission" step); `KeyIdMismatch` -> `resume.auth.key_id_mismatch`;
`GenerationMismatch` -> `resume.store.generation_mismatch` (it also covers CAS /
manifest state, not pure auth); `StateMismatch` -> `resume.store.state_mismatch`;
`SizeCapExceeded` -> `resume.store.size_cap_exceeded`; `Truncated` / `TrailingBytes`
/ `Malformed` -> `resume.store.artifact_malformed` (a structural failure may come
from the pointer / manifest / slot / record, never labelled record-only);
`SlotSetMismatch` -> `resume.slots.set_mismatch`; `CrossCheckpointRejected` ->
`resume.store.cross_checkpoint_rejected`; `WriteFailed` / `CommitInterrupted` ->
`resume.store.commit_failed`; `UnsupportedPlatform` ->
`resume.store.unsupported_platform`; `UnsupportedFilesystem` ->
`resume.store.unsupported_filesystem`. New host controller / gate codes (not
`PayloadStoreError`): `resume.digest.module_mismatch` /
`resume.digest.wire_schema_mismatch` / `resume.digest.exec_manifest_mismatch` (fail
even when numeric ids coincide); `resume.coordinate.mismatch` (record/module/
manifest coordinate join); `resume.event.malformed` (node-event framing / count /
pad / record self-invariant); `resume.payload.schema_invalid` (Verified Result /
Param decode/validate); `resume.transition.invalid` (ineligible state transition);
`resume.preflight.unbounded` (legal schema, no finite canonical upper bound);
`resume.preflight.resource_exhausted` (checked add/mul/align/`size_t` overflow OR
total worst-case reservation exceeds capacity — a DISTINCT host code, never the
compile-time `wasm.RESOURCE_EXHAUSTED`); `resume.module.error` (a `run2` `ERROR`,
including the module's fail-closed normalization of an unknown / import `ERROR`);
and `resume.module.trap` (a module trap). In-module statuses stay EXACTLY
`{AHFL_CAP_OK, AHFL_CAP_ERROR, AHFL_CAP_PENDING}`; only a genuine import callback
returns one, and the host never invents a status or maps a trap to success.

Commit is a cross-resource transaction. Single-host mutual exclusion uses a
kernel-released ADVISORY lock (auto-released on process death; the landed B1a
LOCAL backend uses `flock(LOCK_EX)`). Cross-host generation
authority is a durable KMS: `RESERVE(checkpoint) -> {generation N+1, lease}`
(durable, queryable status in {reserved, finalized, aborted}), `CLAIM(checkpoint)`
returns the current reservation + lease to a single fenced winner so a crashed
reserver's work is recoverable, and `FINALIZE`/`ABORT` require the lease. Each
transaction artifact type (control record, COMMIT manifest, slot, generation
pointer) is HMAC-bound with a distinct domain separator + `key_id` + `generation`
so no cross-type or cross-generation substitution is possible — for the control
record that domain separator is its intrinsic leading `magic || format_version`
authenticated as the prefix's first bytes (§4.2), not a prepended string literal,
and the landed `commit_manifest` (`AHFLCM`) and slot (`AHFLPS`) / generation
pointer (`AHFLGP`) artifacts (B2-B B0 codecs `d355171b`) each carry their OWN
distinct magic. (The compiler
`exec_manifest` is a DIFFERENT artifact — a build-time custom section with no
runtime HMAC, anchored only by `exec_manifest_sha256` in the control record §4.4;
it is never the transaction `commit_manifest`.) The commit sequence is: write each
payload slot to a txn-scoped path and fsync; write the authenticated control
record + a staged `commit_manifest` and fsync; advance the generation via
`RESERVE -> fsync staged -> local pointer rename -> parent-dir fsync -> FINALIZE`.

Recovery truth table (restart reads KMS status + local pointer + staged
artifacts): RESERVED N+1 with an incomplete stage => ABORT, stay at N; RESERVED
N+1 with a fully-fsynced + HMAC-valid record + commit_manifest + all slots =>
FINALIZE forward (idempotent) since the lease authorizes it; FINALIZED N+1 with
pointer N => reapply the pointer ONLY after re-verifying that generation's record +
commit_manifest + all slots are complete and auth-valid, else repair-or-halt
(never fabricate a
pointer from finalized status alone); a missing/corrupt slot while still RESERVED
and the pointer not yet reader-visible => ABORT to N; a missing/corrupt slot when
the pointer is already N+1 or KMS is finalized => repair-or-halt, NEVER a silent
rollback to N; a local pointer N+1 with NO KMS reservation => reject it (floor is
the KMS-finalized generation). Without KMS (`guarantees` bit0 = 0) only the local
advisory lock + a local monotonic pointer exist => accidental-corruption
detection ONLY, explicitly NOT rollback protection. The `resume_state` interplay:
a Suspended record restart re-offers the pending for injection (idempotent,
nothing appended yet); an Injected record restart still replays from
`schedule_pos` 0, returns the already-committed injected memo entry at the
frontier (no re-inject, no live call), and forwards; on workflow completion the
record is marked committed-consumed / tombstoned in a new generation and is never
retained indefinitely. The Consumed tombstone is a SEMANTIC state here: its
authenticated `commit_manifest` byte grammar (the `AHFLCM` Available + Consumed
variants) has landed in the B2-B B0 codecs (`d355171b`); this foundation locks
that a completed generation carries an authenticated Consumed marker, and the
landed integrity-only LOCAL tier persists it, while the KMS-fenced cross-host
lifecycle above remains the future production contract.

### 5.2 Resource contract (FUTURE production gate)

> Status: **B2-0 FOUNDATION DESIGN LOCKED; the per-Verified-Result canonical size
> bound LANDED (B2-D1a `5fbd1a9b`); the host-side TOTAL preflight is NOT
> implemented.** The shared `make_alloc_body` / legacy lane is still an unchecked
> bump (`heap_next += len`); the capability-workflow lane already has B2-C's
> cap-private CHECKED alloc (`4224a52f`: returns the reserved null pointer `0`
> without advancing `heap_next` on insufficient capacity). Memory is a fixed single
> page and there is no `memory.grow`. What HAS landed is the runtime-owned
> per-Verified-Result max-canonical-JSON-size bound authority
> (`core_wire_canonical_size`, a conservative never-underestimating upper bound,
> `max_canonical_json_size(binding) -> std::expected<std::uint64_t,
> MaxCanonicalSizeError>`, values `MaxCanonicalSizeError::Unbounded` /
> `MaxCanonicalSizeError::SizeOverflow`, emits no `resume.*` diagnostic string).
> What is NOT implemented is the host-side TOTAL reservation (the fresh instance's
> actual bump allocations counted exactly once: event-layout heap_base +
> entry_input_slot actual bytes + every memo occurrence at actual length WITH
> multiplicity + the Suspended injected frame's actual bytes + the future live
> Result bounds for call sites strictly after the frontier; no separate
> allocator-framing term — the checked bump advances heap_next by exactly len), the
> one-page
> capacity verdict, the `size_t`/align/u32-host casts, and the mapping of
> `MaxCanonicalSizeError` to the host `resume.preflight.*` catalogue — all future
> D1b.

Under the fixed single-page / no-`memory.grow` contract, a checked PREFLIGHT runs
before any commit, ownership transfer, or live call — every CAS/mutation/live
effect is strictly after it. The owning implementation (B2-D1a, LANDED
`5fbd1a9b`) is a
runtime-owned checked max-canonical-JSON-size analysis over the Verified Result
binding, returning a conservative upper bound for a finite schema, and each size
is a checked arithmetic step; it returns a bare `MaxCanonicalSizeError` and emits
no `resume.*` diagnostic string. The TOTAL reservation and the fixed-single-page
verdict that consume this bound are future D1b. A String is bounded by its
`length_bounds` upper (as
UTF-8 bytes + worst-case JSON escaping); a Sequence/Set by `checked(capacity ×
element bound) + array framing`; a Map by `checked(capacity × checked(key bound +
value bound + per-entry framing)) + object framing` — a Map key String must itself
have an upper bound, and any key or value with no bound makes the Map unbounded;
scalars by their canonical spelling width; a record by the checked sum of its
fields, all with the canonical serializer's worst-case escaping, number spelling,
and framing (quotes/commas/colons/brackets/field names), never underestimating. The
per-Verified-Result analysis returns `MaxCanonicalSizeError::Unbounded` when the
schema has NO finite
canonical upper bound — a String with no upper `length_bounds`, a Sequence/Set/Map
with no `capacity`, a Map key/value with no bound, an unbounded-preserved
Decimal/Duration spelling, or a productive recursive cycle in the node graph (a
zero-capacity-cut cycle is not productive); it returns
`MaxCanonicalSizeError::SizeOverflow` on a checked u64 add/mul overflow of a
finite schema's bound (it makes no `size_t`/align/one-page verdict of its own).
The FUTURE D1b TOTAL controller reserves one fresh instance's actual bump
allocations exactly once: checked_total = the event layout's heap_base
(`align_up(event_log_base + checked(8 + node_count * 40), 8)`, already covering the
[0,1024) baseline + event header + all node-record slots + align) + the
entry_input_slot's ACTUAL authenticated bytes + every record memo occurrence's
ACTUAL slot length (with multiplicity — a repeated result_slot counts per
occurrence, including an Injected record's committed frontier injected memo) +
(Suspended only) the externally-supplied injected frame's ACTUAL bytes after
Verified-decode + the future UNKNOWN live Result bounds (each `result_binding()`'s
`max_canonical_json_size`) summed ONLY over call sites with schedule_pos STRICTLY
AFTER the frontier — the current frontier is never counted both actual and bound,
identity nodes add zero, and there is NO allocator-framing term (the cap-private
checked bump advances heap_next by exactly len). Pass 1 visits ALL future-live
bindings and records `seen_unbounded` and `seen_size_overflow` (a per-binding
`MaxCanonicalSizeError::SizeOverflow` from D1a-4 is RECORDED, NOT early-returned —
early-returning on the first SizeOverflow would mask a later binding's Unbounded).
If ANY binding is Unbounded, D1b returns `resume.preflight.unbounded` (before any
total arithmetic). Only when NO binding is Unbounded does pass 2 run: any recorded
`seen_size_overflow`, OR an event-layout checked add/mul/align overflow, OR a
checked total add overflow, OR a `size_t`/u32-host cast failure, OR checked_total
exceeding the fixed single-page capacity, all map to
`resume.preflight.resource_exhausted`. An early arithmetic overflow never masks a
later Unbounded. These two are
the ONLY host preflight errors and are a DISTINCT D1b runtime catalogue — they are
never the
compile-time `wasm.RESOURCE_EXHAUSTED` / `wasm.BINARY_OVERFLOW`, which stay
compiler-owned. `memory.grow` is a separate future gate, not chosen here. No new
Wasm status code is invented: the capability-workflow-private checked bump keeps the
existing `alloc` signature `(i32) -> i32` and returns the reserved null pointer `0`
on insufficient capacity; the host maps that (and its own D1b preflight/unbounded
detection) to the STABLE orchestrator diagnostics above before any live effect. The
only in-module statuses remain `AHFL_CAP_OK` / `AHFL_CAP_ERROR` / `AHFL_CAP_PENDING`;
a defensive resource guard inside `run2` fails closed as `AHFL_CAP_ERROR`, never a
fabricated status. (A real in-module resource status would be a separate ABI gate;
not chosen.)

### 5.3 Frame lifetimes and fan-out ownership (FUTURE production gate)

> Status: **B2-0 FOUNDATION DESIGN LOCKED; NOT implemented.**

Six distinct frame lifetimes, each with allocate / borrow / transfer / last-use /
failure-cleanup:
- **L0 entry-input transfer frame** — the authenticated `entry_input_slot` bytes,
  OPAQUE / verbatim / reserved by ACTUAL length, never schema-decoded; the D2a
  adapter allocs + writes it into fresh-instance memory and transfers ownership to
  the module at the `run2` invocation; a pre-transfer failure does NOT hand the
  frame to the module and leaves the record unconsumed.
- **L1 host-private decoded Value** — a TEMPORARY Value produced per owner gate,
  never in module memory: a stored memo / injected / live Result is Verified-decoded
  for schema validation (a future live Result is additionally canonicalized +
  bound-checked), and a module Param frame is Verified-decoded in the import
  callback to feed `hash_values`; each is dropped at the end of its gate and is
  never placed in module memory or echoed.
- **L2 module-owned import Param view** — the Param the host reads in an import
  callback is a VIEW/frame pointing at ALREADY-EXISTING module-owned storage
  (sourced from L0 entry / an L3 memo return / an L4 injected transfer / a
  future-live OK Result), routed to the import by identity/fan-out; this step adds
  NO new bump allocation. The host BORROWS it read-only to decode the Param +
  recompute `arg_hash`; ownership stays with the module (its lifetime); the record
  persists NO args slot; failure before the import returns leaves the record
  unconsumed.
- **L3 memo-result return frame** — an OLD committed memo result the host writes
  into fresh-instance memory and returns for a below-frontier import OR an Injected
  record's frontier import; returned as EXACT authenticated bytes; ownership
  transfers to the module ON RETURN (independent already-committed authority; NOT
  gated behind any new pending append, NO CAS); last-use = the module reads it.
- **L4 pending-result return frame** — ONLY a Suspended record's frontier NEW
  injected pending result; Verified-decoded against the Result binding, its NEW memo
  entry CAS-appended atomically FIRST, and only after an explicit CAS-success
  acknowledgement is its `(ptr,len)` ownership transferred to the module; this is
  the ONLY frame gated behind the atomic append (an Injected frontier does NOT use
  this lifetime — it is an L3 ReturnMemo).
- **L5 node-output / fan-out view** — a completed node's output routed to one or
  more downstream nodes is a VIEW/lifetime over EXISTING storage, not a new
  allocation: an identity runner reuses its input pointer, and a capability output
  reuses the already-transferred L3 / L4 / future-live Result storage; the
  scheduler / fan-out routing performs NO extra allocation or copy; each downstream
  consumer BORROWS it; failure on any consumer fails closed, record unconsumed.
Ownership model for this slice: INSTANCE-LIFETIME allocation, no reclaim. The ONLY
host bump allocations are L0 entry, each L3 memo return, an L4 Suspended injected
transfer, and a future-live OK Result transfer — exactly the terms summed in the
§5.2 checked_total; the L2 Param view and the L5 fan-out view are ALIASES/views
into that existing storage and are NOT added to checked_total. Every entry /
replayed / injected / output frame lives until the fresh instance is torn down.
Bounded-ness is proven by the checked preflight (§5.2), not by trap-on-overrun: a
finite acyclic (Kahn) schedule, each node once, so peak memory is bounded by the
committed ledger + one linear pass; if it does not fit, resume fails closed before
any effect. A real allocator/GC with last-use reclaim is a SEPARATE future gate
(needed only for unbounded/long-lived fan-out); NOT in this contract.

## 6. Implementation split after design approval

Implementation is intentionally split before any resume ABI:

1. **B0-C1 schema model/projector/verifier**: flat `WireSchemaTable`, Core
   projection through P4-C templates, recursion/map restrictions, deterministic
   serialization, and positive/negative structural tests. No Wasm byte change.
2. **B0-C2 shared host codec**: schema-guided JSON decode plus native-Value
   validation policies; delete the shallow resume checker; migrate the response
   validator and native replay checks to the shared rules. No resume export.
3. **B1 schema transport — LANDED** (C1 payload decoder `2d25aa3b`, C2
   custom-section writer `a73a8991`, C3 runtime module inspector `b75fc8ec`):
   C2's `core_wasm_codegen` emits exactly one `ahfl.wire-schema.v1` after the Code
   section for a reachable-capability E2 artifact (E1/E3 byte-identical); C1's
   `decode_core_wire_schema_table` is the payload admission authority; C3's
   `core_wasm_schema_transport` inspector frames a transported module, cross-checks
   the `ahfl_cap` import table against the schema table, and mints a verified
   binding through the transported-table factory. This was the first reviewed Wasm
   byte change. It adds NO resume export, opcode, ABI, control record, or digest —
   those remain B2.
4. **B2 durable resume — the B2-S / B2-A-pre / B2-A / B2-B FOUNDATION slices and
   the B2-C capability-workflow emitter FOUNDATION (`4224a52f`) have LANDED; NO
   production host code is implemented (the confidential/at-rest store and the
   production host are separate future gates).** B2
   is a slice ladder, each with an honest
   FOUNDATION-vs-production closure and an explicit dependency order (no slice
   claims "non-inert" before a production host exists):
   - **B2-S security foundation — LANDED (base-support, FOUNDATION only)**: a
     raw-bytes SHA-256 + `hmac_sha256` base-support API as a dedicated security
     shared gate. S1 `b0628f25` added the raw-byte span input + typed 32-byte
     `Sha256Digest` output and preserved `sha256_hex(string_view)` byte-for-byte
     (its existing package/registry/cache/LSP/CLI/compiler callers keep their
     in-domain behavior); S2 `147da993` added the additive one-shot `hmac_sha256`
     (RFC 2104 / FIPS 198-1, block 64 / digest 32, key > 64 hash-then-pad). The
     `sha256(span)` incremental digest core and the `hmac_sha256` digest path are
     allocation-free (no whole-message concat) — `sha256_hex` still allocates its
     returned `std::string`. Both carry a fixed no-echo `std::length_error` length
     domain. Known-answer vectors: the published FIPS 180-4 vectors for SHA-256 and
     the published RFC 4231 vectors for HMAC (cases 1/2/3/4/6/7); the > block
     64/65-byte key boundary, embedded-NUL, and input-immutability vectors are
     independently fixed and OpenSSL cross-checked (not verbatim FIPS/RFC entries).
     Key-derived scratch is scrubbed by a best-effort volatile-`unsigned char` wipe
     (routed through a single `best_effort_wipe`; the state finalizes directly into
     caller-owned scratch via `finalize_into(Sha256Digest&)`) with an explicit
     non-guarantee — it cannot erase register/compiler-hidden copies and is NOT
     production key erasure, constant-time compare, AEAD, or a keyring. Sequenced
     FIRST; nothing that depends on it landed earlier. `hmac_sha256` is now
     directly consumed by the A1 record codec (§4.2); this slice itself adds NO
     keyring/KMS/AEAD/at-rest confidentiality/`PayloadStore` and still has no
     production persistence/key caller.
   - **B2-A-pre shared verified-table authority — LANDED (`compiler_ir`,
     FOUNDATION only)** (`5c9dd67c`): an opaque, copy-only `VerifiedWireSchemaTable`
     handle that PRIVATELY holds a `shared_ptr<const CoreWireSchemaTable>`; a
     binding's `Payload` shares that SAME `CoreWireSchemaTable` backing, so many
     typed Param/Result bindings mint from one authority without per-mint table
     verification/copy. `make_verified_wire_schema_table` runs the public local
     verifier ONCE per authority and mints only when the diagnostic bag is empty;
     `make_wire_binding_from_verified_table` does NO local re-verification and runs
     only the existing root-derivation SSOT. The handle exposes NO raw-table / node
     / `NodeId` accessor and has no public/default ctor (verifying-factory-only);
     the existing `VerifiedWireSchemaBinding::table()` const-ref accessor is
     preserved for codec compatibility. Its new public factories are now directly
     consumed by the A2 module-context (§4.4, `5bd812b2`); the existing
     `migrate_type_ref_to_wire_binding` / `make_wire_binding_from_transported_table`
     production paths also route through the same shared backing internally;
     their source signatures, admission/root-derivation semantics, and diagnostic
     ordering/messages are preserved. `compiler_ir` gains NO Wasm/import/manifest/
     call-sequence knowledge; there is still no production host caller. Additive
     SOURCE API; installed C++ binary compatibility is NOT
     preserved — the private
     `Payload` representation + inline `table()` interpretation changed, so every
     already-compiled C++ consumer must be cleanly rebuilt; public C ABI, persisted
     formats, CLI, and emitted Wasm bytes are unchanged.
   - **B2-A record codec + runtime module-context — LANDED (runtime, FOUNDATION
     only)** (A1 record codec `8a987ca1`; A2 module-context `5bd812b2`): the
     `ahfl.wasm-resume.v1`
     ledger codec (byte-mirror + canonical re-encode) + its authenticated digest
     FIELDS (A1 only encodes / parses / holds the three 64-hex digest fields; it
     does NOT compute or compare artifact digests — that comparison against the
     loaded module/schema is future B2-D), and the runtime
     `VerifiedCoreWasmSchemaModule` (one frame/decode/cross-check; mints Param/
     Result bindings by typed selector; holds the verified table + manifest map;
     the manifest/import/call-sequence cross-check lives ONLY here, never in
     `compiler_ir`). FOUNDATION. A1 introduces a GREENFIELD internal on-wire
     format codec (`ahfl.wasm-resume.v1`); it does not modify the existing
     `ahfl.workflow-recovery.v1|v2` JSON store and performs no persisted-format
     migration, but it is additive (it touches runtime + test CMake and adds a
     security-sensitive artifact codec — not zero-blast) with no production
     persistence caller yet. A2's module-context is the call-site authority; the
     first production caller of both A1 and A2 is future B2-D. B2 and KR6.5 stay
     false. A1 is `encode_and_authenticate` / `decode_and_authenticate` with
     two-pass admission + a single on-wire-prefix HMAC (record-internal invariants
     only; no plan input); A2 is `make_verified_core_wasm_schema_module` framing
     the exec-manifest immediately before the EOF wire-schema section + an exact
     import/schema/manifest set-equality + eager Param{0}/Result mint, exposing
     narrow strong-typed coordinates plus the three named artifact-digest getters
     (D1a-2 `9a859224`, compute/expose only, no comparison). A1 is codec-only (no
     production
     persistence caller); A2 is manifest consumer-only — it does not itself emit
     the manifest (the compiler-side B2-C emitter `4224a52f` now supplies it) and
     makes NO artifact-digest comparison (future B2-D);
     the C3 single-shot inspector's behavior is unchanged; the emitter dependency
     is test-only (production runtime unchanged). Complexity is the honest two
     linear wire-schema local verifies (C1 decode + authority admission), zero
     per-mint verify/copy. No real-Wasm durable-resume is proven (the current
     verification environment SKIPs the Wasmtime lanes); B2 and KR6.5 stay false.
   - **B2-B integrity-only local store foundation — LANDED (B0 artifact codecs
     `d355171b`; B1a `IntegrityPayloadStore` `72a062e0`; B1b cross-process crash
     evidence `c710a997`)**: a bytes-only `PayloadStore` (integrity-store)
     foundation + an integrity-only LOCAL reference backend (Linux POSIX:
     `flock(LOCK_EX)`, immutable generation-dir + atomic pointer swap, an
     implemented + unit-observed syscall/fsync order, `expected_current_generation`
     CAS; EXT-family/XFS/Btrfs allowlist, else fail-closed). B1b's cross-process
     SIGKILL/reopen proves post-process-death atomic visibility + rebuild-not-
     adopt (process-death visibility only; NOT power-loss durability). FOUNDATION
     — this backend is NON-CONFORMING to the §4.3
     protected-store contract (`guarantees` bit0 `rollback_protected` = 0 AND bit1
     `confidential_at_rest` = 0; no rollback protection, no at-rest
     confidentiality); it only exercises the foundation plumbing and must
     never be described as a conforming ProtectedPayloadStore.
   - **Confidential/KMS/rollback protected store (SEPARATE owner/shared gates;
     a PREREQUISITE of full B2-D closure, NOT out-of-Objective)**: at-rest
     confidentiality + real rollback/fencing require a real AEAD/KMS backend, key
     authority, a dependency shared gate, and a non-test production caller; not
     selected in B2-0 and not hidden inside the integrity-only B2-B foundation.
     It may land under its own owner gate, but B2-D is NOT FULLY CLOSED until it,
     the production host/fresh replay, and the digest gate all have evidence.
   - **B2-C capability-bearing workflow + manifest + module event bytes — LANDED
     (`4224a52f`)**: the workflow emitter's `allow_capability = false` refusal is
     lifted on the capability-workflow lane; a node capability `PENDING`
     propagates out of the schedule (import-time), and the compiler emits the
     `ahfl.wasm-exec-manifest.v1` (AHFLXM) exec-manifest exactly once immediately
     before the EOF `ahfl.wire-schema.v1` (AHFLWS) section + a module-written
     node-event buffer. WorkflowFunctionTable reuses the existing
     `import_count + base` index rule (cap-lane indices shift by import_count; no
     new ABI symbol/signature). This is the FIRST capability-workflow byte change;
     the pre-existing E1/E2 and no-capability E3 fixtures stay byte-frozen and a
     NEW capability-workflow baseline is added. FOUNDATION — the module event
     buffer is completion/ordering evidence only, NOT a no-reinvoke proof; the
     host-side fresh-instance replay/injection + artifact-digest gate remain B2-D,
     exact-evidence remains B2-E. Node/binary evidence is layout/structural, NOT
     Wasmtime, NOT durable-resume; the current verification environment SKIPs the
     Wasmtime lanes; B2 and KR6.5 stay false.
   - **B2-D durable-resume closure (laddered)**: full closure requires ALL of —
     **D1a-1..4 host-independent internal authorities — code/evidence LANDED as
     FOUNDATION; `IdempotencyToken` contract-only**: an `entry_input_slot` A1
     RECORD FIELD + the B1 exact-slot-set / store staged-admission change
     (`9b8053cc`; B1 has no record field); A2 typed artifact digests computed
     during A2's single framing pass (`9a859224`, computed/exposed only — NOT
     compared); a runtime-owned node-event decoder (`c59a235d`,
     framing/completion-ordering only); and a per-Verified-Result canonical
     wire-JSON size bound (`5fbd1a9b`, with the value-JSON integer
     locale-independence prerequisite `abf6bd23`). The opaque 32B
     `IdempotencyToken` remains CONTRACT-only (code owner D2b; the fixed
     preimage/authority-namespace is a D1a-locked prerequisite, NOT landed code).
     D1a-1..4 internal code/evidence are closed; this does NOT close B2-D — D1b +
     D2a + D2b + the protected store + the digest-comparison/coordinate-join gates
     remain;
     **D1b** a host-independent, DECISION-ONLY replay controller proving
     deterministic state transitions + the TOTAL result-size verdict (per §5.2:
     actual bump allocations counted once + future-live bounds strictly after the
     frontier) + the digest / coordinate / Param-schema gates + arg_hash ledger
     equality ONLY where an expected hash exists (memo/pending) + typed adapter
     commands (ReturnMemo / PublishInjected / ReadyForLive), where ReadyForLive is
     command-only and the live OK/PENDING/ERROR response API is a D1b/D2b follow-on
     (FOUNDATION — no real VM, no CAS/store mutation, no live capability, no
     durable-effect authority; it decides, it does not execute);
     **D2a** a real embeddable production VM/host adapter + >=1 non-test caller
     (Shared-Change Gate, owner-approved) — this is the production-host step;
     **D2b** a durable-effect intent/result authority with read/recover/dedup/
     result authority (a new Core-Wasm typed authority, NOT the native write-only
     void sink); and the conforming confidential/KMS/rollback protected store
     PREREQUISITE (own owner/shared gates). D1a/D1b stay FOUNDATION; full B2 /
     durable-resume is NOT claimed until D2a + D2b + the protected store all have
     evidence.
   - **B2-E exact evidence + claim gate**: the authenticated host event envelope
     joined with the module event log, wiring `runtime_node_order_observed` /
     `durable_resume_observed`; depends on B2-D's host event channel (no parallel
     after-the-fact observer). Counters/test-hooks are not evidence.
   B2 cannot start on an unchecked schema or plaintext payload shortcut, and this
   foundation design does NOT flip B2 or KR6.5 to implemented / execution-proven.

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

## 9. C2b Stage 3 as implemented: memo/pending result trust seam

Stage 3 wires the RFC 0022 durable-resume memo/pending **result** boundary through
the shared codec (§2.3/§2.4) and closes the persistence-format gaps a schema-free
`value_to_json`/`value_from_json` round trip leaves open. This section records the
landed facts so no reader re-derives them, and so the honest boundaries are explicit.

### 9.1 Scope: memo-result-only seam

The trust authority is the memo/pending **result** value; HOW it is carried depends
on the entry's persisted source (§9.2), NOT a blanket "every `CapabilityMemoEntry`
carries an exact wire + presence". A NativeOnly entry has NO `authoritative_json`
(its native `result` is authority); a LegacyV2 entry's presence is UNKNOWN; only an
ExactSidecar carries both the verbatim wire (`authoritative_json`) and a presence bit.
For Legacy/ExactSidecar the native `result` Value is a compatibility projection only.

Not in this seam (residual risks, stated honestly):

- `node_input` is PERSISTED for format completeness / potential observation, but the
  current `WorkflowRuntime` does NOT restore execution from it. On resume the
  node-input expression is RE-EVALUATED and its capability calls are replayed from the
  memo. `node_input` is informational, NOT a trust authority; assuming it drives
  resume is the residual risk (a future B2 restore-from-input change). The
  `SuspendedNodeState::node_input` and `node_input_snapshot` comments are corrected to
  say so.
- Completed-node output (`RecoveredNodeState::output`) restores via the P0-10
  direct-DOM `value_from_json` path, NOT this schema-guided codec. Known asymmetry.
- **Persistence is plaintext, NOT a protected/secret-free store (security scope).**
  This slice only ADDS append-only `result_wire_json` / `result_present` to the memo
  entries inside the existing native `WorkflowRecoveryStore` **plaintext v2 snapshot**.
  Because the sidecar records the raw wire spelling of the same sensitive result that
  the compat `result` already holds, a sensitive result is now stored TWICE in
  plaintext (compat `result` + sidecar `result_wire_json`). This is NOT secret-free /
  protected persistence per §4.3 and MUST NOT be described as a production-safe durable
  store.
- **This is NOT full B2, and B2 is NOT a single "restore-from-node-input" task.**
  Full B2 still requires, each SEPARATELY reviewed: an append-only ABI / control
  record (§4.2), a protected payload store (§4.3), atomic memo append + crash
  semantics, ownership / last-use gating (§5), and exact node-order observation /
  execution evidence — plus a future decision on whether `node_input` is ever actually
  used. This slice adds NO resume export / custom section / Wasm byte change (§8
  non-goals hold) and does NOT complete B2. See §4 / §5 / §6 for the full boundary;
  the point here is only that Stage 3 is a memo-result trust seam on a plaintext store,
  not the durable-security boundary. The G4 ingress demotion (CLI/gRPC/HTTP/shim entry
  wiring through the codec) is COMPLETE: G4a (`9821046f`), G4b (`471613af`), G4c
  (`187ca7ad`), and G4d (delete the legacy `response_schema_validator` so the codec is
  the sole schema-guided validation authority).

### 9.2 Three-state authority

`PersistedMemoResultSource` = {NativeOnly, LegacyV2, ExactSidecar}, enforced
fail-closed by `memo_result_state_well_formed` at the real save / load / consume entry
points (NOT merely a construction convenience -- the aggregate is public):

- NativeOnly: `authoritative_json` ABSENT, `result_present` SET; native `result` is
  authority. Save UPGRADES it to an ExactSidecar (writes the `value_to_json` spelling
  + presence bit).
- LegacyV2: `authoritative_json` PRESENT (the raw legacy `result` substring, captured
  by offset -- no serialize/reparse laundering), `result_present` ABSENT (UNKNOWN).
- ExactSidecar: `authoritative_json` PRESENT (verbatim `value_to_json` bytes),
  `result_present` SET; the JSON is authority, native `result` is a best-effort compat
  placeholder (P0-20, never a load-admission gate).

Why the sidecar: a schema-free round trip loses type on
Decimal/Duration/Set/Map/Option/Unit, and cannot distinguish integral Float (bare
int) from Int, nor explicit Unit from a valueless success (both spell JSON `null`).

### 9.3 Consume gate ORDER (P0-13 / P0-15 / P0-21)

Memo replay hit: coordinate gate -> identity gate -> binding lookup ->
trust-state+decode.

- P0-13: the persisted memo is READ-ONLY, never pre-cloned into the dense prefix
  (`clone_value` drops null List/Set/Struct/Map children = laundering before the
  trust gate). Each hit validates + decodes the ORIGINAL entry and appends a FRESH
  NativeOnly canonical entry carrying the resolved presence bit.
- P0-15: the pending-call identity is verified BEFORE the host result is
  read/validated/cloned and BEFORE leaving replay mode (the pending record has no
  arg_hash, so identity is the integrity gate).
- P0-21: reaching terminal state while still replaying (recorded pending never
  reached) fails closed as a replay divergence, never a silent completion.

Coordinate mismatch is a "replay diverged" fail-close, never a silent live invoke; a
cache `SchemaFailure` (a capability whose return type did not project to a schema) or
the defensive `MissingId` branch (no cache entry for the id — unreachable once the
identity gate has confirmed a resolvable decl, kept as a fail-closed guard) is a
schema-only fail-close (no payload echo). Note `MissingId` is a cache-lookup status,
not a stored entry.

### 9.4 Legacy byte-stability save gate (P0 -- provenance laundering)

The LegacyV2 save branch has no binding and MUST NOT re-canonicalize. `parse` + generic
`serialize_json` is provenance-lossy: a hostile overflowing integer parses as
`IntegerFallback`, re-serializes to exponent/float form, re-parses as `FloatSyntax`
which the legacy Float decoder would ACCEPT -- re-saving an UNCONSUMED entry flips a
reject into an accept. Rule: a LegacyV2 entry re-saves ONLY when
`serialize_json(parse(bytes)) == bytes`; any would-canonicalize spelling
(`1.0`, `1e3`, an overflowing integer, a bare `-0`) is `InvalidSnapshot` and must be
upgraded THROUGH CONSUMPTION (P0-13 dense prefix -> fresh NativeOnly -> ExactSidecar).
Honest statement: an old v2 snapshot still LOADS and CONSUMES; but a non-byte-stable,
un-binding-consumed Legacy entry cannot be schema-less re-saved -- do not assume
`save(load(old))` always rewrites.

### 9.5 Integral-Float / Unit-null compat assumptions

- `decode_json_legacy_v2` accepts `SignedInteger` -> Float ONLY under a Float binding,
  recursively (Option child etc). Sole documented relaxation vs the exact codec.
- Unit-null presence default for pre-sidecar bytes: decoded Unit -> presence=false
  (old replay presented NoneValue), any other value -> presence=true.
- Legacy negative-zero: an old v2 Float `-0.0` was ALREADY written to disk by the
  outer generic serializer as bare `-0`, so the sign was lost at that old byte layer
  BEFORE this seam ever sees it — a legacy Float widening of `-0` can only reconstruct
  `+0.0`. Separately, the bare `-0` byte is not byte-stable, so it cannot be
  schema-less re-saved (it must upgrade via consumption, which preserves whatever
  canonical value is still decodable at that point). There is NO "-0.0 on re-save"
  case: the sign is a pre-existing old-byte ambiguity, not something this stage
  loses.

### 9.6 Presence bit (P0-17 / P0-18 / P0-19) -- presence-not-value

`result_present` is `optional<bool>` (LegacyV2 UNKNOWN). Save-local normalization:
a present bare `NoneValue` (legacy valueless compat) persists as presence=false
WITHOUT mutating the caller; a presence=false entry MUST spell exactly JSON `null`
(only NoneValue/UnitValue qualify) or save fails closed. Consume: presence=false is
legal ONLY under an exact Unit root. An Option-null does NOT ride the Unit presence
seam -- under `Option<Int>` it exact-decodes to Option None then rejects (one-negative
vs the rich-matrix Option None present=true one-positive).

### 9.7 SymbolId 0 is a legal identity (durable intent included)

Identity presence is `source_capability_symbol_id.has_value()`, NEVER `!= 0`. SymbolId
0 resolves normally on resume, and the durable-write write-ahead intent gate keys off
`has_value()` -- a legal id=0 DurableWrite/FinancialWrite still fires its intent before
dispatch, with the dereferenced id (including 0) flowing into the same idempotency key.

### 9.8 Rollback risk

Reverting Stage 3 restores the pre-C2b schema-free trust path, re-opening BOTH the
historical type losses (Decimal/Duration/Set/Map/Option/Unit degradation,
integral-Float ambiguity, explicit-Unit vs valueless-success collision) AND the
false-accept surface the trust gates close (hostile null-child collections,
cross-capability binding borrow, Legacy IntegerFallback->FloatSyntax laundering). A
rollback is therefore not behavior-neutral.
