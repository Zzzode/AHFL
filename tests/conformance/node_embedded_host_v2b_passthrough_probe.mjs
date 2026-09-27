// RFC 0026 P6-7 frame-bridge v2 rung V2-B: node-only probe for the INPUT-ARENA
// String PASSTHROUGH final (`return Frame { label: input.label }`). No String
// literal is constructed, so the module MUST carry ZERO Data(11) sections; the
// only legal payload source for the returned PtrLen is the packed
// input-payload arena span declared in the frame descriptor.
//
//   1. the host packs one string into the input-payload arena and writes its
//      PtrLen at the input slot;
//   2. runv() returns (OK, outputBase);
//   3. the output PtrLen names the EXACT arena span (same pointer, same
//      length) and the bytes round-trip verbatim (the words are copied, never
//      re-packed);
//   4. the artifact carries no Data(11) section and imports no capability.
//
// args:
//   <module.wasm> <inputBase> <outputBase> <inPtrOff> <outPtrOff>
//   <arenaBase> <arenaCap> <inputString>
import fs from "node:fs";

const [modulePath, inputBase, outBase, inPtrOff, outPtrOff,
       arenaBase, arenaCap, inputString] = process.argv.slice(2);

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

const bytes = fs.readFileSync(modulePath);
if (bytes.subarray(0, 8).toString("binary") !== "\0asm\x01\x00\x00\x00") {
  throw new Error("not a wasm v1 binary");
}

// Zero Data(11) sections: the passthrough constructs no literal.
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
  throw new Error(`a passthrough final must emit zero Data sections, got ${dataCount}`);
}

const mod = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(mod).length !== 0) {
  throw new Error("a computed-final agent must import no capability");
}
const {exports} = await WebAssembly.instantiate(mod, {});
const view = new DataView(exports.memory.buffer);

new Uint8Array(exports.memory.buffer, 1024, 16384 - 1024).fill(0);

// Pack the string into the input-payload arena (the reference host's bump
// cursor: base + one span, fail-closed against the declared capacity).
const payload = Buffer.from(inputString, "utf8");
const arenaLo = Number(arenaBase);
const arenaHi = arenaLo + Number(arenaCap);
if (arenaLo + payload.length > arenaHi) {
  throw new Error("test input exceeds the declared input-payload arena");
}
new Uint8Array(exports.memory.buffer, arenaLo, payload.length).set(payload);

// Write the inline input PtrLen naming that arena span.
view.setInt32(Number(inputBase) + Number(inPtrOff), arenaLo, true);
view.setInt32(Number(inputBase) + Number(inPtrOff) + 4, payload.length, true);

const tuple = exports.runv();
if (tuple[0] !== 0) throw new Error(`runv non-OK status ${tuple[0]}`);
if (tuple[1] !== Number(outBase)) {
  throw new Error(`runv value_ptr ${tuple[1]} != output base ${outBase}`);
}

const ptr = view.getInt32(Number(outBase) + Number(outPtrOff), true);
const len = view.getInt32(Number(outBase) + Number(outPtrOff) + 4, true);
if (ptr !== arenaLo) {
  throw new Error(`passthrough PtrLen pointer ${ptr} does not name the input ` +
                  `arena span ${arenaLo}`);
}
if (len !== payload.length) {
  throw new Error(`passthrough PtrLen length ${len} != ${payload.length}`);
}
const decoded = Buffer.from(
  new Uint8Array(exports.memory.buffer, ptr, len)).toString("utf8");
if (decoded !== inputString) {
  throw new Error(`passthrough bytes ${JSON.stringify(decoded)} do not round-trip ` +
                  JSON.stringify(inputString));
}

console.log("V2-B input-arena String passthrough frame Node execution passed");
