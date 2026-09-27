// RFC 0026 P6-7 frame-bridge v2 rung V2-B: node-only probe for a computed
// final that constructs a String-PAYLOAD ENUM variant through an if
// (Match::Hit("hit")) against a unit variant (Match::Miss). It pins that the
// in-module String(PtrLen) construction reaches a payload-enum union slot, not
// only struct fields:
//
//   1. the module carries exactly ONE Data(11) section after Code(10) whose
//      single active segment initializes rodata [256,1024) with the expected
//      hash-consed, 8-aligned literal image (KAT);
//   2. runv() returns (OK, outputBase);
//   3. Hit: the discriminant selects the payload variant and the inline PtrLen
//      at the reported payload offset names the rodata bytes decoding to
//      "hit"; Miss: tag 0 and every inactive payload/padding word is zero.
//
// args:
//   <module.wasm> <inputBase> <outputBase> <outputSize> <payloadPtrOff>
//   <flag:0|1> <expectedTag> <expectedString|""> <rodataImage:hex>
import fs from "node:fs";

const [modulePath, inputBase, outBase, outSize, payloadOff, flag,
       expectedTag, expectedString, rodataHex] = process.argv.slice(2);

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

// Section-order walk: exactly ONE Data(11) after Code(10).
let offset = 8;
let dataPayload = null;
let sawCode = false;
while (offset < bytes.length) {
  const id = bytes[offset++];
  const [size, after] = readU32(bytes, offset);
  offset = after;
  if (id === 10) sawCode = true;
  if (sawCode && id !== 0 && id !== 11 && id !== 10) {
    throw new Error(`unexpected section id ${id} after Code`);
  }
  if (id === 11) {
    if (!sawCode) throw new Error("Data section precedes Code");
    if (dataPayload !== null) throw new Error("more than one Data section");
    dataPayload = bytes.subarray(offset, offset + size);
  }
  offset += size;
}
if (dataPayload === null) {
  throw new Error("expected exactly one Data(11) section for a Hit literal");
}

// Parse the single active segment and KAT its rodata image.
{
  let p = 0;
  if (dataPayload[p++] !== 1) throw new Error("expected one data segment");
  if (dataPayload[p++] !== 0) throw new Error("segment must be active flags 0");
  if (dataPayload[p++] !== 0x41) throw new Error("offset expr must be i32.const");
  let value = 0, shift = 0, b;
  do {
    b = dataPayload[p++];
    value |= (b & 0x7f) << shift;
    shift += 7;
  } while (b & 0x80);
  if (shift < 32 && (b & 0x40)) value |= (-1 << shift);
  if (value !== 256) throw new Error(`data segment base ${value} != rodata 256`);
  if (dataPayload[p++] !== 0x0b) throw new Error("offset expr must end");
  const [len, afterLen] = readU32(dataPayload, p);
  p = afterLen;
  const image = dataPayload.subarray(p, p + len);
  if (Buffer.from(image).toString("hex") !== rodataHex) {
    throw new Error(
      `rodata image mismatch:\n got ${Buffer.from(image).toString("hex")}\n` +
      `want ${rodataHex}`);
  }
}

const mod = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(mod).length !== 0) {
  throw new Error("a computed-final agent must import no capability");
}
const {exports} = await WebAssembly.instantiate(mod, {});
const view = new DataView(exports.memory.buffer);

new Uint8Array(exports.memory.buffer, 1024, 16384 - 1024).fill(0);
view.setInt32(Number(inputBase), Number(flag), true);

const tuple = exports.runv();
if (tuple[0] !== 0) throw new Error(`runv non-OK status ${tuple[0]}`);
if (tuple[1] !== Number(outBase)) {
  throw new Error(`runv value_ptr ${tuple[1]} != output base ${outBase}`);
}

const tag = view.getInt32(Number(outBase) + 0, true);
if (tag !== Number(expectedTag)) {
  throw new Error(`enum discriminant got ${tag} want ${expectedTag}`);
}

if (expectedString !== "") {
  const ptr = view.getInt32(Number(outBase) + Number(payloadOff), true);
  const len = view.getInt32(Number(outBase) + Number(payloadOff) + 4, true);
  if (ptr < 256 || ptr + len > 1024) {
    throw new Error(`payload PtrLen ${ptr}+${len} escapes rodata [256,1024)`);
  }
  const decoded = Buffer.from(
    new Uint8Array(exports.memory.buffer, ptr, len)).toString("utf8");
  if (decoded !== expectedString) {
    throw new Error(`enum payload string got ${JSON.stringify(decoded)} want ` +
                    JSON.stringify(expectedString));
  }
}

// Every output-frame word other than the active tag (and, for Hit, the two
// PtrLen words) must be zero — an inactive variant's payload and all padding.
const covered = new Set([0]);
if (expectedString !== "") {
  covered.add(Number(payloadOff));
  covered.add(Number(payloadOff) + 4);
}
for (let off = 0; off < Number(outSize); off += 4) {
  if (covered.has(off)) continue;
  const word = view.getUint32(Number(outBase) + off, true);
  if (word !== 0) {
    throw new Error(`non-zero inactive-payload/padding word @${off}: ${word}`);
  }
}

console.log("V2-B enum-String payload frame Node execution passed");
