# B0-C2 Narrow Implementation Plan — rev4 (A′ + Codex Q1/Q2 rulings + must-fixes)

Status: C2a LANDED; C2b COMPLETE. C2b-1/2 codec landed
(`e97aa76d`); P0-9/P0-10/P0-11 shared-change fixes landed. C2b **Stage 3**
(durable-resume memo/pending result trust seam, §12) landed across G1
(`9d35464f`, recovery format), G2 (`58c5ac57`, legacy codec), and G3
(`0c599d94`, runtime trust paths). **G4 (CLI/gRPC/HTTP/shim entry demotion) is
COMPLETE**: G4a (`9821046f`, live seam + admission helper), G4b (`471613af`,
two-phase CLI admission), G4c (`187ca7ad`, raw pending-result subgate), and G4d
(this change: delete the legacy `response_schema_validator`, so the schema-guided
codec is the sole validation authority). B2 (the full durable-resume
ABI/control-record, protected payload store, atomic/crash, ownership/last-use,
and node-order evidence work) remains PENDING.
RFC 0026
KR6.5 E4-B0-C2. Author: Claude (sole writer). Reviewer: Codex (read-only).

rev2 A′ main architecture approved; rev3 rulings approved; rev4 closes 4
documentation blocking pins Codex found on a line-by-line read (stdlib
resolver contract, type-local gate scope, the actually-missing canonical
aggregate table, and two fail-closed policies). Authority stays: one
`CoreWireSchemaTable`; codec sees only a VERIFIED binding; TypeRef never enters
the codec.

## 0. stdlib nominal identity — MATCH the existing sole resolver (rev4 fix 1)

The single resolver `resolve_nominal_strict` (core_lower.cpp:617-655) is the
authority; C2 does NOT invent stricter rules that would diverge the type-only
prelude from full lowering. Its contract, which the migration projector inherits
verbatim (it calls `lower_value_type_into`):

- ref has an id AND a Core type carries that id → use it, and canonical must
  agree (id/canonical drift → fail-closed);
- ref has an id but NO Core type has it → canonical fallback allowed ONLY to a
  **name-only synthetic builtin base** (a `CoreTypeDecl` whose own `symbol_ref`
  has NO id — e.g. an un-inlined std Option/List/Set/Map stamped from the
  descriptor SSOT); a canonical candidate that carries a (different) id →
  fail-closed;
- ref has no id → plain canonical name-only fallback.

So the rule is "**when a ref CARRIES an id, no arbitrary / real-decl name
fallback; the ONLY name-only match permitted for an id-bearing ref is the
descriptor-backed synthetic builtin exception**", NOT "name-only never matches".
The third bullet (ref has NO id → plain canonical name-only fallback,
resolve_nominal_strict :657-663) is a SEPARATE existing compatibility branch that
C2a preserves verbatim — the id-first tightening does not touch it. A verified
frontend user nominal normally carries an id; migration may add its own gate for
malformed verified input, but full-lower byte behavior must not change. Tests
(§10): a WRONG id must never hit a real decl; an un-inlined std builtin resolves
to its exact-descriptor synthetic base.

## 1. Two review commits (Codex-mandated split)

- **C2a** — type-only prelude + AHFL migration projector + `VerifiedWireSchemaBinding`
  + projector-equivalence tests. Completely INERT (no runtime behavior change, no
  codec). core_lower refactor lives here, provably byte/behavior-identical.
- **C2b** — codec (2 policies) + runtime three-path migration (delete shallow
  checker, demote response validator, re-wire wire_capability raw-JSON path).

Rationale: never bundle a hot core_lower refactor with a runtime behavior change
in one un-splittable review unit.

## 2. Q1 = (b): shared type-table-only prelude

Factor the type-table construction OUT of `lower_ahfl_to_core` into a public
prelude shared by full lowering AND migration. No second closure resolver; the
type-table registration + P4-C template finalize keep exactly ONE implementation.

