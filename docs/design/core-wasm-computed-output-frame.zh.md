# Core-Wasm Computed Output-Frame ABI (P6-7) -- Design

> Status: **LANDED** (RFC 0026 P6-7 / KR6.7). V1 rungs A-E landed
> 2026-09-25..28; the v2 amendment (`core-wasm-frame-bridge-v2.zh.md`)
> extended and superseded parts of this document, landing V2-A..V2-E on
> 2026-09-28 (HEAD `2cd6dbf6`). This document IS the independent
> review of the value-returning `run2`-class convention that KR6.5 E1
> explicitly deferred and refused to pre-approve
> (`docs/rfcs/0026-ir-tower-and-execution-model.zh.md` E1 entry: "值返回的
> `run2` 类方案留独立评审、不 pre-approve").
>
> Scope: the computed output-frame ABI that lets a P6 computed **final**
> handler return a real value over the Core-Wasm boundary, plus the inverse
> input-frame packing that projected/constructed node inputs will consume.
>
> All `file:line` anchors were verified against `develop` at HEAD `18ce34af`.
> Where a symbol is named alongside its line, a refactor that moves the line
> does not silently invalidate the anchor.

## 0. Problem, facts, scope

### 0.1 What works today

The KR6.5 run2 boundary carries **opaque canonical value_json bytes**:

* `run2(in_ptr:i32, in_len:i32) -> (status:i32, out_ptr:i32, out_len:i32)`
  (`core_wasm_codegen.cpp:7824`, `make_run2_body`).
* An identity final is exactly two ANF statements -- canonical input let +
  return of the same SSA, `input_type == output_type`
  (`validate_identity_final`, `core_wasm_codegen.cpp:729-769`); run2 returns
  the borrowed input `(ptr,len)` untouched. This alias is the single narrow
  E1 exception, never a general precedent.
* A capability final forwards the imported frame's `(ptr,len)`
  (`append_capability_return`, `core_wasm_codegen.cpp:7765-7822`).
* The Node conformance host allocates the scenario's canonical `input_wire`
  bytes, calls run2, and decodes the returned span as the output observation
  (`tests/conformance/node_embedded_host.mjs:71`, `:207-210`, `:292-310`).

The P6 (KR6.6) slices added real computation: non-final computed handlers,
aggregate frames, bounded collections, outlined fns, closures. A handler that
projects a field of the raw P4-D input frame latches `reads_raw_input_frame_`
(`core_wasm_codegen.cpp:4582-4584`), the agent descriptor then reports
`CoreWasmFrameContract::RawP6Frame` (`core_wasm_codegen.cpp:8969-8970`), and
the conformance producer SKIPs the case with code `p6-7`
(`tests/conformance/wasm_engine.cpp:272-278`).

### 0.2 What is missing

Two pinned conformance cases emit real modules but cannot be canonically
observed: `p6_aggregate` and `p6_collection`
(`tests/conformance/cases/p6_{aggregate,collection}.case.json`,
`node_observation_skip="raw_p6_frame_awaits_p67"`). The module consumes P4-D
shaped bytes at fixed reserved regions -- not JSON -- and nothing:

1. packs canonical input JSON into those P4-D regions (the **input**
   direction);
2. returns a computed final value's location to the host (the **run-value**
   return convention E1 deferred);
3. turns the post-run P4-D bytes back into canonical value_json (the
   **output** direction).

The census is machine-pinned both directions: `kExpectedAgreed = 14`,
`kExpectedSkipped = 7` (`conformance_wasm_node_runner.cpp:63-64`), and the
manifest-declared skip reason must equal the compiler-computed one in both
directions (`wasm_eligibility.cpp:206-250`).

### 0.3 Established facts this design builds on

* **One fixed page.** Every module declares exactly one 64 KiB linear memory,
  min 1 page, no declared maximum
  (`core_wasm_abi_constants.hpp:21-32`). The reserved P6 regions are
  `kP6AggregateInputBase = 1024` (cap 3072), `kP6AggregateContextBase = 4096`
  (cap 3072), `kP6AggregateScratchBase = 7168` (cap 9216),
  `kP6CollectionBackingBase = 16384` (extends to page end)
  (`core_wasm_abi_constants.hpp:54-106`). The capability-workflow node-event
  region occupies `[1024, +8 + node_count*40)` on the workflow lane only
  (`core_wasm_abi_constants.hpp:133-161`).
* **P4-D is the only physical-layout authority.**
  `CoreLayout{size,align,is_zero_sized,shape}` with shapes Scalar(I32/I64/F64),
  Bytes16, PtrLen(size 8/align 4), Closure(8), Struct(field_offsets +
  inline edges), Enum(tag_size 4, payload_offset, per-variant payloads),
  Container(element/value edges, capacity, stride, value_offset,
  backing_size), Uninhabited (`core_layout.hpp:29-136`). Codegen never
  re-derives a size, offset, or alignment.
* **The wire schema is the logical wire authority.** 15-node
  `CoreWireSchemaShape` variant, names/order/capacities/scale/bounds live here
  (`core_wire_schema.hpp:26-152`); it reaches the module in exactly one custom
  section `"ahfl.wire-schema.v1"` with `AHFLWS...` payload, present iff the
  agent has reachable capability imports (`core_wasm_codegen.cpp:149-150`,
  `:8077-8091`, `:9171-9200`). A generic host may consume it only through the
  un-forgeable `VerifiedWireSchemaBinding`
  (`core_wire_migration.hpp:99-117`, `:180-210`).
* **Decode exists, encode does not.**
  `wire_codec::decode_json(JsonValue, VerifiedWireSchemaBinding) -> Value`
  walks schema shapes with a fail-closed family ("wire-codec: ...", schema-only
  phrasing, no payload echo) and `validate_value` is its non-rebuilding twin
  (`core_wire_codec.hpp:84-91`, `core_wire_codec.cpp:141-557`). There is no
  encode direction.
