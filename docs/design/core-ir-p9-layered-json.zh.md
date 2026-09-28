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
> schema, its arena canonicalization / index-remapping rules, and its byte-exact
> round-trip contract. The relationship to the pre-existing single-layer
> projection is recorded in §8 (amended: permanent per-layer projections, no
> deprecation). Design only; no production code in this slice.
>
> All `file:line` anchors below were re-verified against `develop` in the
> fix-forward commit that follows this one; where a symbol moved, the citation
> names the symbol and the enclosing function as well as a line, so the next
> refactor does not silently invalidate it.

## 0. Problem, scope, non-goals

The Semantic IR layer (`ir::Program`) has exactly **one** JSON projection: the
single-layer `ir::Program` writer/reader pair (`src/compiler/ir/ir_json.cpp`,
`print_program_ir_json` / `parse_program_ir_json` declared in
`include/ahfl/compiler/ir/lowering.hpp:36`/`:45`), whose format identity is
`kFormatVersion == "ahfl.ir.v2"` (`include/ahfl/compiler/ir/types.hpp:48`).
RFC 0013 KR5.9 already proved byte-exact round-trip for that format
(`tests/unit/compiler/ir/ir_json_round_trip.cpp`).

That "exactly one" is a claim about `ir::Program`, not about the product: Opt IR
has its own **separate** machine-readable artifact, the `AHFL_OPT_IR_V1` JSON
emitted by `ahflc emit opt-ir-json` (its own serializer, not `IrJsonPrinter`;
`docs/reference/ir-format.zh.md:26`, `docs/reference/cli-commands.zh.md:32`). It is
neither a second projection of `ir::Program` nor a substitute for one, so it does
not weaken the argument below. What matters here is that the **Core** layer has
none.

The execution layer, Core-IR, has **no** JSON projection at all. It carries a
dedicated format constant for precisely this purpose —
`kCoreFormatVersion == "ahfl.core.v1"` (`include/ahfl/compiler/ir/core_ir.hpp:60`),
whose comment states it exists "so a layered IR-JSON projection (RFC 0026 P9)
can tell the layers apart" — but no writer, no reader, no schema, and no CLI
surface consume it (`CoreProgram::format_version` is the only reader,
`core_ir.hpp:1602`). This forces every cross-layer differential test and every
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
  the AHFL-IR layer's projection (KR6.3 alias-first:
  `using AhflIr = Program`, `include/ahfl/compiler/ir/program.hpp:155`
  (`using AhflIr = Program;`); node-set
  purification is a KR6.3 residual). This document does not re-specify it.
- **The single-layer projection is a permanent per-layer projection, not a
  deprecated one.** It was originally marked deprecated with removal bound to
  KR6.8; that transitional labeling was reversed when the three-layer tower
  settled (see §8, amended). Each tower layer keeps its own projection.
- **No new `CoreProgram` field, no node-set change.** The schema projects the
  `CoreProgram` that exists at `core_ir.hpp:1601-1610` plus the body arenas already
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
`IrJsonPrinter`** (`src/compiler/ir/ir_json.cpp:338`, the 2-space streaming
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
| Field order | **Declaration order of the projected C++ struct**, fixed by this document's §4–§5 tables. Never sorted, never reordered. Where a §4–§5 table differs from the struct's literal declaration order it has done so deliberately and is the normative copy (each such deviation is called out in a bullet under its table); implementers copy the §4–§5 column and do not re-derive order from the header |
| String escaping | `write_escaped_json_string` (`src/base/support/json.hpp:10`): `\\`, `\"`, `\b`, `\f`, `\n`, `\r`, `\t`, and `\u00xx` for other bytes `< 0x20`. Non-ASCII bytes pass through unescaped |
| Booleans | `true` / `false` (baretokens) |
| Absent optional | The **field is omitted entirely** — never `null` (mirrors `print_source_range_field`, `ir_json.cpp:584`) |
| `source_range` | When present: `{"begin_offset": <n>, "end_offset": <n>}` with both as bare integers (mirrors `print_source_range`, `ir_json.cpp:576`) |
| Indices / ids | Bare JSON integers (unsigned decimal, no leading zeros, no quotes) |
| `kInvalid` ids | **Never emitted in a REQUIRED-valid position** — the writer is `[[nodiscard]]`-gated and fails closed there rather than emitting `4294967295`. `CoreExprId::kInvalid`, `CorePatternId::kInvalid`, `CoreCoercionPlanId::kInvalid`, `CoreValueTypeId::kInvalid` and the `CoreTypeId` "unresolved" sentinel are lowering-ERROR / pre-verify states, so no REQUIRED-valid slot of a verifier-clean program holds one. The **one** legal serialized `kInvalid` is `CoreTypeDecl::field_nominal_types` (§4): it is a sparse navigation-only table, not a required-valid position |
| `kInvalid` in `field_nominal_types` | `CoreTypeDecl::field_nominal_types` is **navigation-only** and, unlike every other id field, may legally hold `kInvalid` in a verifier-clean program — see the dedicated row in §4 |

The one deliberate difference from the single-layer writer: the Core writer
**must not** print any field the IR does not carry to make output "nicer". A
display convenience that is not a `CoreProgram` field is a second, drifting model
of the IR.

Number precision is inherited, not re-decided: `int_range`-style bounds and index
fields are emitted as bare integers, so a consumer that needs arbitrary precision
must use a bignum-aware JSON parser. This is the same documented limitation the
single-layer projection already has; the layered projection does not make it
worse and does not fix it here; any lexical change waits for a format bump).

