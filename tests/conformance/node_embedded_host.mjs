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

// Fixed P6 frame-region bases (the wire ABI SSOT is
// core_wasm_abi_constants.hpp; the certified host hardcodes the same wasm32
// constants rather than trusting module words).
const kContextFrameBase = 4096;
const kContextFrameEnd = 7168;
const kScratchArenaBase = 7168;
const kScratchArenaEnd = 12288;

// Classify one bridge parameter by its WIRE shape (the SSOT discriminator): a
// scalar / tag-only enum / String PtrLen is spilled into the call site's
// private 8-byte slots; a struct / tuple / option / payload-bearing enum stays
// at its aggregate root address. This mirrors the C++ planning classification
// in plan_capability_call (p6_scalar_kind: PtrLen/Index/Bool/Int/F64 spill,
// Ptr roots keep their address).
function bridgeParamKind(W, wireId) {
  const w = W[wireId];
  if (w === undefined) frameFail("bridge parameter names an unknown wire node");
  switch (w.t) {
    case "bool":
    case "int":
    case "unit":
    case "string":
    case "float":
    case "decimal":
    case "duration":
      return "spill";
    case "struct":
    case "option":
    case "tuple":
    case "map":
      return "root";
    case "enum": {
      const tagOnly = w.variants.every((v) => v.kind === "unit");
      return tagOnly ? "spill" : "root";
    }
    case "timestamp":
    case "uuid":
    case "sequence":
      frameFail(`a bridge parameter of wire shape '${w.t}' is outside the frame-bridge subset`);
      return "reject";
  }
  frameFail(`unhandled bridge parameter wire shape '${w.t}'`);
  return "reject";
}

function inRegion(addr, len, region) {
  return addr >= region.lo && len <= region.hi - addr;
}

// Read ONE bridge argument P4-D span into a JS value. The descriptor ptr/len
// words are UNTRUSTED module evidence (design sections 4.3/5): each argument
// span is independently region-membership-tested against exactly the homes
// that parameter kind may name, and the module-provided len is only
// cross-checked against the dense layout (8 for a spill slot, the layout root
// size for an aggregate root) -- never used as the span authority.
function readBridgeArgValue(e, lane, W, L, backing, currentSite, wireId, layoutId,
                            ptr, len, rootRegions) {
  if (!Number.isInteger(ptr) || !Number.isInteger(len) ||
      ptr < 0 || len < 0 || ptr + len > kPageSize) {
    frameFail("bridge argument span lies outside the fixed page");
  }
  const l = L[layoutId];
  if (l === undefined) frameFail("bridge argument names an unknown layout root");
  const kind = bridgeParamKind(W, wireId);
  if (kind === "spill") {
    // Every spilled slot is one 8-byte private slot in THIS site's spill
    // window. A scalar reads its first 4/8 bytes; a String PtrLen reads the
    // 8-byte pair and its payload is then authorized against the payload
    // regions; a tag-only enum is one i32 tag word (p6_scalar_kind's Index
    // kind): its dense layout carries t:"enum" but it is physically a spilled
    // scalar, never a root address. The module cannot name another site's
    // slot, the context frame, rodata, or an arbitrary page word as a source.
    if (l.t !== "scalar" && l.t !== "ptrlen" && l.t !== "enum") {
      frameFail("a spilled bridge parameter maps to a non-scalar/non-PtrLen/non-tag-enum layout");
    }
    if (len !== 8) {
      frameFail(`a spilled bridge parameter descriptor len must be 8 (got ${len})`);
    }
    const spillRegion = {
      lo: Number(currentSite.spill_base),
      hi: Number(currentSite.spill_base) + Number(currentSite.spill_extent),
    };
    if (!(spillRegion.hi > spillRegion.lo) || !inRegion(ptr, 8, spillRegion)) {
      frameFail("a spilled bridge parameter lies outside its call site's private spill window");
    }
  } else {
    // An aggregate root stays at its existing stable address. Authorize only
    // the packed input frame, the fixed context frame, the constructor scratch
    // arena, or ANOTHER call site's result placement -- never this call's own
    // result (it is the call output, unwritten before the invocation).
    // A Map is a handle-shaped aggregate root (the address of its inline
    // (ptr,len) header); the P4-D container header is always 8 bytes.
    if (l.t !== "struct" && l.t !== "enum" && l.t !== "container") {
      frameFail("an aggregate bridge parameter maps to a non-struct/non-enum/non-container layout");
    }
    const rootSize = l.t === "container" ? 8 : Number(l.size);
    if (len !== rootSize) {
      frameFail("an aggregate bridge parameter descriptor len disagrees with its dense layout " +
                `root size (descriptor ${len}, layout ${rootSize})`);
    }
    if (!rootRegions.some((r) => inRegion(ptr, rootSize, r))) {
      frameFail("an aggregate bridge parameter names a root outside an authorized frame / " +
                "scratch / other-site result placement");
    }
  }
  return readValue(e, lane, W, L, wireId, layoutId, ptr, backing,
                   bridgeStringRegions(lane, currentSite));
}