* **Canonical value_json has one SSOT.** `evaluator::value_to_json`
  (`value_json.cpp:216-220`): compact, no whitespace; struct emits `_type`
  first then FieldMap lexicographic fields; enum fixed order
  `_enum,_variant,_payload,_named_payload` with empty payloads omitted; sets
  and map keys pre-canonicalized by `make_set`/`make_map`; integers via
  locale-independent `std::to_chars`; floats via the shared shortest-round-trip
  `json::format_wire_float` (integral float keeps its decimal point); decimal
  and duration emit their stored spelling verbatim; UUID/Timestamp are exact
  marker objects (`value_json.cpp:39-208`). The conformance canonicality
  contract is pinned in `core-ir-kr6-7-conformance-suite.zh.md:119-145`.
* **Spelling is centralized.** Decimal/Duration parse/validate/spell only
  through `scalar_spelling` (`scalar_spelling.hpp`); the builtin canonical
  decimal spelling is `spell_builtin_decimal(mantissa, scale)` = `s<scale>:
  <mantissa>`; duration bare millis is `std::to_string(i64)`. Parsing is for
  validation only -- inbound spellings are preserved verbatim today.
* **The resume host already trusts run2 tuples against a whole-page read and
  writes memo bytes by instance-lifetime bump allocation**
  (`core_wasm_resume_host.cpp:114-125`, `:378-416`); the engine port
  contract is `alloc_then_write` / `invoke_run2` / `read_whole_memory`
  (`core_wasm_resume_engine.hpp:121-150`). The F3 gate pins the one-page
  declaration before instantiation (`core_wasm_resume_host.cpp:91-102`).
* **Wire-size bounding already exists.** `max_canonical_json_size(binding)`
  gives a conservative, never-underestimating u64 bound over a productive-edge
  schema graph (`core_wire_canonical_size.hpp`).
* **Capability results arrive as wire JSON**, while raw P6 input lives in
  fixed regions; the two representations never mix in a v1 computed module
  (decision in §3.4).
* **No Data section exists** in any emitted module; bytes below address 1024
  are unused. Low globals live outside linear memory.

### 0.4 Non-goals

* No second JSON serializer: the module never builds JSON, and the host's
  canonical bytes come from the existing `value_to_json` SSOT (§2).
* No change to E1-E3, capability-final, or workflow-forwarding modules:
  byte-identical artifact requirement is absolute (§4.3).
* No new CLI command, no new emit artifact (product scope-freeze unaffected).
* No Pending/ERROR semantics for a computed run: the P6 computation subset has
  no effect, so its only non-OK exit is a Wasm trap.
* F64 arithmetic, map/raw-get Core counterparts, the remaining
  `CoreUnsupportedExpr` producers, and projected workflow-node scheduling stay
  on their own ladders (§11).

## 1. Decision summary

| # | Decision |
| --- | --- |
| D1 | **The host, not the module, owns the output frame.** After `runv` returns, host-side code walks the output value's P4-D bytes (verified `CoreLayoutTable` + verified boundary wire binding), builds an `evaluator::Value`, and renders canonical bytes with `value_to_json`. The module never serializes. |
| D2 | Canonical encode = P4-D bytes + `CoreLayoutTable` + verified wire binding -> `evaluator::Value` -> `value_to_json`, with the same fail-closed family as `decode_json`. A deterministic `CoreLayoutTable` custom section is added so the host never re-derives layout. |
| D3 | Add a new additive export **`runv() -> (status:i32, value_ptr:i32)`**, present ONLY in a module that has a raw-frame input or a computed final. A computed value lives at the fixed output frame base; an identity final names the input frame base. `run2` is untouched; every module whose descriptor frame contract is not the new p6_frame (identity, capability, workflow, and closure-using FB modules) stays byte-identical. |
| D4 | Input is the exact inverse: canonical JSON -> `decode_json` -> `Value` -> P4-D regions (`pack_input_frame`). Variable-length input payloads (String bytes) get one bounded frame-payload arena; bounded collections keep the backing region. Workflow node packaging consumes this packer unchanged on the next slice. |
| D5 | One fixed new reserved region: `kP6AggregateOutputBase = 12288` (8-aligned), capacity `16384 - 12288 = 4096`, carved out of the current scratch window (scratch shrinks 9216 -> 5120). Input-reached containers get disjoint per-container backing placements (sum, not max); one frame-payload arena is co-located above the packed backing high-water and below the relocated construct heap; every extent is compile-time gated with the existing RESOURCE family. |
| D6 | Frame boundaries carry inline shapes: scalars (4/8), PtrLen (8 inline bytes = two i32 words naming the payload arena/backing), Bytes16 (16), aggregate/enum/container addresses (one i32). Across **fn** boundaries every multi-word value passes by i32 address (Ptr to its frame/arena slot), including PtrLen -- lifting the FB-design's "multi-word fn args await P6-7" reservation. |
| D7 | The two `raw_p6_frame_awaits_p67` cases leave the Node skip in rung E; the Node-vs-evaluator differential agrees only AFTER the wasm producer stops classifying them `RawP6FrameAwaitsP67` (the runv/pack/encode rungs A-E land). Rung E required evaluator-engine/manifest groundwork -- the capability-bridged dispatcher resolving bare builtin hooks (`list_raw_get`/`list_raw_length`/`map_raw_get`) via the builtin table, `engines.evaluator` flag flips, and evaluator-runner blessings -- which landed in the fix-forward revision of this gate (§9). The census pins move 14/7 -> 16/5 in the exact commit that flips the descriptor contract. `raw_p6_frame` is renamed `p6_frame` and gains both real directions. |

## 2. D1 -- Frame ownership and lifetime

### 2.1 Decision: host-side encode after the run

A computed final returns a value whose physical home is the module's linear
memory. There are two candidates for producing canonical value_json:

* **(A) In-module serialization.** The wasm module grows a JSON writer:
  string escaping, struct/enum framing, a float shortest-round-trip
  implementation, decimal/duration spelling, set ordering. It bumps an output
  buffer and run2 returns its `(ptr,len)`.
* **(B) Host-side encode from P4-D bytes after the run.** The module only
  materializes its result into a fixed output frame (§4); the host reads the
  verified whole page, walks the bytes with P4-D + the wire binding, and
  renders canonical JSON.

**Decision: (B).**

Reasons:

1. **One canonical SSOT, already proven.** (B) terminates in
   `evaluator::value_to_json` -- the exact function that renders the
   evaluator differential reference, conformance observations, and durable
   memo hashes (`value_json.hpp:18-24`). Byte equality with the evaluator lane
   is then true *by construction*; (A) would reimplement escaping, key order,
   `format_wire_float`, and decimal spelling inside wasm and would need a
   permanent differential test proving two serializers never diverge.
