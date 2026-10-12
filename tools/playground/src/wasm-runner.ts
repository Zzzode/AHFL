/**
 * AHFL Playground Browser WASM Runner
 *
 * Drives a compiled AHFL workflow WASM module in the browser using the native
 * WebAssembly API. This is a TypeScript port of the proven Node embedded host
 * (tests/conformance/node_embedded_host.mjs), stripped of its Node `fs` parts
 * and conformance-scenario machinery: the descriptor arrives as parsed JSON and
 * the input JSON is supplied by the UI.
 *
 * The host packs the input JSON into the P4-D frame at the entry node's I
 * block, calls run_workflow, and walks the workflow output slot back to JSON. Every
 * offset/stride/capacity comes from the module's own execution descriptor
 * (ahfl.wasm-execution-descriptor), never a host re-derivation.
 *
 * Capability workflows are supported through the frame bridge ABI (RFC
 * 0026 P6-7, rung capability-bridge): every capability import has functype
 * (block_ptr:i32) -> (status:i32, result_root_ptr:i32). The host walks the
 * dense P4-D argument spans at the bridge control block, builds the same wire
 * argument envelope the native transport uses, and packs a configurable mock
 * response (JSON) into the call site's disjoint result placement. The bridge
 * machinery is a TypeScript port of the certified conformance host
 * (tests/conformance/node_embedded_host.mjs).
 */

// ---------------------------------------------------------------------------
// Descriptor types (mirror ahfl.wasm-execution-descriptor)
// ---------------------------------------------------------------------------

export interface WireField {
    name: string;
    type: number;
}

export type WireNode =
    | { t: 'unit' }
    | { t: 'bool' }
    | { t: 'int'; lo?: number; hi?: number }
    | { t: 'float' }
    | { t: 'string'; lo?: number; hi?: number }
    | { t: 'decimal'; scale: number }
    | { t: 'duration' }
    | { t: 'timestamp' }
    | { t: 'uuid' }
    | { t: 'closure' }
    | { t: 'option'; value: number }
    | { t: 'sequence'; kind: 'list' | 'set'; element: number; capacity?: number }
    | { t: 'map'; key: number; value: number; capacity?: number }
    | { t: 'struct'; name: string; fields: WireField[] }
    | { t: 'enum'; name: string; variants: WireVariant[] }
    | { t: 'tuple'; elements: number[] };

export interface WireVariant {
    name: string;
    kind: 'unit' | 'tuple' | 'struct';
    slots: WireField[];
}

export interface WireRoots {
    input: number;
    output: number;
    node_inputs?: number[];
    node_outputs?: number[];
}

export interface WireSchema {
    nodes: WireNode[];
    roots: WireRoots;
}

export interface LayoutField {
    off: number;
    lay: number;
}

export type LayoutNode =
    | { t: 'scalar'; repr: 'i32' | 'i64' | 'f64'; size: number; align: number; zero_sized: boolean }
    | { t: 'bytes'; n: number; size: number; align: number; zero_sized: boolean }
    | { t: 'ptrlen'; size: number; align: number; zero_sized: boolean }
    | { t: 'fnref'; size: number; align: number; zero_sized: boolean }
    | {
          t: 'closure';
          env: number | null;
          size: number;
          align: number;
          zero_sized: boolean;
      }
    | { t: 'struct'; fields: LayoutField[]; size: number; align: number; zero_sized: boolean }
    | {
          t: 'enum';
          tag_size: number;
          payload_offset: number;
          variants: { lay: number; size: number }[];
          size: number;
          align: number;
          zero_sized: boolean;
      }
    | {
          t: 'container';
          element: number;
          value: number | null;
          capacity: number;
          stride: number;
          value_offset: number;
          backing_size: number;
          size: number;
          align: number;
          zero_sized: boolean;
      }
    | { t: 'uninhabited'; size: number; align: number; zero_sized: boolean }
    | { t: 'pending'; size: number; align: number; zero_sized: boolean };

export interface FramePlacement {
    edge: number;
    lay: number;
    base: number;
    extent: number;
}

export interface FrameLane {
    final_kind: string;
    input_base: number;
    input_size: number;
    output_base: number;
    output_size: number;
    payload_arena_base: number;
    payload_arena_capacity: number;
    rodata_base: number;
    rodata_extent: number;
    bridge_control_base: number;
    bridge_block_stride: number;
    bridge_control_extent: number;
    bridge_spill_base: number;
    bridge_spill_extent: number;
    bridge_call_sites: BridgeCallSite[];
    placements: FramePlacement[];
    input_layout: number;
    output_layout: number;
    layouts: LayoutNode[];
    construct_heap_base: number;
}

export interface BridgeCallSite {
    call_site_id: number;
    import_ordinal: number;
    source_symbol: number;
    arity: number;
    block_offset: number;
    params: number[];
    param_layout: number[];
    result: number;
    result_layout: number;
    result_base: number;
    result_extent: number;
    result_payload_base: number;
    result_payload_capacity: number;
    spill_base: number;
    spill_extent: number;
}

export interface NodeBlock {
    input_base: number;
    input_size: number;
    input_layout: number;
    context_base: number;
    context_size: number;
    context_layout: number;
    scratch_base: number;
    scratch_size: number;
    output_base: number;
    output_size: number;
    output_layout: number;
}

export interface WorkflowNode {
    node_id: number;
    schedule_pos: number;
    runner: number;
    has_capability: boolean;
    capabilities: Array<{ ordinal: number; source_symbol: number }>;
    p6_block_ordinal: number;
}

export interface AgentWalk {
    agent: string;
    walk: string[];
    all_states: string[];
}

export interface WorkflowLane {
    agents: AgentWalk[];
    nodes: WorkflowNode[];
    node_blocks: NodeBlock[];
    entry_payload_base: number;
    entry_payload_capacity: number;
    state_trace_base: number;
    state_trace_capacity: number;
}

export interface ImportDescriptor {
    ordinal: number;
    field: string;
    name: string;
}

export interface ExecutionDescriptor {
    schema: string;
    kind: string;
    entry: string;
    imports: ImportDescriptor[];
    workflow_node_count: number;
    frame_lane: FrameLane;
    wire_schema: WireSchema;
    workflow_lane?: WorkflowLane;
}

// ---------------------------------------------------------------------------
// Run result
// ---------------------------------------------------------------------------

export interface StateTraceEntry {
    agent: string;
    state: string;
}

/**
 * One observed capability invocation: the capability's short source name and
 * the wire argument envelope the module passed (same shape the native
 * transport serializes: 0 args => {}, one Struct => bare object, one
 * non-Struct => {"value":..}, N => {"args":[...]}).
 */
export interface CapabilityCall {
    name: string;
    argument: string;
}

/**
 * Mock responses for capability imports, keyed by the capability's short
 * source name (last segment of its canonical name, e.g. "RouteTicket"). Each
 * value is a wire-JSON string that is packed into the call site's result
 * placement on every invocation of that capability.
 */
export type CapabilityMocks = Record<string, string>;

export interface RunResult {
    status: 'completed' | 'suspended' | 'failed';
    outputJson: string | null;
    stateTrace: StateTraceEntry[];
    transitions: number;
    completedNodes: number | null;
    capabilityCalls?: CapabilityCall[];
    error?: string;
}

