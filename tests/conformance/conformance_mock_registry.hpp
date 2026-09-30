#pragma once

// KR6.7 (RFC 0026 P7) / KR6.8 WH-5: the shared mock-capability registry for
// conformance engine adapters.
//
// Builds the mock capability table declared by a conformance case. `ok`
// outcomes are materialized ONCE from canonical wire JSON (value_from_json)
// and cloned per invocation through register_function; `error` / `pending`
// outcomes return their terminal status directly. This deliberately mirrors
// the canonical native-value path rather than the CLI's string-wrapping
// LLM-tool seam.
//
// Extracted from evaluator_engine.cpp (anonymous namespace) so the native
// engine adapter (native_engine.cpp) can share the identical mock table
// without duplicating the construction logic.

#include <optional>
#include <string>

#include "conformance/conformance_case.hpp"
#include "runtime/engine/capability_bridge.hpp"

namespace ahfl::conformance {

/// Builds the mock capability table declared by the case. Returns the
/// registry, or `nullopt` with `error_out` set when a manifest capability
/// is malformed (e.g. an `ok` outcome without a result frame).
[[nodiscard]] std::optional<ahfl::runtime::CapabilityRegistry>
build_mock_registry(const ConformanceCase &manifest, std::string &error_out);

} // namespace ahfl::conformance