2. **Decode symmetry.** The trust boundary already has a host-side,
   schema-guided JSON direction (`decode_json`) over a verified binding, with
   its fail-closed family and P0 payload-echo discipline. The output direction
   is the inverse walk under the same verified authorities; it belongs beside
   it in `src/runtime/engine/`, not in the artifact.
3. **No locale/host traps in the artifact.** Locale-independent integers and
   shortest-round-trip doubles are properties of the C++ host
   (`value_json.cpp:31-37`, `format_wire_float`); wasm has no locale, but an
   in-module f64 formatter is a large, third copy of the ryu-class logic.
4. **Capacity.** Canonical JSON for a struct is typically larger than its
   P4-D bytes (key names, escaping, punctuation). Reserving a JSON output
   buffer in the 64 KiB page competes with the reserved frames, backing
   region, and construct heap for every module. The host renders outside the
   page after `read_whole_memory()`, exactly as the resume host already copies
   the L5 span from the post-run page (`core_wasm_resume_host.cpp:380-416`).
5. **Trust posture is unchanged.** The post-run page is already treated as
   untrusted evidence (the node-event tamper path, the whole-page length gate,
   the out-of-page tuple-pointer check). Walking output bytes under the
   verified layout adds one more checked read, not a new trust channel.
6. **No string-building capability exists in-module.** The P6 subset admits
   only Bool/Integer literals (`core_wasm_codegen.cpp:4208-4212`); there is no
   string concatenation or buffer writer, and adding one to every artifact
   violates the fixed-page budget for a feature only the boundary needs.

### 2.2 Lifetime

* The output frame is a **fixed reserved region** (§5), written by the final
  handler during `runv` and read by the host immediately after the call
  returns, before the fresh instance is torn down -- the same lifetime rule
  the resume host uses for the output copy
  (`core_wasm_resume_host.cpp:405-416`).
* The frame holds the root value's inline P4-D bytes. Everything variable
  (String payload, collection backing) lives in regions that are stable for
  the whole run (frame-payload arena, backing region); the output frame only
  names them.
* The host never writes through `value_ptr`; it reads once from the verified
  whole-page span.

## 3. D2 -- Canonical encode algorithm

### 3.1 Authorities and shape of the code

New runtime unit, beside `core_wire_codec`, in namespace
`ahfl::runtime::core_wire_codec` (same translation unit family, same
`VerifiedWireSchemaBinding` input):

```
WireEncodeResult encode_value_json(
    std::span<const uint8_t> page,           // the WHOLE fixed 64 KiB page
    uint32_t root_addr,                      // runv's value_ptr
    const VerifiedCoreLayoutBinding& layout, // verified P4-D table + root id
    const VerifiedWireSchemaBinding& wire);  // verified wire table + root node
// -> canonical value_json bytes (== evaluator::value_to_json of the decoded Value)
```

The algorithm is two phases:

1. **Walk**: page bytes + layout edge + wire node, in parallel, producing a
   native `evaluator::Value`. Layout supplies physical facts (offset, width,
   repr, stride, backing extent, tag/payload placement); the wire node
   supplies logical facts (names, order, capacity, scale, bounds, option/
   sequence/map distinction). The two tables are positionally joined
   (§3.5) and their structural consistency is verified at admission.
2. **Render**: `evaluator::value_to_json(value)`. No rendering logic is
   written in this unit. This is what makes the output observation share one
   encoder with the evaluator reference.

The inverse packer (§6) is the exact mirror: `decode_json` -> `Value` ->
walked into the regions. Both walks share one per-shape dispatch table.

### 3.2 Per-shape rules (read phase)

Every read is bounds-checked against the fixed page and against the region the
address is allowed to name (input/output frame, frame-payload arena, backing
region). A wrapping or out-of-region address is a fail-closed shape error,
never a wild read.

| Wire shape / Value | P4-D read | Value produced | Canonical JSON |
| --- | --- | --- | --- |
| Unit | zero-sized; no read | `UnitValue` | `null` |
| Never | unreachable; a live value is an error | -- | -- |
| Bool | i32 @ base, must be 0/1 | `BoolValue` | `true`/`false` |
| Int (narrow bounds) | i32, sign-extended | `IntValue(i64)` | `to_chars` |
| Int (wide) | i64 LE | `IntValue` | `to_chars` |
| Float | f64 LE, must be finite | `FloatValue` | `format_wire_float(v, json_mode=true)`; integral keeps `.0` |
| String | PtrLen: ptr@0, len@4; span inside frame-payload arena, len within schema bounds (UTF-8 bytes) | `StringValue` | JSON-escaped quoted string |
| Decimal | i64 mantissa word + schema-fixed `scale` | `make_decimal(spell_builtin_decimal(mantissa, scale))` | quoted builtin spelling `s<scale>:<mantissa>` |
| Duration | i64 millis | `make_duration(std::to_string(ms))` | quoted bare-millis spelling |
| Timestamp | i64 unix-ms | `make_timestamp(ms)` | `{"_timestamp":<to_chars>}` |
| Uuid | Bytes16 at base | `make_uuid(32 lowercase hex)` | `{"_uuid":"..."}` |
| Option (ordinary enum `std::option::Option`) | enum tag @ 0; None -> tag 0; Some -> child at `payload_offset` | `make_option_none/some` | `null` or the compact inner encoding (matches the decode arm, `core_wire_codec.cpp:286-299`) |
| List | Container header `(ptr@0,len@4)`; `len <= capacity`; elements at `ptr + i*stride` | ordered `Value`s -> `make_list` | positional array |
| Set | same walk, `len <= capacity` | `make_set(values)` -- dedup + canonical sort; a structurally-equal duplicate pair in module memory is an error (module memory is untrusted evidence), mirroring decode's duplicate rejection | canonical array |
| Map (String keys) | header; entries stride apart with key PtrLen at `0` and value at `value_offset`; sorted by key on read; duplicate key is an error | `make_map(pairs)` | object, sorted keys |
| Struct | tagless; field i at `field_offsets[i]` per `field_layouts[i]`, wire name `schema.fields[i].wire_name`; field counts must agree | `make_struct(wire_name, fields)`; the FieldMap sorts names | `{"_type":wire_name,...lex fields}` |
| Enum | tag i32 @ 0 must name a declared variant ordinal; Unit variant is tag only; Tuple payload is an inline aggregate at `payload_offset` read against that variant's slots (positional); Struct variant same with slot names | `make_enum(wire_name, variant, payload/named)` | fixed `_enum,_variant,_payload,_named_payload` order, empty payloads omitted |
| Tuple | Struct-shaped inline aggregate, positional | `make_list` of elements (the runtime models tuples as positional lists, `core_wire_codec.cpp:530-555`) | positional array |
| Fn / Closure | not wire-projectable: a frame root or field of these shapes is an admission-time error (the projector already refuses them inbound) | -- | -- |

