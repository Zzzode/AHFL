// KR6.7 (RFC 0026 P7): generic Node embedded-engine host for the manifest-driven
// conformance suite.
//
// THIS IS NODE EMBEDDED-ENGINE EVIDENCE, NOT WASMTIME EVIDENCE. Node is an
// embedded host that can allocate/write the target module's private linear
// memory and bind its ahfl_cap import; it is not a separate wasmtime runtime.
//
// It consumes ONLY the machine-readable descriptor the manifest-driven C++
// producer emitted plus the compiled module bytes -- no bespoke per-probe
// key=value lines. It drives the stable Core-Wasm ABI
// (alloc/run2/step/current_state, the transition and workflow counters, the
// ahfl_cap import, the node-event buffer) and:
//
//   1. reconstructs the SAME canonical observation document shape the in-process
//      evaluator adapter emits (status / state_sequence / capability_sequence /
//      output_json / transition_count), which the C++ comparator asserts equal
//      on the three KR6.7 differential dimensions;
//
//   2. on FRESH module instances, runs the capability ABI normalization matrix
//      (OK / OK-null / OK-zero-len / ERROR / PENDING / PENDING-nonnull / unknown
//      + the pending latch, the legacy-run pre-effect trap, and -- for a
//      capability workflow -- the node-event record layout, the corrupt-count
//      defensive gate, and the checked-alloc sequence). These are the real-engine
//      robustness contracts the retired bespoke Node probes pinned, now driven by
//      descriptor facts rather than hardcoded constants.
//
// usage:
//   node node_embedded_host.mjs --descriptor <d.json> --module <m.wasm> \
//        --scenario <name> --output <observation.json>
//
// Exit: 0 success, 1 conformance failure, 77 the WebAssembly embedding API or a
// required engine feature is unavailable (environment skip).

import fs from "node:fs";

function fail(message) {
  console.error(`FAIL: ${message}`);
  process.exit(1);
}

function parseArgs(argv) {
  const args = {};
  for (let i = 0; i < argv.length; i += 2) {
    const flag = argv[i];
    const value = argv[i + 1];
    if (!flag.startsWith("--") || value === undefined) fail(`malformed argument ${flag}`);
    args[flag.slice(2)] = value;
  }
  for (const required of ["descriptor", "module", "scenario", "output"]) {
    if (!(required in args)) fail(`missing --${required}`);
  }
  return args;
}

if (typeof WebAssembly === "undefined" || typeof WebAssembly.instantiate !== "function") {
  console.log("SKIP: Node WebAssembly embedding API is unavailable");
  process.exit(77);
}

const args = parseArgs(process.argv.slice(2));
const descriptor = JSON.parse(fs.readFileSync(args.descriptor, "utf8"));
if (descriptor.schema !== "ahfl.conformance-wasm-descriptor.v1") {
  fail(`unexpected descriptor schema ${descriptor.schema}`);
}
const scenario = descriptor.scenarios.find((s) => s.name === args.scenario);
if (scenario === undefined) fail(`descriptor has no scenario '${args.scenario}'`);

const bytes = fs.readFileSync(args.module);
const encoder = new TextEncoder();
const decoder = new TextDecoder();
const inputBytes = encoder.encode(scenario.input_wire);

// RFC 0026 E4-B2-D1a-3 common KAT: the CONTRACT-DERIVED 88-byte node-event
// golden (memory[log_base : log_base+88]) for the committed two-node
// capability-workflow fixture (node0 capability Echo/source_symbol 1, node1
// identity). It is byte-for-byte identical to the hex the C++ decoder test
// pins (tests/unit/runtime/engine/core_wasm_node_events.cpp kGoldenHex), so a
// successful comparison here is a cross-engine common-KAT link, not merely a
// field-by-field equivalence.
const kNodeEventGoldenHex =
  "02000000000000000100000000000000000000000000000001000000000000000000000000000000" +
  "00000000000000000000000001000000010000000000000000000000000000000000000000000000" +
  "0000000000000000";

function regionHex(memory, base, length) {
  const view = new Uint8Array(memory.buffer, base, length);
  let hex = "";
  for (const b of view) hex += b.toString(16).padStart(2, "0");
  return hex;
}

// True iff the descriptor names exactly the fixture the contract-derived KAT
// was derived from: two scheduled nodes, node0 a capability (ordinal 0, source
// SymbolId 1), node1 an identity.
function descriptorMatchesCommonKat(lane, nodeCount) {
  if (nodeCount !== 2 || lane.nodes.length !== 2) return false;
  const bySchedule = [...lane.nodes].sort((a, b) => a.schedule_pos - b.schedule_pos);
  const [first, second] = bySchedule;
  return first.schedule_pos === 0 && first.has_capability &&
         first.capability_ordinal === 0 && first.source_symbol === 1 &&
         second.schedule_pos === 1 && !second.has_capability;
}

// Exact transition expectation for one workflow execution: every scheduled
// node executes its packaged runner's full walk, which performs exactly
// (walk length - 1) gotos. Derived from descriptor facts only.
function expectedWorkflowTransitions() {
  const lane = descriptor.workflow_lane;
  return lane.nodes.reduce((sum, node) => {
    const walk = lane.agents[node.runner]?.walk;
    if (walk === undefined) fail(`node ${node.node_id} has no runner ${node.runner}`);
    return sum + (walk.length - 1);
  }, 0);
}

// Negative evidence: a rejected run must leave node-event state untouched.
// `includeHeader` covers the normalization/pending modes (event_count stays 0);
// the corrupt-count mode host-injects event_count itself, so it asserts only
// that every record body slot stayed zero-filled.
function assertEventRegionZero(exports, label, includeHeader) {
  const buffer = descriptor.event_buffer;
  if (includeHeader) {
    const view = new Uint8Array(exports.memory.buffer, buffer.log_base,
                                buffer.header_bytes +
                                descriptor.workflow_node_count * buffer.record_bytes);
    for (let i = 0; i < view.length; ++i) {
      if (view[i] !== 0) {
        fail(`${label}: node-event header/record region byte ${i} is nonzero on a rejected run`);
      }
    }
    return;
  }
  const recordView = new Uint8Array(exports.memory.buffer, buffer.records_base,
                                    descriptor.workflow_node_count * buffer.record_bytes);
  for (let i = 0; i < recordView.length; ++i) {
    if (recordView[i] !== 0) {
      fail(`${label}: node-event record byte ${i} was written despite the rejected run`);
    }
  }
}

// ---- instance factory --------------------------------------------------------
//
// mode "scenario" replays the scenario's ordered manifest mocks; every other
// mode is a single-shot synthetic capability outcome used by the ABI matrix:
// ok / ok-null / ok-zero-len / error / pending / pending-nonnull / unknown /
// corrupt-count. One instance per mode (the pending-latch and normalization
// contracts are per-instance).
// RFC 0026 P6-7 frame-bridge v2 D3/D4 (rung V2-C): the capability BRIDGE lane.
// A bridge import has functype (block_ptr:i32) -> (status:i32, result_root_ptr).
// The host walks the dense P4-D argument spans at the control block, builds the
// SAME wire argument envelope the native transport uses (0 args => {}, one
// Struct => bare object, one non-Struct => {"value":..}, N => {"args":[...]}),
// invokes the scenario mock, and packs the validated result JSON into the
// call site's disjoint result placement (with String payload bytes in its
// payload arena). Every read/write is bounds-checked against the descriptor;
// a bad control block or result is a host fail (the module traps regardless).

function findBridgeSite(ordinal) {
  const lane = descriptor.frame_lane;
  if (!lane) return undefined;
  return lane.bridge_call_sites.find((s) => s.import_ordinal === ordinal);
}