// ---------------------------------------------------------------------------
// Memory helpers (fixed 64 KiB wasm32 page)
// ---------------------------------------------------------------------------

const kPageSize = 65536;

interface WasmExports {
    memory: WebAssembly.Memory;
    run_workflow: (ptr: number, len: number) => [number, number, number];
    alloc: (len: number) => number;
    workflow_node_count: WebAssembly.Global;
    workflow_completed_count: WebAssembly.Global;
    transition_count: WebAssembly.Global;
}

function dv(memory: WebAssembly.Memory): DataView {
    return new DataView(memory.buffer);
}

function checkRange(addr: number, len: number): void {
    if (!Number.isInteger(addr) || !Number.isInteger(len) || addr < 0 || len < 0 ||
        addr + len > kPageSize) {
        throw new Error(`wasm-runner: out-of-page access @${addr}+${len}`);
    }
}

function writeI32(memory: WebAssembly.Memory, addr: number, value: number): void {
    checkRange(addr, 4);
    dv(memory).setInt32(addr, value | 0, true);
}

function writeI64(memory: WebAssembly.Memory, addr: number, value: number): void {
    checkRange(addr, 8);
    dv(memory).setBigInt64(addr, BigInt(value), true);
}

function readI32(memory: WebAssembly.Memory, addr: number): number {
    checkRange(addr, 4);
    return dv(memory).getInt32(addr, true);
}

function readI64(memory: WebAssembly.Memory, addr: number): bigint {
    checkRange(addr, 8);
    return dv(memory).getBigInt64(addr, true);
}

function writeF64(memory: WebAssembly.Memory, addr: number, value: number): void {
    checkRange(addr, 8);
    dv(memory).setFloat64(addr, value, true);
}

function readF64(memory: WebAssembly.Memory, addr: number): number {
    checkRange(addr, 8);
    return dv(memory).getFloat64(addr, true);
}

function writeRaw(memory: WebAssembly.Memory, addr: number, bytes: Uint8Array): void {
    checkRange(addr, bytes.length);
    new Uint8Array(memory.buffer, addr, bytes.length).set(bytes);
}

function readRaw(memory: WebAssembly.Memory, addr: number, len: number): Uint8Array {
    checkRange(addr, len);
    return new Uint8Array(memory.buffer, addr, len);
}

// ---------------------------------------------------------------------------
// Float leaf + wire JSON serialization
// ---------------------------------------------------------------------------

/**
 * Marker for a frame-read f64 leaf. JSON.stringify serializes an integral
 * double as "2" (integer syntax), which would lose the float classification.
 * stringifyValue formats FloatValue leaves with the runtime float spelling so
 * the token is always valid float syntax.
 */
class FloatValue {
    constructor(public readonly value: number) {}
}

function formatWireFloat(v: number): string {
    if (Object.is(v, -0)) return '-0.0';
    const s = String(v);
    if (!s.includes('.') && !s.includes('e') && !s.includes('E')) {
        return s + '.0';
    }
    return s;
}

function stringifyValue(v: unknown): string {
    if (v instanceof FloatValue) return formatWireFloat(v.value);
    if (v === null) return 'null';
    if (typeof v === 'boolean') return v ? 'true' : 'false';
    if (typeof v === 'number') return JSON.stringify(v);
    if (typeof v === 'string') return JSON.stringify(v);
    if (Array.isArray(v)) {
        return '[' + v.map(stringifyValue).join(',') + ']';
    }
    if (typeof v === 'object') {
        const keys = Object.keys(v as Record<string, unknown>);
        return (
            '{' +
            keys
                .map((k) => JSON.stringify(k) + ':' + stringifyValue((v as Record<string, unknown>)[k]))
                .join(',') +
            '}'
        );
    }
    throw new Error(`wasm-runner: cannot serialize value of type ${typeof v}`);
}

// ---------------------------------------------------------------------------
// P4-D value serde (ported from node_embedded_host.mjs)
// ---------------------------------------------------------------------------

interface Arena {
    cursor: number;
    base: number;
    capacity: number;
    exhausted_message?: string;
}

interface Region {
    lo: number;
    hi: number;
}

function backingByLayout(lane: FrameLane): Map<number, FramePlacement> {
    const map = new Map<number, FramePlacement>();
    for (const placement of lane.placements) {
        if (map.has(placement.lay)) {
            throw new Error('wasm-runner: duplicate backing placement for one container');
        }
        map.set(placement.lay, placement);
    }
    return map;
}

function fieldLayoutOf(structLayout: LayoutNode, index: number): LayoutField {
    if (structLayout.t !== 'struct') {
        throw new Error('wasm-runner: field lookup on a non-struct layout');
    }
    const field = structLayout.fields[index];
    if (field === undefined) {
        throw new Error('wasm-runner: struct field/layout arity mismatch');
    }
    return field;
}