### 3.3 Scalar-spelling asymmetry is deliberate and resolved at the boundary

Physical Decimal is one i64 word; the scale is not stored per value, it is the
schema node's fixed `scale`. Physical Duration is one i64 millisecond count.
The module therefore has exactly one renderable spelling per value:

* Decimal -> `spell_builtin_decimal(mantissa, schema.scale)` (the
  `s<scale>:<mantissa>` form the decimal builtins themselves produce,
  `builtins.cpp:1416`);
* Duration -> bare millis (`std::to_string(ms)`), the form
  `duration_between` produces.

The source-literal families (`1.50d`, `5s`) exist only as inbound spellings,
which decode preserves verbatim for memo fidelity. They are not
module-producible: the physical word carries no spelling. Consequences:

* A differential conformance case whose computed output is Decimal/Duration
  must pin the builtin/bare-millis wire spelling (arithmetic or
  `decimal_raw_*` results), never a source literal -- the canonicality gate
  accepts any quoted string structurally, so this is a fixture-authoring rule,
  not a gate change.
* Inbound the packer CANNOT preserve an arbitrary legal spelling: the physical
  word carries no spelling, and even an identity final on this lane therefore
  renders the builtin/bare-millis form. The packer consequently REQUIRES an
  inbound p6-frame Decimal spelling to already be the builtin canonical form
  (`parse_builtin_decimal` accepts it and `spell(parse(x)) == x`) and an
  inbound Duration spelling to be bare millis (`parse_bare_millis`); a
  source-literal spelling (`1.50d`-family, `5s`-family) fails closed with a
  precise `wire-frame: p6 frame input decimal/duration must use the builtin
  canonical spelling` reason rather than silently normalizing. This is the
  same discipline as the Float "no int widening" rule: the conformance
  manifest pins the one physical-round-trippable spelling. The evaluator
  differential stays byte-exact because the lane's input bytes already carry
  the builtin form, so identity passthrough renders the same bytes on both
  engines. Two different inbound spellings are never silently merged -- the
  non-canonical one is rejected at the boundary.
* This packing requirement is lane-scoped: the general `decode_json` policy
  still accepts and preserves both families verbatim (memo fidelity is
  unchanged); only the P4-D packer imposes the single-form rule, because only
  it erases spelling.
* This matches `scalar_spelling`'s "parse for validation, never normalize
  inbound" contract while giving the output direction its single canonical
  form.

### 3.4 Fail-closed family

Reusing decode's discipline (`core_wire_codec.cpp:141-557`):

* fixed string family `"wire-codec: ..."` / `"wire-frame: ..."`, naming the
  expected schema-side shape (`struct '<wire_name>'`, variant ordinal range,
  field arity) and NEVER echoing page bytes, decoded strings, or addresses;
* every offset/extent computed in checked u64 with an explicit region
  membership test before the read;
* tag/ordinal out of declared range, enum payload kind mismatch, field/arity
  mismatch, `len > capacity`, non-canonical duplicate Set element / Map key,
  non-finite f64, Bool word not in {0,1}, a non-zero padding word where the
  layout requires padding (write side zeroes padding; a non-zero word on read
  indicates tampering), and a layout/wire structural disagreement all fail
  with no partial bytes;
* the root pointer runv returns is authorized against the region the ADMITTED
  DESCRIPTOR declares for this module's final kind -- runv returns only
  `(status, value_ptr)` with no identity/computed discriminator, so the
  descriptor is the sole source of the authorized root set: a descriptor-
  declared identity final authorizes exactly `kP6AggregateInputBase` (1024),
  and a descriptor-declared computed final authorizes exactly
  `kP6AggregateOutputBase` (12288). A value naming any other address, or the
  wrong fixed base for the declared kind, fails closed. The final-kind
  discriminator is a descriptor field (§8), not inferred from the pointer
  value; the resume host's out-of-page tuple check
  (`core_wasm_resume_host.cpp:407-412`) is the existing region-membership
  pattern.

A computed run has no PENDING/ERROR result: any non-OK is either this
fail-closed rejection on the host side or a genuine Wasm trap classified as
today's `Run2Trapped`.

### 3.5 Why the host needs a layout section

The wire schema carries logical identity but not physical facts: it has no
field offsets, no stride/backing arithmetic, no padding, and its Int bounds
only imply i32-vs-i64. Re-deriving offsets on the host would duplicate the
deterministic P4-D algorithm -- a second SSOT, forbidden by the project's
principles. Instead:

* A new deterministic custom section **`"ahfl.core-layout.v1"`** carries the
  finalized `CoreLayoutTable` payload (a new binary codec, mirroring
  `encode/decode_core_wire_schema_table` with canonical re-encode equality and
  a local verifier), plus the boundary root mapping
  `{input_layout_id, output_layout_id}` and, for every input-reached
  container, its DISJOINT planner-assigned backing placement
  (`{container_edge_index, base, extent}` packed in layout-enumeration order
  at the sum-of-prior-backing rule of §6.2 -- every placement names a
  distinct extent; a section whose placements overlap is rejected), and the
  frame-payload arena span.
* The section is emitted iff the module is a P6-frame module (raw input
  projection or computed final), immediately before the wire-schema section;
  section order stays deterministic and identity modules carry neither new
  section beyond today's rule.
* The wire-schema projection is extended with the **agent boundary roots**
  (input nominal, output nominal). Today it projects only reachable
  capability signatures (`project_core_wire_schema(program, plan.imports)`),
  which is why the two capability-free skipped cases have no schema section.
  Boundary roots are a new selector kind on the binding factory; the
  capability-slot-only invariant of `CoreWireRootSelector` is preserved by
  adding a sibling frame-root selector rather than weakening it.