## 3. Node discriminators (`kind`), derived — never hand-maintained

Every variant-typed node emits a `"kind"` string as its **first** field. The
spelling is looked up from a **single table generated from the node list**, exactly
as the single-layer writer already does for expressions via
`expr_node_wire_name(expr.node)` (`ir_json.cpp:1057`, table at
`include/ahfl/compiler/ir/expr.hpp:447` built from
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
| `types` | `CoreProgram::types` (`core_ir.hpp:1603`) | `CoreTypeId` == index | Nominal decls. `kind` ∈ `{"struct","enum"}` |
| `value_types` | `CoreProgram::value_types` (`core_ir.hpp:1604`) | `CoreValueTypeId` == index | Hash-consed arena (§6) |
| `capabilities` | `CoreProgram::capabilities` (`core_ir.hpp:1605`) | `CoreCapabilityId` == index | `effect_kind` reused from `ir::CapabilityEffectKind` |
| `agents` | `CoreProgram::agents` (`core_ir.hpp:1606`) | `CoreAgentId` == index | State machine + typed shell |
| `flows` | `CoreProgram::flows` (`core_ir.hpp:1607`) | position only (no typed `CoreFlowId` wrapper) | Source order preserved (determinism) |
| `workflows` | `CoreProgram::workflows` (`core_ir.hpp:1608`) | `CoreWorkflowId` == index | Source order preserved |
| `instances` | `CoreProgram::instances` (`core_ir.hpp:1609`) | `CoreInstanceId` == `id` field | `id` **is** emitted because `CoreInstanceDecl::id` is a real field |

Per-table field order (normative; implementers copy this column, they do not
choose):

**`types[]`** — `kind`, `name`, `fields`, `field_nominal_types`,
`field_has_default`, `variants`, `variant_payloads`, `member_type_templates`,
`field_type_template_roots`, `type_param_count`, `variances`, `role`,
`symbol_ref`, `source_range`.

- `fields` / `variants` are arrays of strings (display-only names).
- `field_has_default` is an array of booleans parallel to `fields`.
- **`field_nominal_types` is the ONE field where `kInvalid` is a LEGAL serialized
  value**, meaning "navigation cannot advance through this slot" (`core_ir.hpp:1534-1539`:
  a primitive, a non-struct, or a generic `T` field). It is a **sparse
  navigation-only** table — deliberately not the field's logical type, which lives
  in `field_type_template_roots` + `member_type_templates` — and the lowerer only
  ever *writes* a resolved id, never resets one (`fixup_field_nominal_types`,
  `src/compiler/ir/core_lower.cpp:484-496`; the vector is pre-sized with
  `CoreTypeId{}` at `:249-250`), while the verifier **accepts** `kInvalid` and only
  rejects an out-of-range non-`kInvalid` id (`src/compiler/ir/core_verify.cpp:181-185`).
  `tests/unit/compiler/ir/core_lower.cpp:4260` and the in-tree fixture
  `tests/golden/wasm/p6_collection.ahfl:31-35` (`struct Frame { items: List<Int>(4); }`)
  pin this on lowering-clean programs. Encoding: **one element per `fields` slot,
  emitted as JSON `null` when the slot is `kInvalid`** — never the bare integer
  `4294967295`. The `null` is mandatory, not omissible: the array's length is
  verifier-pinned to `fields.size()` (`core_verify.cpp:121-125`), and R1
  byte-equality would fail if a slot could be dropped rather than encoded. This is
  the ONE deliberate use of `null` in the document; §2's "absent optional omits the
  field" rule governs *fields*, not *array elements*, so the two rules do not
  collide.
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
  (`ir_json.cpp:653-667`), and omitted entirely when absent.
- An `std::optional<uint64_t>` (`CoreVtNominal::capacity`) is emitted as a bare
  integer, matching `collection_capacity` (`ir_json.cpp:672-677`), and omitted when
  absent.
- `CoreVtNever` / `CoreVtUnit` / other payload-free nodes emit `kind` alone.

**`capabilities[]`** — `name`, `symbol_ref`, `effect_kind`, `param_types`,
`return_type`, `source_range`. The signature is the fully materialized logical
signature in the program-global arena; the source-level parameter names are
intentionally absent (retaining both would be two signature SSOTs —
`core_ir.hpp` §CoreCapabilityDecl).

**`agents[]`** — `name`, `symbol_ref`, `states`, `initial`, `finals`,
`transitions`, `input_type`, `context_type`, `output_type`, `context_kind`
(`"unit"|"struct"`), `capabilities`, `source_range`.

- This order is `CoreAgentDecl`'s declaration order (`core_ir.hpp:233`:
  `input_type`, `context_type`, `output_type`, then the `ContextKind` enum, then
  `context_kind`) — `context_kind` trails `output_type`, it does not precede
  `context_type`. §2's Field-order row is normative and this table matches it.

