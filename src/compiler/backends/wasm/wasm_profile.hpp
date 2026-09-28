#pragma once

namespace ahfl::backends {

// Deployment profile for an emitted executable wasm module. The module ABI is
// profile-independent; the profile only selects the host import set (WASI
// imports vs JS-proxy capability imports) and the browser capability
// restrictions. Backend-local mirror of ahfl::WasmProfile, so the backend
// need not depend on the public driver header.
enum class WasmProfileKind {
    Wasi,
    Browser,
};

} // namespace ahfl::backends