* Admission verifies once that layout and schema describe the same type:
  same field/variant arity and declaration order, scalar width implied by
  bounds agrees with the layout repr, container capacities agree, String/
  PtrLen correspondence, Option/Enum correspondence, and both roots resolve to
  the agent's declared input/output nominals. The compile-time projector emits
  both from one `CoreProgram`; the host-side admission rejects a transported
  pair that disagrees.

## 4. D3 -- The run-value return convention

### 4.1 New export: `runv`

```
runv() -> (status:i32, value_ptr:i32)
```

* Present **only** in a P6-frame module -- one whose descriptor frame
  contract is the new `p6_frame` (a raw-input projection OR a computed final);
  the predicate is purely the frame contract, unrelated to closure presence.
  Pure E1-E3 identity, capability, and workflow modules -- and the
  closure-using FB-1..FB-4 modules that neither project a raw frame nor return
  a computed final -- do not export it; their function/export/code sections are
  byte-identical to today (exports count stays 9 for agents,
  `core_wasm_codegen.cpp:8000`; 11 for workflows, `:8773`).
* New additive functype appended after the existing fixed and per-fn types;
  the function body is appended in the same additive position family as the
  P6-2 handler functions and FB-1 fns. No existing index moves.
* `status` reuses the `AHFL_CAP_*` u32 namespace; a completed computed run
  returns `AHFL_CAP_OK`. There is no PENDING arm (the subset has no effect);
  the fallthrough after the final-state switch is `unreachable`, exactly like
  run2 (`core_wasm_codegen.cpp:7858`).
* `value_ptr` names the root value's fixed frame home: the output frame base
  (`kP6AggregateOutputBase`) for a computed result, or the input frame base
  (`kP6AggregateInputBase`) for an identity final (no copy). Which of the two
  is authorized is fixed by the admitted descriptor's final-kind
  discriminator, not by the pointer itself (§3.4, §8). At that address:
  * scalar/string/collection-header/enum root: the inline word(s)/tag at the
    base;
  * struct/tuple root: the struct's shaped bytes at the base;
  so the host has one walk rule -- read at `value_ptr` inside the one region
  the descriptor declares for the final kind -- independent of the output
  type.
* Inputs are not parameters: the packed input already lives at the fixed
  input region (§6), and no current or planned caller needs to name an
  alternate input. Keeping runv parameter-free prevents a dead-argument ABI.

### 4.2 runv body

Same deterministic state walk run2 performs (`append_run_to_final`,
`core_wasm_codegen.cpp:7689-7721`), then a final-state switch:

* **Identity final in a P6-frame module** (e.g. the pinned p6_aggregate
  fixture, whose final is literally `return input;`): `(OK,
  kP6AggregateInputBase)` -- the input frame IS the output value; no copy.
* **Computed final** (new `ComputedReturn` planner action, §10): the final
  handler has already materialized its result into the output frame as its
  last effect; runv returns `(OK, kP6AggregateOutputBase)`.
* Trap otherwise. A capability final in a P6-frame agent is rejected at
  planning in v1 (§4.4), so no capability tuple arm exists in runv.

The host reads `value_ptr` only when `status == OK`, under the same
whole-page and in-region checks as the resume host.

### 4.3 Byte-identity proof obligation

The additive gate is structural and tested, not argued:

* a module with no raw-input projection and no computed final emits byte-for-
  byte the same artifact (existing golden/binary probes extended with an
  explicit "runv absent" assertion);
* the existing P6 raw-frame modules are the FIRST modules whose export set
  changes (they gain runv and the layout section); every census/golden count
  moves in the same commit as the emitter change.

### 4.4 Computed-final materialization

A new final-action kind replaces today's rejection path for a final,
capability-free, non-identity region (today such a region is only accepted in
the non-final builder; finals pass through `validate_identity_final`). The
final handler is a P6 subset region (`is_p6_subset_region`,
`core_wasm_codegen.cpp:902+`) whose terminator is `CoreReturnStmt`; planning
emits the result into the output frame:

* scalar result word -> i32/i64/f64 store at base+0, from the result's
  `P6ScalarKind`/layout repr (f64 at the boundary is supported even though
  in-module f64 arithmetic lands later; a boundary-only f64 is a frame
  spill/reload, not an opcode ladder);
* aggregate/enum/tuple result Ptr -> a bounded, planner-sized byte copy from
  the constructed address into the output frame, emitted as a checked copy
  loop with the layout's constant size (no bulk-memory proposal dependency);
* collection result -> copy of the 8-byte inline header; the backing words
  already name the shared backing region, which is stable for the run;
* String result can only be a frame-reached PtrLen (the subset cannot
  construct strings): the 8 inline bytes (two i32 words) are copied and still
  name the frame-payload arena the packer wrote;
* padding words are stored zero.

A final region containing a capability effect, closure value return, or any
out-of-subset node keeps today's fail-closed rejection with no partial
artifact. Mixing a raw-input computed agent with a capability FINAL state is
rejected in v1: the capability tuple is wire JSON and the runv contract is
P4-D. An agent that forwards a capability stays on the run2 lane; the mixed
agent is listed in §11.

## 5. D4 -- Input packing (inverse encode) and the workflow seam

### 5.1 Packer

```
pack_input_frame(Value, layout binding + wire binding, page writer)
```

1. `decode_json(json, wire_binding)` -- the existing policy, with its exact
   fail-closed family -- yields the native `Value` (or the host calls
   `validate_value` on an already-native value).
2. The inverse walk writes P4-D bytes:
   * root struct/enum/scalar into `[kP6AggregateInputBase, +input_size)`,
     gated by `kP6AggregateInputCapacity` as today;
   * each input-reached bounded collection: 8-byte header at its inline field
     slot, elements at that container's DISJOINT planner-assigned backing
     placement (§6.2; carried in the layout section), `ptr/len` words set,
     `len <= capacity`;
   * String PtrLen fields: 8-byte `(ptr,len)` inline, payload copied into the
     frame-payload arena (§6.2), payload extent bounded by the schema
     length_bounds (UTF-8 bytes) and the arena budget;
   * UUID bytes16 inline; Decimal -> mantissa word (spelling validated,
     scale cross-checked against schema); Duration -> millis word; Timestamp
     -> i64; Option -> tag + optional payload; padding zeroed.