- `states` is an array of strings; `initial` / `finals[]` / `transitions[]`
  (`from`, `to`) are `CoreStateId` integers into `states`.
- `context_type` is emitted **only** when `context_kind == "struct"` — a Unit
  context has `context_type == kInvalid` and the **field is omitted entirely**
  (the §2 absent-optional rule), never `null` and never `4294967295`. This is a
  *whole-field* omission and is the only encoding for it; `kInvalid` inside
  `field_nominal_types` (above) is the separate *array-element* case, which uses
  `null` because its length is pinned. One kInvalid, one encoding per position
  class.

**`flows[]`** — `agent` (`CoreAgentId`), `agent_name`, `target_ref`,
`value_count`, `exprs`, `value_types`, `coercion_plans`, `patterns`, `states`.

- `states[]`: `state` (`CoreStateId`), `state_name`, `policy`, `body`.
- `policy`: `retry_limit`, `retry_on`, `timeout` — each omitted when absent.

**`workflows[]`** — `id`, `name`, `symbol_ref`, `input_type`, `output_type`,
`value_count`, `exprs`, `value_types`, `coercion_plans`, `patterns`, `nodes`,
`return_region`.

- This order is `CoreWorkflowDecl`'s declaration order (`core_ir.hpp:1166`) with
  one documented promotion: `value_count` is emitted with the shell (after
  `output_type`) rather than between `exprs` and `value_types` as declared, so a
  reader meets the density witness for the `value_types` array together with the
  body's other shell facts. The same promotion is applied in `flows[]` below.
  §2's Field-order row names §4–§5 as the normative copy precisely so this
  deviation is legal and unambiguous.

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
  (`core_ir.hpp:74`), `CorePatternId` (`:123`), `CoreCoercionPlanId` (`:197`), and
  `CoreValueId` (`:68`) are only meaningful inside one flow/workflow
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
`CoreMatchStmt::fallback_region`), mirroring the `unique_ptr` ownership. The reader
is therefore a recursive descent over untrusted input, so the reader enforces an
explicit **maximum region-nesting depth** and fails closed on exceed.