function packValue(
    memory: WebAssembly.Memory,
    lane: FrameLane,
    W: WireNode[],
    L: LayoutNode[],
    value: unknown,
    wId: number,
    lId: number,
    addr: number,
    backing: Map<number, FramePlacement>,
    arena: Arena,
): void {
    const w = W[wId];
    const l = L[lId];
    if (w === undefined || l === undefined) throw new Error('wasm-runner: pack node id out of range');
    switch (w.t) {
        case 'unit':
            return;
        case 'bool':
            if (typeof value !== 'boolean') throw new Error('wasm-runner: bool slot got a non-bool value');
            writeI32(memory, addr, value ? 1 : 0);
            return;
        case 'int': {
            if (typeof value !== 'number' || !Number.isInteger(value)) {
                throw new Error('wasm-runner: int slot got a non-integer value');
            }
            if (Math.abs(value) > Number.MAX_SAFE_INTEGER) {
                throw new Error('wasm-runner: int value exceeds the JavaScript exact-integer domain');
            }
            const wideInt = l.t === 'scalar' && l.repr === 'i64';
            if (w.lo !== undefined && value < w.lo) {
                throw new Error('wasm-runner: int value is below the schema lower bound');
            }
            if (w.hi !== undefined && value > w.hi) {
                throw new Error('wasm-runner: int value is above the schema upper bound');
            }
            if (!wideInt && (value < -2147483648 || value > 2147483647)) {
                throw new Error('wasm-runner: narrow int value does not fit its physical i32 word');
            }
            if (wideInt) writeI64(memory, addr, value);
            else writeI32(memory, addr, value);
            return;
        }
        case 'float': {
            if (typeof value !== 'number' || !Number.isFinite(value)) {
                throw new Error('wasm-runner: float slot got a non-finite or non-number value');
            }
            if (l.t !== 'scalar' || l.repr !== 'f64') {
                throw new Error('wasm-runner: float wire shape maps to a non-f64 scalar layout');
            }
            writeF64(memory, addr, value);
            return;
        }
        case 'decimal':
        case 'duration':
        case 'timestamp':
        case 'uuid':
        case 'map':
            throw new Error(`wasm-runner: pack shape '${w.t}' is outside the frame subset`);
        case 'string': {
            if (typeof value !== 'string') throw new Error('wasm-runner: string slot got a non-string value');
            const bytes = new TextEncoder().encode(value);
            if (w.lo !== undefined && bytes.length < w.lo) {
                throw new Error('wasm-runner: string payload is shorter than the schema length lower bound');
            }
            if (w.hi !== undefined && bytes.length > w.hi) {
                throw new Error('wasm-runner: string payload exceeds the schema length upper bound');
            }
            if (arena.cursor + bytes.length > arena.base + arena.capacity) {
                throw new Error(arena.exhausted_message ?? 'wasm-runner: frame-payload arena exhausted');
            }
            writeRaw(memory, arena.cursor, bytes);
            writeI32(memory, addr + 0, arena.cursor);
            writeI32(memory, addr + 4, bytes.length);
            arena.cursor += bytes.length;
            return;
        }
        case 'struct': {
            if (value === null || typeof value !== 'object' || Array.isArray(value)) {
                throw new Error('wasm-runner: struct slot got a non-object value');
            }
            if (l.t !== 'struct') {
                throw new Error(`wasm-runner: wire struct '${w.name}' maps to a non-struct layout`);
            }
            const obj = value as Record<string, unknown>;
            for (let i = 0; i < w.fields.length; ++i) {
                const field = w.fields[i];
                const slot = fieldLayoutOf(l, i);
                if (!Object.prototype.hasOwnProperty.call(obj, field.name)) {
                    throw new Error(`wasm-runner: struct input is missing field '${field.name}'`);
                }
                packValue(memory, lane, W, L, obj[field.name], field.type, slot.lay,
                          addr + slot.off, backing, arena);
            }
            return;
        }
        case 'option': {
            if (l.t !== 'enum') throw new Error('wasm-runner: wire option maps to a non-enum layout');
            if (value === null) {
                writeI32(memory, addr, 0);
                return;
            }
            writeI32(memory, addr, 1);
            const variant = l.variants[1];
            if (variant === undefined) throw new Error('wasm-runner: option Some variant has no payload layout');
            packValue(memory, lane, W, L, value, w.value, variant.lay,
                      addr + l.payload_offset, backing, arena);
            return;
        }
        case 'enum': {
            if (l.t !== 'enum') throw new Error('wasm-runner: wire enum maps to a non-enum layout');
            if (value === null || typeof value !== 'object') {
                throw new Error('wasm-runner: enum slot got a non-object');
            }
            const ev = value as { _variant?: string; _payload?: unknown; _named_payload?: unknown };
            const ordinal = w.variants.findIndex((variant) => variant.name === ev._variant);
            if (ordinal < 0) throw new Error(`wasm-runner: enum value names an unknown variant '${ev._variant}'`);
            writeI32(memory, addr, ordinal);
            const wireVariant = w.variants[ordinal];
            const layoutVariant = l.variants[ordinal];
            if (wireVariant.kind !== 'unit') {
                if (layoutVariant === undefined) throw new Error('wasm-runner: enum variant has no payload layout');
                const payloadAddr = addr + l.payload_offset;
                if (wireVariant.kind === 'tuple') {
                    const arr = Array.isArray(ev._payload) ? ev._payload : null;
                    if (arr === null) throw new Error('wasm-runner: tuple enum variant is missing its _payload array');
                    packSlots(memory, lane, W, L, arr, wireVariant.slots,
                              L[layoutVariant.lay], payloadAddr, backing, arena, false);
                } else {
                    const obj = (ev._named_payload !== undefined ? ev._named_payload : ev._payload) ?? null;
                    if (obj === null || typeof obj !== 'object') {
                        throw new Error('wasm-runner: struct enum variant is missing its named payload object');
                    }
                    packSlots(memory, lane, W, L, obj, wireVariant.slots,
                              L[layoutVariant.lay], payloadAddr, backing, arena, true);
                }
            }
            return;
        }
        case 'sequence': {
            if (l.t !== 'container') throw new Error('wasm-runner: wire sequence maps to a non-container layout');
            if (!Array.isArray(value)) throw new Error('wasm-runner: sequence slot got a non-array value');
            const capacity = w.capacity ?? l.capacity;
            if (value.length > capacity) throw new Error('wasm-runner: packed collection length exceeds its capacity');
            const placement = backing.get(lId);
            if (placement === undefined) throw new Error('wasm-runner: input container has no backing placement');
            const stride = l.stride;
            if (value.length * stride > placement.extent) {
                throw new Error('wasm-runner: packed collection overruns its backing placement');
            }
            for (let i = 0; i < value.length; ++i) {
                packValue(memory, lane, W, L, value[i], w.element, l.element,
                          placement.base + i * stride, backing, arena);
            }
            writeI32(memory, addr + 0, placement.base);
            writeI32(memory, addr + 4, value.length);
            return;
        }
        case 'tuple': {
            if (l.t !== 'struct') throw new Error('wasm-runner: wire tuple maps to a non-struct layout');
            if (!Array.isArray(value)) throw new Error('wasm-runner: tuple slot got a non-array value');
            packSlots(memory, lane, W, L, value,
                      w.elements.map((type, index) => ({ name: `${index}`, type })),
                      l, addr, backing, arena, false);
            return;
        }
    }
    throw new Error(`wasm-runner: unhandled pack shape '${(w as WireNode).t}'`);
}

function packSlots(
    memory: WebAssembly.Memory,
    lane: FrameLane,
    W: WireNode[],
    L: LayoutNode[],
    source: unknown,
    wireSlots: WireField[],
    payloadLayout: LayoutNode,
    payloadAddr: number,
    backing: Map<number, FramePlacement>,
    arena: Arena,
    named: boolean,
): void {
    if (payloadLayout.t !== 'struct') {
        throw new Error('wasm-runner: aggregate payload has no struct layout');
    }
    const src = (source ?? {}) as Record<string, unknown>;
    for (let i = 0; i < wireSlots.length; ++i) {
        const slot = fieldLayoutOf(payloadLayout, i);
        const child = named ? src[wireSlots[i].name] : (source as unknown[])[i];
        packValue(memory, lane, W, L, child, wireSlots[i].type, slot.lay,
                  payloadAddr + slot.off, backing, arena);
    }
}