// Read ONE bridge argument P4-D span into a JS value. `wireId`/`layoutId` name
// the logical shape and dense physical root; the physical span starts at `addr`
// and is `len` bytes. Only the frame-walk subset is reachable (compile +
// admission already rejected the rest).
function readBridgeArgValue(e, lane, W, L, backing, currentSite, wireId, layoutId,
                            addr, len) {
  if (addr < 0 || len < 0 || addr + len > kPageSize) {
    frameFail("bridge argument span lies outside the fixed page");
  }
  return readValue(e, lane, W, L, wireId, layoutId, addr, backing,
                   bridgeStringRegions(lane, currentSite));
}

// Union of the String-payload regions the read walker may authorize for a
// BRIDGE argument: the packed input-payload arena, the rodata region, and
// every OTHER call site's result placement payload. The CURRENT call site's
// own result region is excluded — its block is this call's output, so before
// the host packs it that region can never be a valid arg source.
function bridgeStringRegions(lane, currentSite) {
  const regions = [];
  for (const s of lane.bridge_call_sites) {
    if (s.call_site_id === currentSite.call_site_id) continue;
    if (s.result_payload_capacity > 0) {
      regions.push({lo: s.result_payload_base,
                    hi: s.result_payload_base + s.result_payload_capacity});
    }
  }
  return regions;
}

// Pack one bridge result value at the call site's result root. String payload
// bytes are written to the site's disjoint payload arena (one bump cursor).
function packBridgeResult(e, lane, W, L, site, wireId, value) {
  if (site.result_base < 0 || site.result_base + site.result_extent > kPageSize) {
    frameFail("bridge result placement lies outside the fixed page");
  }
  // Zero the whole result placement (padding reads back deterministically).
  new Uint8Array(e.memory.buffer, site.result_base, site.result_extent).fill(0);
  const arena = {cursor: site.result_payload_base,
                 base: site.result_payload_base,
                 capacity: site.result_payload_capacity,
                 exhausted_message: "bridge result payload arena exhausted"};
  // A fresh per-call backing map. (A collection RESULT is rejected until a
  // later ladder gives it its own backing placement, so no sequence reaches
  // this map today; it stays because packValue's contract is uniform.)
  const result_backing = backingByLayout(lane);
  packValue(e, lane, W, L, value, wireId, site.result_layout,
            site.result_base, result_backing, arena);
}

function makeBridgeCallback(importEntry, ordinal, getInstance, getState, mode) {
  return (blockPtr) => {
    const instance = getInstance();
    const state = getState();
    const e = instance.exports;
    const dv = new DataView(e.memory.buffer);
    const lane = descriptor.frame_lane;
    const site = findBridgeSite(ordinal);
    if (site === undefined) {
      frameFail(`import ${importEntry.field} is invoked on the bridge protocol but has no ` +
                `descriptor bridge record`);
    }
    // Control-block membership: exactly
    // [bridge_control_base, bridge_control_base + site_count*stride).
    const blocks_end = lane.bridge_control_base +
                       lane.bridge_call_sites.length * lane.bridge_block_stride;
    if (blockPtr < lane.bridge_control_base || blockPtr >= blocks_end) {
      frameFail("bridge control-block pointer names an address outside the control-block region");
    }
    const block_index = Math.floor(
      (blockPtr - lane.bridge_control_base) / lane.bridge_block_stride);
    const site_at_block = lane.bridge_call_sites[block_index];
    if (site_at_block === undefined ||
        blockPtr !== lane.bridge_control_base + site.block_offset) {
      frameFail("bridge control-block pointer is not at its dense fixed-stride offset");
    }
    const call_site_id = dv.getUint32(blockPtr, true);
    const arg_count = dv.getUint32(blockPtr + 4, true);
    if (call_site_id !== site.call_site_id) {
      frameFail("bridge control-block call_site_id disagrees with its dense block address");
    }
    if (arg_count !== site.arity) {
      frameFail(`bridge control-block arg_count ${arg_count} != descriptor arity ${site.arity}`);
    }

    // Walk the arguments.
    const W = descriptor.wire_schema.nodes;
    const L = lane.layouts;
    const backing = backingByLayout(lane);
    const argValues = [];
    for (let i = 0; i < arg_count; ++i) {
      const desc = blockPtr + 8 + 8 * i;
      const ptr = dv.getInt32(desc, true);
      const len = dv.getUint32(desc + 4, true);
      argValues.push(readBridgeArgValue(e, lane, W, L, backing, site,
                                        site.params[i], site.param_layout[i],
                                        ptr, len));
    }

    state.calls += 1;
    state.events.push({name: importEntry.name, argument: bridgeEnvelope(argValues)});

    // ABI-matrix modes for the bridge protocol: a non-OK status makes the module
    // trap single-run, no result packing needed.
    if (mode === "error" || mode === "pending" || mode === "unknown") {
      return [mode === "pending" ? 2 : 1, 0];
    }

    // Invoke the scenario mock (per-name cursor matches the opaque lane).
    // "states" is the separate step()-instrumentation instance: it replays the
    // same scenario mocks but only its state names are evidence.
    const index = state.perName.get(importEntry.name) ?? 0;
    state.perName.set(importEntry.name, index + 1);
    const mocks = scenario.mocks.filter((m) => m.name === importEntry.name);
    const mock = mocks[index];
    if (mock === undefined) {
      frameFail(`scenario '${scenario.name}' has no bridge mock for call ${index} of ` +
                importEntry.name);
    }
    if (mock.status !== "ok") {
      // Single-run: error AND pending both trap (the module branches on any
      // non-zero status; the bridge has no pending arm).
      return [mock.status === "pending" ? 2 : 1, 0];
    }
    const resultValue = JSON.parse(mock.result_wire);
    packBridgeResult(e, lane, W, L, site, site.result, resultValue);
    return [0, site.result_base];
  };
}

// The wire argument envelope SSOT (mirrors serialize_args_for_wire_json).
function bridgeEnvelope(args) {
  if (args.length === 0) return "{}";
  if (args.length === 1) {
    const only = args[0];
    if (only && typeof only === "object" && !Array.isArray(only) &&
        !(only instanceof Boolean)) {
      return jsonString(only);
    }
    return jsonString({value: only});
  }
  return jsonString({args});
}

async function makeInstance(compiled, mode) {
  const state = { calls: 0, events: [], perName: new Map() };
  const imports = {};
  for (const importEntry of descriptor.imports) {
    if (importEntry.mode === "bridge") {
      const ordinal = importEntry.ordinal;
      imports[importEntry.field] = makeBridgeCallback(
        importEntry, ordinal, () => instance, () => state, mode);
      continue;
    }
    imports[importEntry.field] = (ptr, len) => {
      const argument = decoder.decode(
        new Uint8Array(instance.exports.memory.buffer, ptr, len));
      state.calls += 1;
      state.events.push({ name: importEntry.name, argument });
      if (argument !== scenario.input_wire) {
        fail(`opaque capability argument frame changed for ${importEntry.name}`);
      }

      // scenario mode: replay the manifest mock for this capability at its
      // per-name invocation cursor.
      if (mode === "scenario") {
        const index = state.perName.get(importEntry.name) ?? 0;
        state.perName.set(importEntry.name, index + 1);
        const mocks = scenario.mocks.filter((m) => m.name === importEntry.name);
        const mock = mocks[index];
        if (mock === undefined) {
          fail(`scenario '${scenario.name}' has no mock for call ${index} of ${importEntry.name}`);
        }
        if (mock.status === "ok") {
          const resultBytes = encoder.encode(mock.result_wire);
          const out = instance.exports.alloc(resultBytes.length);
          new Uint8Array(instance.exports.memory.buffer, out, resultBytes.length)
            .set(resultBytes);
          return [0, out, resultBytes.length];
        }
        if (mock.status === "pending") return [2, 0, 0];
        return [1, 0, 0];
      }

      if (mode === "ok" || mode === "corrupt-count") {
        if (mode === "corrupt-count") {
          // Tamper the event_count header before returning a legal OK frame; the
          // scheduler's defensive coordinate gate must reject it.
          new DataView(instance.exports.memory.buffer).setUint32(
            descriptor.event_buffer.log_base, 99, true);
        }
        const resultBytes = encoder.encode(scenario.mocks[state.calls - 1]?.result_wire ?? "");
        const out = instance.exports.alloc(resultBytes.length);
        new Uint8Array(instance.exports.memory.buffer, out, resultBytes.length)
          .set(resultBytes);
        return [0, out, resultBytes.length];
      }
      if (mode === "ok-null") return [0, 0, 0];
      if (mode === "ok-zero-len") return [0, 1234, 0];
      if (mode === "error") return [1, 1234, 9];
      if (mode === "pending") return [2, 0, 99];
      if (mode === "pending-nonnull") return [2, 1234, 9];
      return [77, 1234, 9]; // unknown
    };
  }
  const instance = (await WebAssembly.instantiate(compiled, { ahfl_cap: imports }));
  state.instance = instance;
  state.exports = instance.exports;
  state.writeInput = function writeInput() {
    const ptr = instance.exports.alloc(inputBytes.length);
    new Uint8Array(instance.exports.memory.buffer, ptr, inputBytes.length).set(inputBytes);
    return [ptr, inputBytes.length];
  };
  // RFC 0026 P6-7: the canonical input JSON a p6-frame host packs into P4-D.
  state.scenarioInput = scenario.input_wire;
  return state;
}