The depth bound is justified by the reader being an **untrusted-input admission
boundary**, not by any claim about the in-memory IR. Stated plainly, because the
opposite has been assumed before: at this commit **the recursive region walks are
recursive and NO depth bound exists anywhere on the Core-IR region path.**
`core_region_exit` (`include/ahfl/compiler/ir/core_ir.hpp:980`) recurses into
`*node.then_region` / `*node.else_region` / `*node.fallback_region` via its
per-alternative `CORE_REGION_EXIT_*` handlers; the verifier's `verify_region`
(`src/compiler/ir/core_verify.cpp:2049`) recurses at `:2210`, `:2217` and `:2390`,
and `for_each_region_path_expr` (`:2875`) recurses into then / else / guard / body /
fallback through its `CORE_REGION_PATHS_*` handlers. A `grep` for
`kMaxDepth`/`max_depth`/`nesting_depth` across `src/compiler/ir` and
`include/ahfl/compiler/ir` returns **nothing** — no bound of any kind exists on this
path. (The iterative walks that do exist — the expr-arena deferral at `:1315`, the
expr-use collector at `:1943`, the workflow DAG at `:2707` — are different graphs
and do not protect the region tree.) The reader is thus not "the one recursive
component without a bound": it is the *first* one to get a bound, and the bound
must be enforced regardless of what the in-memory IR does, because a
whitespace-cheap source with ~10^5 nested `if`s otherwise overflows the native
stack in the reader.

The two pre-existing unbounded recursive walks above are a **known, tracked
follow-up** (a source-nesting cap at lowering, or an iterative rewrite of
`core_region_exit` / `verify_region` / `for_each_region_path_expr`), not an
implicit claim that recursion is already safe.

Statement field order: `CoreStmt` emits `kind` then `source_range`, then the
alternative's own fields. `CoreMatchStmt` **always** emits `fallback_region`
("never null in a well-formed program"); `CoreIfStmt` emits `else_region` only when
non-null; `CoreMatchArm` emits `guard_region` only when non-null. The writer's
"always emit" set is exactly the set the verifier requires to be non-null — one
rule, not two.

## 6. Arena canonicalization and index remapping (decision 3)

The program-global `value_types` arena is a **true hash-cons**: `ValueTypeArena`
(`src/compiler/ir/core_lower.cpp:686-690`) interns structurally, children are interned
**before** their parent so a node's key contains only child ids, and storage is
deterministic append-only, so two lowerings of the same input produce the same
arena order. Three rules follow, and they are normative for the projection:

### 6.1 Write in arena order, and prove the order is canonical

`value_types` is serialized **in arena order** (index `0..N-1`), one element per
slot, no re-sorting and no dedup at write time. The determinism is not assumed — it
is **already enforced** at the consumption boundary:
`verify_value_types` (`src/compiler/ir/core_verify.cpp:3018`) rejects (a) any child id `>=` the
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
   that remap. The rule is exhaustive by construction, not a hand-maintained list:
   **every `CoreValueTypeId` in the serialized document that indexes
   `CoreProgram::value_types` is rewritten, wherever it lives.** A field of that
   type added to any struct below is automatically in scope; a future field cannot
   silently fall outside the rewrite. The owning sites today are:
   - **arena-internal children** — `CoreVtNominal::base` (a `CoreTypeId`, NOT
     remapped — nominal ids are table positions, §6.3) and `args`,
     `CoreVtTuple::elements`, `CoreVtFn::params`/`ret`,
     `CoreVtClosure::signature`/`captures[].value_type`
     (`core_ir.hpp:1390`/`:1398`/`:1408`/`:1422`/`:1433`).
   - **non-arena references in the program-global tables** —
     `CoreCapabilityDecl::param_types`/`return_type` (`:320`/`:322`),
     `CoreInstanceDecl::dispatch_types` (`:1513`),
     `CoreCoercionPlanNode::source`/`result` (`:529`/`:530`, interned through the
     *same* program-global interner: `normalize_adjustment_plan`'s
     `intern_plan_type` at `src/compiler/ir/core_lower.cpp:3096-3099` calls
     `interner_`, which is `intern_value_type` over `shared_arena`,
     `core_lower.cpp:3564`/`:3678`/`:3733-3736`), and
     `CoreMemberTypeTemplateNode::concrete` (`:1311`, interned into the shared
     arena by `TypeEnv::finalize_member_templates(shared_arena)`,
     `core_lower.cpp:3718`).
   - **per-body references** — every `CoreFlowDecl::value_types` /
     `CoreWorkflowDecl::value_types` entry (`:1121`/`:1177`), and
     `CoreExpr::result_type` (`:628`).
   Leaving `CoreCoercionPlanNode::source`/`result` or
   `CoreMemberTypeTemplateNode::concrete` unremapped is the exact failure mode this
   bullet exists to prevent: they sit in program-global objects and their ids shift
   on read, so a reordered-but-structurally-valid document would point coercion
   proof endpoints and member-template concs at wrong arena slots, and R2 could not
   catch it (`core_program_equal` would compare the unremapped ids consistently on
   both sides).
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
`ir_json_round_trip.cpp` "IR JSON round-trips byte-identically for every golden"
(the quoted comment is at `:65`).

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
`kInvalid`-valued id in a required position (which the writer never emits, §2); a
`null` / out-of-range entry in `field_nominal_types` where §4 forbids it;
`value_count != value_types.size()` in a body; over-deep region nesting; an unknown
`kind` string; and any node violating its per-kind field mask (an unused payload
field present, or a used one absent). "Silently defaulting" is forbidden — the
single-layer reader's historical demotion of an unknown kind to `nullopt` is not
replicated where a specific cause is knowable.