function readValue(
    memory: WebAssembly.Memory,
    lane: FrameLane,
    W: WireNode[],
    L: LayoutNode[],
    wId: number,
    lId: number,
    addr: number,
    backing: Map<number, FramePlacement>,
    extraStringRegions: Region[] = [],
): unknown {
    const w = W[wId];
    const l = L[lId];
    if (w === undefined || l === undefined) throw new Error('wasm-runner: read node id out of range');
    switch (w.t) {
        case 'unit':
            return null;
        case 'bool': {
            const word = readI32(memory, addr);
            if (word !== 0 && word !== 1) throw new Error('wasm-runner: bool word is not in {0,1}');
            return word === 1;
        }
        case 'int': {
            let word: number;
            if (l.t === 'scalar' && l.repr === 'i64') {
                const big = readI64(memory, addr);
                if (big > BigInt(Number.MAX_SAFE_INTEGER) || big < BigInt(Number.MIN_SAFE_INTEGER)) {
                    throw new Error('wasm-runner: i64 frame value is not exactly representable as a JS number');
                }
                word = Number(big);
            } else {
                word = readI32(memory, addr);
            }
            if (w.lo !== undefined && word < w.lo) {
                throw new Error('wasm-runner: frame int word is below the schema lower bound');
            }
            if (w.hi !== undefined && word > w.hi) {
                throw new Error('wasm-runner: frame int word is above the schema upper bound');
            }
            return word;
        }
        case 'float': {
            if (l.t !== 'scalar' || l.repr !== 'f64') {
                throw new Error('wasm-runner: float wire shape maps to a non-f64 scalar layout');
            }
            return new FloatValue(readF64(memory, addr));
        }
        case 'decimal':
        case 'duration':
        case 'timestamp':
        case 'uuid':
        case 'map':
            throw new Error(`wasm-runner: read shape '${w.t}' is outside the frame subset`);
        case 'string': {
            if (l.t !== 'ptrlen') throw new Error('wasm-runner: wire string maps to a non-PtrLen layout');
            const ptr = readI32(memory, addr + 0);
            const len = readI32(memory, addr + 4);
            if (len < 0) throw new Error('wasm-runner: string length is negative');
            if (w.lo !== undefined && len < w.lo) {
                throw new Error('wasm-runner: frame string length is below the schema length lower bound');
            }
            if (w.hi !== undefined && len > w.hi) {
                throw new Error('wasm-runner: frame string length exceeds the schema length upper bound');
            }
            const arenaLo = lane.payload_arena_base;
            const arenaHi = arenaLo + lane.payload_arena_capacity;
            const inArena = ptr >= arenaLo && len <= arenaHi - ptr;
            const rodataLo = lane.rodata_base || 0;
            const rodataHi = rodataLo + lane.rodata_extent;
            const inRodata = rodataLo !== 0 && ptr >= rodataLo && len <= rodataHi - ptr;
            const heapLo = lane.construct_heap_base || 0;
            const inHeap = heapLo !== 0 && ptr >= heapLo && len <= kPageSize - ptr;
            const inExtra = extraStringRegions.some(
                (r) => ptr >= r.lo && len <= r.hi - ptr);
            if (!inArena && !inRodata && !inHeap && !inExtra) {
                throw new Error(
                    'wasm-runner: frame string payload lies outside an authorized region ' +
                    '(input-payload arena, rodata, construct heap, or a bridge result placement)');
            }
            return new TextDecoder().decode(readRaw(memory, ptr, len));
        }
        case 'struct': {
            if (l.t !== 'struct') throw new Error('wasm-runner: wire struct maps to a non-struct layout');
            const out: Record<string, unknown> = { _type: w.name };
            for (let i = 0; i < w.fields.length; ++i) {
                const field = w.fields[i];
                const slot = fieldLayoutOf(l, i);
                out[field.name] = readValue(memory, lane, W, L, field.type, slot.lay,
                                            addr + slot.off, backing, extraStringRegions);
            }
            return out;
        }
        case 'option': {
            if (l.t !== 'enum') throw new Error('wasm-runner: wire option maps to a non-enum layout');
            const tag = readI32(memory, addr);
            if (tag === 0) return null;
            if (tag !== 1) throw new Error('wasm-runner: option tag is not in {0,1}');
            const variant = l.variants[1];
            if (variant === undefined) throw new Error('wasm-runner: option Some variant has no payload layout');
            return readValue(memory, lane, W, L, w.value, variant.lay,
                             addr + l.payload_offset, backing, extraStringRegions);
        }
        case 'enum': {
            if (l.t !== 'enum') throw new Error('wasm-runner: wire enum maps to a non-enum layout');
            const ordinal = readI32(memory, addr);
            if (ordinal < 0 || ordinal >= w.variants.length) {
                throw new Error('wasm-runner: enum tag is out of the declared variant range');
            }
            const wireVariant = w.variants[ordinal];
            const out: Record<string, unknown> = { _enum: w.name, _variant: wireVariant.name };
            if (wireVariant.kind !== 'unit') {
                const layoutVariant = l.variants[ordinal];
                if (layoutVariant === undefined) throw new Error('wasm-runner: enum variant has no payload layout');
                const payloadAddr = addr + l.payload_offset;
                if (wireVariant.kind === 'tuple') {
                    const payloadLayout = L[layoutVariant.lay];
                    const arr: unknown[] = [];
                    for (let i = 0; i < wireVariant.slots.length; ++i) {
                        const slot = fieldLayoutOf(payloadLayout, i);
                        arr.push(readValue(memory, lane, W, L, wireVariant.slots[i].type, slot.lay,
                                           payloadAddr + slot.off, backing, extraStringRegions));
                    }
                    out._payload = arr;
                } else {
                    const payloadLayout = L[layoutVariant.lay];
                    const named: Record<string, unknown> = {};
                    for (let i = 0; i < wireVariant.slots.length; ++i) {
                        const slot = fieldLayoutOf(payloadLayout, i);
                        named[wireVariant.slots[i].name] = readValue(
                            memory, lane, W, L, wireVariant.slots[i].type, slot.lay,
                            payloadAddr + slot.off, backing, extraStringRegions);
                    }
                    out._named_payload = named;
                }
            }
            return out;
        }
        case 'sequence': {
            if (l.t !== 'container') throw new Error('wasm-runner: wire sequence maps to a non-container layout');
            const len = readI32(memory, addr + 4);
            const capacity = w.capacity ?? l.capacity;
            if (len < 0 || len > capacity) throw new Error('wasm-runner: container length is out of capacity');
            let base = readI32(memory, addr + 0);
            const placement = backing.get(lId);
            if (placement !== undefined) {
                base = placement.base;
                if (len * l.stride > placement.extent) {
                    throw new Error('wasm-runner: container overruns placement');
                }
            }
            const stride = l.stride;
            const out: unknown[] = [];
            for (let i = 0; i < len; ++i) {
                out.push(readValue(memory, lane, W, L, w.element, l.element, base + i * stride,
                                   backing, extraStringRegions));
            }
            if (w.kind === 'set') {
                const canonical = out.map((v) => JSON.stringify(v));
                canonical.sort();
                for (let i = 1; i < canonical.length; ++i) {
                    if (canonical[i] === canonical[i - 1]) {
                        throw new Error('wasm-runner: set carries a duplicate element');
                    }
                }
            }
            return out;
        }
        case 'tuple': {
            if (l.t !== 'struct') throw new Error('wasm-runner: wire tuple maps to a non-struct layout');
            const out: unknown[] = [];
            for (let i = 0; i < w.elements.length; ++i) {
                const slot = fieldLayoutOf(l, i);
                out.push(readValue(memory, lane, W, L, w.elements[i], slot.lay,
                                   addr + slot.off, backing, extraStringRegions));
            }
            return out;
        }
    }
    throw new Error(`wasm-runner: unhandled read shape '${(w as WireNode).t}'`);
}

// ---------------------------------------------------------------------------
// State-trace decoding
// ---------------------------------------------------------------------------