```
struct CoreTypeEnvironmentSeed {
    std::vector<CoreTypeDecl>  types;        // all struct/enum + builtins, source order
    std::vector<CoreValueType> value_types;  // arena seeded as today
    std::vector<CoreLowerDiagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept;  // fail-closed at this boundary
};
// NEW public entry. Registers every struct/enum + builtin from a verified AHFL
// Program in SOURCE order, runs field-nav fixup + P4-C member-template finalize,
// and runs a TYPE-LOCAL structural gate (see below). Lowers NO
// capability/agent/flow/workflow/body.
[[nodiscard]] CoreTypeEnvironmentSeed build_core_type_environment(const AhflIr&);
```

**rev4 fix 2 — the gate is TYPE-LOCAL, never a whole-Program BackendReady.**
It checks ONLY the invariants the type table + value-type arena need:
member-template arena well-formedness (postorder/reachable/kind), variance
parallelism, nominal identity resolution, field-nav fixup consistency, and the
Core value-type/type-table structural checks. It does NOT traverse any
capability/agent/flow/workflow body, so an unrelated handler / P6 match / loop /
adjustment can NEVER block a migration projection (that was the Q1(a) failure
mode). Concretely: reuse the existing decl/type-table structural checks
(`verify_types` / member-template + `verify_value_types` portions of
core_verify), NOT the full `verify_core_program` body walk.

- `lower_ahfl_to_core`'s refactor to consume the seed MUST preserve the existing
  diagnostic ORDERING and continue-or-stop behavior on invalid input: the seed's
  `ok()` must NOT introduce an early-out that changes the error set full lowering
  produces today. full-lower keeps emitting the same diagnostics in the same
  order; only the type-table CONSTRUCTION is shared. migration, seeing a seed
  error, yields no binding.

- `lower_ahfl_to_core` is refactored to CALL this prelude and MOVE the seed in,
  then continue with body lowering. **Behavior + bytes identical to pre-refactor**
  (proven: full compiler_ir/core_lower suite + E1/E2/E3 md5 unchanged in C2a).
- Migration projector: COPY the seed's `types`+`value_types` into a scratch
  CoreProgram, `lower_value_type_into(scratch, type)` → root `CoreValueTypeId`
  (strict id-first, fails closed on Any/Unresolved/erased/missing nominal), add
  ONE synthetic capability (param/return = the lowered root), run the C1
  `project_core_wire_schema` + C1 verifier. It NEVER lowers a body, so an
  unrelated P6 match/loop cannot block it.
- WorkflowRuntime builds the seed ONCE at construction (or caches per-capability
  schema); NEVER caches by a bare `Program*` pointer.

## 3. Q2 = append-only verified `WireSchemaBinding` (no Program lifetime in config)

```
// Opaque, immutable, constructor-guarded: only a verifying factory can mint one.
class VerifiedWireSchemaBinding {
  public:
    [[nodiscard]] const CoreWireSchemaTable& table() const noexcept;
    [[nodiscard]] CoreWireSchemaNodeId root() const noexcept;
  private:
    VerifiedWireSchemaBinding(...);           // private; factory-only
    CoreWireSchemaTable table_; CoreWireSchemaNodeId root_;
};
// Factories (fail-closed; both run the public local verifier + root gate):
std::optional<VerifiedWireSchemaBinding>
  make_binding_from_typeref(const ir::TypeRef&, const CoreTypeEnvironmentSeed&);  // CLI/native
std::optional<VerifiedWireSchemaBinding>
  make_binding_from_table(CoreWireSchemaTable, CoreWireSchemaNodeId, /*caps meta*/);  // B1 transport
```

- HTTP/gRPC config gains an append-only `std::optional<VerifiedWireSchemaBinding>`
  (NOT a Program / CoreProgram / TypeRef held for its lifetime).
- CLI `load_runtime_capability_bindings(program, ...)` (workflow_run.cpp:912)
  ALREADY holds the full Program: it builds the type seed once and pre-projects
  each capability's response TypeRef into a binding BEFORE registering it — so
  user nominals are fully supported on the normal CLI path.
- Standalone caller with only legacy `response_schema: TypeRef` and NO Program:
  only declaration-free closed shapes migrate; any nominal / missing decl
  fail-closes, and MUST be detected BEFORE any transport/network effect.
- B1 wraps the transported table into the SAME `VerifiedWireSchemaBinding`; call
  sites never change again.

