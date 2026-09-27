// RFC 0026 P6-7 frame-bridge v2 rung V2-B: node-only probe for a computed
// STRING final. Drives runv on a real Node v22 module (NOT wasmtime) and
// validates the whole D1 contract:
//
//   1. the module carries exactly ONE Data(11) section AFTER Code(10) and
//      before the EOF custom sections, with one active segment (flags 0,
//      memory 0, i32.const 256, end) whose payload is EXACTLY the expected
//      hash-consed, byte-sorted, 8-aligned literal image (rodata layout /
//      hash-cons / extent KAT);
//   2. runv() returns (OK, outputBase);
//   3. the output frame holds the inline PtrLen (payload_ptr, byte_len) at
//      the String slot and the i64 leaf; the payload pointer names the
//      rodata region [256,1024), and the bytes it points at decode to the
//      expected UTF-8 string (canonical-bytes agreement with the evaluator).
//
// args:
//   <module.wasm> <outputBase> <flag:0|1> <expectedString> <expectedInt|"">
//   <expectedRodataImage:hex>
import fs from "node:fs";

const [modulePath, outBase, flag, expectedString, expectedIntArg,
       rodataHex] = process.argv.slice(2);

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
if (bytes.subarray(0, 8).toString("binary") !==
    "\0asm\x01\x00\x00\x00") {
  throw new Error("not a wasm v1 binary");
}

// Walk the section table, collecting the Data(11) payload and checking
// section order: ... Code(10), Data(11), then only customs.
let offset = 8;
let dataPayload = null;
let sawCode = false;
const order = [];
while (offset < bytes.length) {
  const id = bytes[offset++];
  const [size, after] = readU32(bytes, offset);
  offset = after;
  order.push(id);
  if (id !== 0 && id !== 11 && !sawCode) {
    // standard pre-code section; allowed
  }
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
  throw new Error("expected exactly one Data(11) section");
}

// Parse the single active segment: count=1, flags=0, i32.const, s32 LEB,
// end, vec len, bytes.
{
  let p = 0;
  if (dataPayload[p++] !== 1) throw new Error("expected one data segment");
  if (dataPayload[p++] !== 0) throw new Error("segment must be active flags 0");
  if (dataPayload[p++] !== 0x41) throw new Error("offset expr must be i32.const");
  // signed LEB32
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
  if (p + len !== dataPayload.length) {
    throw new Error("trailing bytes in data segment");
  }
}

const mod = await WebAssembly.compile(bytes);
if (WebAssembly.Module.imports(mod).length !== 0) {
  throw new Error("a computed-final agent must import no capability");
}
const {exports} = await WebAssembly.instantiate(mod, {});
const view = new DataView(exports.memory.buffer);

// Pack the input frame: flag @0 i32.
new Uint8Array(exports.memory.buffer, 1024, 16384 - 1024).fill(0);
view.setInt32(1024, Number(flag), true);

const tuple = exports.runv();
if (tuple[0] !== 0) throw new Error(`runv non-OK status ${tuple[0]}`);
if (tuple[1] !== Number(outBase)) {
  throw new Error(`runv value_ptr ${tuple[1]} != output base ${outBase}`);
}

// Out layout: label PtrLen @0/@4; the optional Int leaf sits at @8.
const ptr = view.getInt32(Number(outBase) + 0, true);
const len = view.getInt32(Number(outBase) + 4, true);
if (ptr < 256 || ptr + len > 1024) {
  throw new Error(`PtrLen ${ptr}+${len} escapes the rodata region [256,1024)`);
}
if (len !== Buffer.byteLength(expectedString, "utf8")) {
  throw new Error(`PtrLen byte_len ${len} != expected ${expectedString.length}`);
}
const decoded = Buffer.from(
  new Uint8Array(exports.memory.buffer, ptr, len)).toString("utf8");
if (decoded !== expectedString) {
  throw new Error(`rodata string mismatch: got ${JSON.stringify(decoded)} want ` +
                  JSON.stringify(expectedString));
}
if (expectedIntArg !== "") {
  const value = Number(view.getBigInt64(Number(outBase) + 8, true));
  if (value !== Number(expectedIntArg)) {
    throw new Error(`output int @8: got ${value} want ${expectedIntArg}`);
  }
}

console.log("V2-B computed-string frame Node execution passed");