// Union of the String-payload regions the read walker may authorize for a
// BRIDGE argument: the packed input-payload arena, the rodata region, the
// construct heap, and every OTHER call site's result placement payload. The
// CURRENT call site's own result region is excluded — its block is this
// call's output, so before the host packs it that region can never be a
// valid arg source.
//
// Mirrors build_bridge_string_regions in capability_import.cpp: a
// capability-final's construct terminal builds its argument at the node's
// O_k block (workflow lane) or the agent output frame (agent lane), so
// String leaves inside that argument must be authorized.
function bridgeStringRegions(lane, currentSite) {
  const regions = [];
  // Packed input-payload arena.
  const arenaLo = Number(lane.payload_arena_base || 0);
  const arenaHi = arenaLo + Number(lane.payload_arena_capacity || 0);
  if (arenaLo !== 0 && arenaHi > arenaLo) {
    regions.push({lo: arenaLo, hi: arenaHi});
  }
  // Workflow entry payload arena (packed input frame; String arguments
  // spilled from a bridge call site point into it).
  const wfLane = descriptor.workflow_lane;
  if (wfLane !== undefined && Number(wfLane.entry_payload_capacity || 0) > 0) {
    regions.push({lo: Number(wfLane.entry_payload_base),
                  hi: Number(wfLane.entry_payload_base) +
                      Number(wfLane.entry_payload_capacity)});
  }
  // Rodata region.
  const rodataLo = Number(lane.rodata_base || 0);
  const rodataHi = rodataLo + Number(lane.rodata_extent || 0);
  if (rodataLo !== 0 && rodataHi > rodataLo) {
    regions.push({lo: rodataLo, hi: rodataHi});
  }
  // Construct heap [construct_heap_base, 65536) (String concat results).
  const heapLo = Number(lane.construct_heap_base || 0);
  if (heapLo !== 0) {
    regions.push({lo: heapLo, hi: 65536});
  }
  for (const s of lane.bridge_call_sites) {
    if (s.call_site_id === currentSite.call_site_id) continue;
    if (s.result_payload_capacity > 0) {
      regions.push({lo: s.result_payload_base,
                    hi: s.result_payload_base + s.result_payload_capacity});
    }
  }
  // Workflow lane: every packaged node's O_k block is an authorized String
  // payload home (a construct terminal materializes its argument there).
  if (wfLane?.node_blocks) {
    for (const block of wfLane.node_blocks) {
      if (Number(block.output_size) > 0) {
        regions.push({lo: Number(block.output_base),
                      hi: Number(block.output_base) + Number(block.output_size)});
      }
    }
  } else if (Number(lane.output_size) > 0) {
    // Agent lane: the fixed output frame.
    regions.push({lo: Number(lane.output_base),
                  hi: Number(lane.output_base) + Number(lane.output_size)});
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
    // Resolve the call site FROM THE CONTROL-BLOCK ADDRESS, never from the
    // import ordinal: one capability legally occupies one import (one ordinal)
    // shared by MANY dense call sites (the normal loop / chain shape, design
    // D3 "each reachable capability occupies one low ordinal"). The ordinal is
    // verified against the resolved site only after the block identifies it.
    const sites = lane.bridge_call_sites;
    const stride = Number(lane.bridge_block_stride);
    const rel = blockPtr - lane.bridge_control_base;
    if (!Number.isInteger(blockPtr) || rel < 0 ||
        rel >= sites.length * stride || rel % stride !== 0) {
      frameFail("bridge control-block pointer names an address outside a dense fixed-stride " +
                "control-block slot");
    }
    const block_index = Math.floor(rel / stride);
    const site = sites[block_index];
    if (site === undefined ||
        blockPtr !== lane.bridge_control_base + site.block_offset) {
      frameFail("bridge control-block pointer is not at its dense fixed-stride offset");
    }
    if (site.import_ordinal !== ordinal) {
      frameFail(`bridge call site ${site.call_site_id} names import ordinal ` +
                `${site.import_ordinal} but import ordinal ${ordinal} was invoked`);
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
    // The authorized aggregate-root homes for THIS call (design 4.3/5):
    //  * agent lane: the packed input frame, the fixed context frame, the
    //    constructor scratch arena, and every OTHER call site's disjoint
    //    result placement;
    //  * V2-D workflow lane: every packaged node's I_k / C_k / scratch_k
    //    block span (a bridge in a non-final handler may project the node
    //    input or an earlier handler's context/scratch) and every OTHER call
    //    site's result placement. The current site's own result is excluded
    //    in both modes.
    const rootRegions = [];
    if (descriptor.kind === "workflow" && descriptor.workflow_lane?.node_blocks) {
      for (const block of descriptor.workflow_lane.node_blocks) {
        rootRegions.push({lo: Number(block.input_base),
                          hi: Number(block.input_base) + Number(block.input_size)});
        if (Number(block.context_size) > 0) {
          rootRegions.push({lo: Number(block.context_base),
                            hi: Number(block.context_base) + Number(block.context_size)});
        }
        if (Number(block.scratch_size) > 0) {
          rootRegions.push({lo: Number(block.scratch_base),
                            hi: Number(block.scratch_base) + Number(block.scratch_size)});
        }
        // RFC 0026 dual-track elimination: a capability-final construct
        // terminal materializes its result into the node's O_k block and
        // passes it as the bridge argument. The output block is an
        // authorized aggregate-root home (mirrors build_bridge_root_regions
        // in capability_import.cpp).
        if (Number(block.output_size) > 0) {
          rootRegions.push({lo: Number(block.output_base),
                            hi: Number(block.output_base) + Number(block.output_size)});
        }
      }
    } else {
      rootRegions.push(
        {lo: Number(lane.input_base), hi: Number(lane.input_base) + Number(lane.input_size)},
        {lo: kContextFrameBase, hi: kContextFrameEnd},
        {lo: kScratchArenaBase, hi: kScratchArenaEnd});
    }
    for (const other of sites) {
      if (other.call_site_id === site.call_site_id) continue;
      rootRegions.push({lo: Number(other.result_base),
                        hi: Number(other.result_base) + Number(other.result_extent)});
    }
    const argValues = [];
    for (let i = 0; i < arg_count; ++i) {
      const desc = blockPtr + 8 + 8 * i;
      const ptr = dv.getInt32(desc, true);
      const len = dv.getUint32(desc + 4, true);
      argValues.push(readBridgeArgValue(e, lane, W, L, backing, site,
                                        site.params[i], site.param_layout[i],
                                        ptr, len, rootRegions));
    }

    state.calls += 1;
    state.events.push({
      name: importEntry.name,
      argument: bridgeEnvelope(W, site.params, argValues),
    });

    // ABI-matrix modes for the bridge protocol: a non-OK status makes the module
    // trap single-run, no result packing needed.
    if (mode === "error" || mode === "pending" || mode === "unknown") {
      return [mode === "pending" ? 2 : 1, 0];
    }

    // Invoke the scenario mock (per-name cursor matches the bridge callback).
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

// The wire argument envelope SSOT (mirrors serialize_args_for_wire_json in
// src/runtime/engine/wire_value.cpp EXACTLY). The single-argument "bare
// object" decision is made on the evaluator Value kind there (bare iff the
// value is a StructValue); its wire image is precisely a wire node of
// t=='struct'. A single enum renders {_enum,_variant} but an EnumValue is not
// a StructValue, so the envelope must be {"value":{...}} -- classifying on the
// JS JSON shape ("any object") would diverge for enum / option / tuple args.
function bridgeEnvelope(W, paramWireIds, args) {
  if (args.length === 0) return "{}";
  if (args.length === 1) {
    const wireNode = W[paramWireIds[0]];
    if (wireNode === undefined) frameFail("bridge envelope names an unknown wire parameter");
    if (wireNode.t === "struct") {
      return stringifyValue(args[0]);
    }
    return stringifyValue({value: args[0]});
  }
  return stringifyValue({args});
}

async function makeInstance(compiled, mode) {
  const state = { calls: 0, events: [], perName: new Map() };
  const imports = {};
  for (const importEntry of descriptor.imports) {
    // RFC 0026 dual-track elimination: every capability import uses the bridge
    // type (i32)->(i32,i32).
    const ordinal = importEntry.ordinal;
    imports[importEntry.field] = makeBridgeCallback(
      importEntry, ordinal, () => instance, () => state, mode);
  }
  const importObject = { ahfl_cap: imports };
  const instance = (await WebAssembly.instantiate(compiled, importObject));
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
function emitObservation({ status, states, capabilities, capabilityArguments, outputRaw,
                           transitions,
                           completedNodes }) {
  const fields = [
    [`"capability_arguments"`, `[${capabilityArguments.join(",")}]`],
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
function writeF64(e, addr, value) {
  checkRange(addr, 8);
  dataView(e).setFloat64(addr, value, true);
}
function readF64(e, addr) {
  checkRange(addr, 8);
  return dataView(e).getFloat64(addr, true);
}

// Marker for a frame-read f64 leaf. JSON.stringify serializes an integral
// double as "2" (integer syntax), which would make the C++ comparator parse
// the token as Int (not Float) and diverge from the blessing's "2.0".
// stringifyValue formats FloatValue leaves with the runtime SSOT float
// spelling (mirrors ahfl::json::format_wire_float) so the token is always
// classified as Float and canonical re-serialization agrees.
class FloatValue {
  constructor(value) { this.value = value; }
}

// Mirrors ahfl::json::format_wire_float: shortest round-trip spelling, then
// keep ".0" for integral floats so the wire token is valid float syntax.
function formatWireFloat(v) {
  if (Object.is(v, -0)) return "-0.0";
  const s = String(v);
  if (!s.includes(".") && !s.includes("e") && !s.includes("E")) {
    return s + ".0";
  }
  return s;
}

// Serializes a readValue() result tree to wire JSON. FloatValue leaves keep
// the float spelling; everything else delegates to JSON.stringify. The C++
// comparator re-serializes both sides through canonical_json, so key order
// and exponent spelling are normalized there — only the Int/Float token
// classification matters, and FloatValue guarantees Float.
function stringifyValue(v) {
  if (v instanceof FloatValue) return formatWireFloat(v.value);
  if (v === null) return "null";
  if (typeof v === "boolean") return v ? "true" : "false";
  if (typeof v === "number") return JSON.stringify(v);
  if (typeof v === "string") return JSON.stringify(v);
  if (Array.isArray(v)) {
    return "[" + v.map(stringifyValue).join(",") + "]";
  }
  if (typeof v === "object") {
    const keys = Object.keys(v);
    return "{" + keys.map((k) => JSON.stringify(k) + ":" + stringifyValue(v[k])).join(",") + "}";
  }
  frameFail(`cannot serialize observation value of type ${typeof v}`);
}

function writeRaw(e, addr, bytes) {
  checkRange(addr, bytes.length);
  new Uint8Array(e.memory.buffer, addr, bytes.length).set(bytes);
}
function readRaw(e, addr, len) {
  checkRange(addr, len);
  return new Uint8Array(e.memory.buffer, addr, len);
}

// WH-5c.7: the JS-host spelling authority for Decimal and Duration, mirroring
// frame_spelling.hpp + scalar_spelling.hpp EXACTLY. The P6 frame carries only
// the bare i64 word (mantissa / millis); the spelling the reader rebuilds must
// be provably the spelling the packer validated, so both sides go through
// THESE helpers.

function isDigitsAllowLeadingZero(s) {
  if (s.length === 0) return false;
  for (const c of s) {
    if (c < "0" || c > "9") return false;
  }
  return true;
}

function isCanonicalUnsigned(s) {
  return isDigitsAllowLeadingZero(s) &&
         !(s.length > 1 && s[0] === "0");
}

// Parse a canonical signed i64 (optional '-', canonical unsigned, never "-0").
// Returns a JS number or null.
function parseCanonicalI64(text) {
  if (typeof text !== "string" || text.length === 0) return null;
  let negative = false;
  let digits = text;
  if (text[0] === "-") {
    negative = true;
    digits = text.slice(1);
  }
  if (!isCanonicalUnsigned(digits)) return null;
  if (negative && digits === "0") return null;
  const value = BigInt(text);
  if (value > 9223372036854775807n || value < -9223372036854775808n) return null;
  return Number(value);
}

// Parse an unsigned i64 allowing leading zeros. Returns a JS number or null.
function parseUnsignedI64AllowLeadingZero(text) {
  if (!isDigitsAllowLeadingZero(text)) return null;
  const value = BigInt(text);
  if (value > 9223372036854775807n) return null;
  return Number(value);
}

// Parse a Decimal spelling into {mantissa, scale} or null. Mirrors
// scalar_spelling::parse_decimal: a source-literal `DIGIT+ "." DIGIT+ ["d"]`
// or a builtin-canonical `s<scale>:<mantissa>`.
function parseDecimalSpelling(spelling) {
  if (typeof spelling !== "string") return null;
  if (spelling.startsWith("s")) {
    const rest = spelling.slice(1);
    const colon = rest.indexOf(":");
    if (colon < 0) return null;
    const scale = parseCanonicalI64(rest.slice(0, colon));
    const mantissa = parseCanonicalI64(rest.slice(colon + 1));
    if (scale === null || mantissa === null) return null;
    if (scale > 2147483647 || scale < -2147483648) return null;
    return {mantissa, scale};
  }
  let s = spelling;
  if (s.endsWith("d")) s = s.slice(0, -1);
  const dot = s.indexOf(".");
  if (dot < 0) return null;
  if (s.indexOf(".", dot + 1) >= 0) return null;
  const intPart = s.slice(0, dot);
  const fracPart = s.slice(dot + 1);
  if (!isDigitsAllowLeadingZero(intPart) || !isDigitsAllowLeadingZero(fracPart)) {
    return null;
  }
  const scale = fracPart.length;
  if (scale > 2147483647) return null;
  const mantissa = parseUnsignedI64AllowLeadingZero(intPart + fracPart);
  if (mantissa === null) return null;
  return {mantissa, scale};
}

// Rebuild a source-literal Decimal spelling from (mantissa, scale).
// Mirrors format_decimal_spelling: negative mantissas or scale <= 0 fall back
// to the builtin canonical form `s<scale>:<mantissa>`.
function formatDecimalSpelling(mantissa, scale) {
  if (scale <= 0 || mantissa < 0) {
    return `s${scale}:${mantissa}`;
  }
  let digits = String(mantissa);
  const frac = scale;
  if (digits.length <= frac) {
    digits = "0".repeat(frac + 1 - digits.length) + digits;
  }
  const insertPos = digits.length - frac;
  return digits.slice(0, insertPos) + "." + digits.slice(insertPos);
}

// Parse a Duration spelling to i64 milliseconds, or null. Mirrors
// scalar_spelling::parse_duration: a source-unit `DIGIT+ (ms|s|m|h)` or a
// bare-millis canonical i64.
function parseDurationSpelling(spelling) {
  if (typeof spelling !== "string") return null;
  let multiplier = 0;
  let s = spelling;
  if (s.endsWith("ms")) {
    multiplier = 1;
    s = s.slice(0, -2);
  } else if (s.endsWith("s")) {
    multiplier = 1000;
    s = s.slice(0, -1);
  } else if (s.endsWith("m")) {
    multiplier = 60000;
    s = s.slice(0, -1);
  } else if (s.endsWith("h")) {
    multiplier = 3600000;
    s = s.slice(0, -1);
  } else {
    return parseCanonicalI64(spelling);
  }
  const value = parseUnsignedI64AllowLeadingZero(s);
  if (value === null) return null;
  if (BigInt(value) > 9223372036854775807n / BigInt(multiplier)) return null;
  return value * multiplier;
}

// Rebuild a source-unit Duration spelling from i64 milliseconds, choosing the
// largest unit (h > m > s > ms) that divides the value exactly. Mirrors
// format_duration_spelling.
function formatDurationSpelling(millis) {
  if (millis % 3600000 === 0) return `${millis / 3600000}h`;
  if (millis % 60000 === 0) return `${millis / 60000}m`;
  if (millis % 1000 === 0) return `${millis / 1000}s`;
  return `${millis}ms`;
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
    case "float": {
      if (typeof value !== "number" || !Number.isFinite(value)) {
        frameFail("float slot got a non-finite or non-number value");
      }
      // The wire shape and the dense layout must agree on the physical repr
      // (mirrors the C++ packer's ShapeMismatch gate): a Float wire node is
      // one 8-byte f64 word, never a narrower scalar or an aggregate.
      if (l.t !== "scalar" || l.repr !== "f64") {
        frameFail("float wire shape maps to a non-f64 scalar layout");
      }
      writeF64(e, addr, value);
      return;
    }
    case "decimal": {
      if (typeof value !== "string") frameFail("decimal slot got a non-string value");
      if (l.t !== "scalar" || l.repr !== "i64") {
        frameFail("decimal wire shape maps to a non-i64 scalar layout");
      }
      const parsed = parseDecimalSpelling(value);
      if (parsed === null) frameFail("decimal spelling is not a valid decimal");
      if (parsed.scale !== Number(w.scale)) {
        frameFail("decimal spelling scale does not match the wire-schema scale");
      }
      // WH-5c.7 spelling round-trip guard: the spelling the reader will
      // rebuild from the bare i64 mantissa must equal the source spelling
      // exactly, so non-canonical forms are rejected at entry pack.
      if (formatDecimalSpelling(parsed.mantissa, parsed.scale) !== value) {
        frameFail("decimal spelling is not canonical (reader rebuild would diverge)");
      }
      writeI64(e, addr, parsed.mantissa);
      return;
    }
    case "duration": {
      if (typeof value !== "string") frameFail("duration slot got a non-string value");
      if (l.t !== "scalar" || l.repr !== "i64") {
        frameFail("duration wire shape maps to a non-i64 scalar layout");
      }
      const millis = parseDurationSpelling(value);
      if (millis === null) frameFail("duration spelling is not a valid duration");
      if (formatDurationSpelling(millis) !== value) {
        frameFail("duration spelling is not canonical (reader rebuild would diverge)");
      }
      writeI64(e, addr, millis);
      return;
    }
    case "timestamp":
    case "uuid":
      frameFail(`pack shape '${w.t}' is outside the P6-7 rung-E frame subset`);
      return;
    case "map": {
      if (l.t !== "container" || l.value === null || l.value === undefined) {
        frameFail("wire map maps to a non-container or keyless container layout");
      }
      if (value === null || typeof value !== "object" || Array.isArray(value)) {
        frameFail("map slot got a non-object value");
      }
      const entries = Object.entries(value);
      const len = entries.length;
      const capacity = Number(w.capacity ?? l.capacity);
      if (len > capacity) frameFail("packed map length exceeds its capacity");
      const placement = backing.get(lId);
      if (placement === undefined) frameFail("input map has no backing placement");
      const stride = Number(l.stride);
      if (len * stride > placement.extent) {
        frameFail("packed map overruns its backing placement");
      }
      for (let i = 0; i < len; ++i) {
        const entryBase = placement.base + i * stride;
        packValue(e, lane, W, L, entries[i][0], w.key, l.element,
                  entryBase, backing, arena);
        packValue(e, lane, W, L, entries[i][1], w.value, l.value,
                  entryBase + Number(l.value_offset), backing, arena);
      }
      writeI32(e, addr + 0, placement.base);
      writeI32(e, addr + 4, len);
      return;
    }
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
    case "float": {
      // Mirror the C++ reader's ShapeMismatch gate: a Float wire node must
      // land on an f64 scalar layout, never a narrower word or an aggregate.
      if (l.t !== "scalar" || l.repr !== "f64") {
        frameFail("float wire shape maps to a non-f64 scalar layout");
      }
      return new FloatValue(readF64(e, addr));
    }
    case "decimal": {
      if (l.t !== "scalar" || l.repr !== "i64") {
        frameFail("decimal wire shape maps to a non-i64 scalar layout");
      }
      const big = readI64(e, addr);
      if (big > BigInt(Number.MAX_SAFE_INTEGER) ||
          big < BigInt(Number.MIN_SAFE_INTEGER)) {
        frameFail("decimal mantissa is not exactly representable as a JS number");
      }
      return formatDecimalSpelling(Number(big), Number(w.scale));
    }
    case "duration": {
      if (l.t !== "scalar" || l.repr !== "i64") {
        frameFail("duration wire shape maps to a non-i64 scalar layout");
      }
      const big = readI64(e, addr);
      if (big > BigInt(Number.MAX_SAFE_INTEGER) ||
          big < BigInt(Number.MIN_SAFE_INTEGER)) {
        frameFail("duration millis is not exactly representable as a JS number");
      }
      return formatDurationSpelling(Number(big));
    }
    case "timestamp":
    case "uuid":
      frameFail(`read shape '${w.t}' is outside the P6-7 rung-E frame subset`);
      return undefined;
    case "map": {
      if (l.t !== "container" || l.value === null || l.value === undefined) {
        frameFail("wire map maps to a non-container or keyless container layout");
      }
      const len = readI32(e, addr + 4);
      const capacity = Number(w.capacity ?? l.capacity);
      if (len < 0 || len > capacity) frameFail("map length is out of capacity");
      let base = readI32(e, addr + 0);
      const placement = backing.get(lId);
      if (placement !== undefined) {
        base = placement.base;
        if (len * Number(l.stride) > placement.extent) {
          frameFail("map overruns placement");
        }
      }
      const stride = Number(l.stride);
      const out = {};
      for (let i = 0; i < len; ++i) {
        const entryBase = base + i * stride;
        const key = readValue(e, lane, W, L, w.key, l.element, entryBase,
                              backing, extraStringRegions);
        const val = readValue(e, lane, W, L, w.value, l.value,
                              entryBase + Number(l.value_offset), backing,
                              extraStringRegions);
        out[String(key)] = val;
      }
      return out;
    }
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
      // without echoing bytes. KR6.6: the runtime construct-heap span
      // [construct_heap_base, 65536) is also authorized (String concat
      // result payloads).
      const arenaLo = Number(lane.payload_arena_base);
      const arenaHi = arenaLo + Number(lane.payload_arena_capacity);
      const inArena = ptr >= arenaLo && len <= arenaHi - ptr;
      const rodataLo = Number(lane.rodata_base || 0);
      const rodataHi = rodataLo + Number(lane.rodata_extent || 0);
      const inRodata =
        rodataLo !== 0 && ptr >= rodataLo && len <= rodataHi - ptr;
      const heapLo = Number(lane.construct_heap_base || 0);
      const inHeap =
        heapLo !== 0 && ptr >= heapLo && len <= 65536 - ptr;
      const inExtra = extraStringRegions.some(
        (r) => ptr >= Number(r.lo) && len <= Number(r.hi) - ptr);
      if (!inArena && !inRodata && !inHeap && !inExtra) {
        frameFail("frame string payload lies outside an authorized region " +
                  "(input-payload arena, rodata, construct heap, or a bridge " +
                  "result placement)");
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
      (lane.final_kind === "computed" || lane.final_kind === "capability")
          ? lane.output_base : lane.input_base;
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
  return stringifyValue(output);
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
  // RFC 0026 dual-track elimination: every module is P6-frame, so the bridge
  // predicate reads frame_lane directly (no frame_contract discriminator).
  const hasBridge = descriptor.frame_lane.bridge_call_sites.length > 0;

  // RFC 0026 P6-7: pack the canonical input into the P4-D frame BEFORE the
  // step() walk, since non-final computed handlers branch on packed input.
  const p6 = packP6Input(probe);

  let walked;
  if (hasBridge) {
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
  // encodes the output. This is the sole return path.
  const outputRaw = encodeP6Output(probe, p6);
  return {
    status: "completed",
    states: walked.map((state) => ({agent: lane.agent, state})),
    capabilities: probe.events.map((event) => event.name),
    capabilityArguments: probe.events.map((event) => event.argument),
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

async function runWorkflowP6(compiled) {
  const lane = descriptor.workflow_lane;
  const frameLane = descriptor.frame_lane;
  const W = descriptor.wire_schema.nodes;
  const L = frameLane.layouts;
  const roots = descriptor.wire_schema.roots;
  const nodeCount = descriptor.workflow_node_count;
  const probe = await makeInstance(compiled, "scenario");
  const e = probe.exports;
  if (e.workflow_node_count.value !== nodeCount) {
    fail(`workflow_node_count ${e.workflow_node_count.value} != ${nodeCount}`);
  }
  for (const name of ["step", "current_state"]) {
    expectTraps(() => e[name](), `${name}()`);
  }

  // RFC 0026 dual-track elimination: every workflow node is P6-frame, so the
  // entry is always packed as a P4-D frame into the entry node's I block.
  // The P6-dense node_blocks / node_inputs arrays are indexed by
  // p6_block_ordinal (not the runner index).
  const entryNode = lane.nodes.find((n) => n.schedule_pos === 0);
  // The backing map resolves each bounded container's layout id to its
  // disjoint backing placement (WH-5c.7: Set / Map elements live in the
  // placement-backed region, not inline). It is threaded through both the
  // input pack and the output walk.
  const backing = backingByLayout(frameLane);
  {
    const p6Ord = entryNode.p6_block_ordinal;
    const entryBlock = lane.node_blocks[p6Ord];
    const entryWireRoot = roots.node_inputs[p6Ord];
    const arena = {cursor: lane.entry_payload_base,
                   base: lane.entry_payload_base,
                   capacity: lane.entry_payload_capacity};
    // Zero the frame regions so padding/union words are 0.
    new Uint8Array(e.memory.buffer, 1024, 16384 - 1024).fill(0);
    packValue(e, frameLane, W, L, JSON.parse(probe.scenarioInput),
              entryWireRoot, entryBlock.input_layout,
              entryBlock.input_base, backing, arena);
    var run2Ptr = entryBlock.input_base;
    var run2Len = entryBlock.input_size;
  }

  const tuple = e.run2(run2Ptr, run2Len);
  let outputRaw = null;
  // WH-5b.2: a bridge PENDING arm latches and returns (PENDING,0,0) from the
  // P6 runner. The workflow suspends: no output slot, no completed-count
  // check. Read the state trace + probe events and return the suspended
  // observation.
  const suspended = tuple[0] === 2;
  if (!suspended && tuple[0] !== 0) {
    fail(`p6 workflow run2 returned non-OK status ${tuple[0]}`);
  }
  if (!suspended) {
    if (tuple[1] !== frameLane.output_base) {
      fail(`p6 workflow run2 returned ${tuple[1]} != output slot ${frameLane.output_base}`);
    }
    if (tuple[2] !== frameLane.output_size) {
      fail(`p6 workflow run2 length ${tuple[2]} != ${frameLane.output_size}`);
    }
    if (e.workflow_completed_count.value !== nodeCount) {
      fail(`completed_count ${e.workflow_completed_count.value} != ${nodeCount}`);
    }
    // Encode the workflow output slot against the workflow output root. String
    // payloads are authorized in rodata, the entry payload arena, or any bridge
    // call site's disjoint result placement (V2-D emission half 2). KR6.6: the
    // runtime construct-heap span [construct_heap_base, 65536) is also
    // authorized (String concat result payloads).
    const stringRegions = [];
    if (frameLane.rodata_extent > 0) {
      stringRegions.push({lo: frameLane.rodata_base,
                          hi: frameLane.rodata_base + frameLane.rodata_extent});
    }
    if (lane.entry_payload_capacity > 0) {
      stringRegions.push({lo: lane.entry_payload_base,
                          hi: lane.entry_payload_base + lane.entry_payload_capacity});
    }
    for (const site of (frameLane.bridge_call_sites ?? [])) {
      if (site.result_payload_capacity > 0) {
        stringRegions.push({lo: Number(site.result_payload_base),
                            hi: Number(site.result_payload_base) +
                                Number(site.result_payload_capacity)});
      }
    }
    if (Number(frameLane.construct_heap_base || 0) !== 0) {
      stringRegions.push({lo: Number(frameLane.construct_heap_base), hi: 65536});
    }
    const output = readValue(e, frameLane, W, L, roots.output,
                             frameLane.output_layout, tuple[1], backing,
                             stringRegions);
    outputRaw = stringifyValue(output);
  }
  // V2-D emission half 2: the state sequence is REAL runtime evidence.
  // Every node is P6-frame, so every node writes its states to the trace
  // ring; the node-event records the scheduler writes per completed node
  // are validated below but carry no state recovery (that path was deleted
  // with the WireJson lane).
  const traceBase = Number(lane.state_trace_base);
  const traceCap = Number(lane.state_trace_capacity);
  const traceCount = traceCap > 0
      ? new DataView(e.memory.buffer).getUint32(traceBase, true) : 0;
  if (traceCap === 0 || traceCount * 8 + 8 > traceCap) {
    fail(`state-trace count ${traceCount} exceeds its fixed ring capacity ${traceCap}`);
  }
  // Per-node state lists indexed by schedule_pos.
  const statesPerNode = Array.from({length: nodeCount}, () => []);
  // 1. P6 nodes: states from the trace ring, grouped by runner -> schedule_pos.
  const runnerToSchedule = new Map();
  for (const node of lane.nodes) {
    runnerToSchedule.set(node.runner, node.schedule_pos);
  }
  for (let i = 0; i < traceCount; ++i) {
    const addr = traceBase + 8 + 8 * i;
    const dv2 = new DataView(e.memory.buffer);
    const runner = dv2.getUint32(addr, true);
    const stateId = dv2.getUint32(addr + 4, true);
    const agentWalk = lane.agents[runner];
    const stateNames = agentWalk.all_states ?? agentWalk.walk;
    if (agentWalk === undefined || stateId >= stateNames.length) {
      fail(`state-trace record ${i} names an out-of-range runner/state ` +
           `(${runner},${stateId})`);
    }
    // all_states names every dense state id; walk can omit an untaken branch.
    const stateName = stateNames[stateId];
    if (typeof stateName !== "string") {
      fail(`state-trace record ${i} runner ${runner} names an unknown state ${stateId}`);
    }
    const schedulePos = runnerToSchedule.get(runner);
    if (schedulePos === undefined) {
      fail(`state-trace record ${i} runner ${runner} is not in the schedule`);
    }
    statesPerNode[schedulePos].push({agent: agentWalk.agent, state: stateName});
  }
  // 2. Node-event records: validate the record fields (pad/reserved/
  //    schedule_pos/status). The node-event region overlaps the P6 input
  //    frame in an import-free module, so skip decoding when there are no
  //    imports (mirrors the C++ host's guard).
  if (descriptor.imports.length > 0) {
    const evtView = new DataView(e.memory.buffer);
    const evtBase = descriptor.event_buffer.log_base;
    const evtRecordsBase = descriptor.event_buffer.records_base;
    const evtRecordBytes = descriptor.event_buffer.record_bytes;
    if (evtView.getUint32(evtBase + 4, true) !== 0) {
      fail("node-event header pad is nonzero");
    }
    const evtCount = evtView.getUint32(evtBase, true);
    if (evtCount > nodeCount) {
      fail(`event_count ${evtCount} exceeds node_count ${nodeCount}`);
    }
    for (let i = 0; i < evtCount; ++i) {
      const addr = evtRecordsBase + i * evtRecordBytes;
      const tag = evtView.getUint8(addr);
      if (evtView.getUint8(addr + 1) !== 0 || evtView.getUint8(addr + 2) !== 0 ||
          evtView.getUint8(addr + 3) !== 0) {
        fail(`node-event record ${i} pad bytes are nonzero`);
      }
      if (evtView.getUint32(addr + 36, true) !== 0) {
        fail(`node-event record ${i} reserved bytes are nonzero`);
      }
      const nodeId = evtView.getUint32(addr + 4, true);
      const schedulePos = evtView.getUint32(addr + 8, true);
      const recordStatus = evtView.getUint32(addr + 32, true);
      if (schedulePos !== i) {
        fail(`node-event record ${i} schedule_pos ${schedulePos} != ${i}`);
      }
      if (recordStatus !== 0) {
        fail(`node-event record ${i} status is not OK`);
      }
      const node = lane.nodes.find((n) => n.node_id === nodeId);
      if (node === undefined) {
        fail(`node-event record ${i} names unknown node ${nodeId}`);
      }
      // RFC 0026 dual-track elimination: every node is P6, so node-event
      // records carry no state collection (P6 nodes' states are already
      // collected from the trace ring above). The record validation
      // (pad/reserved/schedule_pos/status) still runs for every record.
    }
  }
  // 3. Flatten in schedule order.
  const states = [];
  for (const nodeStates of statesPerNode) {
    states.push(...nodeStates);
  }
  // V2-D emission half 2: in-handler capability bridges record their
  // capabilities + argument envelopes on probe events (no node-event records).
  const capabilities = probe.events.map((event) => event.name);
  const capabilityArguments = probe.events.map((event) => event.argument);
  return {
    status: suspended ? "suspended" : "completed",
    states,
    capabilities,
    capabilityArguments,
    outputRaw,
    transitions: e.transition_count.value,
    completedNodes: suspended ? null : e.workflow_completed_count.value,
  };
}

async function runWorkflow(compiled) {
  // Every workflow is P6-frame; runWorkflowP6 is the sole execution path.
  return runWorkflowP6(compiled);
}

// ---- ABI normalization matrix (real-engine negative coverage) ----------------
//
// These assert the module's own normalization of the capability tuple and the
// pending latch, on FRESH instances, using descriptor facts (import presence,
// heap base, event buffer layout). They do not change the canonical observation.
async function runAbiProbes(compiled) {
  const isWorkflow = Boolean(descriptor.workflow_lane);

  if (!isWorkflow) {
    // ---- agent lane ----
    // V2-C: a frame-bridge agent performs its capability in non-final handlers,
    // behind the (i32)->(i32,i32) protocol. Its normalization probes use the
    // packed P4-D frame + runv, NEVER the legacy opaque entry points: run2/run
    // have no defined contract on this lane and writing opaque JSON over the
    // fixed input frame would corrupt the bridge argument spans.
    const hasBridge = (descriptor.frame_lane?.bridge_call_sites?.length ?? 0) > 0;
    if (hasBridge) {
      // OK path: every dense bridge call site fires exactly once on one
      // completed run (one capability may legally own several sites that share
      // one import ordinal -- the chain/loop shape), and runv returns the
      // computed output base.
      const bridgeCallCount = descriptor.frame_lane.bridge_call_sites.length;
      {
        const p = await makeInstance(compiled, "scenario");
        packP6Input(p);
        const tuple = p.exports.runv();
        if (tuple[0] !== 0 || p.calls !== bridgeCallCount) {
          fail(`bridge runv OK normalization mismatch: ${tuple}, calls=${p.calls}, ` +
               `expected=${bridgeCallCount}`);
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
    // RFC 0026 dual-track elimination: every agent is P6-frame. A capability
    // agent always carries bridge call sites (the bridge is the sole capability
    // calling convention), so the hasBridge path above is the only capability
    // probe. Agents without bridge call sites are identity agents: runv is the
    // sole defined observation entry. Pack the P4-D frame on a FRESH instance
    // and assert runv returns OK with the descriptor-authorized root (the input
    // base for an identity final, the output base for a computed final).
    {
      const p = await makeInstance(compiled, "ok");
      const packed = packP6Input(p);
      const tuple = p.exports.runv();
      if (tuple[0] !== 0) fail("p6-frame runv did not return OK");
      const authorized = (packed.lane.final_kind === "computed" ||
                          packed.lane.final_kind === "capability")
          ? packed.lane.output_base : packed.lane.input_base;
      if (tuple[1] !== authorized) fail("p6-frame runv returned an unauthorized value_ptr");
    }
    return;
  }

  // ---- workflow lane ----
  for (const name of ["step", "current_state"]) {
    const p = await makeInstance(compiled, "ok");
    expectTraps(() => p.exports[name](), `workflow ${name}()`);
  }

  // Every workflow is P6-frame. run2 drives the frame lane and is already
  // fully exercised by runWorkflowP6 (packed I block, fixed output slot);
  // the legacy identity/capability normalization probes are deleted.
  {
    const p = await makeInstance(compiled, "ok");
    expectTraps(() => p.exports.run(...p.writeInput()), "p6 workflow legacy run()");
    if (p.calls !== 0) fail("legacy workflow run() reached a capability");
  }
  // Checked bump allocator: over-capacity returns 0 without advancing, then the
  // next allocations start at the descriptor heap base and advance exactly.
  // Only capability workflows use the checked allocator; identity workflows
  // use the unchecked variant.
  if (descriptor.imports.length > 0) {
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