**rev4 fix 4b — legacy `response_schema` and the new verified binding must not
coexist ambiguously.** If BOTH a legacy `response_schema: TypeRef` AND a
`VerifiedWireSchemaBinding` are present on one config, that is fail-closed at the
binding factory / registration stage (BEFORE any transport/network effect) — no
silent precedence, no ignoring one. The intended shapes are exclusive: the normal
CLI path fills ONLY the verified binding (pre-projected from the TypeRef via the
seed); a legacy standalone caller fills ONLY the legacy TypeRef and migrates it
immediately at registration. A config carrying both is a construction error.

## 4. rev3 must-fix 1 — codec never eats a bare "self-claimed verified" table

The codec entry does NOT take a bare `const CoreWireSchemaTable&`. It takes a
`const VerifiedWireSchemaBinding&` (un-forgeable) OR runs the public local
verifier + root gate at its own entry. Locked invariants (shared with B1):
format/kind legality, scalar bounds, ref-id bounds, **cycle-SAFE reachability**,
no-orphan, AND root ∈ the binding's capability param/result set. Failure ⇒ no
Value.

**rev4 fix 4a — the local verifier is cycle-SAFE, not reject-any-cycle.** C1's
wire schema legitimately supports recursive nominals (a node referring back to an
ancestor node). The public local verifier therefore does cycle-safe reachability
(visited-set marking), ACCEPTING a root-reachable cyclic graph and rejecting only
bad ref / orphan / illegal shape. It must NOT be implemented as "reject any
cycle" (that would break recursive Tree/Node). This is exactly C1's existing
`verify_local` (which already marks reachability and permits cycles); C2 promotes
it to a public entry unchanged. Tests: a recursive Tree/Node binding decodes /
validates a FINITE value (termination is guaranteed by the finite value tree, not
by an acyclic schema).

The C1 **Core reprojection** gate runs ONLY when a CoreProgram is present (it
proves the table faithfully re-derives from Core); it does NOT replace the
generic-host **local** verifier, which is the gate B1's untrusted transport
needs.

```
WireDecodeResult      decode_json(const json::JsonValue&, const VerifiedWireSchemaBinding&);
SchemaValidationResult validate_value(const evaluator::Value&, const VerifiedWireSchemaBinding&);
```

## 5. rev3 must-fix 2 — §3.1 canonical forms aligned to value_json writer

Corrected exact rules (verified against value_json.cpp):

- **Option**: JSON `null` → `make_option_none()` (an `EnumValue`
  std::option::Option::None), NOT a bare `NoneValue`. Non-null → `make_option_some(child)`.
  RuntimeValuePolicy accepts ONLY the exact Option `EnumValue` (Some/None), not a
  bare NoneValue.
  - **Nullable-child restriction (P0-9)**: an `Option` whose DIRECT child itself
    encodes as JSON `null` — a `Unit`, or another `Option` — is rejected by the
    wire-schema local verifier with `core.wire.UNSUPPORTED`. Because `None` writes
    `null` and `Some(x)` writes x's own encoding, a null-encoding child makes
    `None` and `Some(child-null)` indistinguishable on the wire, so `Option<Unit>`
    and `Option<Option<T>>` are NOT projectable/transportable. `Option` of any
    non-null-encoding shape (scalars, String, List/Set/Map, Struct — including a
    recursive `Node{next: Option<Node>}` — Enum, Tuple) stays legal. This is the
    reason C2b does NOT claim an unconditional Option round-trip. The gate lives
    only in `LocalSchemaVerifier::validate_node`, covering both the source
    projector and any transported table.
  - **Reserved wire-name restriction (P0-11)**: the wire-schema local verifier
    also rejects (with `core.wire.UNSUPPORTED`, after all structural checks) a
    Struct field named `_type` (collides with the value_json struct discriminator)
    and an ORDINARY Enum whose `wire_name` is `std::option::Option` (collides with
    the value_json Option special-case). A genuine Option is the distinct
    `CoreWireSchemaOption` shape and is unaffected. Normal source-derived Core
    never produces either; any synthetic (projector-API) or transported attempt is
    fail-closed by the same `LocalSchemaVerifier::validate_node` gate, so no such
    table is published or minted.
- **UUID**: canonical wire is the object `{"_uuid":"<32 lowercase hex>"}`
  (value_json.cpp:174-178), NOT a bare string. decode requires that object.