// ---- canonical observation envelope ------------------------------------------
function jsonString(value) { return JSON.stringify(value); }
function emitStateSequence(entries) {
  return `[${entries.map((entry) => {
    const agent = jsonString(entry.agent);
    const state = jsonString(entry.state);
    return `{"agent":${agent},"state":${state}}`;
  }).join(",")}]`;
}
function emitObservation({ status, states, capabilities, outputRaw, transitions,
                           completedNodes }) {
  const fields = [
    [`"capability_sequence"`, `[${capabilities.map(jsonString).join(",")}]`],
    [`"case"`, jsonString(descriptor.case)],
    ...(outputRaw !== null ? [[`"output_json"`, outputRaw]] : []),
    [`"scenario"`, jsonString(scenario.name)],
    [`"schema"`, jsonString("ahfl.node-observation.v1")],
    [`"state_sequence"`, emitStateSequence(states)],
    [`"status"`, jsonString(status)],
    [`"transition_count"`, String(transitions)],
    ...(completedNodes !== null
      ? [[`"workflow_completed_count"`, String(completedNodes)]] : []),
  ];
  fields.sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0));
  return `{${fields.map(([k, v]) => `${k}:${v}`).join(",")}}`;
}

function normalizeStatus(code) {
  if (code === 0) return "completed";
  if (code === 2) return "suspended";
  return "failed";
}

function expectTraps(fn, label) {
  let trapped = false;
  try { fn(); } catch (error) { trapped = error instanceof WebAssembly.RuntimeError; }
  if (!trapped) fail(`${label} did not trap`);
}

// ---- RFC 0026 P6-7 p6-frame lane ---------------------------------------------
//
// A p6-frame module does NOT carry canonical wire JSON over run2. The host
// (1) packs the scenario's canonical input JSON into the fixed P4-D input
// regions using the descriptor's verified layout + wire-schema tables, (2)
// invokes runv() -> (status, value_ptr), and (3) walks the returned frame's
// P4-D bytes back to JSON. The descriptor tables mirror the module's
// `ahfl.core-layout.v1` / `ahfl.wire-schema.v1` sections value-for-value, so
// every offset/stride/capacity comes from the module's own layout, never a
// host re-derivation. The C++ comparator canonicalizes the resulting JSON
// (key order, escaping), so the host only needs a structurally faithful DOM.
//
// Only the shapes the verified boundary reaches are walked; an unrecognized
// shape fails closed. Every access is bounds-checked against the fixed page.

const kPageSize = 65536;

function frameFail(message) {
  fail(`p6-frame: ${message}`);
}

function dataView(e) { return new DataView(e.memory.buffer); }

function checkRange(addr, len) {
  if (!Number.isInteger(addr) || !Number.isInteger(len) || addr < 0 || len < 0 ||
      addr + len > kPageSize) {
    frameFail(`out-of-page access @${addr}+${len}`);
  }
}
function writeI32(e, addr, value) {
  checkRange(addr, 4);
  dataView(e).setInt32(addr, value | 0, true);
}
function writeI64(e, addr, value) {
  checkRange(addr, 8);
  dataView(e).setBigInt64(addr, BigInt(value), true);
}
function readI32(e, addr) {
  checkRange(addr, 4);
  return dataView(e).getInt32(addr, true);
}
function readI64(e, addr) {
  checkRange(addr, 8);
  return dataView(e).getBigInt64(addr, true);
}
function writeRaw(e, addr, bytes) {
  checkRange(addr, bytes.length);
  new Uint8Array(e.memory.buffer, addr, bytes.length).set(bytes);
}
function readRaw(e, addr, len) {
  checkRange(addr, len);
  return new Uint8Array(e.memory.buffer, addr, len);
}

// Wire layout-id -> its disjoint backing placement {base,extent}.
function backingByLayout(lane) {
  const map = new Map();
  for (const placement of lane.placements) {
    if (map.has(placement.lay)) frameFail("duplicate backing placement for one container");
    map.set(placement.lay, placement);
  }
  return map;
}

function fieldLayoutOf(structLayout, index) {
  const field = structLayout.fields[index];
  if (field === undefined) frameFail("struct field/layout arity mismatch");
  return field;
}

