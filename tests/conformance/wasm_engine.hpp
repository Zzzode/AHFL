#pragma once

// KR6.7 (RFC 0026 P7): manifest-driven Core-Wasm module producer.
//
// Given one loaded conformance case + one scenario, `produce_conformance_wasm`
// runs the SAME compile -> lower -> P4-D layout -> typed-entry -> emit pipeline
// the eligibility classifier runs, writes the emitted module bytes, and returns
// a machine-readable execution DESCRIPTOR. The descriptor is produced by the
// backend from the internal plan the bytes were emitted from
// (`CoreWasmCodegenResult::descriptor`); this layer only decorates it with the
// scenario's manifest data (canonical input bytes and the capability mock
// table) and serializes it. There are no bespoke `key=value` observation lines:
// the Node embedded host consumes the descriptor JSON and the wasm bytes only.
//
// The descriptor is the neutral contract that lets ONE generic embedded host
// reconstruct the canonical observation for every eligible case (agent or
// workflow), instead of one bespoke host script per probe.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "base/json/json_value.hpp"
#include "compiler/backends/infra/core_wasm_codegen.hpp"
#include "conformance/conformance_case.hpp"

namespace ahfl::conformance {

inline constexpr std::string_view kConformanceWasmDescriptorFormat =
    "ahfl.conformance-wasm-descriptor.v1";

/// Why a case/scenario cannot produce a canonical Node observation. Mirrors the
/// manifest lane but is computed against the real emit path.
enum class WasmProduceSkip {
    None,
    /// The emit path rejects the case on the orchestration lane (KR6.6
    /// computation seam / layout / out-of-contract).
    Blocked,
    /// The module emits, but a handler projects the raw P4-D input frame; a
    /// canonical wire-JSON output observation awaits the P6-7 frame decision.
    RawP6FrameAwaitsP67,
    /// The module emits and runs, but the surfaced construct (user-defined
    /// pure fn calls / first-class closures) has no in-process evaluator
    /// reference yet; the Node run is authoritative (evaluator retires KR6.8).
    EvaluatorSurfaceAwaitsKr68,
};

struct WasmProduceResult {
    /// True iff the module was emitted and a descriptor is available.
    bool ok{false};
    /// Emitted module bytes (populated iff ok).
    std::vector<std::uint8_t> artifact_bytes;
    /// Canonical descriptor document bytes (populated iff ok).
    std::string descriptor_json;
    /// Structured skip verdict when the case must not run the differential.
    WasmProduceSkip skip{WasmProduceSkip::None};
    /// Stable skip/error code ("kr6.6", "p6-7", a wasm/core diagnostic code).
    std::string code;
    /// Human-readable reason.
    std::string reason;
};

/// Emits the Core-Wasm module for the case's typed manifest entry and builds the
/// scenario descriptor. Pure with respect to the repository (writes nothing);
/// the caller owns artifact persistence.
[[nodiscard]] WasmProduceResult produce_conformance_wasm(const LoadedConformanceCase &loaded,
                                                         const ConformanceScenario &scenario);

} // namespace ahfl::conformance
