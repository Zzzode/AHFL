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
async function makeInstance(compiled, mode) {
  const state = { calls: 0, events: [], perName: new Map() };
  const imports = {};
  for (const importEntry of descriptor.imports) {
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

// ---- agent lane --------------------------------------------------------------
async function runAgent(compiled) {
  const lane = descriptor.agent_lane;
  const probe = await makeInstance(compiled, "scenario");
  const e = probe.exports;
  if (e.ahfl_abi_version.value !== 1) fail("abi version mismatch");
  if (e.current_state() !== lane.initial_state) {
    fail(`agent initial state ${e.current_state()} != ${lane.initial_state}`);
  }

  // step() drives the state walk to its stable final state WITHOUT invoking a
  // terminal capability (a capability final reports its state on step, it does
  // not call).
  const walked = [lane.states[lane.initial_state]];
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