// Pack `value` into the absolute frame slot `addr`. Scalars are stored inline
// at addr; aggregates occupy the whole struct/enum bytes rooted at addr (the
// slot is the root); a bounded container's inline (ptr,len) header is at addr
// and its elements live in the placement-backed backing region.
function packValue(e, lane, W, L, value, wId, lId, addr, backing, arena) {
  const w = W[wId];
  const l = L[lId];
  if (w === undefined || l === undefined) frameFail("pack node id out of range");
  switch (w.t) {
    case "unit":
      return;
    case "bool":
      if (typeof value !== "boolean") frameFail("bool slot got a non-bool value");
      writeI32(e, addr, value ? 1 : 0);
      return;
    case "int":
      if (typeof value !== "number" || !Number.isInteger(value)) {
        frameFail("int slot got a non-integer value");
      }
      // Fail closed instead of relying on writeI32's `value | 0`: a JSON value
      // the descriptor's schema bounds or the physical i32 word cannot hold
      // would otherwise be silently truncated and the module would branch on a
      // different word (design section 3.4 fail-closed family).
      if (Math.abs(value) > Number.MAX_SAFE_INTEGER) {
        frameFail("int value exceeds the JavaScript exact-integer domain");
      }
      const wideInt = l.t === "scalar" && l.repr === "i64";
      if (w.lo !== undefined && value < Number(w.lo)) {
        frameFail("int value is below the schema lower bound");
      }
      if (w.hi !== undefined && value > Number(w.hi)) {
        frameFail("int value is above the schema upper bound");
      }
      if (!wideInt && (value < -2147483648 || value > 2147483647)) {
        frameFail("narrow int value does not fit its physical i32 word (refusing truncation)");
      }
      if (wideInt) writeI64(e, addr, value);
      else writeI32(e, addr, value);
      return;
    case "float":
    case "decimal":
    case "duration":
    case "timestamp":
    case "uuid":
    case "map":
      frameFail(`pack shape '${w.t}' is outside the P6-7 rung-E frame subset`);
      return;
    case "string": {
      if (typeof value !== "string") frameFail("string slot got a non-string value");
      const bytes = new TextEncoder().encode(value);
      if (w.lo !== undefined && bytes.length < Number(w.lo)) {
        frameFail("string payload is shorter than the schema length lower bound");
      }
      if (w.hi !== undefined && bytes.length > Number(w.hi)) {
        frameFail("string payload exceeds the schema length upper bound");
      }
      if (arena.cursor + bytes.length > arena.base + arena.capacity) {
        frameFail(arena.exhausted_message ?? "frame-payload arena exhausted");
      }
      writeRaw(e, arena.cursor, bytes);
      writeI32(e, addr + 0, arena.cursor);
      writeI32(e, addr + 4, bytes.length);
      arena.cursor += bytes.length;
      return;
    }
    case "struct": {
      if (value === null || typeof value !== "object" || Array.isArray(value)) {
        frameFail("struct slot got a non-object value");
      }
      if (l.t !== "struct") frameFail(`wire struct '${w.name}' maps to a non-struct layout`);
      for (let i = 0; i < w.fields.length; ++i) {
        const field = w.fields[i];
        const slot = fieldLayoutOf(l, i);
        if (!Object.prototype.hasOwnProperty.call(value, field.name)) {
          frameFail(`struct input is missing field '${field.name}'`);
        }
        packValue(e, lane, W, L, value[field.name], field.type, slot.lay,
                  addr + Number(slot.off), backing, arena);
      }
      return;
    }
    case "option": {
      if (l.t !== "enum") frameFail("wire option maps to a non-enum layout");
      if (value === null) {
        writeI32(e, addr, 0);
        return;
      }
      writeI32(e, addr, 1);
      const variant = l.variants[1];
      if (variant === undefined) frameFail("option Some variant has no payload layout");
      packValue(e, lane, W, L, value, w.value, variant.lay,
                addr + Number(l.payload_offset), backing, arena);
      return;
    }
    case "enum": {
      if (l.t !== "enum") frameFail("wire enum maps to a non-enum layout");
      if (value === null || typeof value !== "object") frameFail("enum slot got a non-object");
      const ordinal = w.variants.findIndex((variant) => variant.name === value._variant);
      if (ordinal < 0) frameFail(`enum value names an unknown variant '${value._variant}'`);
      writeI32(e, addr, ordinal);
      const wireVariant = w.variants[ordinal];
      const layoutVariant = l.variants[ordinal];
      if (wireVariant.kind !== "unit") {
        if (layoutVariant === undefined) frameFail("enum variant has no payload layout");
        const payloadAddr = addr + Number(l.payload_offset);
        if (wireVariant.kind === "tuple") {
          const arr = Array.isArray(value._payload) ? value._payload : null;
          if (arr === null) frameFail("tuple enum variant is missing its _payload array");
          packSlots(e, lane, W, L, arr, wireVariant.slots,
                    L[layoutVariant.lay], payloadAddr, backing, arena,
                    /*named=*/false);
        } else {
          const obj = (value._named_payload !== undefined ? value._named_payload
                                                           : value._payload) ?? null;
          if (obj === null || typeof obj !== "object") {
            frameFail("struct enum variant is missing its named payload object");
          }
          packSlots(e, lane, W, L, obj, wireVariant.slots,
                    L[layoutVariant.lay], payloadAddr, backing, arena,
                    /*named=*/true);
        }
      }
      return;
    }
    case "sequence": {
      if (l.t !== "container") frameFail("wire sequence maps to a non-container layout");
      if (!Array.isArray(value)) frameFail("sequence slot got a non-array value");
      const capacity = Number(w.capacity ?? l.capacity);
      if (value.length > capacity) frameFail("packed collection length exceeds its capacity");
      const placement = backing.get(lId);
      if (placement === undefined) frameFail("input container has no backing placement");
      const stride = Number(l.stride);
      if (value.length * stride > placement.extent) {
        frameFail("packed collection overruns its backing placement");
      }
      for (let i = 0; i < value.length; ++i) {
        packValue(e, lane, W, L, value[i], w.element, l.element,
                  placement.base + i * stride, backing, arena);
      }
      writeI32(e, addr + 0, placement.base);
      writeI32(e, addr + 4, value.length);
      return;
    }
    case "tuple": {
      if (l.t !== "struct") frameFail("wire tuple maps to a non-struct layout");
      if (!Array.isArray(value)) frameFail("tuple slot got a non-array value");
      packSlots(e, lane, W, L, value,
                w.elements.map((type, index) => ({name: `${index}`, type})),
                l, addr, backing, arena, /*named=*/false);
      return;
    }
  }
  frameFail(`unhandled pack shape '${w.t}'`);
}

function packSlots(e, lane, W, L, source, wireSlots, payloadLayout, payloadAddr,
                   backing, arena, named) {
  if (payloadLayout === undefined || payloadLayout.t !== "struct") {
    frameFail("aggregate payload has no struct layout");
  }
  for (let i = 0; i < wireSlots.length; ++i) {
    const slot = fieldLayoutOf(payloadLayout, i);
    const child = named ? source[wireSlots[i].name] : source[i];
    packValue(e, lane, W, L, child, wireSlots[i].type, slot.lay,
              payloadAddr + Number(slot.off), backing, arena);
  }
}