function decodeStateTrace(
    memory: WebAssembly.Memory,
    lane: WorkflowLane,
    nodeCount: number,
): StateTraceEntry[] {
    const traceBase = lane.state_trace_base;
    const traceCap = lane.state_trace_capacity;
    const traceCount = traceCap > 0 ? dv(memory).getUint32(traceBase, true) : 0;
    if (traceCap === 0 || traceCount * 8 + 8 > traceCap) {
        throw new Error(
            `wasm-runner: state-trace count ${traceCount} exceeds its fixed ring capacity ${traceCap}`);
    }
    const statesPerNode: StateTraceEntry[][] = Array.from({ length: nodeCount }, () => []);
    const runnerToSchedule = new Map<number, number>();
    for (const node of lane.nodes) {
        runnerToSchedule.set(node.runner, node.schedule_pos);
    }
    for (let i = 0; i < traceCount; ++i) {
        const addr = traceBase + 8 + 8 * i;
        const runner = dv(memory).getUint32(addr, true);
        const stateId = dv(memory).getUint32(addr + 4, true);
        const agentWalk = lane.agents[runner];
        if (agentWalk === undefined) {
            throw new Error(`wasm-runner: state-trace record ${i} names an out-of-range runner ${runner}`);
        }
        const stateNames = agentWalk.all_states ?? agentWalk.walk;
        if (stateId >= stateNames.length) {
            throw new Error(
                `wasm-runner: state-trace record ${i} names an out-of-range state ${stateId}`);
        }
        const stateName = stateNames[stateId];
        const schedulePos = runnerToSchedule.get(runner);
        if (schedulePos === undefined) {
            throw new Error(`wasm-runner: state-trace record ${i} runner ${runner} is not in the schedule`);
        }
        statesPerNode[schedulePos].push({ agent: agentWalk.agent, state: stateName });
    }
    const states: StateTraceEntry[] = [];
    for (const nodeStates of statesPerNode) {
        states.push(...nodeStates);
    }
    return states;
}

// ---------------------------------------------------------------------------
// Capability bridge (frame bridge, ported from node_embedded_host.mjs)
// ---------------------------------------------------------------------------

/**
 * The short source name of a capability import: the last segment of its
 * canonical name ("playground::main::RouteTicket" -> "RouteTicket").
 */
export function capabilityShortName(canonical: string): string {
    const segments = canonical.split('::');
    return segments[segments.length - 1] ?? canonical;
}

/**
 * Classify one bridge parameter by its WIRE shape (the SSOT discriminator): a
 * scalar / tag-only enum / String PtrLen is spilled into the call site's
 * private 8-byte slots; a struct / tuple / option / payload-bearing enum stays
 * at its aggregate root address. Mirrors the C++ planning classification in
 * plan_capability_call.
 */
function bridgeParamKind(W: WireNode[], wireId: number): 'spill' | 'root' {
    const w = W[wireId];
    if (w === undefined) throw new Error('wasm-runner: bridge parameter names an unknown wire node');
    switch (w.t) {
        case 'bool':
        case 'int':
        case 'unit':
        case 'string':
        case 'float':
            return 'spill';
        case 'struct':
        case 'option':
        case 'tuple':
            return 'root';
        case 'enum': {
            const tagOnly = w.variants.every((v) => v.kind === 'unit');
            return tagOnly ? 'spill' : 'root';
        }
        case 'decimal':
        case 'duration':
        case 'timestamp':
        case 'uuid':
        case 'map':
        case 'sequence':
        case 'closure':
            throw new Error(`wasm-runner: a bridge parameter of wire shape '${w.t}' is outside the frame-bridge subset`);
    }
}

function inRegion(addr: number, len: number, region: Region): boolean {
    return addr >= region.lo && len <= region.hi - addr;
}

/**
 * The authorized aggregate-root homes for a bridge call's arguments: every
 * packaged node's I_k / C_k / scratch_k / O_k block span (a bridge in a
 * non-final handler may project the node input or an earlier handler's
 * context/scratch/output) and every OTHER call site's disjoint result
 * placement. The current site's own result is excluded — it is the call
 * output, unwritten before the invocation.
 */
function bridgeRootRegions(descriptor: ExecutionDescriptor, currentSite: BridgeCallSite): Region[] {
    const lane = descriptor.frame_lane;
    const regions: Region[] = [];
    if (descriptor.kind === 'workflow' && descriptor.workflow_lane?.node_blocks) {
        for (const block of descriptor.workflow_lane.node_blocks) {
            regions.push({ lo: block.input_base, hi: block.input_base + block.input_size });
            if (block.context_size > 0) {
                regions.push({ lo: block.context_base, hi: block.context_base + block.context_size });
            }
            if (block.scratch_size > 0) {
                regions.push({ lo: block.scratch_base, hi: block.scratch_base + block.scratch_size });
            }
            if (block.output_size > 0) {
                regions.push({ lo: block.output_base, hi: block.output_base + block.output_size });
            }
        }
    } else {
        regions.push({ lo: lane.input_base, hi: lane.input_base + lane.input_size });
        regions.push({ lo: 4096, hi: 7168 });
        regions.push({ lo: 7168, hi: 12288 });
    }
    for (const other of lane.bridge_call_sites) {
        if (other.call_site_id === currentSite.call_site_id) continue;
        regions.push({ lo: other.result_base, hi: other.result_base + other.result_extent });
    }
    return regions;
}

/**
 * Union of the String-payload regions the read walker may authorize for a
 * bridge argument: the packed input-payload arena, the workflow entry payload
 * arena, the rodata region, the construct heap, every OTHER call site's result
 * placement payload, and every packaged node's O_k block (a construct terminal
 * materializes its argument there). Mirrors build_bridge_string_regions in
 * capability_import.cpp.
 */
function bridgeStringRegions(descriptor: ExecutionDescriptor, currentSite: BridgeCallSite): Region[] {
    const lane = descriptor.frame_lane;
    const regions: Region[] = [];
    const arenaLo = lane.payload_arena_base || 0;
    const arenaHi = arenaLo + lane.payload_arena_capacity;
    if (arenaLo !== 0 && arenaHi > arenaLo) {
        regions.push({ lo: arenaLo, hi: arenaHi });
    }
    const wfLane = descriptor.workflow_lane;
    if (wfLane !== undefined && wfLane.entry_payload_capacity > 0) {
        regions.push({
            lo: wfLane.entry_payload_base,
            hi: wfLane.entry_payload_base + wfLane.entry_payload_capacity,
        });
    }
    const rodataLo = lane.rodata_base || 0;
    const rodataHi = rodataLo + lane.rodata_extent;
    if (rodataLo !== 0 && rodataHi > rodataLo) {
        regions.push({ lo: rodataLo, hi: rodataHi });
    }
    const heapLo = lane.construct_heap_base || 0;
    if (heapLo !== 0) {
        regions.push({ lo: heapLo, hi: kPageSize });
    }
    for (const s of lane.bridge_call_sites) {
        if (s.call_site_id === currentSite.call_site_id) continue;
        if (s.result_payload_capacity > 0) {
            regions.push({ lo: s.result_payload_base, hi: s.result_payload_base + s.result_payload_capacity });
        }
    }
    if (wfLane?.node_blocks) {
        for (const block of wfLane.node_blocks) {
            if (block.output_size > 0) {
                regions.push({ lo: block.output_base, hi: block.output_base + block.output_size });
            }
        }
    } else if (lane.output_size > 0) {
        regions.push({ lo: lane.output_base, hi: lane.output_base + lane.output_size });
    }
    return regions;
}

