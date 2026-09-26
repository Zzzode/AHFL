// RFC 0026 P6-7 frame-bridge v2 rung V2-A: node-only probe for a
// COMPUTED FINAL module. Packs raw input words, drives runv, and
// walks every reported output word including zero padding. This is a
// bespoke node-only lane (the census/descriptor walker does the
// manifest path), driven entirely by producer-reported P4-D facts.
//
// args:
//   <module.wasm> <inputBase> <outputBase> <outputSize>
//   <in words off:width:value,...> <expected out off:width:value,...>
import fs from "node:fs";

const [modulePath, inBase, outBase, outSize, inWords, outWords] =
  process.argv.slice(2);

function parseWords(spec) {
  if (spec === "") return [];
  return spec.split(",").map((entry) => {
    const [off, width, value] = entry.split(":");
    return {off: Number(off), width: Number(width), value: Number(value)};
  });
}

const inputs = parseWords(inWords);
const expected = parseWords(outWords);

const bytes = fs.readFileSync(modulePath);
const mod = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(mod).length !== 0) {
  throw new Error("a computed-final agent must import no capability");
}
const {exports} = await WebAssembly.instantiate(mod, {});
const view = new DataView(exports.memory.buffer);

// Pack the raw input frame; zero the reserved frame region first
// so stale words never leak into the output materialization.
new Uint8Array(exports.memory.buffer, 1024, 16384 - 1024).fill(0);
for (const w of inputs) {
  const addr = Number(inBase) + w.off;
  if (w.width === 64) view.setBigInt64(addr, BigInt(w.value), true);
  else view.setInt32(addr, w.value | 0, true);
}

const tuple = exports.runv();
if (tuple[0] !== 0) throw new Error(`runv non-OK status ${tuple[0]}`);
if (tuple[1] !== Number(outBase)) {
  throw new Error(`runv value_ptr ${tuple[1]} != output base ${outBase}`);
}

// Every expected leaf word must match exactly.
for (const w of expected) {
  const addr = Number(outBase) + w.off;
  const got =
    w.width === 64 ? Number(view.getBigInt64(addr, true))
                   : view.getInt32(addr, true);
  if (got !== w.value) {
    throw new Error(`output word @${w.off} width ${w.width}: got ${got} want ${w.value}`);
  }
}

// Every remaining output-frame 4-byte word within the root size must be
// zero (padding / inactive union words the materializer proved zero).
// A 64-bit leaf covers TWO words (off and off+4); a 32-bit leaf
// one. Build the covered word set from the expected leaf widths.
const covered = new Set();
for (const w of expected) {
  covered.add(w.off);
  if (w.width === 64) covered.add(w.off + 4);
}
for (let off = 0; off < Number(outSize); off += 4) {
  if (covered.has(off)) continue;
  const word = view.getUint32(Number(outBase) + off, true);
  if (word !== 0) {
    throw new Error(`non-zero padding/output word @${off}: ${word}`);
  }
}

console.log("V2-A computed-final frame Node execution passed");