// Read a packed frame back to a JSON-friendly JS value. `lane` supplies the
// frame-payload arena span: a module-written PtrLen is untrusted evidence, so a
// string payload must name that exact region (and satisfy the schema length
// bounds), never an arbitrary in-page address (design sections 3.2/3.4).
function readValue(e, lane, W, L, wId, lId, addr, backing, extraStringRegions = []) {
  const w = W[wId];
  const l = L[lId];
  if (w === undefined || l === undefined) frameFail("read node id out of range");
  switch (w.t) {
    case "unit":
      return null;
    case "bool": {
      const word = readI32(e, addr);
      if (word !== 0 && word !== 1) frameFail("bool word is not in {0,1}");
      return word === 1;
    }
    case "int": {
      let word;
      if (l.t === "scalar" && l.repr === "i64") {
        const big = readI64(e, addr);
        if (big > BigInt(Number.MAX_SAFE_INTEGER) || big < BigInt(Number.MIN_SAFE_INTEGER)) {
          frameFail("i64 frame value is not exactly representable as a JS number");
        }
        word = Number(big);
      } else {
        word = readI32(e, addr);
      }
      // The module's bytes are untrusted evidence: a word outside the schema's
      // declared Int bounds is a tampered/inconsistent frame, not an accepted
      // value (design section 3.4).
      if (w.lo !== undefined && word < Number(w.lo)) {
        frameFail("frame int word is below the schema lower bound");
      }
      if (w.hi !== undefined && word > Number(w.hi)) {
        frameFail("frame int word is above the schema upper bound");
      }
      return word;
    }
    case "float":
    case "decimal":
    case "duration":
    case "timestamp":
    case "uuid":
    case "map":
      frameFail(`read shape '${w.t}' is outside the P6-7 rung-E frame subset`);
      return undefined;
    case "string": {
      if (l.t !== "ptrlen") frameFail("wire string maps to a non-PtrLen layout");
      const ptr = readI32(e, addr + 0);
      const len = readI32(e, addr + 4);
      if (len < 0) frameFail("string length is negative");
      if (w.lo !== undefined && len < Number(w.lo)) {
        frameFail("frame string length is below the schema length lower bound");
      }
      if (w.hi !== undefined && len > Number(w.hi)) {
        frameFail("frame string length exceeds the schema length upper bound");
      }
      // Region membership (untrusted module-written PtrLen): authorize the
      // union of the packed input frame-payload arena and, with frame-bridge
      // v2 V2-B, the fixed read-only rodata span [rodata_base, +extent) the
      // module's Data section initialized. A V2-C bridge result span is added
      // later; anything else (scratch/heap pointer, wrap-around) fails closed
      // without echoing bytes.
      const arenaLo = Number(lane.payload_arena_base);
      const arenaHi = arenaLo + Number(lane.payload_arena_capacity);
      const inArena = ptr >= arenaLo && len <= arenaHi - ptr;
      const rodataLo = Number(lane.rodata_base || 0);
      const rodataHi = rodataLo + Number(lane.rodata_extent || 0);
      const inRodata =
        rodataLo !== 0 && ptr >= rodataLo && len <= rodataHi - ptr;
      const inExtra = extraStringRegions.some(
        (r) => ptr >= Number(r.lo) && len <= Number(r.hi) - ptr);
      if (!inArena && !inRodata && !inExtra) {
        frameFail("frame string payload lies outside an authorized region " +
                  "(input-payload arena, rodata, or a bridge result placement)");
      }
      return new TextDecoder().decode(readRaw(e, ptr, len));
    }
    case "struct": {
      if (l.t !== "struct") frameFail("wire struct maps to a non-struct layout");
      const out = {_type: w.name};
      for (let i = 0; i < w.fields.length; ++i) {
        const field = w.fields[i];
        const slot = fieldLayoutOf(l, i);
        out[field.name] = readValue(e, lane, W, L, field.type, slot.lay,
                                    addr + Number(slot.off), backing,
                                    extraStringRegions);
      }
      return out;
    }
    case "option": {
      if (l.t !== "enum") frameFail("wire option maps to a non-enum layout");
      const tag = readI32(e, addr);
      if (tag === 0) return null;
      if (tag !== 1) frameFail("option tag is not in {0,1}");
      const variant = l.variants[1];
      if (variant === undefined) frameFail("option Some variant has no payload layout");
      return readValue(e, lane, W, L, w.value, variant.lay,
                       addr + Number(l.payload_offset), backing,
                       extraStringRegions);
    }
    case "enum": {
      if (l.t !== "enum") frameFail("wire enum maps to a non-enum layout");
      const ordinal = readI32(e, addr);
      if (ordinal < 0 || ordinal >= w.variants.length) {
        frameFail("enum tag is out of the declared variant range");
      }
      const wireVariant = w.variants[ordinal];
      const out = {_enum: w.name, _variant: wireVariant.name};
      if (wireVariant.kind !== "unit") {
        const layoutVariant = l.variants[ordinal];
        if (layoutVariant === undefined) frameFail("enum variant has no payload layout");
        const payloadAddr = addr + Number(l.payload_offset);
        if (wireVariant.kind === "tuple") {
          const payloadLayout = L[layoutVariant.lay];
          const arr = [];
          for (let i = 0; i < wireVariant.slots.length; ++i) {
            const slot = fieldLayoutOf(payloadLayout, i);
            arr.push(readValue(e, lane, W, L, wireVariant.slots[i].type, slot.lay,
                               payloadAddr + Number(slot.off), backing,
                               extraStringRegions));
          }
          out._payload = arr;
        } else {
          const payloadLayout = L[layoutVariant.lay];
          const named = {};
          for (let i = 0; i < wireVariant.slots.length; ++i) {
            const slot = fieldLayoutOf(payloadLayout, i);
            named[wireVariant.slots[i].name] =
              readValue(e, lane, W, L, wireVariant.slots[i].type, slot.lay,
                        payloadAddr + Number(slot.off), backing,
                        extraStringRegions);
          }
          out._named_payload = named;
        }
      }
      return out;
    }
    case "sequence": {
      if (l.t !== "container") frameFail("wire sequence maps to a non-container layout");
      const len = readI32(e, addr + 4);
      const capacity = Number(w.capacity ?? l.capacity);
      if (len < 0 || len > capacity) frameFail("container length is out of capacity");
      let base = readI32(e, addr + 0);
      const placement = backing.get(lId);
      if (placement !== undefined) {
        // The packed input container's elements live in its fixed placement.
        base = placement.base;
        if (len * Number(l.stride) > placement.extent) frameFail("container overruns placement");
      }
      const stride = Number(l.stride);
      const out = [];
      for (let i = 0; i < len; ++i) {
        out.push(readValue(e, lane, W, L, w.element, l.element, base + i * stride,
                           backing, extraStringRegions));
      }
      if (w.kind === "set") {
        const canonical = out.map((v) => JSON.stringify(v));
        canonical.sort();
        for (let i = 1; i < canonical.length; ++i) {
          if (canonical[i] === canonical[i - 1]) frameFail("set carries a duplicate element");
        }
      }
      return out;
    }
    case "tuple": {
      if (l.t !== "struct") frameFail("wire tuple maps to a non-struct layout");
      const out = [];
      for (let i = 0; i < w.elements.length; ++i) {
        const slot = fieldLayoutOf(l, i);
        out.push(readValue(e, lane, W, L, w.elements[i], slot.lay,
                           addr + Number(slot.off), backing,
                           extraStringRegions));
      }
      return out;
    }
  }
  frameFail(`unhandled read shape '${w.t}'`);
  return undefined;
}

// Packs the canonical input frame for a p6-frame agent. Must run BEFORE the
// step() walk because non-final computed handlers read the packed input frame
// to decide their goto.
function packP6Input(probe) {
  const lane = descriptor.frame_lane;
  const W = descriptor.wire_schema.nodes;
  const L = lane.layouts;
  const backing = backingByLayout(lane);
  const arena = {cursor: lane.payload_arena_base,
                 base: lane.payload_arena_base,
                 capacity: lane.payload_arena_capacity};
  const e = probe.exports;
  // Zero every reserved region the packer may name, so padding words are 0.
  new Uint8Array(e.memory.buffer, 1024, 16384 - 1024).fill(0);
  packValue(e, lane, W, L, JSON.parse(probe.scenarioInput),
            descriptor.wire_schema.roots.input, lane.input_layout,
            lane.input_base, backing, arena);
  return {lane, W, L, backing};
}

// Encodes the runv result frame after the run.
function encodeP6Output(probe, packed) {
  const e = probe.exports;
  const {lane, W, L, backing} = packed;
  const tuple = e.runv();
  if (tuple[0] !== 0) frameFail(`runv returned non-OK status ${tuple[0]}`);
  const authorizedBase =
      lane.final_kind === "computed" ? lane.output_base : lane.input_base;
  if (tuple[1] !== authorizedBase) {
    frameFail(`runv value_ptr ${tuple[1]} is not the authorized base ${authorizedBase} ` +
              `for a '${lane.final_kind}' final`);
  }
  // V2-C: a computed final may materialize a String projected from a capability
  // bridge result through the context frame; its payload bytes live in that
  // call site's disjoint result placement. Authorize every bridge result
  // payload arena for the output-frame walk.
  const outputStringRegions = (lane.bridge_call_sites ?? [])
      .filter((s) => s.result_payload_capacity > 0)
      .map((s) => ({lo: s.result_payload_base,
                    hi: s.result_payload_base + s.result_payload_capacity}));
  const output = readValue(e, lane, W, L, descriptor.wire_schema.roots.output,
                           lane.output_layout, tuple[1], backing,
                           outputStringRegions);
  return JSON.stringify(output);
}

// V2-C: collect the state sequence on a FRESH instance via step(). A frame
// bridge puts real effects inside non-final handlers, and runv() walks
// init->final itself, so an explicit step() walk on the canonical instance
// would fire every bridge capability a second time (the evaluator runs the
// flow exactly once). The instrumentation instance's bridge replays are
// served in "states" mode and its events discarded; only the state names are
// evidence.
async function collectStatesViaStep(compiled) {
  const statesProbe = await makeInstance(compiled, "states");
  packP6Input(statesProbe);
  const se = statesProbe.exports;
  const lane = descriptor.agent_lane;
  const walked = [lane.states[lane.initial_state]];
  let previous = lane.initial_state;
  let guard = lane.states.length + 2;
  let stabilized = false;
  while (guard-- > 0) {
    const before = se.transition_count.value;
    const next = se.step();
    if (next !== previous) {
      if (se.current_state() !== next) fail("current_state does not match step()");
      if (se.transition_count.value !== before + 1) {
        fail("transition_count not bumped exactly once per goto");
      }
      walked.push(lane.states[next]);
      previous = next;
    } else {
      if (se.current_state() !== next) fail("final state is not stable");
      stabilized = true;
      break;
    }
  }
  if (!stabilized) {
    fail(`step() bounded walk guard (${lane.states.length + 2}) expired without a stable ` +
         `final state for ${lane.agent}; last state ${previous}`);
  }
  return walked;
}