/**
 * Read ONE bridge argument P4-D span into a JS value. The descriptor ptr/len
 * words are untrusted module evidence: each argument span is independently
 * region-membership-tested against exactly the homes that parameter kind may
 * name, and the module-provided len is only cross-checked against the dense
 * layout — never used as the span authority.
 */
function readBridgeArg(
    memory: WebAssembly.Memory,
    descriptor: ExecutionDescriptor,
    lane: FrameLane,
    W: WireNode[],
    L: LayoutNode[],
    backing: Map<number, FramePlacement>,
    currentSite: BridgeCallSite,
    wireId: number,
    layoutId: number,
    ptr: number,
    len: number,
    rootRegions: Region[],
): unknown {
    if (!Number.isInteger(ptr) || !Number.isInteger(len) || ptr < 0 || len < 0 ||
        ptr + len > kPageSize) {
        throw new Error('wasm-runner: bridge argument span lies outside the fixed page');
    }
    const l = L[layoutId];
    if (l === undefined) throw new Error('wasm-runner: bridge argument names an unknown layout root');
    const kind = bridgeParamKind(W, wireId);
    if (kind === 'spill') {
        if (l.t !== 'scalar' && l.t !== 'ptrlen' && l.t !== 'enum') {
            throw new Error('wasm-runner: a spilled bridge parameter maps to a non-scalar/non-PtrLen/non-tag-enum layout');
        }
        if (len !== 8) {
            throw new Error(`wasm-runner: a spilled bridge parameter descriptor len must be 8 (got ${len})`);
        }
        const spillRegion = {
            lo: currentSite.spill_base,
            hi: currentSite.spill_base + currentSite.spill_extent,
        };
        if (!(spillRegion.hi > spillRegion.lo) || !inRegion(ptr, 8, spillRegion)) {
            throw new Error("wasm-runner: a spilled bridge parameter lies outside its call site's private spill window");
        }
    } else {
        if (l.t !== 'struct' && l.t !== 'enum') {
            throw new Error('wasm-runner: an aggregate bridge parameter maps to a non-struct/non-enum layout');
        }
        const rootSize = l.size;
        if (len !== rootSize) {
            throw new Error(
                'wasm-runner: an aggregate bridge parameter descriptor len disagrees with its dense ' +
                `layout root size (descriptor ${len}, layout ${rootSize})`);
        }
        if (!rootRegions.some((r) => inRegion(ptr, rootSize, r))) {
            throw new Error('wasm-runner: an aggregate bridge parameter names a root outside an authorized frame / scratch / other-site result placement');
        }
    }
    return readValue(memory, lane, W, L, wireId, layoutId, ptr, backing,
                     bridgeStringRegions(descriptor, currentSite));
}

/**
 * Pack one bridge result value at the call site's result root. String payload
 * bytes are written to the site's disjoint payload arena (one bump cursor).
 */
function packBridgeResult(
    memory: WebAssembly.Memory,
    lane: FrameLane,
    W: WireNode[],
    L: LayoutNode[],
    site: BridgeCallSite,
    value: unknown,
): void {
    if (site.result_base < 0 || site.result_base + site.result_extent > kPageSize) {
        throw new Error('wasm-runner: bridge result placement lies outside the fixed page');
    }
    // Zero the whole result placement (padding reads back deterministically).
    new Uint8Array(memory.buffer, site.result_base, site.result_extent).fill(0);
    const arena: Arena = {
        cursor: site.result_payload_base,
        base: site.result_payload_base,
        capacity: site.result_payload_capacity,
        exhausted_message: 'wasm-runner: bridge result payload arena exhausted',
    };
    const resultBacking = backingByLayout(lane);
    packValue(memory, lane, W, L, value, site.result, site.result_layout,
              site.result_base, resultBacking, arena);
}

/**
 * The wire argument envelope SSOT (mirrors serialize_args_for_wire_json in
 * src/runtime/engine/wire_value.cpp): 0 args => {}, one Struct => bare object,
 * one non-Struct => {"value":..}, N => {"args":[...]}.
 */
function bridgeEnvelope(W: WireNode[], paramWireIds: number[], args: unknown[]): string {
    if (args.length === 0) return '{}';
    if (args.length === 1) {
        const wireNode = W[paramWireIds[0]];
        if (wireNode === undefined) {
            throw new Error('wasm-runner: bridge envelope names an unknown wire parameter');
        }
        if (wireNode.t === 'struct') return stringifyValue(args[0]);
        return stringifyValue({ value: args[0] });
    }
    return stringifyValue({ args });
}

/** Everything a bridge callback needs that is not known at import-construction time. */
interface BridgeHost {
    descriptor: ExecutionDescriptor;
    mocks: Map<string, unknown>;
    calls: CapabilityCall[];
    getMemory: () => WebAssembly.Memory;
}

/**
 * Build the host function for one capability import. The import has functype
 * (block_ptr:i32) -> (status:i32, result_root_ptr:i32): the call site is
 * resolved FROM THE CONTROL-BLOCK ADDRESS (one capability legally occupies one
 * import shared by many dense call sites), its arguments are walked and
 * recorded, and the configured mock JSON is packed into the site's result
 * placement. Returns [0, result_base] on success.
 */
function makeBridgeCallback(
    entry: ImportDescriptor,
    host: BridgeHost,
): (blockPtr: number) => [number, number] {
    const lane = host.descriptor.frame_lane;
    const W = host.descriptor.wire_schema.nodes;
    const L = lane.layouts;
    const backing = backingByLayout(lane);
    const shortName = capabilityShortName(entry.name);
    return (blockPtr: number): [number, number] => {
        const memory = host.getMemory();
        const sites = lane.bridge_call_sites;
        const stride = lane.bridge_block_stride;
        const rel = blockPtr - lane.bridge_control_base;
        if (!Number.isInteger(blockPtr) || rel < 0 ||
            rel >= sites.length * stride || rel % stride !== 0) {
            throw new Error('wasm-runner: bridge control-block pointer names an address outside a dense fixed-stride control-block slot');
        }
        const site = sites[Math.floor(rel / stride)];
        if (site === undefined || blockPtr !== lane.bridge_control_base + site.block_offset) {
            throw new Error('wasm-runner: bridge control-block pointer is not at its dense fixed-stride offset');
        }
        if (site.import_ordinal !== entry.ordinal) {
            throw new Error(
                `wasm-runner: bridge call site ${site.call_site_id} names import ordinal ` +
                `${site.import_ordinal} but import ordinal ${entry.ordinal} was invoked`);
        }
        const view = new DataView(memory.buffer);
        const callSiteId = view.getUint32(blockPtr, true);
        const argCount = view.getUint32(blockPtr + 4, true);
        if (callSiteId !== site.call_site_id) {
            throw new Error('wasm-runner: bridge control-block call_site_id disagrees with its dense block address');
        }
        if (argCount !== site.arity) {
            throw new Error(`wasm-runner: bridge control-block arg_count ${argCount} != descriptor arity ${site.arity}`);
        }
        const rootRegions = bridgeRootRegions(host.descriptor, site);
        const argValues: unknown[] = [];
        for (let i = 0; i < argCount; ++i) {
            const desc = blockPtr + 8 + 8 * i;
            const ptr = view.getInt32(desc, true);
            const len = view.getUint32(desc + 4, true);
            argValues.push(readBridgeArg(memory, host.descriptor, lane, W, L, backing, site,
                                         site.params[i], site.param_layout[i], ptr, len, rootRegions));
        }
        host.calls.push({ name: shortName, argument: bridgeEnvelope(W, site.params, argValues) });
        const mock = host.mocks.get(shortName);
        if (mock === undefined) {
            throw new Error(`wasm-runner: no mock response configured for capability '${shortName}'`);
        }
        packBridgeResult(memory, lane, W, L, site, mock);
        return [0, site.result_base];
    };
}

