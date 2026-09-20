# Core-IR P9: Layered IR-JSON Projection for the Execution Layer -- Design

> Status: **APPROVED** (RFC 0026 P9 / KR6.9). Design gate for the KR6.9 slice ladder
> (§10 B1–B4). This document is the tracked design decision that
> `docs/reference/rfc-process.zh.md` §设计原则.1 requires for a new stable
> artifact: RFC 0026 calls the layered IR-JSON projection a **new format**
> (`docs/rfcs/0026-ir-tower-and-execution-model.zh.md` L262-263, Open Question Q5
> L369-372), and the roadmap tracks it as KR6.9
> (`docs/plans/q4-2026-roadmap.zh.md` L200).
>
> Scope: the **Core-IR layer's JSON projection** — its envelope, its per-table
> schema, its arena canonicalization / index-remapping rules, its byte-exact
> round-trip contract, and the deprecation boundary for the pre-existing
> single-layer projection. Design only; no production code in this slice.

## 0. Problem, scope, non-goals

Today the IR tower has exactly **one** JSON projection: the single-layer
`ir::Program` writer/reader pair (`src/compiler/ir/ir_json.cpp`,
`print_program_ir_json` / `parse_program_ir_json` declared in
`include/ahfl/compiler/ir/lowering.hpp:36-45`), whose format identity is
`kFormatVersion == "ahfl.ir.v2"` (`include/ahfl/compiler/ir/types.hpp:48`).
RFC 0013 KR5.9 already proved byte-exact round-trip for that format
(`tests/unit/compiler/ir/ir_json_round_trip.cpp`).

The execution layer, Core-IR, has **no** JSON projection at all. It carries a
dedicated format constant for precisely this purpose —
`kCoreFormatVersion == "ahfl.core.v1"` (`include/ahfl/compiler/ir/core_ir.hpp:59`),
whose comment states it exists "so a layered IR-JSON projection (RFC 0026 P9)
can tell the layers apart" — but no writer, no reader, no schema, and no CLI
surface consume it (`CoreProgram::format_version` is the only reader,
`core_ir.hpp:1541`). This forces every cross-layer differential test and every
external tool to go through the single-layer projection, which is the **AHFL-IR
(verification / orchestration)** layer's node set and cannot express the
execution layer's facts: monomorphized `CoreValueType`, ANF `CoreValueId`
liveness, capability-call statements, coercion proof arenas, structured regions.

This design removes that gap by fixing the Core-IR projection **contract** so the
B1–B4 implementation slices (§10) are mechanical.

**In scope:** the Core-IR layer only.

**Non-goals:**

- **No physical layout.** Core-IR's target-specific layout is deliberately *not* a
  `CoreProgram` field; P4-D projects it into the pure side artifact declared by
  `include/ahfl/compiler/ir/core_layout.hpp`. The JSON envelope inherits that
  boundary: it carries logical identity and never a target data-layout fact.
- **No AHFL-IR re-projection work.** The single-layer `ir::Program` projection is
  today the AHFL-IR layer's projection (KR6.3 alias-first:
  `using AhflIr = Program`, `include/ahfl/compiler/ir/program.hpp:120`; node-set
  purification is a KR6.3 residual). This document does not re-specify it — it
  only fixes its **deprecation boundary** (§8).
- **No removal of the single-layer projection.** Removal is bound to KR6.8
  (evaluator retirement) by RFC 0026 Q5; this slice is **mark-deprecate only** and
  emits no `BREAKING CHANGE:` footer.
- **No new `CoreProgram` field, no node-set change.** The schema projects the
  `CoreProgram` that exists at `core_ir.hpp:1540-1549` plus the body arenas already
  owned by `CoreFlowDecl` / `CoreWorkflowDecl`. A field that is not in the IR
  cannot appear in the projection; adding one is a separate IR slice that must
  extend this schema in the same commit.

## 1. Envelope (decision 1: ONE bundled envelope per layer)

**Decision: a single bundled JSON object per program per layer — not one document
per table, and not one document per arena.**