// ---- agent lane --------------------------------------------------------------
async function runAgent(compiled) {
  const lane = descriptor.agent_lane;
  const probe = await makeInstance(compiled, "scenario");
  const e = probe.exports;
  if (e.ahfl_abi_version.value !== 1) fail("abi version mismatch");
  if (e.current_state() !== lane.initial_state) {
    fail(`agent initial state ${e.current_state()} != ${lane.initial_state}`);
  }

  // V2-C: a frame-bridge module invokes capabilities from non-final handlers.
  // runv() is the one canonical run (it walks init->final on its own); the
  // state sequence is collected on a separate instrumentation instance so the
  // bridge effects fire exactly once on the canonical instance.
  const hasBridge = descriptor.frame_contract === "p6_frame" &&
                    descriptor.frame_lane.bridge_call_sites.length > 0;

  // RFC 0026 P6-7: pack the canonical input into the P4-D frame BEFORE the
  // step() walk, since non-final computed handlers branch on packed input.
  const p6 = descriptor.frame_contract === "p6_frame" ? packP6Input(probe) : null;

  let walked;
  if (p6 !== null && hasBridge) {
    walked = await collectStatesViaStep(compiled);
  } else {
    // step() drives the state walk to its stable final state WITHOUT invoking a
    // terminal capability (a capability final reports its state on step, it does
    // not call).
    walked = [lane.states[lane.initial_state]];
    let previous = lane.initial_state;
    let guard = lane.states.length + 2;
    let stabilized = false;
    while (guard-- > 0) {
      const before = e.transition_count.value;
      const next = e.step();
      if (next !== previous) {
        if (e.current_state() !== next) fail("current_state does not match step()");
        if (e.transition_count.value !== before + 1) {
          fail("transition_count not bumped exactly once per goto");
        }
        walked.push(lane.states[next]);
        previous = next;
      } else {
        if (e.current_state() !== next) fail("final state is not stable");
        stabilized = true;
        break;
      }
    }
    if (!stabilized) {
      fail(`step() bounded walk guard (${lane.states.length + 2}) expired without a stable ` +
           `final state for ${lane.agent}; last state ${previous}`);
    }
  }

  // RFC 0026 P6-7: a p6-frame agent invokes runv on the packed frame and
  // encodes the output instead of exchanging opaque JSON over run2.
  if (p6 !== null) {
    const outputRaw = encodeP6Output(probe, p6);
    return {
      status: "completed",
      states: walked.map((state) => ({agent: lane.agent, state})),
      capabilities: probe.events.map((event) => event.name),
      outputRaw,
      transitions: e.transition_count.value,
      completedNodes: null,
    };
  }

  // run2 resets and performs the terminal identity/capability action.
  const [ptr, len] = probe.writeInput();
  const tuple = e.run2(ptr, len);
  let outputRaw = null;
  if (tuple[0] === 0) {
    outputRaw = decoder.decode(new Uint8Array(e.memory.buffer, tuple[1], tuple[2]));
    // Release the host-allocated result frame after decoding it. For an
    // identity final this is the borrowed input frame (dealloc is a no-op
    // either way); for a capability final tuple[1..2] names the frame the
    // ahfl_cap import allocated, which the host owns once run2 returns it.
    e.dealloc(tuple[1], tuple[2]);
  }
  e.dealloc(ptr, len);

  return {
    status: normalizeStatus(tuple[0]),
    states: walked.map((state) => ({ agent: lane.agent, state })),
    capabilities: probe.events.map((event) => event.name),
    outputRaw,
    transitions: e.transition_count.value,
    completedNodes: null,
  };
}

// ---- workflow lane -----------------------------------------------------------
function readEventRecords(lane, nodeCount, e) {
  const view = new DataView(e.memory.buffer);
  const base = descriptor.event_buffer.log_base;
  const recordsBase = descriptor.event_buffer.records_base;
  const recordBytes = descriptor.event_buffer.record_bytes;
  const hasEventRegion = descriptor.imports.length > 0;

  if (hasEventRegion) {
    if (view.getUint32(base + 4, true) !== 0) fail("node-event header pad is nonzero");
    const count = view.getUint32(base, true);
    if (count !== nodeCount) fail(`event_count ${count} != node_count ${nodeCount}`);
    const records = [];
    for (let i = 0; i < count; ++i) {
      const addr = recordsBase + i * recordBytes;
      const record = {
        tag: view.getUint8(addr),
        pad1: view.getUint8(addr + 1),
        pad2: view.getUint8(addr + 2),
        pad3: view.getUint8(addr + 3),
        nodeId: view.getUint32(addr + 4, true),
        schedulePos: view.getUint32(addr + 8, true),
        capability: view.getUint32(addr + 12, true),
        sourceSymbol: view.getBigUint64(addr + 16, true),
        ordinal: view.getBigUint64(addr + 24, true),
        recordStatus: view.getUint32(addr + 32, true),
        reserved: view.getUint32(addr + 36, true),
      };
      if (record.pad1 !== 0 || record.pad2 !== 0 || record.pad3 !== 0) {
        fail(`record ${i} pad bytes are nonzero`);
      }
      if (record.reserved !== 0) fail(`record ${i} reserved bytes are nonzero`);
      if (record.schedulePos !== i) fail(`record ${i} schedule_pos ${record.schedulePos}`);
      if (record.recordStatus !== 0) fail(`record ${i} status is not OK`);
      if (record.ordinal !== 0n) fail(`record ${i} invocation ordinal is nonzero`);

      // Cross-check every field against the descriptor's scheduled node, so the
      // record is real execution evidence, not just a count: tag 1 carries the
      // capability ordinal + source SymbolId; tag 0 is an identity node and all
      // capability fields are zero.
      const node = lane.nodes.find((n) => n.node_id === record.nodeId);
      if (node === undefined) fail(`record ${i} names unknown node ${record.nodeId}`);
      if (record.schedulePos !== node.schedule_pos) {
        fail(`record ${i} schedule_pos disagrees with the descriptor node`);
      }
      if (node.has_capability) {
        if (record.tag !== 1) fail(`record ${i} capability node tag is not 1`);
        if (record.capability !== node.capability_ordinal) {
          fail(`record ${i} capability ordinal ${record.capability} != ${node.capability_ordinal}`);
        }
        if (record.sourceSymbol !== BigInt(node.source_symbol)) {
          fail(`record ${i} source_symbol ${record.sourceSymbol} != ${node.source_symbol}`);
        }
      } else {
        if (record.tag !== 0) fail(`record ${i} identity node tag is not 0`);
        if (record.capability !== 0 || record.sourceSymbol !== 0n) {
          fail(`record ${i} identity node carries capability identity`);
        }
      }
      records.push(record);
    }
    return records;
  }
  // Identity workflow: no event region (bump heap starts at the log base), so
  // reconstruct schedule-order walks from the descriptor; the counters are the
  // real execution evidence.
  return lane.nodes.map((node) => ({ nodeId: node.node_id, schedulePos: node.schedule_pos }));
}

function recordStates(lane, record) {
  const node = lane.nodes.find((n) => n.node_id === record.nodeId);
  if (node === undefined) fail(`event record names unknown node ${record.nodeId}`);
  const agentWalk = lane.agents[node.runner];
  if (agentWalk === undefined) fail(`node ${record.nodeId} has no runner ${node.runner}`);
  return agentWalk.walk.map((state) => ({ agent: agentWalk.agent, state }));
}