// ---------------------------------------------------------------------------
// Workflow runner
// ---------------------------------------------------------------------------

/**
 * Compile, instantiate, and run a workflow WASM module.
 *
 * Capability-free modules run with an empty import object. Modules that
 * import capabilities are run with bridge host functions (frame bridge
 * ABI): each capability invocation packs `capabilityMocks[short-name]` (or a
 * wire-shape skeleton default) into the call site's result placement. Throws
 * on a non-workflow descriptor, an unexpected host module, or any frame/ABI
 * violation. The caller catches and surfaces the error message.
 */
export async function runWasmWorkflow(
    wasmBytes: Uint8Array,
    descriptor: ExecutionDescriptor,
    inputJson: string,
    capabilityMocks?: CapabilityMocks,
): Promise<RunResult> {
    if (descriptor.kind !== 'workflow' || descriptor.workflow_lane === undefined) {
        throw new Error(
            `wasm-runner: only workflow modules are supported (got kind '${descriptor.kind}')`);
    }

    // Copy into a fresh ArrayBuffer-backed view so WebAssembly.compile accepts
    // it (a SharedArrayBuffer-backed view is not a valid BufferSource).
    const bytes = new Uint8Array(wasmBytes.length);
    bytes.set(wasmBytes);
    const module = await WebAssembly.compile(bytes);
    // Every import must name the single AHFL capability host module.
    const listedImports = WebAssembly.Module.imports(module);
    for (const listed of listedImports) {
        if (listed.module !== 'ahfl_cap') {
            throw new Error(
                `wasm-runner: module imports an unexpected host module '${listed.module}.${listed.name}'`);
        }
    }
    if (listedImports.length > 0 && descriptor.imports.length === 0) {
        throw new Error(
            'wasm-runner: module imports capabilities but the execution descriptor lists none ' +
            '(the descriptor may be stale)');
    }

    // Resolve the mock response for every declared capability: the caller's
    // mock wins, otherwise a wire-shape skeleton of the result type. The mock
    // JSON is parsed up front so a bad edit fails before run_workflow starts.
    const calls: CapabilityCall[] = [];
    const mocks = new Map<string, unknown>();
    const defaults = defaultCapabilityMocks(descriptor);
    for (const imp of descriptor.imports) {
        const shortName = capabilityShortName(imp.name);
        const raw = capabilityMocks?.[shortName] ?? defaults[shortName];
        if (raw === undefined) {
            throw new Error(`wasm-runner: no mock response configured for capability '${shortName}'`);
        }
        try {
            mocks.set(shortName, JSON.parse(raw));
        } catch (err) {
            throw new Error(
                `wasm-runner: mock response for capability '${shortName}' is not valid JSON: ` +
                `${(err as Error).message}`);
        }
    }

    let memory: WebAssembly.Memory | undefined;
    const host: BridgeHost = {
        descriptor,
        mocks,
        calls,
        getMemory: () => {
            if (memory === undefined) {
                throw new Error('wasm-runner: bridge invoked before module instantiation');
            }
            return memory;
        },
    };
    const bridgeImports: Record<string, (blockPtr: number) => [number, number]> = {};
    for (const imp of descriptor.imports) {
        bridgeImports[imp.field] = makeBridgeCallback(imp, host);
    }

    const instance = await WebAssembly.instantiate(module, { ahfl_cap: bridgeImports });
    const e = instance.exports as unknown as WasmExports;
    memory = e.memory;

    const lane = descriptor.workflow_lane;
    const frameLane = descriptor.frame_lane;
    const W = descriptor.wire_schema.nodes;
    const L = frameLane.layouts;
    const roots = descriptor.wire_schema.roots;
    const nodeCount = descriptor.workflow_node_count;

    if (e.workflow_node_count.value !== nodeCount) {
        throw new Error(
            `wasm-runner: workflow_node_count ${e.workflow_node_count.value} != ${nodeCount}`);
    }

    // Pack the input JSON into the entry node's I block. Workflow P6 frames
    // carry no bounded collections, so the backing map is empty; it is still
    // threaded through readValue for the output walk.
    const entryNode = lane.nodes.find((n) => n.schedule_pos === 0);
    if (entryNode === undefined) {
        throw new Error('wasm-runner: workflow has no entry node (schedule_pos 0)');
    }
    const backing = new Map<number, FramePlacement>();
    const p6Ord = entryNode.p6_block_ordinal;
    const entryBlock = lane.node_blocks[p6Ord];
    if (entryBlock === undefined) {
        throw new Error(`wasm-runner: entry node names an unknown node block ordinal ${p6Ord}`);
    }
    const entryWireRoot = roots.node_inputs?.[p6Ord] ?? roots.input;
    const arena: Arena = {
        cursor: lane.entry_payload_base,
        base: lane.entry_payload_base,
        capacity: lane.entry_payload_capacity,
    };
    // Zero the frame regions so padding/union words are 0.
    new Uint8Array(memory.buffer, 1024, 16384 - 1024).fill(0);

    let parsedInput: unknown;
    try {
        parsedInput = JSON.parse(inputJson);
    } catch (err) {
        throw new Error(`wasm-runner: input is not valid JSON: ${(err as Error).message}`);
    }
    packValue(memory, frameLane, W, L, parsedInput, entryWireRoot, entryBlock.input_layout,
              entryBlock.input_base, backing, arena);

    const tuple = e.run_workflow(entryBlock.input_base, entryBlock.input_size);
    const status = tuple[0];
    const suspended = status === 2;
    if (!suspended && status !== 0) {
        throw new Error(`wasm-runner: run_workflow returned non-OK status ${status}`);
    }

    let outputJson: string | null = null;
    if (!suspended) {
        if (tuple[1] !== frameLane.output_base) {
            throw new Error(
                `wasm-runner: run_workflow returned ${tuple[1]} != output slot ${frameLane.output_base}`);
        }
        if (tuple[2] !== frameLane.output_size) {
            throw new Error(
                `wasm-runner: run_workflow length ${tuple[2]} != ${frameLane.output_size}`);
        }
        if (e.workflow_completed_count.value !== nodeCount) {
            throw new Error(
                `wasm-runner: completed_count ${e.workflow_completed_count.value} != ${nodeCount}`);
        }
        // Authorize String payloads in rodata, the entry payload arena, any
        // bridge call site's result placement, and the construct heap.
        const stringRegions: Region[] = [];
        if (frameLane.rodata_extent > 0) {
            stringRegions.push({
                lo: frameLane.rodata_base,
                hi: frameLane.rodata_base + frameLane.rodata_extent,
            });
        }
        if (lane.entry_payload_capacity > 0) {
            stringRegions.push({
                lo: lane.entry_payload_base,
                hi: lane.entry_payload_base + lane.entry_payload_capacity,
            });
        }
        for (const site of frameLane.bridge_call_sites) {
            if (site.result_payload_capacity > 0) {
                stringRegions.push({
                    lo: site.result_payload_base,
                    hi: site.result_payload_base + site.result_payload_capacity,
                });
            }
        }
        if (frameLane.construct_heap_base !== 0) {
            stringRegions.push({ lo: frameLane.construct_heap_base, hi: kPageSize });
        }
        const output = readValue(memory, frameLane, W, L, roots.output,
                                 frameLane.output_layout, tuple[1], backing, stringRegions);
        outputJson = stringifyValue(output);
    }

    const stateTrace = decodeStateTrace(memory, lane, nodeCount);

    return {
        status: suspended ? 'suspended' : 'completed',
        outputJson,
        stateTrace,
        transitions: e.transition_count.value,
        completedNodes: suspended ? null : e.workflow_completed_count.value,
        capabilityCalls: calls,
    };
}