```json
{
  "format_version": "ahfl.core.v1",
  "layer": "core",
  "types": [ ... ],
  "value_types": [ ... ],
  "capabilities": [ ... ],
  "agents": [ ... ],
  "flows": [ ... ],
  "workflows": [ ... ],
  "instances": [ ... ]
}
```

Rationale:

1. **The stores cross-reference.** `CoreInstanceDecl::dispatch_types` and
   `CoreCapabilityDecl::param_types` index the program-global `value_types` arena;
   `CoreAgentDecl::capabilities` indexes `capabilities`; `CoreFlowDecl::target`
   indexes `agents`; `CoreWorkflowNode::target_instance` indexes `instances`. A
   per-table document split would need a second, cross-document reference scheme —
   a second identity layer on top of the index-based one, which Principle 2
   forbids. One envelope keeps every id meaningful within exactly one document.
2. **Atomicity.** The artifact is one compilation unit's execution-layer snapshot;
   a partial bundle is not a valid artifact. One document makes "whole artifact or
   nothing" a file-system fact.
3. **Precedent.** The single-layer projection is already one bundled object with a
   flat `declarations` array; the layered projection keeps that shape and adds the
   layer tag.

`format_version` **reuses `CoreProgram::format_version`** — i.e. the envelope emits
the `CoreProgram` field's own string value, which defaults to `kCoreFormatVersion`
(`"ahfl.core.v1"`). There is deliberately **no separate JSON-envelope version
constant**: the Core layer has exactly one interchange version, and a second
constant would be a parallel SSOT that could silently drift from the field it
supposedly versions. A future incompatible envelope change bumps
`kCoreFormatVersion` once, and both the field and the envelope move together.

`layer` is a fixed discriminator literal (`"core"`) that lets a consumer reject a
document handed to the wrong reader **before** parsing any table. It is not a
version: the single-layer projection's `"ahfl.ir.v2"` and this envelope's
`"ahfl.core.v1"` are already distinguishable by `format_version` alone, so `layer`
is a redundant, deliberately cheap first check (fail-closed admission, §9).

Determinism boundary (artifact-chain, RFC 0026 L262-263): the envelope contains
**no** wall-clock, pid, host path, environment value, or secret. Every emitted
string is either a source-level name / diagnostic spelling or a canonical
identity; every number is a structural index or a source-level literal fact.

## 2. Lexical convention (decision 2: mirror the single-layer streaming writer)

**Decision: byte-for-byte, the Core-IR writer follows the lexical rules of
`IrJsonPrinter`** (`src/compiler/ir/ir_json.cpp:338-420`, the 2-space streaming
writer), promoted to a shared base rather than re-implemented.

The rules, stated normatively so a reimplementation cannot drift:

| Rule | Value |
|------|-------|
| Object/array formatting | Multi-line; `{` / `[` then newline; one member/element per line; closing `}` / `]` on its own line at the parent indent |
| Indentation | Exactly 2 spaces per nesting level (`indent_level * 2`) |
| Key separator | `": "` (colon + one space) |
| Element separator | `,` immediately after the previous element, then newline + indent (no trailing comma) |
| Empty object / array | `{}` / `[]` — no newline, no inner indent |
| Document terminator | Exactly one trailing `'\n'` after the root object's closing `}` |
| Field order | **Declaration order of the projected C++ struct**, fixed by this document's §4–§5 tables. Never sorted, never reordered |
| String escaping | `write_escaped_json_string` (`src/base/support/json.hpp:11`): `\\`, `\"`, `\b`, `\f`, `\n`, `\r`, `\t`, and `\u00xx` for other bytes `< 0x20`. Non-ASCII bytes pass through unescaped |
| Booleans | `true` / `false` (baretokens) |
| Absent optional | The **field is omitted entirely** — never `null` (mirrors `print_source_range_field`, `ir_json.cpp:584-590`) |
| `source_range` | When present: `{"begin_offset": <n>, "end_offset": <n>}` with both as bare integers (mirrors `print_source_range`, `ir_json.cpp:576-582`) |
| Indices / ids | Bare JSON integers (unsigned decimal, no leading zeros, no quotes) |
| `kInvalid` ids | **Never emitted.** `CoreExprId::kInvalid`, `CorePatternId::kInvalid`, `CoreCoercionPlanId::kInvalid`, `CoreValueTypeId::kInvalid`, `CoreTypeId` "unresolved" sentinels are all lowering-ERROR / pre-verify states; a projection of a verifier-clean program cannot contain one. The writer is `[[nodiscard]]`-gated and fails closed on an invalid id rather than emitting `4294967295` |