async function runWorkflow(compiled) {
  const lane = descriptor.workflow_lane;
  const nodeCount = descriptor.workflow_node_count;
  const probe = await makeInstance(compiled, "scenario");
  const e = probe.exports;
  if (e.workflow_node_count.value !== nodeCount) {
    fail(`workflow_node_count ${e.workflow_node_count.value} != ${nodeCount}`);
  }
  // Fail-closed contract: step()/current_state() trap BEFORE any effect.
  for (const name of ["step", "current_state"]) {
    expectTraps(() => e[name](), `${name}()`);
    if (e.workflow_completed_count.value !== 0 || e.transition_count.value !== 0) {
      fail(`${name}() had an effect before trapping`);
    }
  }

  // Deterministic transition expectation: each scheduled node executes its
  // runner agent's full walk, which performs exactly (walk length - 1) gotos.
  // Summed over nodes (two nodes may share one packaged runner instance), this
  // is the module's exact transition_count, derived from descriptor facts --
  // never a weak non-zero check.
  const expectedTransitions = expectedWorkflowTransitions();

  const [ptr, len] = probe.writeInput();
  const tuple = e.run2(ptr, len);
  const status = normalizeStatus(tuple[0]);
  let outputRaw = null;
  let states = [];
  if (tuple[0] === 0) {
    if (tuple[1] === 0 || tuple[2] === 0) fail("OK workflow tuple carried an empty frame");
    outputRaw = decoder.decode(new Uint8Array(e.memory.buffer, tuple[1], tuple[2]));
    if (e.workflow_completed_count.value !== nodeCount) {
      fail(`completed_count ${e.workflow_completed_count.value} != ${nodeCount}`);
    }
    if (e.transition_count.value !== expectedTransitions) {
      fail(`transition_count ${e.transition_count.value} != ${expectedTransitions} ` +
           "(sum of runner walk gotos)");
    }
    const records = readEventRecords(lane, nodeCount, e);
    states = records.map((record) => recordStates(lane, record)).flat();
    // Common-KAT bridge: for the committed two-node cap/identity fixture the
    // executed node-event region must equal the contract-derived golden hex,
    // byte for byte -- the same value the C++ decoder KAT pins.
    if (descriptor.imports.length > 0 && descriptorMatchesCommonKat(lane, nodeCount)) {
      const regionLength = descriptor.event_buffer.header_bytes +
                           nodeCount * descriptor.event_buffer.record_bytes;
      const actualHex = regionHex(e.memory, descriptor.event_buffer.log_base, regionLength);
      if (actualHex !== kNodeEventGoldenHex) {
        fail(`node-event region != contract-derived common KAT golden: ${actualHex}`);
      }
    }
    // Identity workflow: run2 is idempotent on a persistent instance; the
    // replay reaches the exact same counters (the retired bespoke host pinned
    // this, not just non-zero).
    if (descriptor.imports.length === 0) {
      const replay = e.run2(ptr, len);
      if (replay[0] !== tuple[0] || replay[1] !== tuple[1] || replay[2] !== tuple[2] ||
          e.workflow_completed_count.value !== nodeCount ||
          e.transition_count.value !== expectedTransitions) {
        fail(`identity workflow replay counters/tuple mismatch: ${replay}`);
      }
      // The replayed OK tuple still names a host-readable frame; release it.
      e.dealloc(replay[1], replay[2]);
    }
    // Release the host-allocated OK result frame (the capability-returned
    // tuple, or the borrowed input frame of an identity final) after decoding.
    e.dealloc(tuple[1], tuple[2]);
  } else {
    if (e.workflow_completed_count.value !== 0) fail("non-OK run published completion events");
    if (e.transition_count.value !== 0) fail("non-OK run advanced the transition counter");
  }
  e.dealloc(ptr, len);

  return {
    status,
    states,
    capabilities: probe.events.map((event) => event.name),
    outputRaw,
    transitions: e.transition_count.value,
    completedNodes: e.workflow_completed_count.value,
  };
}