**Reader return type (pinned here so B3 does not reopen it).** The single-layer
reader returns `std::optional<ir::Program>`
(`include/ahfl/compiler/ir/lowering.hpp:45`), but `optional`/`nullopt` carries
**no typed reason and no source range** — precisely the shape rejected above. The
Core reader therefore returns a **result struct**, mirroring the repo's other Core
sub-systems (`CoreVerifyResult`, `CoreLayoutBuildResult`):

```cpp
struct CoreJsonDiagnostic {
    std::string code;        // stable code, e.g. "core.json.NONCANONICAL_ARENA"
    std::string message;     // human-readable, actionable (Principle 5)
    SourceRangeOpt range;    // the document's range where it supplies one
};
struct CoreJsonParseResult {
    std::optional<CoreProgram> program;          // engaged iff `ok()`
    std::vector<CoreJsonDiagnostic> diagnostics;
    [[nodiscard]] bool ok() const noexcept { return program.has_value(); }
};
[[nodiscard]] CoreJsonParseResult parse_core_ir_json(std::string_view json);
```

A caller must be able to distinguish a wrong-`layer` document from a forward
value-type reference, and §9 promises "never a partially populated program" — both
require the typed struct. `SourceRangeOpt` (not `SourceRange`) is used because the
JSON document supplies no range for an envelope-level failure.

## 8. Layer boundary for the AHFL-IR projection (amended)

> **Amendment (2026-09-29).** This section originally recorded a
> MARK-DEPRECATED decision for the single-layer projection, with removal bound to
> KR6.8 (evaluator retirement). That decision is **superseded**: once the
> three-layer IR tower settled, `ir::Program` remained the permanent
> verification/orchestration layer consumed by SMV, assurance and formal, and its
> JSON projection is that layer's first-class inspection surface. Keeping a
> "deprecated but retained" label on a live layer's projection created exactly the
> transitional coexistence state that repository Principle 1 forbids (no
> deprecation periods, no old-and-new coexistence). The label was removed from the
> CLI help and reference docs; the projection itself, its writer/reader, CLI
> flags, artifact ids and goldens are unchanged and permanently supported. The
> original mark-deprecate rationale is preserved in RFC 0026 history; this
> amendment states the settled design, it does not delete the projection.

**Decision: the AHFL-IR layer (`ir::Program`) keeps its own JSON/text projection
(`emit ir-json` / `emit ir`, `ahfl.ir.v2`) permanently, alongside the Core-IR
layer's projection (`emit core-ir-json`, `ahfl.core.v1`). The two project
different layers; neither replaces or deprecates the other.**

Consequences:

- The single-layer writer, reader, CLI flag, artifact id and golden files stay as
  they are, guarded by `ahfl.ir.json_round_trip`; the Core projection is guarded
  by `ahfl.ir.core_json_round_trip`. Both guards are permanent.
- No `BREAKING CHANGE:` footer is associated with either projection: nothing is
  being removed.
- Analogy: rustc exposes `--emit=mir` and `--emit=llvm-ir` simultaneously. A
  compiler offering one inspection projection per tower layer is standard
  practice, not technical debt; the projection count is not a migration surface.

Historical rationale for the original mark-only decision (for context): before
the layer split was complete the single-layer projection *was* the AHFL-IR
projection (KR6.3 alias-first), so removing it would have deleted a live layer's
projection; binding a hypothetical removal to KR6.8 was meant to avoid that. With
the tower settled, the layer is permanent rather than pending retirement, so the
deprecation framing itself was the defect.