The one deliberate difference from the single-layer writer: the Core writer
**must not** print any field the IR does not carry to make output "nicer". A
display convenience that is not a `CoreProgram` field is a second, drifting model
of the IR.

Number precision is inherited, not re-decided: `int_range`-style bounds and index
fields are emitted as bare integers, so a consumer that needs arbitrary precision
must use a bignum-aware JSON parser. This is the same documented limitation the
single-layer projection already has; the layered projection does not make it
worse and does not fix it here (§8 defers any lexical change to a format bump).

## 3. Node discriminators (`kind`), derived — never hand-maintained

Every variant-typed node emits a `"kind"` string as its **first** field. The
spelling is looked up from a **single table generated from the node list**, exactly
as the single-layer writer already does for expressions via
`expr_node_wire_name(node.node)` (`ir_json.cpp:1053`, table at
`include/ahfl/compiler/ir/expr.hpp:446` built from
`include/ahfl/compiler/ir/expr_nodes.def`).

Normative rules:

1. For a node family whose variant is already generated from an X-macro `.def`
   list (`CoreValueTypeNode` from `core_value_types.def`,
   `include/ahfl/compiler/ir/node_tags.hpp`), the writer's `kind` table and the
   reader's `kind` dispatch are **both** derived from that same list. A
   hand-written `if (kind == "...")` chain on either side is forbidden — it is
   precisely the drift class `expr_nodes.def` was introduced to kill (see its
   header comment).
2. For families that are still hand-declared variants (`CoreDecl`,
   `CoreExprNode`, `CoreStmtNode`, `CorePatternNode`, `CoreInstancePayload`), B1
   **must** introduce the `.def` list in the same commit as the schema, so the
   discriminator is derived from the start rather than retrofitted. Until then the
   writer uses `std::visit` with `Overloaded` — no name lookup by index.
3. `kind` spellings are `snake_case` and match the C++ alternative's semantic name
   with the family prefix dropped (e.g. `CoreVtNominal` → `"nominal"`,
   `CoreCapabilityCallStmt` → `"capability_call"`, `CoreVtFn` → `"fn"`). The
   mapping table is the SSOT; this document does not enumerate it.
4. `kind` **must not** be `CoreValueTypeNode`'s only node identity for an enum
   value — an enum's *value type* normalizes to its parent `CoreVtNominal`
   (`core_ir.hpp` §Logical value types), and variant identity rides on
   `CoreVtNominal`'s owning decl + `CoreVariantId`, not on a per-variant value-type
   node.

## 4. Program-global tables

Top-level arrays, in the envelope's fixed order (§1). Every array index **is** the
corresponding typed id, so no id field is emitted for the element's own position —
only for references to other tables.

| Envelope key | C++ source | Element id | Notes |
|---|---|---|---|
| `types` | `CoreProgram::types` (`core_ir.hpp:1542`) | `CoreTypeId` == index | Nominal decls. `kind` ∈ `{"struct","enum"}` |
| `value_types` | `CoreProgram::value_types` (`core_ir.hpp:1543`) | `CoreValueTypeId` == index | Hash-consed arena (§6) |
| `capabilities` | `CoreProgram::capabilities` (`core_ir.hpp:1544`) | `CoreCapabilityId` == index | `effect_kind` reused from `ir::CapabilityEffectKind` |
| `agents` | `CoreProgram::agents` (`core_ir.hpp:1545`) | `CoreAgentId` == index | State machine + typed shell |
| `flows` | `CoreProgram::flows` (`core_ir.hpp:1546`) | position only (no typed `CoreFlowId` wrapper) | Source order preserved (determinism) |
| `workflows` | `CoreProgram::workflows` (`core_ir.hpp:1547`) | `CoreWorkflowId` == index | Source order preserved |
| `instances` | `CoreProgram::instances` (`core_ir.hpp:1548`) | `CoreInstanceId` == `id` field | `id` **is** emitted because `CoreInstanceDecl::id` is a real field |