// ---- ABI normalization matrix (real-engine negative coverage) ----------------
//
// These assert the module's own normalization of the capability tuple and the
// pending latch, on FRESH instances, using descriptor facts (import presence,
// heap base, event buffer layout). They do not change the canonical observation.
async function runAbiProbes(compiled) {
  const hasCapabilities = descriptor.imports.length > 0;
  const isWorkflow = Boolean(descriptor.workflow_lane);

  if (!isWorkflow) {
    // ---- agent lane ----
    // V2-C: a frame-bridge agent performs its capability in non-final handlers,
    // behind the (i32)->(i32,i32) protocol. Its normalization probes use the
    // packed P4-D frame + runv, NEVER the legacy opaque entry points: run2/run
    // have no defined contract on this lane and writing opaque JSON over the
    // fixed input frame would corrupt the bridge argument spans.
    const hasBridge = descriptor.frame_contract === "p6_frame" &&
                      (descriptor.frame_lane?.bridge_call_sites?.length ?? 0) > 0;
    if (hasBridge) {
      // OK path: one bridge invocation, runv returns the computed output base.
      {
        const p = await makeInstance(compiled, "scenario");
        packP6Input(p);
        const tuple = p.exports.runv();
        if (tuple[0] !== 0 || p.calls !== 1) {
          fail(`bridge runv OK normalization mismatch: ${tuple}, calls=${p.calls}`);
        }
        if (tuple[1] !== descriptor.frame_lane.output_base) {
          fail("bridge runv returned a value_ptr other than the computed output base");
        }
      }
      // Every non-OK bridge status (error / pending / unknown) traps single-run;
      // there is no pending arm. The instance does not latch (durable replay is
      // the D2b authority), but each probe starts fresh anyway.
      for (const mode of ["error", "pending", "unknown"]) {
        const p = await makeInstance(compiled, mode);
        packP6Input(p);
        let trapped = false;
        try { p.exports.runv(); }
        catch (error) { trapped = error instanceof WebAssembly.RuntimeError; }
        if (!trapped || p.calls !== 1) {
          fail(`bridge runv ${mode} did not trap after exactly one call (trapped=${trapped}, ` +
               `calls=${p.calls})`);
        }
      }
      return;
    }
    if (hasCapabilities) {
      // Every non-OK / malformed-OK normalization collapses to (ERROR,0,0) with
      // exactly one import call and no state/capability side effect beyond it.
      for (const mode of ["ok-null", "ok-zero-len", "error", "unknown"]) {
        const p = await makeInstance(compiled, mode);
        const result = p.exports.run2(...p.writeInput());
        const expected = [1, 0, 0];
        if (result.some((v, i) => v !== expected[i]) || p.calls !== 1) {
          fail(`agent ${mode} normalization mismatch: ${result}`);
        }
      }
      // PENDING-nonnull normalizes to ERROR and does NOT latch: a second run2 on
      // the same instance re-invokes the import.
      {
        const p = await makeInstance(compiled, "pending-nonnull");
        const first = p.exports.run2(...p.writeInput());
        if (first[0] !== 1 || first[1] !== 0 || first[2] !== 0) {
          fail("agent pending-nonnull did not normalize to (ERROR,0,0)");
        }
        let trapped = false;
        let second;
        try { second = p.exports.run2(...p.writeInput()); }
        catch (error) { trapped = error instanceof WebAssembly.RuntimeError; }
        if (trapped || second[0] !== 1 || p.calls !== 2) {
          fail("agent pending-nonnull incorrectly latched the instance");
        }
      }
      // PENDING latches: (PENDING,0,0), then a second run2 traps pre-effect and
      // does not re-invoke the import.
      {
        const p = await makeInstance(compiled, "pending");
        const first = p.exports.run2(...p.writeInput());
        if (first[0] !== 2 || first[1] !== 0 || first[2] !== 0 || p.calls !== 1) {
          fail("agent pending did not return (PENDING,0,0) with one call");
        }
        expectTraps(() => p.exports.run2(...p.writeInput()),
                    "second agent run2 after PENDING (latch)");
        if (p.calls !== 1) fail("agent latch re-invoked the capability");
      }
      // The pointer-only legacy run() traps before any capability effect.
      {
        const p = await makeInstance(compiled, "ok");
        expectTraps(() => p.exports.run(...p.writeInput()), "legacy agent run()");
        if (p.calls !== 0) fail("legacy agent run() reached the capability");
      }
    } else {
      // RFC 0026 P6-7: runv is the SOLE defined observation entry for a
      // p6-frame agent. Only it is probed here: pack the P4-D frame on a FRESH
      // instance and assert runv returns OK with the descriptor-authorized
      // root (the input base for an identity final, the output base for a
      // computed final).
      //
      // The legacy pointer exports run()/run2() are deliberately NOT exercised
      // on this lane, because they have no defined p6-frame contract and the
      // outcome is frame-shape dependent. Both share the E1 identity arm: the
      // walk runs and run2 returns (OK, borrowed host ptr, len), run() echoes
      // the host ptr. But a p6-frame agent's canonical input lives in the
      // fixed P4-D regions, and the host bump allocator starts at the input
      // frame base (1024), so a legacy writeInput() overwrites the packed
      // header with JSON bytes: a scalar-only frame's walk still completes and
      // returns the borrowed (arbitrary) span, while a frame whose walk
      // dereferences a collection pointer traps Wasm OOB. Neither result is a
      // value observation -- only runv's value_ptr is -- so asserting either
      // would pin incidental behavior, not a contract.
      if (descriptor.frame_contract === "p6_frame") {
        const p = await makeInstance(compiled, "ok");
        const packed = packP6Input(p);
        const tuple = p.exports.runv();
        if (tuple[0] !== 0) fail("p6-frame runv did not return OK");
        const authorized = packed.lane.final_kind === "computed"
            ? packed.lane.output_base : packed.lane.input_base;
        if (tuple[1] !== authorized) fail("p6-frame runv returned an unauthorized value_ptr");
        return;
      }
      // Identity agent: legacy run() executes the same schedule and returns the
      // borrowed input pointer.
      const p = await makeInstance(compiled, "ok");
      const [ptr, len] = p.writeInput();
      if (p.exports.run(ptr, len) !== ptr) fail("identity agent legacy run() pointer");
      p.exports.dealloc(ptr, len);
    }
    return;
  }

  // ---- workflow lane ----
  for (const name of ["step", "current_state"]) {
    const p = await makeInstance(compiled, "ok");
    expectTraps(() => p.exports[name](), `workflow ${name}()`);
  }

  if (!hasCapabilities) {
    // Identity workflow: run2 returns the borrowed tuple, counters reach the
    // node count, and a second run2 replays the full schedule idempotently.
    const p = await makeInstance(compiled, "ok");
    const input = p.writeInput();
    const expectedTransitions = expectedWorkflowTransitions();
    const a = p.exports.run2(...input);
    if (a[0] !== 0 || a[1] !== input[0] || a[2] !== input[1]) {
      fail("identity workflow run2 tuple mismatch");
    }
    if (p.exports.workflow_completed_count.value !== descriptor.workflow_node_count ||
        p.exports.transition_count.value !== expectedTransitions) {
      fail(`identity workflow counters mismatch (completed=${p.exports.workflow_completed_count.value}, ` +
           `transitions=${p.exports.transition_count.value}, expected=${expectedTransitions})`);
    }
    const b = p.exports.run2(...input);
    if (b[0] !== 0 || b[1] !== input[0] || b[2] !== input[1] ||
        p.exports.workflow_completed_count.value !== descriptor.workflow_node_count ||
        p.exports.transition_count.value !== expectedTransitions) {
      fail(`identity workflow second run2 did not replay identically (transitions=` +
           `${p.exports.transition_count.value}, expected=${expectedTransitions})`);
    }
    p.exports.dealloc(a[1], a[2]);
    if (p.exports.run(input[0], input[1]) !== input[0]) {
      fail("identity workflow legacy run() did not return the borrowed frame");
    }
    return;
  }

  // Capability workflow non-OK normalization: no event, no completion, and the
  // node-event header + record region stays byte-for-byte zero.
  for (const mode of ["ok-null", "ok-zero-len", "error", "unknown"]) {
    const p = await makeInstance(compiled, mode);
    const result = p.exports.run2(...p.writeInput());
    if (result[0] !== 1 || result[1] !== 0 || result[2] !== 0 || p.calls !== 1) {
      fail(`workflow ${mode} normalization mismatch: ${result}`);
    }
    if (p.exports.workflow_completed_count.value !== 0) {
      fail(`workflow ${mode} published a completion event`);
    }
    assertEventRegionZero(p.exports, `workflow ${mode}`, /*includeHeader=*/true);
  }
  // PENDING-nonnull normalizes to ERROR and does not latch.
  {
    const p = await makeInstance(compiled, "pending-nonnull");
    const first = p.exports.run2(...p.writeInput());
    if (first[0] !== 1 || p.calls !== 1) {
      fail("workflow pending-nonnull did not normalize to ERROR with one call");
    }
    assertEventRegionZero(p.exports, "workflow pending-nonnull first", /*includeHeader=*/true);
    let trapped = false;
    try { p.exports.run2(...p.writeInput()); }
    catch (error) { trapped = error instanceof WebAssembly.RuntimeError; }
    if (trapped || p.calls !== 2) fail("workflow pending-nonnull incorrectly latched");
    assertEventRegionZero(p.exports, "workflow pending-nonnull replay", /*includeHeader=*/true);
  }
  // PENDING latches the instance.
  {
    const p = await makeInstance(compiled, "pending");
    const first = p.exports.run2(...p.writeInput());
    if (first[0] !== 2 || p.calls !== 1 ||
        p.exports.workflow_completed_count.value !== 0) {
      fail("workflow pending did not suspend with no event");
    }
    assertEventRegionZero(p.exports, "workflow pending first", /*includeHeader=*/true);
    expectTraps(() => p.exports.run2(...p.writeInput()),
                "second workflow run2 after PENDING (latch)");
    if (p.calls !== 1) fail("workflow latch re-invoked the capability");
    assertEventRegionZero(p.exports, "workflow pending after latch trap",
                          /*includeHeader=*/true);
  }
  // Corrupt-count defensive coordinate gate: a host-injected event_count before
  // the OK frame returns makes the scheduler reject (ERROR) with no record. The
  // scheduler must NOT overwrite the host-injected count, and every record-body
  // slot must remain zero-filled.
  {
    const p = await makeInstance(compiled, "corrupt-count");
    const result = p.exports.run2(...p.writeInput());
    if (result[0] !== 1 || p.calls !== 1) {
      fail("workflow corrupt-count was not rejected with one call");
    }
    if (new DataView(p.exports.memory.buffer).getUint32(
          descriptor.event_buffer.log_base, true) !== 99) {
      fail("workflow corrupt-count published despite coordinate mismatch");
    }
    assertEventRegionZero(p.exports, "workflow corrupt-count", /*includeHeader=*/false);
  }
  // Legacy pointer-only run() traps before any effect.
  {
    const p = await makeInstance(compiled, "ok");
    expectTraps(() => p.exports.run(...p.writeInput()), "legacy workflow run()");
    if (p.calls !== 0) fail("legacy workflow run() reached a capability");
  }
  // Checked bump allocator: over-capacity returns 0 without advancing, then the
  // next allocations start at the descriptor heap base and advance exactly.
  {
    const p = await makeInstance(compiled, "ok");
    if (p.exports.alloc(70000) !== 0) fail("over-capacity alloc did not return 0");
    const heapBase = descriptor.heap_base;
    if (p.exports.alloc(1) !== heapBase) fail("post-fail alloc did not return heap_base");
    if (p.exports.alloc(1) !== heapBase + 1) fail("successful alloc did not advance");
  }
}

// ---- run ---------------------------------------------------------------------
const compiled = await WebAssembly.compile(bytes);
for (const listedImport of WebAssembly.Module.imports(compiled)) {
  if (listedImport.module !== "ahfl_cap" ||
      !descriptor.imports.some((i) => i.field === listedImport.name)) {
    fail(`module imports undeclared capability ${listedImport.module}.${listedImport.name}`);
  }
}
for (const importEntry of descriptor.imports) {
  if (!WebAssembly.Module.imports(compiled)
        .some((i) => i.module === "ahfl_cap" && i.name === importEntry.field)) {
    fail(`descriptor names capability ${importEntry.field} the module does not import`);
  }
}

const observation = descriptor.workflow_lane
  ? await runWorkflow(compiled)
  : await runAgent(compiled);

await runAbiProbes(compiled);

fs.writeFileSync(args.output, emitObservation(observation));
console.log(`Node embedded-engine observation (NOT wasmtime evidence) for ` +
            `${descriptor.case}/${scenario.name}: ${observation.status}`);
