#pragma once

#include "ahfl/compiler/ir/core_wire_migration.hpp"
#include "base/json/json_value.hpp"
#include "runtime/engine/core_wire_codec.hpp"

// RFC 0026 KR6.5 E4-B0-C2b stage3 (P0-16): a NARROW, recovery/runtime-INTERNAL
// decode entry for old (pre-sidecar) durable-resume memo results.
//
// This header is intentionally NOT the public codec surface (core_wire_codec.hpp).
// The public exact `decode_json` / `validate_value` take no policy parameter, so
// live trust paths (HTTP/gRPC capabilities, CLI raw pending) literally cannot
// select the legacy policy — they cannot name this entry. Only the durable-resume
// loader/runtime, which alone constructs a LegacyV2-source memo entry, includes
// this header and calls `decode_json_legacy_v2`.
//
// The ONLY behavioral difference from the exact decoder is that under a Float
// schema node a JSON SignedInteger is accepted as a historical integral-Float
// artifact (an old snapshot serialized `1.0` as the bare int `1` through the
// generic serializer, which C2b's exact decoder rejects as int->float widening).
// This widening is recursive (it applies to a Float inside a Struct/List/Option
// etc.), and it is the irreducible compatibility cost of the pre-sidecar bytes:
// on that one slot a hostile bare Int can no longer be distinguished from a
// legitimate integral-Float artifact. Everything else runs the SAME shape
// traversal with the SAME exact rules.

namespace ahfl::runtime::wire_codec {

// Decode a raw JSON DOM value under the given binding using the LegacyV2 policy
// (Float schema node accepts SignedInteger -> Float, recursively). For old
// pre-sidecar durable-resume memo results ONLY. The policy type itself is a
// private detail of core_wire_codec.cpp; callers only ever pick between the exact
// `decode_json` and this named legacy entry.
[[nodiscard]] WireDecodeResult
decode_json_legacy_v2(const json::JsonValue &json,
                      const ir::core::VerifiedWireSchemaBinding &binding);

} // namespace ahfl::runtime::wire_codec
