// RFC 0026 P6-7 frame-bridge v2 rung V2-B fix-forward: node-only probe for the
// D6 input-payload ARENA multiplicity rule. A computed-final agent takes a
// bounded collection whose ELEMENT subtree carries one or more bounded String
// slots. The host packs EVERY capacity element through the reference packer's
// one-bump-cursor-per-live-element protocol and asserts:
//
//   1. the declared payload arena capacity equals align8(sum over OCCURRENCES
//      of the String byte bounds) — capacity*subtree, not the subtree once
//      (metering the shape once fail-closed while packing element >= 2);
//   2. all capacity elements pack back-to-back inside that arena and every
//      inline PtrLen (in the backing slots) names the bytes;
//   3. runv() succeeds and the computed Int final is materialized;
//   4. the artifact carries zero Data(11) sections (no literal is built).
//
// args:
//   <module.wasm> <inputBase> <outputBase> <arenaBase> <arenaCap>
//   <placement:lay:base:extent,...> <container:lay:hdrOff:stride:cap:elem:slot+slot;...>
//   <elementsJson: [[s,...],...] (inner array = the element subtree's PtrLen
//                                  slots in report order; one string for a bare
//                                  String element)>
//   <expectedOutInt>
import fs from "node:fs";

const [modulePath, inputBase, outBase, arenaBase, arenaCap,
       placementArg, containerArg, elementsJson, expectedOutInt] =
  process.argv.slice(2);

function readU32(data, offset) {
  let value = 0;
  let shift = 0;
  for (;;) {
    const byte = data[offset++];
    value |= (byte & 0x7f) << shift;
    if ((byte & 0x80) === 0) return [value, offset];
    shift += 7;
  }
}

const align8 = (n) => (n + 7n) & ~7n;

const bytes = fs.readFileSync(modulePath);
if (bytes.subarray(0, 8).toString("binary") !== "\0asm\x01\x00\x00\x00") {
  throw new Error("not a wasm v1 binary");
}
let offset = 8;
let dataCount = 0;
while (offset < bytes.length) {
  const id = bytes[offset++];
  const [size, after] = readU32(bytes, offset);
  offset = after;
  if (id === 11) ++dataCount;
  offset += size;
}
if (dataCount !== 0) {
  throw new Error(`an input-arena fixture must emit zero Data sections, got ${dataCount}`);
}

const placements = new Map();
for (const part of placementArg.split(",").filter((s) => s.length > 0)) {
  const [lay, base, extent] = part.split(":").map(Number);
  placements.set(lay, {base, extent});
}

const containers = containerArg.split(";").filter((s) => s.length > 0)
  .map((part) => {
    const [lay, hdrOff, stride, cap, elem, slotsArg] = part.split(":");
    return {
      lay: Number(lay),
      hdrOff: Number(hdrOff),
      stride: Number(stride),
      cap: Number(cap),
      elem: Number(elem),
      slots: slotsArg.split("+").filter((s) => s.length > 0).map(Number),
    };
  });
if (containers.length !== 1) {
  throw new Error(`expected exactly one input container, got ${containers.length}`);
}
const container = containers[0];
const placement = placements.get(container.lay);
if (placement === undefined) {
  throw new Error(`no backing placement for dense container layout ${container.lay}`);
}

const elements = JSON.parse(elementsJson);
if (!Array.isArray(elements) || elements.length !== container.cap) {
  throw new Error(`elements must be an array of ${container.cap} packed elements`);
}

const mod = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(mod).length !== 0) {
  throw new Error("a computed-final agent must import no capability");
}
const {exports} = await WebAssembly.instantiate(mod, {});
const view = new DataView(exports.memory.buffer);
new Uint8Array(exports.memory.buffer, 1024, 16384 - 1024).fill(0);

// One bump cursor, exactly like node_embedded_host.mjs packValue.
let cursor = Number(arenaBase);
const arenaHi = Number(arenaBase) + Number(arenaCap);
let totalBytes = 0n;

for (let i = 0; i < elements.length; ++i) {
  const slotValues = Array.isArray(elements[i]) ? elements[i] : [elements[i]];
  if (slotValues.length !== container.slots.length) {
    throw new Error(`element ${i} provides ${slotValues.length} strings for ` +
                    `${container.slots.length} PtrLen slots`);
  }
  for (let s = 0; s < slotValues.length; ++s) {
    const payload = Buffer.from(slotValues[s], "utf8");
    if (cursor + payload.length > arenaHi) {
      throw new Error(`HOST FAIL-CLOSED packing element ${i}: cursor=${cursor} ` +
        `needs ${payload.length} more but declared capacity is ${arenaCap}`);
    }
    new Uint8Array(exports.memory.buffer, cursor, payload.length).set(payload);
    const slotAddr = placement.base + i * container.stride + container.slots[s];
    view.setInt32(slotAddr, cursor, true);
    view.setInt32(slotAddr + 4, payload.length, true);
    cursor += payload.length;
    totalBytes += BigInt(payload.length);
  }
}

// The inline container header names the backing placement and the LIVE count
// (the fixture packs to full capacity).
const headerAddr = Number(inputBase) + container.hdrOff;
view.setInt32(headerAddr, placement.base, true);
view.setInt32(headerAddr + 4, elements.length, true);

// D6 multiplicity KAT: the declared capacity must be exactly align8 over the
// per-occurrence sum. One element's subtree budget (the pre-fix metering) is
// strictly smaller, so this fails on the old code and passes on the new.
const expectedCap = align8(totalBytes);
if (BigInt(arenaCap) !== expectedCap) {
  throw new Error(`payload arena capacity ${arenaCap} != align8(per-occurrence ` +
                  `sum) ${expectedCap} (container multiplicity under-metered)`);
}
if (elements.length > 1) {
  const oneElement = align8(totalBytes / BigInt(elements.length));
  if (expectedCap <= oneElement) {
    throw new Error("fixture no longer distinguishes per-occurrence metering from " +
                    "the single-subtree budget");
  }
}

const tuple = exports.runv();
if (tuple[0] !== 0) throw new Error(`runv non-OK status ${tuple[0]}`);
if (tuple[1] !== Number(outBase)) {
  throw new Error(`runv value_ptr ${tuple[1]} != output base ${outBase}`);
}
const got = Number(view.getBigInt64(Number(outBase), true));
if (got !== Number(expectedOutInt)) {
  throw new Error(`computed final Int got ${got} want ${expectedOutInt}`);
}

// Read every packed PtrLen back through its declared span as a round-trip.
for (let i = 0; i < elements.length; ++i) {
  const slotValues = Array.isArray(elements[i]) ? elements[i] : [elements[i]];
  for (let s = 0; s < slotValues.length; ++s) {
    const slotAddr = placement.base + i * container.stride + container.slots[s];
    const ptr = view.getInt32(slotAddr, true);
    const len = view.getInt32(slotAddr + 4, true);
    if (ptr < Number(arenaBase) || ptr + len > arenaHi) {
      throw new Error(`element ${i} slot ${s} PtrLen escapes the packed arena`);
    }
    const decoded = Buffer.from(
      new Uint8Array(exports.memory.buffer, ptr, len)).toString("utf8");
    if (decoded !== slotValues[s]) {
      throw new Error(`element ${i} slot ${s} round-trip mismatch: ` +
                      `${JSON.stringify(decoded)} != ${JSON.stringify(slotValues[s])}`);
    }
  }
}

console.log("V2-B bounded-collection String arena multiplicity Node execution passed");