// ---------------------------------------------------------------------------
// Capability discovery and mock defaults
// ---------------------------------------------------------------------------

/**
 * Parse a WASM binary's import section and return every import as
 * "module.field" pairs (e.g. "ahfl_cap.cap_4"). Synchronous and allocation-
 * free: it walks the section header table without compiling the module, so the
 * UI can decide whether to show the capability-mock panel before instantiating.
 */
export function discoverCapabilities(wasmBytes: Uint8Array): string[] {
    if (wasmBytes.length < 8 ||
        wasmBytes[0] !== 0x00 || wasmBytes[1] !== 0x61 ||
        wasmBytes[2] !== 0x73 || wasmBytes[3] !== 0x6d) {
        throw new Error('wasm-runner: not a WASM binary');
    }
    let p = 8;
    const need = (n: number): void => {
        if (p + n > wasmBytes.length) {
            throw new Error('wasm-runner: truncated WASM binary while parsing imports');
        }
    };
    const u8 = (): number => {
        need(1);
        return wasmBytes[p++];
    };
    const u32 = (): number => {
        let result = 0;
        let shift = 0;
        for (;;) {
            const b = u8();
            const bits = b & 0x7f;
            if (shift >= 32) {
                throw new Error('wasm-runner: LEB128 value exceeds 32 bits');
            }
            // At shift=28, only 4 bits fit in a u32; reject non-canonical
            // encodings that set bits beyond bit 31.
            if (shift === 28 && (bits & 0x70) !== 0) {
                throw new Error('wasm-runner: LEB128 value exceeds 32 bits');
            }
            result |= bits << shift;
            if ((b & 0x80) === 0) return result >>> 0;
            shift += 7;
        }
    };
    const str = (): string => {
        const len = u32();
        need(len);
        const s = new TextDecoder().decode(wasmBytes.subarray(p, p + len));
        p += len;
        return s;
    };
    const skipLimits = (): void => {
        const flags = u8();
        u32(); // min
        if ((flags & 0x01) !== 0) u32(); // max
    };
    while (p < wasmBytes.length) {
        const id = u8();
        const size = u32();
        need(size);
        if (id === 2) {
            const count = u32();
            const names: string[] = [];
            for (let i = 0; i < count; ++i) {
                const mod = str();
                const field = str();
                const kind = u8();
                switch (kind) {
                    case 0x00: u32(); break; // function: type index
                    case 0x01: u8(); skipLimits(); break; // table: elem type + limits
                    case 0x02: skipLimits(); break; // memory: limits
                    case 0x03: u8(); u8(); break; // global: value type + mutability
                    default: throw new Error(`wasm-runner: unknown WASM import kind ${kind}`);
                }
                names.push(`${mod}.${field}`);
            }
            return names;
        }
        p += size;
    }
    return [];
}

/**
 * Build a default mock response for every capability import: a wire-shape
 * skeleton of the capability's result type (from its first bridge call site),
 * so the default mock packs cleanly and the user edits a valid starting point.
 */
export function defaultCapabilityMocks(descriptor: ExecutionDescriptor): CapabilityMocks {
    const mocks: CapabilityMocks = {};
    const sites = descriptor.frame_lane.bridge_call_sites;
    for (const imp of descriptor.imports) {
        const shortName = capabilityShortName(imp.name);
        const site = sites.find((s) => s.import_ordinal === imp.ordinal);
        if (site !== undefined) {
            mocks[shortName] = JSON.stringify(
                skeletonFor(descriptor.wire_schema.nodes, site.result), null, 2);
        } else {
            mocks[shortName] = '{"ok":true,"data":null}';
        }
    }
    return mocks;
}

// ---------------------------------------------------------------------------
// Input skeleton generation
// ---------------------------------------------------------------------------

/**
 * Walks the wire schema from the entry input root to produce a JSON skeleton
 * with field names and default values, so the user does not hand-write blind
 * JSON. Shapes outside the frame subset (decimal/duration/timestamp/uuid/map)
 * yield `null`.
 */
export function generateInputSkeleton(descriptor: ExecutionDescriptor): string {
    const W = descriptor.wire_schema.nodes;
    const roots = descriptor.wire_schema.roots;
    // Workflow entry root: the entry node's node_inputs root, falling back to
    // the frame input root.
    const entryNode = descriptor.workflow_lane?.nodes.find((n) => n.schedule_pos === 0);
    const rootId = entryNode !== undefined
        ? (roots.node_inputs?.[entryNode.p6_block_ordinal] ?? roots.input)
        : roots.input;
    const skeleton = skeletonFor(W, rootId);
    return JSON.stringify(skeleton, null, 2);
}

function skeletonFor(W: WireNode[], wId: number): unknown {
    const w = W[wId];
    if (w === undefined) return null;
    switch (w.t) {
        case 'unit':
            return null;
        case 'bool':
            return false;
        case 'int':
            return 0;
        case 'float':
            return 0.0;
        case 'string':
            return '';
        case 'decimal':
        case 'duration':
        case 'timestamp':
        case 'uuid':
        case 'map':
        case 'closure':
            return null;
        case 'option':
            return null;
        case 'sequence':
            return [];
        case 'struct': {
            const obj: Record<string, unknown> = {};
            for (const field of w.fields) {
                obj[field.name] = skeletonFor(W, field.type);
            }
            return obj;
        }
        case 'enum': {
            const first = w.variants[0];
            if (first === undefined) return null;
            if (first.kind === 'unit') {
                return { _enum: w.name, _variant: first.name };
            }
            // Payload-carrying variants need a payload; default to the first
            // variant with a skeleton payload.
            const payload: Record<string, unknown> = {};
            for (const slot of first.slots) {
                payload[slot.name] = skeletonFor(W, slot.type);
            }
            return first.kind === 'tuple'
                ? { _enum: w.name, _variant: first.name, _payload: Object.values(payload) }
                : { _enum: w.name, _variant: first.name, _named_payload: payload };
        }
        case 'tuple':
            return w.elements.map((e) => skeletonFor(W, e));
    }
    return null;
}