- **Timestamp**: canonical wire is `{"_timestamp":<int64 unix_ms>}` (:179-184),
  NOT a string.
- **Unit**: JSON `null` → `UnitValue` under Unit schema (writer emits null for
  Unit). null-vs-None is disambiguated by schema kind (Unit vs Option), never by
  value shape.
- Decimal/Duration remain their canonical string spellings (value_json writer:
  DecimalValue → bare JSON string `inner.spelling`; DurationValue → bare JSON
  string `inner.spelling`).
- Add an encode→schema-decode round-trip probe for every scalar/aggregate so the
  decoder is proven byte-symmetric with the canonical writer.

### 5.1 Canonical AGGREGATE forms (rev4 fix 3 — the table rev3 only claimed)

Verified against value_json.cpp `write_json_impl`. Each is a writer-symmetric
rule the codec's decode_json must accept EXACTLY and RuntimeValuePolicy must
match on outer variant. Reserved keys begin `_`; unknown reserved keys and any
extra/user key not in the schema are REJECTED.

- **Struct** (StructValue, writer :66-81): JSON object with `"_type":"<canonical
  name>"` PLUS one key per declared field (fields are siblings of `_type`, NOT
  nested). decode requires exact `_type`, the EXACT declared field set (no
  missing, no extra), each field value decoded under its field schema. Empty
  field list ⇒ `{"_type":name}` only.
- **Enum / Result** (EnumValue, writer :82-130): `"_enum":"<enum canonical>"`,
  `"_variant":"<variant>"`. Payload keys are mutually exclusive:
  - Unit payload → NO `_payload` and NO `_named_payload` key at all.
  - Tuple payload → exactly `"_payload":[ ... ]` (array, exact arity, each slot
    under its schema); NO `_named_payload`.
  - Struct payload → exactly `"_named_payload":{ ... }` (object, EXACT declared
    field-name set, each under its schema); NO `_payload`.
  Wrong `_enum`/`_variant`, a payload key that disagrees with the schema's
  payload kind, both payload keys present, or an extra key ⇒ reject. Result is an
  ordinary enum here (`_enum` = std::result::Result, variants Ok/Err), NOT a
  bespoke object.
- **Option** (EnumValue std::option::Option): `null` → `make_option_none()`
  (None variant, no payload); non-null JSON `x` → `make_option_some(decode(x))`
  under the Some child. (Note: this is the schema-driven exception — Option does
  NOT round-trip through the generic `_enum` object form; it uses null / bare
  value, matching how the option value is produced. RuntimeValuePolicy accepts
  only the exact Option EnumValue Some/None.)
- **Tuple**: JSON array, EXACT arity, each element under its element schema.
- **List** (ListValue) / **Set** (SetValue): both serialize as a JSON array; the
  SCHEMA (not the JSON shape) selects List vs Set. decode builds the exact
  collection kind, enforces capacity (len ≤ N when bounded), decodes each element.
- **Map<String,V>** (MapValue, writer :151-172): JSON object; each key is a JSON
  string → StringValue key; each value decoded under V; capacity enforced;
  duplicate JSON keys ⇒ reject (a JSON object with a repeated key is malformed
  for a Map decode — no last-wins). Non-string key schema cannot occur (C1 +
  migration both fail-close it).
- **Unit**: JSON `null` → UnitValue, only under Unit schema.
- Scalars (writer :52-65,174-184): Bool→bool; Int→bare integer; Float→
  `format_double(json_mode)` float spelling; String→JSON string; Decimal→JSON
  string spelling; Duration→JSON string spelling; UUID→`{"_uuid":"<32 hex>"}`;
  Timestamp→`{"_timestamp":<int64>}`.

## 6. rev3 must-fix 3 — Float rule disambiguated by JSON DOM Kind

The JSON DOM distinguishes `Kind::Int` from `Kind::Float` (json_value.hpp:17-18).
- Float schema accepts ONLY `Kind::Float` (the canonical writer emits float
  spelling even for 1.0); `Kind::Int` under Float schema is REJECTED (no
  Int-as-Float).