## 9. Admission model

The new reader is a **hard admission boundary**, not a best-effort loader. A parsed
document is accepted only if it (a) passes envelope checks (§1), (b) parses with no
unknown kind and no missing/extra per-kind field, (c) rebuilds the value-type arena
to the identity remap (§6.2), (d) satisfies the structural rules the Core verifier
already enforces, and (e) is within the region-nesting depth bound. Failure is a
typed error (`CoreJsonDiagnostic`, §7) with a source range where the document
supplies one, returned in `CoreJsonParseResult` — never a partially populated
program, and never a bare `nullopt`.

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
| **B1** | Envelope + program-global tables: `CoreJsonPrinter` for `types` / `value_types` / `capabilities` / `agents` / `instances`, the shared 2-space writer base, the X-macro `.def` node lists for the hand-declared Core variants (§3 rule 2), and the `kind`/field-order tables of §4 | Unit tests over hand-built minimal programs; the writer is `[[nodiscard]]`-gated, fails closed on a `kInvalid` in any required-valid position, and encodes the one legal `kInvalid` (`field_nominal_types`, §4) as `null` |
| **B2** | Per-body nested arenas: `flows` / `workflows` incl. `exprs` / `patterns` / `coercion_plans` / dense `value_types` and the recursive region/statement projection (§5) | Unit tests per node family; the verifier-clean corpus serializes without error |
| **B3** | Reader `CoreJsonParseResult parse_core_ir_json(std::string_view)` (signature pinned in §7) with interning rebuild + identity-remap assert (§6.2), fail-closed admission (§9), `core_program_equal` (§7 R2), and the R1/R2 byte-exact round-trip corpus test | New `ahfl.ir.core_json_round_trip` test; negative-reader cases per §7 (each asserting the typed diagnostic code, not just `!ok()`); the existing `ahfl.ir.json_round_trip` stays green |
| **B4** | CLI surface + docs: `emit core-ir-json` command kind + artifact id registered in `config/product-scope-freeze.json` (the gate is currently lifted — RFC 0012 is `stabilized`, `scripts/check-product-scope-freeze.py:138` — but the catalog entries are still the documented registration point), plus the layer-boundary wording in `docs/reference/ir-format.zh.md` / `docs/reference/cli-commands.zh.md` (§8) | New CLI golden fleet alongside the existing `ahflc.emit_ir_json.*` ones; `ahfl.docs.ir_sync_gate` stays green |

Every slice keeps the AHFL-IR projection and all its goldens byte-identical
(§8): both layers' projections are permanent. No slice branches on a feature
flag: the layered projection is purely additive.

## 11. Rejected alternatives

| Alternative | Rejected because |
|---|---|
| One document per layer, one file per table | Breaks program-global cross-references (`dispatch_types` → `value_types`, flow `target` → `agents`) into a second cross-document id scheme; violates Principle 2 |
| Hoist per-body arenas to a top-level pool | The body-relative ids would need fabricated per-owner namespacing, and an id could resolve against the wrong body's arena |
| A separate JSON-envelope version constant | Parallel SSOT with `CoreProgram::format_version`; the Core layer has exactly one interchange version |
| Trust serialized ids on read | Would accept a reordered / hand-edited arena and would create a second notion of structural equality; §6.2's rebuild + identity assert is the single decision point |
| Re-sort or dedup `value_types` at write time | The arena is already canonical (verifier-enforced); re-sorting is a second canonicalizer that can disagree with the hash-cons |
| Re-run the full Core verifier inside the reader | The reader is an admission boundary for a document's shape, not a substitute for verification; conflating them makes round-trip failures ambiguous |
| Delete the single-layer projection | It is the AHFL-IR layer's permanent projection (KR6.3 alias-first); the three-layer tower keeps one inspection projection per layer (§8, amended) |
| Pretty-print with `null` for absent optionals | Diverges from the single-layer writer and makes "absent" and "present-but-null" two encodings of one state. NOTE: this rejects `null` as the encoding for an ABSENT FIELD only. `field_nominal_types` (§4) has no absent state — the slot exists and the array length is pinned — so it uses `null` for `kInvalid`, which is the ONE sanctioned `null` in the document |