3. All writes are checked against region bounds; packing failure produces no
   partial frame (write into a staging page or pre-validate every extent
   before the first store).

This replaces the test-only mirror the Python raw-frame hosts use today
(`tests/scripts/wasm_p6_aggregate_node_host.py` parses producer-reported
`fields=<name>@<off>:<val>`; the collection host does the same for backing):
those scripts remain as focused memory witnesses, while the production packer
is the generic host's single authority.

### 5.2 Context

The context frame `[4096, +ctx_size)` keeps its zero-initialized state; the
packer writes nothing there.

### 5.3 Workflow packaging (next-slice consumer)

Workflow run2 today forwards opaque frames: a node input is either the
workflow entry frame or an upstream node's returned `(ptr,len)`
(`append_workflow_source`, `core_wasm_codegen.cpp:8359-8376`; schedule at
`:8440-8452`). Projected or constructed node inputs -- "node B receives
field `x` of node A's output" -- need no in-module JSON:

* the scheduler/host already holds the upstream P4-D page; it walks the
  upstream output with the same verified layout+wire pair, applies the
  declared projection/construction as a `Value` transformation, and calls
  `pack_input_frame` for the target node's runner;
* the runner then reaches its node via the existing `(ptr,len)`-style call
  shape, with its input at the fixed region (one packed node input per runner
  invocation; sequential Kahn scheduling makes a single reusable region
  sound);
* this needs no new artifact ABI beyond D1-D6 -- it consumes the packer and
  the boundary bindings verbatim. The packaging implementation itself is the
  next slice and is out of scope here; this section fixes its seam so it
  cannot grow a third frame representation.

## 6. D5 -- One-page capacity verdict

### 6.1 Final memory map (agent, P6-frame module)

```
[ 1024, 4096)  input frame        (root + inline words; existing)
[ 4096, 7168)  context frame      (0-init; existing)
[ 7168,12288)  constructor scratch (existing; capacity 9216 -> 5120)
[12288,16384)  OUTPUT frame (new)  (kP6AggregateOutputBase; cap 4096)
[16384,  B1 )  collection backing  (existing region; disjoint per-container placements, §6.2 sum rule)
[   B1,  B2 )  frame-payload arena (new; input String payloads)
[   B2,  ...)  per-activation construct/closure heap (existing relocation)
...           <= 65536 hard ceiling
```

* `kP6AggregateOutputBase = 12288` is 8-aligned and splits the existing
  7168..16384 scratch window; `kP6AggregateScratchCapacity` becomes 5120,
  `kP6AggregateOutputCapacity = 4096` with the same static_assert family as
  the other frame constants (`core_wasm_abi_constants.hpp:94-106`). The
  scratch planner already fail-closes on overflow, so the reduction is a
  compile-time RESOURCE gate, never a silent alias.
* The OUTPUT frame gets its own compile-time fit gate, symmetric to the
  existing input gate
  (`fits_frame_region(program, layouts, agent.input_type,
  kP6AggregateInputCapacity, "input", result)`,
  `core_wasm_codegen.cpp:6994-6997`): a computed final requires
  `fits_frame_region(program, layouts, agent.output_type,
  kP6AggregateOutputCapacity, "output", result)` -- the materialized output
  root's P4-D inline bytes plus inline headers must fit the 4096-byte window
  (collection backing and string payloads live OUTSIDE the frame and are not
  counted). A computed result whose output nominal is larger than the input
  is therefore rejected with the same RESOURCE family before any handler byte
  is emitted. "Output capacity (4096) exceeds input capacity (3072)" does NOT
  by itself justify omitting this gate: that argument only covers identity
  finals, whose output type equals the input type; a computed final can have a
  different, larger output nominal, and the planner's constant-sized copy loop
  (§4.4) must never overrun the output window into the backing region.
* Addresses below 1024 stay unused; no Data section is introduced.

### 6.2 Frame-payload arena and construct heap

* The backing high-water already drives the construct-heap relocation
  (`core_wasm_codegen.cpp:6497-6520`). Every input-reached container receives a
  DETERMINISTIC DISJOINT backing placement, packed in the stable order the
  layout table enumerates its boundary edges (depth-first, declaration
  order): placement 0 starts at `kP6CollectionBackingBase` (16384), placement
  k starts at `align_up(16384 + sum of backing_size over placements 0..k-1,
  8)`, and each placement has exactly its layout's aligned `backing_size`
  extent. Two simultaneously-live input containers therefore never overlap --
  using the MAX backing size would alias every container onto 16384 (today
  every container layout names the same region-relative base and the emit-
  time extent check at `core_wasm_codegen.cpp:3832-3876` is region-relative),
  which is only sound while at most one container is packed. The frame-
  payload arena for packed input String bytes begins after the packed
  placements: `B1 = align_up(16384 + sum of every input-reached container's
  aligned backing_size, 8)`; `B2 = align_up(B1 + total bounded input string
  payload, 8)`; the per-activation construct/closure heap begins at `B2`.
  The existing construct-heap high-water accounting
  (`core_wasm_codegen.cpp:6825-6846`), which today takes the max() of one
  `base + backing_size` per storage, switches to the same SUM over placed
  containers when the packer lands, so its relocation base matches the
  packer's disjoint placements rather than the single-container shortcut.
* Every input string payload bound comes from the schema `length_bounds` and
  the max-canonical-size graph discipline (`core_wire_canonical_size.hpp`):
  the arena extent is a compile-time constant of the verified schema, never a
  runtime length. Runtime `len` words are validated against it on both pack
  and encode.
* The single existing capacity gate
  (`core_wasm_codegen.cpp:6547-6559`) is extended with the output frame size
  and arena extent -- one comparison family against
  `kCoreWasmFixedLinearMemoryCapacityBytes`, emitting
  `wasm.RESOURCE_EXHAUSTED` with the same actionable message shape. There is
  no runtime growth and no second page; the module keeps min 1 / no max, so
  the F3 one-page admission (`core_wasm_resume_host.cpp:91-102`) is
  unchanged.