- Int/BoundedInt schema accepts ONLY `Kind::Int` (in bounds).
- Native RuntimeValuePolicy: Float accepts only FloatValue, Int only IntValue.
- Cross-policy tests: (i) every Value a decode produces MUST pass
  RuntimeValuePolicy under the same binding; (ii) JSON `Kind::Int` under Float
  AND native IntValue under Float BOTH reject. (Removes the rev2 table's internal
  contradiction.)

### 6.1 Numeric provenance (P0-10, plan A — landed as its own shared commit)

The base JSON DOM now tags every number with a `NumberProvenance`
(`NotNumeric` default / `SignedInteger` / `UnsignedInteger` [dedicated
`uint_val`] / `FloatSyntax` / `IntegerFallback`). Legal pairings: non-numeric
Kind + `NotNumeric`; `Int` + `Signed`/`Unsigned`; `Float` +
`FloatSyntax`/`IntegerFallback`. `as_int` → `Signed` only; `as_uint` → `Unsigned`
+ non-negative `Signed`; numeric accessors return nullopt for any invalid pairing
and `serialize_json` fails closed (empty string) on one. The generic
serializer's Float branch and every legal-producer snapshot byte are unchanged
(integral `FloatSyntax` still emits bare `1`), so v1/v2 snapshot bytes and
`arg_hash` (hashes `value_to_json` directly, never the base serializer) are
unaffected; the only byte the serializer now changes is a formerly-corrupt
high-bit unsigned, which serializes as its correct unsigned decimal instead of a
negative. Schema-free `value_from_json` gains a
direct `const JsonValue&` overload; both overloads build `IntValue`/`Timestamp`
only from `Signed` and `FloatValue` only from `FloatSyntax`, failing closed on
`Unsigned`/`IntegerFallback` at any depth. Trust paths decode the DOM directly
(durable resume load workflow_recovery.cpp:190/223/248, CLI tool catalog
workflow_run.cpp) rather than `serialize_json`→reparse, because a generic
serialize→parse round-trip loses `FloatSyntax`/`IntegerFallback` provenance (a
documented A boundary). Option B (a fully provenance-preserving serializer) was
rejected as an out-of-scope persisted-format change. Codec consumption of
provenance (Int/Timestamp via `as_int`, Float rejecting `IntegerFallback`) lands
with the C2b-1/2 codec commit, not the shared P0-10 commit.

## 7. rev3 must-fix 4 — projector-equivalence + policy phrasing precise

- The AHFL synthetic-cap table and the real Core-cap table may differ in
  capability id / source_symbol. Equivalence compares the **canonical reachable
  schema graph** rooted at the corresponding param/result NodeIds (or builds
  identical binding metadata), NOT a blanket `CoreWireSchemaTable operator==`.
- "one traversal / two policies" = shared schema SEMANTICS, not byte-identical
  outer representations (Option is the example: JSON `null` ↔ Option None
  EnumValue). The Result/Struct/Enum canonical object keys and
  tuple/unit/struct payload exact forms are now written out in §5.1
  (writer-symmetric per shape); the tests assert the ACCEPT/REJECT decision
  matches across policies — not that the two inputs are spelled the same.

## 8. Deletions / migrations (C2b)