Per-table field order (normative; implementers copy this column, they do not
choose):

**`types[]`** — `kind`, `name`, `fields`, `field_nominal_types`,
`field_has_default`, `variants`, `variant_payloads`, `member_type_templates`,
`field_type_template_roots`, `type_param_count`, `variances`, `role`,
`symbol_ref`, `source_range`.

- `fields` / `variants` are arrays of strings (display-only names).
- `field_has_default` is an array of booleans parallel to `fields`.
- `variant_payloads[]`: `kind` (`"unit"|"tuple"|"struct"`),
  `slot_type_template_roots`, `field_names`.
- `member_type_templates[]`: `kind` (`"concrete"|"param"|"nominal"|"fn"`),
  `concrete`, `param_index`, `nominal`, `capacity`, `children`, `fn_return` —
  emitted **only** for the fields the node's kind uses (a `Concrete` node emits no
  `children`; the reader enforces the same per-kind field mask the verifier
  enforces, `core_verify.cpp` §member-template shape checks).
- `variances` is an array of strings (`"invariant"|"covariant"|"contravariant"`).
- `role` is a string (`"ordinary"|"option"|"result"|"list"|"set"|"map"`).

**`value_types[]`** — a single `kind` field plus the alternative's own payload
fields (§3 rule 1 derives both). Structural nodes carry child **ids**, never
inlined nests: `nominal` = `base` + `args` + `capacity`; `tuple` = `elements`;
`fn` = `params` + `ret`; `closure` = `signature` + `captures`
(`value_type`, `mode`).

Composite-field encodings are inherited from the single-layer writer so one
spelling exists across both projections:

- An `std::optional<std::pair<int64,int64>>` (a scalar refinement — `CoreVtInt::bounds`,
  `CoreVtString::length_bounds`) is emitted as an object
  `{"minimum": <i64>, "maximum": <i64>}`, matching `int_bounds` / `string_bounds`
  (`ir_json.cpp:653-668`), and omitted entirely when absent.
- An `std::optional<uint64_t>` (`CoreVtNominal::capacity`) is emitted as a bare
  integer, matching `collection_capacity` (`ir_json.cpp:670-675`), and omitted when
  absent.
- `CoreVtNever` / `CoreVtUnit` / other payload-free nodes emit `kind` alone.

**`capabilities[]`** — `name`, `symbol_ref`, `effect_kind`, `param_types`,
`return_type`, `source_range`. The signature is the fully materialized logical
signature in the program-global arena; the source-level parameter names are
intentionally absent (retaining both would be two signature SSOTs —
`core_ir.hpp` §CoreCapabilityDecl).

**`agents[]`** — `name`, `symbol_ref`, `states`, `initial`, `finals`,
`transitions`, `input_type`, `context_kind` (`"unit"|"struct"`), `context_type`,
`output_type`, `capabilities`, `source_range`.

- `states` is an array of strings; `initial` / `finals[]` / `transitions[]`
  (`from`, `to`) are `CoreStateId` integers into `states`.
- `context_type` is emitted **only** when `context_kind == "struct"` — a Unit
  context has `context_type == kInvalid` and must never be projected (§2).

**`flows[]`** — `agent` (`CoreAgentId`), `agent_name`, `target_ref`,
`value_count`, `exprs`, `value_types`, `coercion_plans`, `patterns`, `states`.

- `states[]`: `state` (`CoreStateId`), `state_name`, `policy`, `body`.
- `policy`: `retry_limit`, `retry_on`, `timeout` — each omitted when absent.

**`workflows[]`** — `id`, `name`, `symbol_ref`, `input_type`, `output_type`,
`value_count`, `exprs`, `value_types`, `coercion_plans`, `patterns`, `nodes`,
`return_region`.

- `nodes[]`: `id` (`CoreWorkflowNodeId`), `target_instance` (`CoreInstanceId`),
  `node_name`, `target_ref`, `after` (`CoreWorkflowNodeId[]`), `input_region`.