* Node-event region interaction: none. Event records are workflow-lane only
  and computed/P6-frame agents are never capability workflows; a capability
  workflow's runner set keeps `allow_computed_goto=false` and the wire-JSON
  contract (`build_workflow_descriptor`, `core_wasm_codegen.cpp:8984-8992`).

## 7. D6 -- PtrLen and word-width boundary rules

* **Frame boundary (host <-> module):** every shape crosses with its P4-D
  inline form. Scalars 4/8; PtrLen two inline i32 words naming the
  frame-payload arena; Bytes16 sixteen inline bytes; structs/enums/
  collections one i32 address of their region slot; closures remain
  module-internal and never appear at a wire root. The single-word P6 rule
  for non-final projection leaves (`core_wasm_codegen.cpp:4593-4599`) is
  unchanged -- projections still land on one word; PtrLen is only ever read
  whole by the frame walker, never half-loaded by a handler expression.
* **fn boundary (module-internal):** lifting the CORE-FNBODY-DESIGN
  reservation that multi-word arguments await P6-7. One uniform rule: an
  argument that is not a single wasm word is passed **by i32 address** of its
  frame/arena/scratch slot -- an aggregate address (already the rule), a
  PtrLen slot address (new), a Bytes16 slot address (new), a collection
  handle (already one word), a closure (already two explicit words). Callees
  load through the pointer; the slot's region is live for the whole run
  (reserved frames and the never-reclaimed instance-lifetime bump arena,
  `core_wasm_resume_engine.hpp:138-143`). No fn may outlive the run, so no
  escape analysis is needed.
* fn return values keep the same rule in reverse: a multi-word result is a
  caller-provided destination address (the output frame for the final call;
  a bump slot for interior calls), never a multi-value expansion beyond the
  existing closure word pair.
* f64 is one 64-bit word at frame slots and in fn signatures even before the
  f64 opcode ladder; only arithmetic is missing, not the ABI.

## 8. Trust and admission summary

* Two verified sections per P6-frame module: `ahfl.wire-schema.v1` (extended
  boundary roots) and `ahfl.core-layout.v1` (new), each with deterministic
  encode, decode-with-re-encode-equality, and local verification; admission
  additionally verifies their structural consistency and root identity
  against the descriptor, the pairwise-DISJOINTNESS of every declared backing
  placement (§6.2), and the final-kind discriminator against the emitted
  runv/descriptor contract.