- DELETE `value_matches_return_type` (workflow_runtime.cpp:286) + uses @674/@708.
  **Stage-3 correction (this was the rev4 plan's mistake):** the replacement is NOT
  a blanket `validate_value(materialized Value, binding)` on both memo and pending.
  The authority is SOURCE-DEPENDENT (see §12 and design §9):
  - a **disk** memo entry (LegacyV2 / ExactSidecar) is decoded from its
    authoritative JSON via `decode_json_legacy_v2` / `decode_json` under the
    capability binding — the schema-free materialized `result` Value is NEVER the
    authority there;
  - a **programmatic NativeOnly** memo entry AND the **host-supplied native pending**
    result are validated on the ORIGINAL Value via `validate_value` (P0-13: validate
    in place, clone only after success);
  - all of this runs AFTER the coordinate + identity + binding-cache gates in the
    §12 order, and only for the existing memo/pending trust paths. The G4 live /
    CLI / shim ingress demotion is still PENDING.
  Same fail-closed status / diagnostic_code / ownership — the check is deeper AND
  source-aware.
- `response_schema_validator`: old ad-hoc TypeRef recursion (Any-accept,
  Float-accepts-Int, enum-payload-unchecked, struct field.type_ref erasure,
  canonical-string stdlib match) DELETED (RFC 0026 C2b G4d). The file and its
  `validate_value_against_schema` entry points are removed entirely — NOT kept as
  a thin shim; every trust path now migrates the declared type to a verified wire
  binding and calls `decode_json` / `validate_value`. The `wire_capability`
  raw-JSON call sites and the CLI resume path were flipped to that codec path in
  G4a/G4b/G4c; G4d removes the last legacy validator symbol.
- KEEP `value_from_json` / `value_to_json` for non-trust callers untouched.
- stdlib / user nominal identity follows `resolve_nominal_strict` verbatim (§0):
  a present id must hit by id with canonical agreement; the ONLY name-only match
  is the descriptor-backed synthetic builtin exception. The old
  `is_nominal_std_*` canonical-STRING matching that lived in the now-deleted
  response_schema_validator was an independent, weaker matcher; identity now flows
  through the one resolver via the migration projector.

## 9. Non-goals — unchanged

NO resume export, NO custom section, NO wasm byte. E1/E2/E3 md5 gate (E1 =
5afff711...). 3 existing trust paths only (live OK @159/@309, memo @674, pending
@708); Wasm-host arg frames are B1+. TextPlain+schema accepted only under
String/BoundedString with bounds enforced; no-schema behavior unchanged.

## 10. Tests

- **C2a**: prelude byte/behavior identity (compiler_ir + core_lower suites green,
  E1/E2/E3 md5 unchanged); migration projector fail-closed matrix (Any/Unresolved/
  erased TypeVar/user-nominal-without-Program/Never/Fn/Closure/non-String-Map →
  no binding); nominal identity per §0 (WRONG id never hits a real decl; an
  un-inlined std builtin resolves to its exact-descriptor synthetic base);
  projector-equivalence by canonical reachable graph.
- **C2b**: cross-policy accept/reject matrix identical for decode_json &
  validate_value over Unit-vs-Option-None, Decimal/Duration/UUID/Timestamp
  canonical objects, List-vs-Set (schema-driven), struct/enum nested wrong type,
  bounds/capacity, Int-as-Float reject, String-as-Decimal reject, wrong outer
  variant; encode→schema-decode round-trip per shape; VerifiedWireSchemaBinding
  gate rejects hand-built/tampered table (bad ref / cycle-orphan / root not a
  cap param|result / bad bounds); RFC0022 memo/pending still fail closed with
  SAME status/code (deeper); `value_matches_return_type` absent; E1/E2/E3 md5 +
  wasm/agent_runtime suites green; ASan clean.

## 11. Deliverable order

1. rev3 sign-off.
2. **C2a** on develop (sole writer): prelude refactor + migration projector +
   VerifiedWireSchemaBinding + equivalence/fail-closed tests → prove byte/behavior
   identity → @ Codex delta review.
3. **C2b** on develop after C2a signs: codec + 2 policies + 3-path migration +
   delete shallow + demote response validator → cross-policy/round-trip/gate
   tests → ASan + dev + byte gate → @ Codex delta review.

## 12. C2b Stage 3 — durable-resume trust seam (memo/pending result)

Stage 3 wires the RFC 0022 durable-resume memo/pending **result** trust boundary
through the shared codec and closes the persistence-format gaps a schema-free
round-trip left open. Full design:
`docs/design/core-ir-kr6-5-e4b-wire-resume-seam.zh.md` §9.

Locked facts (each has a permanent regression):

- **memo-result-only sidecar.** The trust authority is the memo/pending result, and
  how it is carried depends on the entry's persisted source (three-state, below) —
  NOT a blanket "every entry has an exact wire + presence". A NativeOnly entry has NO
  `authoritative_json`; a LegacyV2 entry's presence is UNKNOWN. `node_input` and
  completed-node output are NOT in this seam (see residual risks).
- **three-state authority / compat projection.** `PersistedMemoResultSource` =
  {NativeOnly, LegacyV2, ExactSidecar}, enforced fail-closed by
  `memo_result_state_well_formed` at the real save / load / consume entry points (not
  a construction convenience — the aggregate is public). NativeOnly's native `result`
  is authority (no wire); LegacyV2's authority is the raw `result` substring (presence
  UNKNOWN); ExactSidecar's authority is the sidecar wire + presence bit. The native
  `result` is a compat projection for Legacy/Exact only.
