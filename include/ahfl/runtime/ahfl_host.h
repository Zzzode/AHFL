/* ahfl_host.h — AHFL Capability Embedding ABI (RFC 0021), slice 1: contract only.
 *
 * The single language-agnostic C contract by which a host (implemented in any
 * language: C++/Rust/Go/Java/Node/browser JS) provides capabilities to an
 * embedded AHFL agent workflow. Both the native binding (a C++ function-pointer
 * table, replacing the current ContextualCapabilityInvoker wiring) and the WASM
 * binding (RFC 0019 ahfl_cap imports) derive from THIS header — one contract,
 * two projections.
 *
 * Positioning: AHFL is an embeddable, verifiable agent-workflow orchestration
 * DSL (RFC 0020). It orchestrates workflow structure/behavior; general
 * computation is provided by the host across this capability boundary. The
 * AHFL language keeps SYNCHRONOUS capability-call semantics — asynchrony is
 * absorbed host-side via AHFL_CAP_PENDING; the language never gains async/await.
 *
 * ---------------------------------------------------------------------------
 * CURRENT ABI RULES:
 *   Fixed-width integer types and natural alignment; no bitfields or packed
 *   layouts. Callers and runtime use the same current header. struct_size must
 *   equal sizeof(ahfl_invoke_args); missing or appended layouts are rejected.
 *   Wire frames are length-prefixed canonical wire JSON bytes.
 * ---------------------------------------------------------------------------
 *
 * This header is slice 1: it defines the contract. The native/WASM bindings,
 * the PENDING resume interface, and per-language host SDKs are later slices
 * (see RFC 0021 Implementation Plan and its DEFER list).
 */
#ifndef AHFL_HOST_H
#define AHFL_HOST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Capability call status: fixed width, fail-closed --------------------
 * The ABI type is uint32_t (not a bare enum). OK is 0. Codes 0x0001..0x0FFF
 * are reserved for the core; 0x1000+ for hosts. RULE (fail-closed): any status
 * the runtime does not recognize MUST be treated as AHFL_CAP_ERROR. */
typedef uint32_t ahfl_cap_status;
enum {
    AHFL_CAP_OK = 0u,      /* success; result frame is populated (see below) */
    AHFL_CAP_ERROR = 1u,   /* capability failed; workflow terminates + propagates */
    AHFL_CAP_PENDING = 2u  /* async: workflow suspends at its current node    */
};

/* ---- Opaque per-connection host context --------------------------------
 * Passed to every entry point so the runtime can carry per-connection /
 * multi-instance / thread state for the current invocation. The host
 * defines the concrete type; the runtime treats it as opaque. */
typedef struct ahfl_host ahfl_host;

/* ---- Allocator: single-owner rule for frame memory ----------------------
 * Both take `host` so a frame is always freed by the same allocator that made
 * it (no cross-allocator free). In the WASM binding these project onto the
 * module's exported alloc/dealloc (RFC 0019); in the native binding they are
 * the runtime's frame allocator over the same address space. */
uint8_t *ahfl_alloc(ahfl_host *host, uint32_t len);
void ahfl_dealloc(ahfl_host *host, uint8_t *ptr, uint32_t len);

/* ---- Invoke arguments (exact current layout) ---------------------------
 * Frames are length-prefixed little-endian serialized bytes in the connection's
 * canonical wire JSON format. */
typedef struct ahfl_invoke_args {
    uint32_t struct_size;  /* caller sets = sizeof(ahfl_invoke_args)          */
    uint32_t cap_id;       /* capability SymbolId (index-based identity)      */
    const uint8_t *args_ptr; /* caller-owned serialized argument frame        */
    uint32_t args_len;
    uint8_t **result_ptr;  /* out: callee-allocated result frame, or NULL     */
    uint32_t *result_len;  /* out: result frame length, or 0                  */
} ahfl_invoke_args;

/* ---- The capability call ------------------------------------------------
 * The runtime (caller) owns the argument frame; the host capability (callee)
 * produces the result frame. Post-conditions by status:
 *
 *   AHFL_CAP_OK      : callee sets *result_ptr to a buffer obtained from
 *                      ahfl_alloc(host, ...) and *result_len to its length.
 *                      The caller decodes it and frees it exactly once via
 *                      ahfl_dealloc; it is never read after free.
 *   AHFL_CAP_ERROR   : callee MUST set *result_ptr = NULL and *result_len = 0.
 *                      The caller MUST NOT free. The workflow terminates and
 *                      propagates the failure (fail-closed; never silently
 *                      swallowed).
 *   AHFL_CAP_PENDING : callee sets *result_ptr = NULL. The workflow suspends at
 *                      its current node; ownership of the argument frame
 *                      transfers to the host for the suspension lifetime. The
 *                      host later resumes the workflow with the result (resume
 *                      interface is a later slice; see RFC 0021).
 *
 * The argument frame is caller-owned and MUST NOT be retained by the callee
 * past return, except under AHFL_CAP_PENDING (ownership transfer above). */
ahfl_cap_status ahfl_invoke(ahfl_host *host, ahfl_invoke_args *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* AHFL_HOST_H */