* The descriptor (`CoreWasmExecutionDescriptor`) gains, for a `p6_frame`
  module: the **final-kind discriminator** (identity vs computed -- the sole
  authority for runv's authorized `value_ptr` base, §3.4), input/output
  layout root ids and sizes, per input-reached container backing placements,
  the payload-arena span, and the output frame base. The descriptor remains
  derived solely from the plan that emitted the bytes
  (`core_wasm_codegen.hpp:60-77`).
* Host walks are read-only over the post-run page and write-only into a fresh
  page before the run; the artifact never gains a host callback for
  serialization.
* Capability wire frames remain opaque JSON on the run2 lane; the P4-D lane
  and the wire lane do not mix in v1.

## 9. Conformance and census implications

* `p6_aggregate` and `p6_collection` (one scenario each) CANNOT move from
  `RawP6FrameAwaitsP67` skip to full Node-vs-evaluator differential agree by
  merely dropping `node_observation_skip`: the differential agree happens only
  in the rung-E commit where the wasm producer stops classifying them
  `RawP6FrameAwaitsP67` (packer writes their input regions -- struct fields;
  the bounded list's header plus backing elements; runv returns the input
  frame base because both fixtures' finals are the literal identity `return
  input;`; the encoder walks that frame -- struct + collection
  header/backing -- to render the manifest's blessed canonical `output_json`).
  These two rungs prove D4 + the runv identity arm + D1-D2; the
  computed-return arm is exercised by a new fixture added in rung B (a
  computed final that materializes into the output frame).
* The rung-E differential also depends on EVALUATOR-ENGINE and manifest/test
  prerequisites that are independent of the packer/runv work and were missing
  at the original design time (verified empirically on `develop`); all three
  landed in the fix-forward commit that revised this gate:
  1. **Bare-builtin-hook dispatch.** The conformance evaluator path
     (`run_evaluator_scenario` -> `run_agent`,
     `tests/conformance/evaluator_engine.cpp`) always installs the capability
     wrapper; its dispatcher
     (`src/runtime/engine/capability_eval.cpp`,
     `eval_expr_with_capability_call_handler`) routed only `std::`-prefixed
     callees to the intrinsic/builtin table, so the bare hooks
     `xs.length`/`xs[0]`/`xs[1]` lower to (`list_raw_get` /
     `list_raw_length`, `src/compiler/ir/typed_hir_lower.cpp:2546`) fell
     through to the empty mock capability registry and failed the run in
     `Decide` with `capability_sequence=['list_raw_get']`. A direct
     `runtime::AgentRuntime` without the wrapper completes, proving the gap is
     the conformance dispatch path, not evaluator semantics. The fix-forward
     commit made the capability-bridged dispatcher resolve bare builtin hooks
     via the builtin table regardless of a `std::` prefix (the default
     evaluator path already reaches the builtin table as the final arm of
     `eval_intrinsic_call`), with a regression test in
     `tests/unit/runtime/engine/capability_bridge.cpp`.
  2. **Manifest flag flips.** Both cases set `engines.evaluator: false` at
     the original gate; the fix-forward commit flipped both to `true` (the
     evaluator runner skips cases with the flag false,
     `conformance_evaluator_runner.cpp:113`, and the Node runner invokes the
     evaluator observation for every non-node-only case
     `conformance_wasm_node_runner.cpp:419-427` regardless of the flag, so the
     flag is what gates the evaluator blessing lane).
  3. **Two evaluator blessings.**
     `tests/conformance/observations/p6_aggregate.high.json` and
     `p6_collection.high.json` did not exist at the original gate; the fix-
     forward commit added both. The evaluator runner's verify mode byte-
     compares each observation against that file and fails when it is
     missing. Both bytes were generated by the runner's `bless` mode and
     byte-match the manifest `expect` block.

  All three groundwork items landed in the fix-forward commit that revised
  this design gate: the capability-bridged dispatcher now resolves bare
  builtin hooks via the builtin table, both manifests carry
  `engines.evaluator: true`, and both blessings exist. The Node census stays
  14/7 and both manifests keep `node_observation_skip=raw_p6_frame_awaits_p67`
  until rung E, because the wasm producer still classifies both cases
  `RawP6FrameAwaitsP67`; only the Node lane and census move in rung E.
* Census pins move, in the SAME commit that renames the descriptor contract
  AND removes the producer's p6-7 classification:
  `kExpectedAgreed 14 -> 16`, `kExpectedSkipped 7 -> 5`
  (`conformance_wasm_node_runner.cpp:63-64`); the two manifests drop
  `node_observation_skip` while keeping `eligible: orchestration`. Until that
  commit both cases keep `node_observation_skip=raw_p6_frame_awaits_p67` (the
  compiler still computes the skip, so the both-directions enforcement
  requires the declaration), stay counted in the skip census at 14/7, and the
  two items above can land independently as evaluator-lane groundwork --
  flipping `engines.evaluator` and adding the blessings changes neither the
  Node skip count nor the manifest skip declaration, which the wasm
  eligibility gate pins off `engines.wasm` only
  (`wasm_eligibility.cpp:206-250`). The existing both-directions enforcement
  (`conformance_wasm_node_runner.cpp:353-357`, `wasm_eligibility.cpp:211-250`)
  is unchanged in shape: a compiler-computed `p6_frame` without a runnable
  packed observation, or a stale manifest declaration, still fails.
* The descriptor JSON spelling `raw_p6_frame` is replaced by `p6_frame`
  (`wasm_engine.cpp:40-48`) in that commit; the Node host gains the
  pack/runv/encode path keyed off it, and refuses a descriptor version it
  does not understand.
* The five remaining skips (blocked on KR6.6 computation: `if_let_e2e` x2,
  `enum_variant_e2e`, `e2e_multi_agent` x2) and the one node-only
  `evaluator_surface_awaits_kr68` stem are untouched.
* Decimal/Duration computed-output fixtures, when added, pin builtin/
  bare-millis spellings (§3.3).

## 10. Ordered implementation ladder

Each rung is one reviewable commit; the artifact stays shippable after each,
and no rung changes identity-module bytes.

1. **P6-7-A sections and bindings.** Deterministic `CoreLayoutTable` binary
   codec + local verifier (mirror the wire-schema codec pair); extend the wire
   projection with agent input/output boundary roots and a sibling frame-root
   binding selector; emit/consume `ahfl.core-layout.v1`; layout/wire
   consistency admission; DESCRIPTOR CARRIES THE FINAL-KIND DISCRIMINATOR and
   pairwise-DISJOINT per-container backing placements (§6.2 sum rule; the
   local verifier rejects overlapping placements); descriptor fields for
   roots, placements, arena.
   Host-side unit tests: codec round trip, tamper/re-encode rejection,
   disagreement rejection, overlapping-placement rejection. No runv yet.
   Identity/capability/workflow modules are byte-identical (they carry
   neither new section); today's two raw-frame modules are the only artifacts
   whose bytes change in this rung (they gain the two sections), and every
   golden/binary probe that pins them moves in the same commit.
2. **P6-7-B computed-final emission.** `ComputedReturn` planning action;
   final-region materialization (scalar spill, bounded aggregate copy, header
   copy, zero padding); additive `runv` export and functype; output frame
   constants + capacity gate INCLUDING the output-side
   `fits_frame_region(..., kP6AggregateOutputCapacity, "output", ...)`
   rejection for a computed final (§6.1); reject capability/raw-mixed finals
   and non-subset returns with today's codes. Binary/byte-identity probes.
3. **P6-7-C frame walks.** Host `Value <-> P4-D regions` walk pair
   (`encode_value_json` via `value_to_json`, `pack_input_frame` via
   `decode_json`), region/bounds checks, DISJOINT per-container backing
   placements per the §6.2 sum-of-prior-backing rule (the packer materializes
   the layout section's `{base,extent}` records, never a shared base), String
   arena, Set/Map canonical duplicates fail-closed, Decimal/Duration spelling
   rules, f64 finiteness.
   Pure engine unit tests over hand-built verified pages (FakeResumeEngine
   style; include a two-input-container overlap regression page), no Node
   dependency.
4. **P6-7-D multi-word fn ABI.** By-address PtrLen/Bytes16 fn arguments and
   destination-address returns (§7); layout/verifier updates; recursion/heap
   budget unchanged (addresses add no bytes).
5. **P6-7-E Node lane and census.** Node host pack/runv/encode path and
   descriptor bump; unblock the two pinned cases at the WASM-PRODUCER level
   (the descriptor contract flip is what removes the producer's
   `RawP6FrameAwaitsP67` classification); move 14/7 -> 16/5 and drop both
   `node_observation_skip` declarations in the same commit; rename
   `raw_p6_frame` -> `p6_frame`; keep the Python raw-frame hosts as memory
   witnesses. The evaluator-engine/manifest groundwork in §9 (bare-hook
   dispatch through the builtin table on the capability-bridged path, the two
   `engines.evaluator: false -> true` flips, and the two
   `tests/conformance/observations/p6_*.high.json` blessings) already landed
   in the fix-forward revision of this design gate, so rung E changes only
   the Node side and the census -- it must not land as "drop the skip and
   hope the differential is green".
6. **P6-7-F workflow node packaging** (next slice, tracked separately):
   consume packer + boundary bindings for projected/constructed node inputs
   (§5.3).

## 11. Explicitly not in this design's scope

* f64 arithmetic opcodes in handler bodies (the boundary ABI supports f64;
  the opcode ladder does not).
* in-module String/Decimal/Duration construction and `map_raw_get` / the
  remaining `CoreUnsupportedExpr` arms in `core_lower.cpp`; computed string
  outputs are therefore input-reachable only in v1.
* mixing raw-P4-D computation with a capability final in one agent (capability
  results are wire JSON); such agents stay on run2 or are rejected until a
  explicit frame-bridge design.
* workflow projected-node scheduling mechanics (seam fixed in §5.3, work in
  P6-7-F).
* Closures as boundary values (the projector refuses them; KR6.8 territory).
* any change to the durable-resume memo bytes, the node-event record layout,
  or the fixed one-page declaration; B2 durable resume consumes the same
  host-side encoder when computed agents appear there, under its own gate.