- **consume gate ORDER (P0-13/15/21).** coordinate → identity → binding lookup →
  trust-state+decode. P0-13: persisted memo is read-only, never pre-cloned (clone
  drops null collection children = laundering); validate ORIGINAL, append fresh
  NativeOnly. P0-15: pending identity verified before read/clone and before leaving
  replay. P0-21: residual-replay-not-consumed fails closed, never silently completes.
- **Legacy byte-stability save gate (P0 provenance laundering).** LegacyV2 re-save is
  permitted ONLY when `serialize_json(parse(bytes)) == bytes`; a would-canonicalize
  entry (1.0 / 1e3 / an overflowing integer / a bare `-0`) is InvalidSnapshot and must
  be upgraded through consumption. Prevents an IntegerFallback→FloatSyntax false accept.
- **integral-Float / Unit-null compat.** `decode_json_legacy_v2` accepts
  SignedInteger→Float ONLY under a Float binding (recursive); the sole relaxation vs
  the exact codec. Unit-null presence defaults: decoded Unit → presence=false, else
  true. Legacy negative-zero: an old v2 Float `-0.0` was already written by the outer
  serializer as bare `-0` (sign lost at that old byte layer, before this seam), so a
  legacy widening can only reconstruct `+0.0`; the bare `-0` itself is not byte-stable
  and so cannot be schema-less re-saved (must upgrade via consumption).
- **presence bit (P0-17/18/19).** `optional<bool>` (LegacyV2 UNKNOWN). Save-local
  normalizes a present bare None → presence=false without mutating the caller; a
  presence=false entry must spell exactly null. Consume: presence=false legal ONLY
  under a Unit root; Option-null does NOT ride the Unit seam (one-negative vs the
  rich-matrix Option None present=true one-positive).
- **SymbolId 0 presence-not-value.** Identity presence = `has_value()`, never != 0;
  covers resume id=0 AND the durable-write write-ahead intent (a legal id=0
  DurableWrite still fires its intent; the dereferenced id, incl. 0, flows into the
  idempotency key).
- **residual risks (honest boundaries).** `node_input` is persisted but
  informational — resume RE-EVALUATES the input expression and replays the memo; it
  is NOT a trust authority (comments corrected). Completed-node output restores via
  the P0-10 direct-DOM path, NOT this codec.
- **plaintext persistence, NOT full B2 (security scope).** This slice only adds
  append-only `result_wire_json` / `result_present` inside the existing native
  `WorkflowRecoveryStore` PLAINTEXT v2 snapshot; the sidecar stores the same sensitive
  result's raw wire spelling a SECOND time (compat `result` + sidecar), so it is NOT a
  secret-free / protected store and must NOT be called production-safe durable
  persistence (see design §4.3, §9.1). Full B2 is NOT one "restore-from-node-input"
  task: it separately requires an append-only ABI/control record, a protected payload
  store, atomic memo append + crash semantics, ownership/last-use gating, and exact
  node-order observation/execution evidence (design §4/§5/§6), plus whether
  `node_input` is ever truly used. No resume export / custom section / Wasm byte change
  in this round; B2 is PENDING. G4 (CLI/gRPC/HTTP/shim demotion) is COMPLETE
  (G4a-d landed; the legacy `response_schema_validator` is deleted).
- **rollback risk.** Reverting re-opens both the historical type losses
  (Decimal/Duration/Set/Map/Option/Unit, integral-Float, explicit-Unit vs valueless)
  AND the false-accept surface the gates close (hostile null-child collections,
  cross-capability binding borrow, Legacy laundering). Not behavior-neutral.
- **G4d ABI/symbol risk.** Deleting `response_schema_validator.{hpp,cpp}` removes
  two internal `ahfl_runtime_engine` symbols (`validate_value_against_schema`
  overloads). The header was never in installed `include/ahfl` and there is no
  supported public source-header contract, and there are zero in-tree production
  callers; but any downstream that enabled installing the internal engine target
  or linked those symbols directly must rebuild / migrate to the codec's
  `validate_value`. Not a public-API break, but not a pure no-op for such
  consumers.