- `return_region` is a nested region object (§5). Emitted **only** when present;
  the reader distinguishes "absent" from "empty region".

**`instances[]`** — `id`, `instance_key`, `origin` (a `symbol_ref`),
`dispatch_types` (`CoreValueTypeId[]`), `payload`.

- `payload.kind` ∈ `{"capability","predicate","agent","workflow","fn"}` derived
  from `CoreInstancePayload`; the kind is a **structural fact of the variant**, so
  the writer must not emit a parallel `enum` field that could disagree with it
  (`variant_size_v<CoreInstancePayload> == 5` is pinned adjacent to the variant).
- Non-variant-owning kinds still emit `origin` (carried by the enclosing decl).
- `instance_key` is emitted byte-exact (it is the canonical execution dispatch
  label, never re-derived or string-parsed in Core).

`symbol_ref` objects are projected by exactly the layout the single-layer writer
already uses (`ir_json.cpp` §symbol ref: `kind`, `canonical_name`, `local_name`,
`module_name`, `id`), so one symbol spelling exists across both projections.

## 5. Per-body arenas (decision 4: nested, not flattened)

**Decision: an arena owned by a decl is projected as a nested array inside that
decl. It is never hoisted to a top-level id-addressed pool.**

The Core IR has four arena classes, with different ownership:

| Arena | Owner | Id | Serialized as |
|---|---|---|---|
| `exprs` | `CoreFlowDecl` / `CoreWorkflowDecl` | `CoreExprId` == index | nested `exprs[]` |
| `patterns` | same | `CorePatternId` == index | nested `patterns[]` |
| `coercion_plans` | same | `CoreCoercionPlanId` == index | nested `coercion_plans[]` |
| `value_types` (per body) | same | `CoreValueId` == index | nested `value_types[]` |
| `value_types` (program) | `CoreProgram` | `CoreValueTypeId` == index | top-level (§4, §6) |

Rationale, and why this does **not** contradict §1's "one envelope":

- The per-body ids are **body-relative by construction** — `CoreExprId`
  (`core_ir.hpp:73`), `CorePatternId` (`:122`), `CoreCoercionPlanId` (`:196`), and
  `CoreValueId` (`:67`) are only meaningful inside one flow/workflow
  (`CoreCoercionPlanId` is documented "meaningful only within its owning
  CoreFlowDecl/CoreWorkflowDecl"). Hoisting them
  into a shared pool would either need per-owner namespacing (a fabricated identity
  layer) or would silently allow an id from one body to resolve against another's
  arena. Nesting makes the scoping a property of the document.
- The decl **owns** the arena in memory. `CoreWorkflowNode::input_region`,
  `CoreWorkflowDecl::return_region`, `CoreMatchArm::body` / `guard_region`, and
  `CoreIfStmt::then_region` / `else_region` are `std::unique_ptr<CoreRegion>`; the
  nested JSON object mirrors that ownership one-to-one, so the reader rebuilds
  `unique_ptr` by value and no cross-object aliasing is possible.
- The program-global `value_types` stays top-level because it is **referenced
  program-wide** (capability signatures, dispatch types, instance shells) — it is
  not body-scoped, so nesting it would break those cross-references.

The per-body `value_types[]` is **dense** (`size == value_count`, index ==
`CoreValueId`) in a lowering-clean body, and every entry is a valid non-`Never`
`CoreValueTypeId` into the program-global arena (`core_verify.cpp`
`verify_body_value_types`). The projection preserves that: the array is emitted as
the dense table (not as a sparse map) and `value_count` is emitted as a sibling
field so a reader can assert density without inferring it.

**Regions and statements.** `CoreRegion` is a nested object `{"statements": [...]}`.
Statements nest recursively (`CoreIfStmt` branches, `CoreMatchArm` guard/body,
`CoreMatchStmt::fallback_region`), mirroring the `unique_ptr` ownership. A
recursive reader is therefore stack-bounded by the IR's own nesting depth, so the
reader enforces an explicit **maximum region-nesting depth** and fails closed on
exceed. The IR itself models region nesting with `std::unique_ptr` chains
(`core_ir.hpp` `CoreRegion` / `CoreIfStmt` / `CoreMatchArm`), and both the
lowerer's structural walk `core_region_exit` and the verifier's own per-path walk
(`core_verify.cpp`) are written **iteratively** precisely so a deep or malformed
nest cannot overflow the C++ stack; the JSON reader inherits the same obligation
and must not be the one recursive component without a bound.

Statement field order: `CoreStmt` emits `kind` then `source_range`, then the
alternative's own fields. `CoreMatchStmt` **always** emits `fallback_region`
("never null in a well-formed program"); `CoreIfStmt` emits `else_region` only when
non-null; `CoreMatchArm` emits `guard_region` only when non-null. The writer's
"always emit" set is exactly the set the verifier requires to be non-null — one
rule, not two.

## 6. Arena canonicalization and index remapping (decision 3)

The program-global `value_types` arena is a **true hash-cons**: `ValueTypeArena`
(`src/compiler/ir/core_lower.cpp:686`) interns structurally, children are interned
**before** their parent so a node's key contains only child ids, and storage is
deterministic append-only, so two lowerings of the same input produce the same
arena order. Three rules follow, and they are normative for the projection:

### 6.1 Write in arena order, and prove the order is canonical

`value_types` is serialized **in arena order** (index `0..N-1`), one element per
slot, no re-sorting and no dedup at write time. The determinism is not assumed — it
is **already enforced** at the consumption boundary:
`verify_value_types` (`core_verify.cpp:2906`) rejects (a) any child id `>=` the
parent's index ("not interned earlier"), and (b) any two structurally identical
entries (`kValueTypeDuplicate`, "the arena must be a canonical hash-cons"). So a
verifier-clean program's arena is already a canonical, postordered hash-cons, and
"serialize in arena order" is a total, deterministic function of the program.

### 6.2 The reader REBUILDS the arena by interning; it does not trust ids

The reader **must not** place entries at their serialized positions and then trust
the ids inside nodes. Instead it:

1. Walks the serialized `value_types` array in order, interning each node into a
   **fresh** arena through the same structural interning rule, and recording the
   `serialized index → freshly minted id` remap.
2. Because children always precede parents, a single forward pass suffices; a
   forward/self reference (child index `>=` its own index) is a **fail-closed read
   error**, the same rule the verifier applies.
3. Rewrites every value-type id **reference** in the rest of the document through
   that remap: `CoreVtNominal::base` (a `CoreTypeId`, not remapped — nominal ids
   are table positions, §4) and `args`, `CoreVtTuple::elements`,
   `CoreVtFn::params`/`ret`, `CoreVtClosure::signature`/`captures`, plus the
   non-arena references `CoreCapabilityDecl::param_types`/`return_type`,
   `CoreInstanceDecl::dispatch_types`, every body `value_types[]` entry, and
   `CoreExpr::result_type`.
4. **Asserts the remap is the identity** on a canonical input. After rebuilding,
   the freshly minted id for serialized slot `i` must be `i`; otherwise the input
   was not in canonical arena order (a reordered-but-structurally-valid document, or
   a hand-edited one) and the read is **rejected**. This is the round-trip's
   self-check: it makes "trusts ids" and "rebuilds by interning" observably
   different, and rejects the former.

Rebuilding-then-asserting-identity is what makes the reader robust to a document
produced by an independently written Canonical encoder while still refusing any
document that is not in canonical form. It also makes the reader the **only**
place that decides what structural equality means for value types — no second
equality implementation.

### 6.3 Non-arena tables are positional; ids are not remapped

`types`, `capabilities`, `agents`, `flows`, `workflows`, `instances`, and every
per-body arena are **positional**: the array index IS the id, so the reader
reconstructs them by position and does not remap them. The only remapping in the
whole document is the program-global `value_types` map of §6.2. This is a
deliberate asymmetry: the hash-cons is the one table whose identity is *structural*
rather than *positional*, and therefore the only one where ids can legitimately
disagree with positions.

## 7. Byte-exact round-trip requirements

Two distinct obligations; both are acceptance criteria for B3.

**R1 — byte-exact re-emit (the KR5.9 method).** For every representative program
`p`,
`print_core_ir_json(parse_core_ir_json(print_core_ir_json(p))) == print_core_ir_json(p)`
as bytes. Composition order matters: it is `print ∘ parse ∘ print`, because the
input to the second print is a freshly interned arena (§6.2), and byte equality
proves the rebuild preserved canonical order. This is the same shape as
`ir_json_round_trip.cpp:48` ("parse(original) -> print == original").

**R2 — structural identity.** The parsed program must be structurally equal to the
original. `CoreProgram` has **no** program-level `operator==` today (per-decl
equality exists: `CoreFlowDecl`, `CoreWorkflowDecl`, `CoreStmt`, `CoreRegion`,
`CoreMatchArm`, `CoreInstanceDecl`). B3 **must** add one canonical program-level
structural equality (`core_program_equal`) and use it here; the round-trip test
asserts `core_program_equal(p, parse(print(p)))`. R2 is not implied by R1 for a
writer/reader pair that are jointly wrong, so it is required, not optional.

**Corpus.** B3's round-trip test runs over every in-tree fixture that lowers to a
verifier-clean Core program, not over a hand-picked pair. The corpus is discovered
the way KR6.7 fixed the conformance classifier to do it (directory discovery with
an asserted pinned set), so a new fixture cannot silently escape round-trip.

**Negative cases (fail-closed admission, §9).** R1/R2 are the positive contract; the
reader must additionally **reject**, with a source-ranged diagnostic where a range
exists: a wrong `format_version`; a wrong `layer`; a forward/self value-type
reference; a non-canonical arena order (identity remap failure, §6.2); a
`kInvalid`-valued id in a required position (which the writer never emits, §2);
`value_count != value_types.size()` in a body; over-deep region nesting; an unknown
`kind` string; and any node violating its per-kind field mask (an unused payload
field present, or a used one absent). "Silently defaulting" is forbidden — the
single-layer reader's historical demotion of an unknown kind to `nullopt` is not
replicated where a specific cause is knowable.

## 8. Deprecation boundary for the single-layer projection

**Decision (recorded, matching RFC 0026 Q5 verbatim): the pre-existing single-layer
IR-JSON projection is MARK-DEPRECATED, not removed. Its removal is bound to
evaluator retirement (KR6.8) plus all downstream consumers having migrated to the
layered projection (KR6.9 complete).**

This slice:

- **adds** a deprecation marker: the single-layer `ir-json` / text-`ir` emit paths
  are documented as deprecated-in-favour-of the layered projection in
  `docs/reference/ir-format.zh.md` and `docs/reference/cli-commands.zh.md`, and the
  marker is machine-readable (a stable doc fragment, not prose only) so the KR6.9
  completion slice can assert it is present. Those two files are among the ones
  `scripts/check-ir-doc-sync.py` inspects, so the marker is added **without**
  introducing any new string that script requires unless the script is updated in
  the same commit (`docs/reference/ir-format.zh.md` already satisfies the gate; the
  marker must be additive prose, not a new gate). Documenting this is B4's job, not
  B0's.
- **does NOT** change the single-layer writer, reader, CLI flag, artifact id, or
  golden files. The existing goldens (`tests/golden/ir/*.json`, the
  `ahflc.emit_ir_json.*` fleet in `tests/cmake/SingleFileCliTests.cmake`) stay
  byte-identical, and the dual round-trip guards
  (`ahfl.ir.json_round_trip` + the new Core round-trip) run **side by side** during
  the transition — the "round-trip golden 双守护" RFC 0026 Q5 asks for.
- **does NOT** emit a `BREAKING CHANGE:` footer. That footer belongs to the removal
  slice (KR6.8-gated), which is the only slice allowed to delete the single-layer
  projection and its goldens.

Why mark-only: the single-layer projection currently **is** the AHFL-IR layer's
projection (KR6.3 alias-first, §0), so deleting it before the layer split is
complete would delete a *live* layer's projection, not a legacy one. Binding
removal to KR6.8 avoids that and matches the RFC's own rationale — the deprecation
point is bound to a definite milestone rather than left hanging (RFC 0026
L369-372).

## 9. Admission model

The new reader is a **hard admission boundary**, not a best-effort loader. A parsed
document is accepted only if it (a) passes envelope checks (§1), (b) parses with no
unknown kind and no missing/extra per-kind field, (c) rebuilds the value-type arena
to the identity remap (§6.2), (d) satisfies the structural rules the Core verifier
already enforces, and (e) is within the region-nesting depth bound. Failure is a
typed error with a source range where the document supplies one, never a partially
populated program.

Two consequences, stated so they are not re-litigated later:

1. The reader does **not** re-run the full Core verifier. It enforces the shape and
   canonicalization rules the verifier's own preconditions guarantee, so a
   round-tripped program is a valid input to the verifier; it does not claim to
   stand in for the verifier.
2. Reading is **not** an alternate lowering path. It reconstructs the exact program
   the writer was given; it never re-derives, re-mangles, or re-interns anything
   from source-level names.

## 10. Implementation ladder (this document unblocks B1–B4)

One commit per slice, each independently reviewable and regression-guarded. B0 (this
document) is docs-only and adds no code.

| Slice | Deliverable | Regression guard |
|---|---|---|
| **B1** | Envelope + program-global tables: `CoreJsonPrinter` for `types` / `value_types` / `capabilities` / `agents` / `instances`, the shared 2-space writer base, the X-macro `.def` node lists for the hand-declared Core variants (§3 rule 2), and the `kind`/field-order tables of §4 | Unit tests over hand-built minimal programs; the writer is `[[nodiscard]]`-gated and never emits `kInvalid` |
| **B2** | Per-body nested arenas: `flows` / `workflows` incl. `exprs` / `patterns` / `coercion_plans` / dense `value_types` and the recursive region/statement projection (§5) | Unit tests per node family; the verifier-clean corpus serializes without error |
| **B3** | Reader (`parse_core_ir_json`) with interning rebuild + identity-remap assert (§6.2), fail-closed admission (§9), `core_program_equal` (§7 R2), and the R1/R2 byte-exact round-trip corpus test | New `ahfl.ir.core_json_round_trip` test; negative-reader cases per §7; the existing `ahfl.ir.json_round_trip` stays green |
| **B4** | CLI surface + docs: `emit core-ir-json` command kind + artifact id registered in `config/product-scope-freeze.json` (the gate is currently lifted — RFC 0012 is `stabilized`, `scripts/check-product-scope-freeze.py:138` — but the catalog entries are still the documented registration point), plus the deprecation marker in `docs/reference/ir-format.zh.md` / `docs/reference/cli-commands.zh.md` (§8) | New CLI golden fleet alongside the existing `ahflc.emit_ir_json.*` ones; `ahfl.docs.ir_sync_gate` stays green |

Every slice keeps the single-layer projection and all its goldens byte-identical
(§8). No slice branches on a feature flag: the layered projection is purely
additive until the removal slice, which is out of scope here.

## 11. Rejected alternatives

| Alternative | Rejected because |
|---|---|
| One document per layer, one file per table | Breaks program-global cross-references (`dispatch_types` → `value_types`, flow `target` → `agents`) into a second cross-document id scheme; violates Principle 2 |
| Hoist per-body arenas to a top-level pool | The body-relative ids would need fabricated per-owner namespacing, and an id could resolve against the wrong body's arena |
| A separate JSON-envelope version constant | Parallel SSOT with `CoreProgram::format_version`; the Core layer has exactly one interchange version |
| Trust serialized ids on read | Would accept a reordered / hand-edited arena and would create a second notion of structural equality; §6.2's rebuild + identity assert is the single decision point |
| Re-sort or dedup `value_types` at write time | The arena is already canonical (verifier-enforced); re-sorting is a second canonicalizer that can disagree with the hash-cons |
| Re-run the full Core verifier inside the reader | The reader is an admission boundary for a document's shape, not a substitute for verification; conflating them makes round-trip failures ambiguous |
| Delete the single-layer projection now | It is currently the AHFL-IR layer's projection (KR6.3 alias-first); removal is KR6.8-gated (RFC 0026 Q5) |
| Pretty-print with `null` for absent optionals | Diverges from the single-layer writer and makes "absent" and "present-but-null" two encodings of one state |
