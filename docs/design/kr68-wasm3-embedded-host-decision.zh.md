# DECISION REPORT — RFC 0026 KR6.8: Production Wasm Engine + Evaluator Retirement Ladder

**Deciding agent:** dedicated KR6.8 decision agent (read-only; HEAD `acc8d197`, 2026-09-29).
**Status:** design decision only. No production code written, no file in the repository modified, no commit made.
**Method note:** every engine-coverage claim below was tested by me today in THIS offline environment — I downloaded, built, and embedded both candidate interpreters and ran actual AHFL-emitted modules plus a hand-built multi-value-import fixture through each. No coverage claim is taken from vendor documentation or marketing material.

---

## 1. Decision + rationale

**Choose candidate B: vendor wasm3 (MIT) — release tag `v0.9.0` (2026-08-24) — as C sources under `third_party/wasm3/`, driven by a thin C++ embedded host in a NEW peer-tier runtime directory, fast-tier portable interpreter, no WASI, `ahfl_cap` bound through wasm3's raw-function ABI.**

Rationale, in order of decisiveness:

1. **It is the only candidate proven to execute the ACTUAL emitted ABI, and it does so today, fully offline.** The load-bearing fact: every capability-bearing AHFL module imports `ahfl_cap.cap_<symbol-id>` with functype **`(i32,i32) -> (i32,i32,i32)`** — a THREE-result host import. That import is emitted at `src/compiler/backends/wasm/core_wasm_codegen.cpp:14093` (agent) and `:16339` (workflow), typed by the fixed type-table entry `kTypeCapabilityTuple = 4` declared at `:298` (comment `:4017` spells the shape `(i32,i32) -> (i32,i32,i32): an opaque frame`), and applied at `:14098` / `:14115` (agent) and `:16344` / `:16362` / `:16365` (workflow). I built a 67-byte wasm module with exactly that import and a one-call body (`/tmp/tri.wasm`). Under **wasm3 v0.5.0 and v0.9.0**, `m3_LinkRawFunction(mod, "ahfl_cap", "cap_0", "iii(i)", cb)` + `m3_CallV` returns all three host values end-to-end:

   ```
   $ /tmp/wasm3_probe/tri9 /tmp/tri.wasm      # wasm3 v0.9.0
   wasm3 cb slots: 0 0 0 42 0 0
   wasm3 results: 100 8 7                      # all three result cells survive
   $ /tmp/wasm3_probe/tri3 /tmp/tri.wasm      # wasm3 v0.5.0
   wasm3 results: 100 8 7
   ```

   Under **WAMR 2.4.5 AND upstream main @ `b70d708d` (2026-09-21)**, the identical module silently loses results 2 and 3. The classic and fast interpreters both declare `uint32 argv_ret[2]` and write back only `ret_cell_num == 1` / `== 2` arms:

   ```
   /tmp/wamr_probe/wasm-micro-runtime-WAMR-2.4.5/core/iwasm/interpreter/wasm_interp_fast.c:1204
       uint32 argv_ret[2], cur_func_index;
   :1295    if (cur_func->ret_cell_num == 1) {
   :1296        prev_frame->lp[prev_frame->ret_offset] = argv_ret[0];
   :1298    else if (cur_func->ret_cell_num == 2) {
   :1299        prev_frame->lp[prev_frame->ret_offset] = argv_ret[0];
   :1300        prev_frame->lp[prev_frame->ret_offset + 1] = argv_ret[1];
   ```
   ```
   /tmp/wamr_probe/wasm-micro-runtime-WAMR-2.4.5/core/iwasm/interpreter/wasm_interp_classic.c:1213
       uint32 argv_ret[2], cur_func_index;
   :1307    if (cur_func->ret_cell_num == 1) { prev_frame->sp[0] = argv_ret[0]; prev_frame->sp++; }
   :1311    else if (cur_func->ret_cell_num == 2) { prev_frame->sp[0]=argv_ret[0]; prev_frame->sp[1]=argv_ret[1]; prev_frame->sp += 2; }
   ```

   Confirmed empirically against both WAMR invocation paths with the same 67-byte fixture:
   ```
   $ /tmp/wamr_probe/tri /tmp/tri.wasm        # WAMR 2.4.5 raw native
   register=1
   wamr cb got 42                              # callback fires with the CORRECT argument
   wamr results cells: r[0]=0 r[1]=0 r[2]=0 r[3]=0   # results truncated to zero
   $ /tmp/wamr_probe/tri_capi /tmp/tri.wasm   # WAMR 2.4.5 C API
   c-api cb kind=0 i32=42                      # callback fires with the CORRECT argument
   c-api results: 0 0 0                        # results truncated
   ```
   And WAMR main is unchanged: `grep -rn 'argv_ret\[2\]' /tmp/wamr_probe/wamr-main/core/iwasm/interpreter/*.c` → `wasm_interp_fast.c:1204`, `wasm_interp_classic.c:1213`, with the identical 1/2-cell writeback at `wasm_interp_fast.c:1295-1301`. The standard C-API marshalling path (`wasm_runtime_invoke_c_api_native`, `core/iwasm/common/wasm_runtime_common.c:7227`) does handle up to 4 result cells internally, but the interpreter's fixed two-cell `argv_ret` truncates before the guest resumes — so this is not a config flag, it is the hard-coded import-call frame. Since `(i32,i32)->(i32,i32,i32)` is the foundational E2 opaque capability ABI (while the bridge `(i32)->(i32,i32)` and `runv()->(i32,i32)` are 2-result and DO work), WAMR **cannot run the capability lane without us maintaining a fork patch to its interpreter frame** — exactly the wheel-patching Principle 1 forbids, and a permanent merge burden on every upgrade.

2. **Coverage of every other emitted feature is identical and verified on both engines**, but only wasm3 covers the one that matters. I ran real AHFL modules under both engines: identity `run2(1024,5)->(0,1024,5)`; closure `Table`+`Element`+`call_indirect` construction; active `Data(11)` rodata String modules (`runv->(0,12288)`); `i64.extend_i32_s` widening; bounded recursion; p6 collections. Both pass everything except the three-result host import.

3. **Offline/vendor shape is the smaller, cleaner fit.** wasm3 core is the interpreter set only — `m3_bind.c m3_code.c m3_compile.c m3_core.c m3_env.c m3_exec.c m3_function.c m3_info.c m3_module.c m3_parse.c m3_validate.c` (11 C files, enumerated alongside the WASI/libc/tracer extras in upstream `source/CMakeLists.txt:1-17`), zero FetchContent, builds with `-DBUILD_WASI=none -DBUILD_NATIVE=OFF` in seconds, links only pthread/m/dl. WAMR's default CMake attempts to `git clone simde` from the network during configure (I hit this; defeated with `-DWAMR_BUILD_SIMD=0`), and its vendored tree is ~27 MB with many optional libraries and per-arch assembly trees that would have to be excluded with care.

4. **License is compatible.** wasm3 is MIT (Copyright Steven Massey / Volodymyr Shymanskyy), redistributable inside an Apache-2.0 project with attribution. WAMR is Apache-2.0 WITH LLVM-exception — also compatible; license did not decide this.

5. **Maintenance is healthy, not abandoned.** The "wasm3 is dormant" intuition is stale: `v0.5.0` was 2021-06-02, but `v0.9.0` shipped 2026-08-24 (214 commits in between — `git rev-list --count v0.5.0..v0.9.0` = 214), `v0.9.1-beta.1` on 2026-09-10, and the clone HEAD is a 2026-09-28 commit ("Exception handling API"). WAMR is more active still (Bytecode Alliance), which is the one real point in WAMR's favor — but activity cannot override a missing three-result import frame.

6. **It preserves the RFC's determinism/embeddability model.** Synchronous callbacks, a single fixed 64 KiB memory, no threads/WASI/JIT, a fresh instance per run, a deterministic interpreter — the `CoreWasmResumeEngine` port contract (`src/runtime/engine/core_wasm_resume_engine.hpp:19-31`, `:59-73`, `:121`) maps directly onto one wasm3 runtime + module per invocation.

**AHFL-specific divergence note (required by the project's reference-hierarchy rule):** the mainstream "safest" choice here is WAMR (Bytecode Alliance, Apache, WASI, JIT/AOT). We diverge for one documented, empirically demonstrated reason: AHFL's ABI is non-standard in requiring host imports to return **three** i32 words `(status, result_ptr, result_len)`, a shape Node/V8 and wasm3 accept but WAMR's interpreter frames structurally cap at two. Standardizing the ABI down to ≤2 results is NOT on the table in this decision: it is the wire-visible E2 contract frozen across E2/B1/B2/D2a and the durable-resume authority, and changing it would re-open the sealed resume seam. Choosing the engine that runs the frozen ABI unmodified is the only Principle-1-consistent path.

---

## 2. Emitted-module feature requirements (verified from codegen, with file:line)

| Feature | Required? | Evidence |
|---|---|---|
| **Wasm spec target** | MVP + **multi-value** + sign-extension ops only. NO SIMD, NO tail-call, NO bulk-memory instrs, NO GC/memory64/threads | Opcode constants used span the MVP numeric/memory/control set plus `kOpI64ExtendI32S = 0xac` (`:269`) and `kOpI32WrapI64 = 0xa7` (`:274`); `kOpCallIndirect = 0x11` (`:209`). No `br_table`, no `select`, no float opcodes anywhere |
| **i32 / i64** | Yes — full scalar ladder (compare/add/sub/mul/div_s/rem_s/and/or; widening and narrowing) | `kOpI64ExtendI32S` used at `:4920`, `:5015`, `:5355`; `kOpI32WrapI64` at `:5066` |
| **f32 / f64 value types** | **No.** f64 has NO value-type byte and NO numeric opcodes emitted; F64 fails closed | `Float` literal → `reject("float literals need the f64 opcode ladder, a later P6 slice", ...)` at `:4488`. f64 remains a P4-D frame word only |
| **Memory** | Exactly ONE linear memory, min = 1 page (64 KiB), **no declared max**, flags byte 0 | Agent memory section `:14155-14159`; workflow `:16383-16387`; capacity SSOT `include/ahfl/compiler/ir/core_wasm_abi_constants.hpp:28` with `static_assert(kCoreWasmFixedLinearMemoryCapacityBytes == 65536, ...)` at `:31` |
| **memory.grow / memory.size** | **Neither emitted.** Capacity is compile-time; `alloc` is a module-internal bump | No `0x3f`/`0x40` instruction in the opcode table; `kP6CollectionBackingBase < kCoreWasmFixedLinearMemoryCapacityBytes` asserted at `core_wasm_abi_constants.hpp:117` |
| **Table + Element + call_indirect** | Exactly ONE funcref table (min-only) and ONE active Element(9) segment **iff** the module constructs a first-class closure. Zero-closure modules omit both and stay byte-identical | `kSectionTable = 4` (`:146`), emission `:14146`; `kSectionElement = 9` (`:152`), emission `:14223`; `kOpCallIndirect` used at `:6400` |
| **Mutable globals** | Yes — mix of immutable and mutable i32 globals. Agent: 5 (`current_state` mutable, `transition_count` mutable, `ahfl_abi_version` immutable = 1, heap pointer, pending) | Agent globals `:14159-14174` (count literal `5`, then `append_global(globals, true, ...)` / `(globals, false, 1)`); workflow `:16393-16419`, count `(capability_workflow ? 6u : 5u) + (p6 ? runner_count : 0u)` |
| **Active Data section** | Exactly ONE active segment (id 11, flags 0, memory 0, offset expr `i32.const 256; end`) **iff** at least one String literal is constructed | `kSectionData = 11` (`:155`), emission `:14287` (agent) and `:16504`-region (workflow); rodata region SSOT `core_wasm_abi_constants.hpp:136-141` |
| **Imports** | ONLY module name **`ahfl_cap`**, field **`cap_<symbol-id>`** (numeric symbol identity — never the source name). **No `wasi_snapshot_preview1`, no `env`, no memory/global/table imports** | Agent import loop `:14086-14099` (`imports.name("ahfl_cap")` + `imports.name("cap_" + std::to_string(symbol))` at `:14093`); workflow `:16328-16348` (`:16339`) |
| Import functypes | Opaque capability `(i32,i32) -> (i32,i32,i32)` — **THREE results** (`kTypeCapabilityTuple = 4`); bridge `(i32) -> (i32,i32)` (`kTypeCapabilityBridge`, referenced at `:6562`, type index 6 in workflow at `:16313-16318`) | `kTypeCapabilityTuple` decl `:298`; shape comment `:4017`; applied `:14098`, `:14115`, `:16344`, `:16362`, `:16365` |
| **Exports (agent)** | 9, plus `runv` for the p6-frame lane: `memory, alloc, dealloc, run, run2, step, current_state, transition_count, ahfl_abi_version` [+ `runv`] | `:14185-14193`; `runv` conditional at `:14193` (`!p6_frame || append_export(... "runv" ...)`) |
| **Exports (workflow)** | 11: adds `workflow_node_count` and `workflow_completed_count`, no `runv` | `:16416-16427` |
| Export functypes | `alloc(i32)->i32`; `dealloc(i32,i32)->void`; `run`/`run2(i32,i32)->(i32,i32,i32)`; `step()->i32`; `current_state()->i32`; `runv()->(i32,i32)` (agent p6 only) | Fixed type table around `:14048-14052`; `runv` body builder `make_runv_body` declared at `:13951`, used at `:14258` |
| **Custom sections** (ignored by the engine, parsed by the host) | 0..3 EOF sections: `ahfl.wire-schema.v1`, `ahfl.core-layout.v1`, `ahfl.wasm-exec-manifest.v1` | Names at `:162-190` (`kWireSchemaSectionName`, `kCoreLayoutSectionName`, `kExecManifestSectionName`) |
| Status semantics | Host returns raw u32; `AHFL_CAP_OK = 0`, `ERROR = 1`, `PENDING = 2`; a non-OK bridge result is a trap; the host never pre-classifies | Bridge trap policy v2 D3; `CapabilityCallStatus` incl. `Pending` at `src/runtime/engine/capability_bridge.hpp:49-60` |
| Node-event region | Header `[1024,1032)`, then 40-byte records at 1032 | `core_wasm_abi_constants.hpp:188-207` (`kNodeEventLogBase = 1024`, `kNodeEventHeaderBytes = 8`, `kNodeEventRecordBytes = 40`, `kNodeEventRecordsBase = 1032` with `static_assert`) |

**Net engine requirement:** a deterministic interpreter implementing MVP numeric/memory/control + the multi-value proposal (multi-result exports AND, critically, **multi-result host imports up to three i32**) + sign-extension + mutable globals + one active Data segment + one funcref table with an active Element segment and `call_indirect`; the ability to register synchronous host functions by module/field name; and trap surfacing. No float ops, no `memory.grow`, no WASI, no JIT, no threads, no SIMD are required.

---

## 3. Candidate matrix + explicit rejects

| | **B wasm3 v0.9.0 (CHOSEN)** | **A WAMR 2.4.5 fast-interp** | **C Wasmtime C API** | **D Node.js subprocess** |
|---|---|---|---|---|
| License vs Apache-2.0 | MIT — compatible with NOTICE attribution | Apache-2.0 + LLVM-exception — compatible | Apache-2.0 engine, but a prebuilt is distributing third-party binaries; a source build pulls Cranelift/Rust | mixed MIT/BSD; AHFL stays Apache but bundles a runtime |
| Runs the `(i32,i32)->(i32,i32,i32)` host import | **YES — proven end-to-end today** (`tri9`: cb slots `0 0 0 42 0 0`, results `100 8 7`) | **NO — proven truncated to 2 cells** in both interpreters, on 2.4.5 and on main `b70d708d` | Yes (wasmtime fully supports multi-value) | Yes (V8 proven — current 66/0 census) |
| MVP + multi-value + sign-extension + globals + Data + table/call_indirect | all proven on real modules | all proven on real modules (except the import ceiling) | yes (full spec) | yes (V8) |
| Trap / status mapping | `m3Err_*` return from the call; raw callback returns a trap token — adequate | rich trap API | rich trap + typed errors | JS throw → trap (used today) |
| Determinism | deterministic threaded interpreter; synchronous raw callbacks; no JIT tier | deterministic fast-interp; JIT/AOT exist but off | Cranelift is deterministic but JIT adds surface + warm-up | V8 has JIT + tiering; deterministic only given fixed inputs (current evidence is fine, but heavier) |
| Footprint | 11 C files; small static archive; links pthread/m/dl | slim archive possible but ~27 MB tree and much more config surface | tens of MB of Rust build artifacts, or a ~40-70 MB prebuilt per platform | external Node install; `ahflc` stops being a single binary |
| CMake with NO network | builds offline; no FetchContent with WASI off; a flat file list | default configure **attempts `git clone simde`** (observed); needs `-DWAMR_BUILD_SIMD=0` plus exclusion discipline | source build needs rustc + cargo + crates fetch (impossible offline here); prebuilt needs per-OS artifacts checked in or downloaded | build is fine; **runtime requires node on PATH** |
| linux / mac / windows | pure C99, no asm in the core (portable threaded interpreter), one code path | has per-arch asm `invokeNative_*` — more files, still portable | excellent coverage but prebuilt-per-OS burden | Node exists on all three but must be installed by the user |
| Fresh-instance replay (D2b) | new `m3_NewRuntime` + reload per resume; synchronous import nested in `m3_Call` — matches `CoreWasmResumeEngine` exactly | same shape | same | current NodeResumeEngine forks + framed pipe — heavy, but exists |
| Maintenance | revived: v0.9.0 2026-08-24, beta 2026-09-10, HEAD 2026-09-28 (verified from the clone) | strongest (Bytecode Alliance, frequent releases) | strongest | strongest (V8) |
| Risk | historically a smaller project; the raw-callback ABI is C-level and WE own the safety wrappers; a future f64 ladder needs no engine change | **blocking**: would need a maintained fork patch for 3-result imports, or an ABI change to the sealed E2/resume contract | offline + distribution + dependency weight conflict with the vendored-only rule | embeddability/strategic conflict with the single-binary host DSL; fragile PATH dependency |

### Explicit rejects

- **A (WAMR) — rejected on demonstrated functional grounds, not preference.** Everything else about WAMR is first-rate, but the fast and classic interpreters hard-code `uint32 argv_ret[2]` with write-back arms only for `ret_cell_num` 1 or 2 (`wasm_interp_fast.c:1204,1295-1301`; `wasm_interp_classic.c:1213,1307-1314`), so a three-result host import — AHFL's opaque capability ABI on every capability module — loses its `result_ptr`/`result_len`. Confirmed on release **2.4.5** (`core/version.h:19-21` → 2.4.5) and on upstream **main** (`b70d708d`, 2026-09-21), with the same `argv_ret[2]` declaration and the same 1/2-cell arms: **not fixed**. The C-API path marshals up to 4 cells internally (`wasm_runtime_invoke_c_api_native`, `wasm_runtime_common.c:7227`) but the interpreter frame truncates before guest resume. Adopting WAMR therefore means either (a) a permanent local fork patch inside `third_party` (Principle-1-rejected wheel surgery, with re-application on every upgrade), or (b) changing the frozen `(status, ptr, len)` import ABI, which re-opens the sealed E2/B1/B2/D2a wire + resume contract and the `Run2ResultTuple` port (`core_wasm_resume_engine.hpp:59-73`) — far more invasive than choosing the engine that already runs it. WAMR's optional AOT/JIT tiers do not dodge the interpreter's import frame and add asm + build complexity. WAMR stays the obvious pick **if** upstream ever lands >2-cell host imports; a future decision agent should revisit then with a new dated record. The fast-JIT tier is an unverified possible workaround, not a fact, and would still ship assembly plus a non-default configuration — insufficient to override a proven blocker.
- **C (Wasmtime embedded) — rejected on the repo's hard constraints.** Technically excellent and it would run the ABI, but the repo builds CI offline, the project permits only **vendored** dependencies, and wasmtime is a large Rust/Cranelift system that cannot be source-built offline here and would otherwise demand checked-in or downloaded prebuilt platform binaries. That breaks the single-binary, reproducible, offline-compiler model.
- **D (Node.js external subprocess) — rejected as the PRODUCTION engine; retained only as a test oracle during the ladder.** Keeping it permanently means `ahflc run` / `ahfl-repl` / `ahfl-dap` are not standalone — they fail on any machine without Node on PATH (the conformance runner already encodes skip-77 for exactly this), contradicting the embeddable single-binary host-DSL north star and the "capabilities via host" positioning. It is also a much heavier process/IPC surface for what is a fixed-64-KiB, synchronous, deterministic workload. Node stays as the cross-engine independent oracle in conformance until the native lane is proven, then the subprocess lane is demoted (big-bang, Principle 1).
- **Self-written interpreter — rejected.** Building a wasm validator + interpreter (multi-value, tables, `call_indirect`, Data init, traps, bounds) is precisely the reinvention Principle 1 forbids; the RFC already rejected a self-written bytecode VM for this reason (RFC 0026 Alternative 4). Two mature engines exist and one is proven.
- **Keeping the evaluator / dual-engine forever — rejected.** Violates the RFC end state and Principle 1's no-coexistence rule (RFC 0026 P8 at `:287`, Alternative 5; gate text at `:367`, `:399`). It is retained ONLY as the differential reference through the bounded ladder and deleted atomically once the native conformance census is green.
- **Embedded V8 — rejected on weight.** Tens of MB, a heavy build/toolchain, a JIT-tier non-determinism surface, no offline source build here; massive overkill for a fixed 64 KiB deterministic sandbox.

---

## 4. Vendoring and build plan

### Layout (mirror the antlr4/doctest precedent exactly)

```
third_party/wasm3/
  LICENSE          # MIT text, verbatim from v0.9.0
  VERSION          # one line: v0.9.0 + upstream commit sha + fetch date
  CMakeLists.txt   # static lib, SYSTEM includes, third-party warning profile
  source/          # the 11 interpreter .c + headers, EXCLUDING the WASI/libc/tracer extras
```

The core set is exactly the interpreter files enumerated in upstream `source/CMakeLists.txt:1-17` minus the API glue:

```
m3_bind.c m3_code.c m3_compile.c m3_core.c m3_env.c m3_exec.c
m3_function.c m3_info.c m3_module.c m3_parse.c m3_validate.c
```

Do **not** vendor `m3_api_libc.c`, `m3_api_wasi.c`, `m3_api_uvwasi.c`, `m3_api_meta_wasi.c`, `m3_api_tracer.c`, `platforms/`, `platforms/app` (that is the `wasm3` CLI), or `extensions/`. This is a deliberate source-subset drop — the antlr4 precedent already excludes a file (`Any.cpp`, `third_party/antlr4/CMakeLists.txt:3`) — and the subset must be recorded in the vendoring commit + `VERSION` so it is a decision, not an accident. I built precisely this subset standalone.

### Root wiring

- `third_party/wasm3/CMakeLists.txt`: `add_library(wasm3 STATIC <11 c files>)`; `target_include_directories(wasm3 SYSTEM PUBLIC $<BUILD_INTERFACE:.../source> $<INSTALL_INTERFACE:include/ahfl/third_party/wasm3>)`; `ahfl_apply_third_party_warnings(wasm3)` (the same helper antlr4 uses); C99. It is a C target compiled by the C++ project's toolchain, so ensure `enable_language(C)`.
- Root `CMakeLists.txt`: `add_subdirectory(third_party/wasm3)` beside `add_subdirectory(third_party/antlr4)` / `(third_party/doctest)` at `:149-150`. Guard it behind the existing build-time option `AHFL_ENABLE_BACKEND_WASM` (`CMakeLists.txt:145`) — the embedded host requires the emitter, so WASM=OFF means no wasm3.
- Install/export: add `wasm3` to the internal-target install list exactly where `antlr4_runtime` is listed (`cmake/modules/AhflInstall.cmake:56`), so an installed `ahflc` links statically with no runtime `.so` dependency.

### Option shape

No NEW user-facing runtime-engine flag is introduced (Principle 1 forbids a selectable engine / compatibility switch). The only option is the existing `AHFL_ENABLE_BACKEND_WASM`; with it ON, wasm3 is vendored and linked into the runtime and there is exactly one execution path post-retirement. Do **not** add `AHFL_USE_WASM3` toggles or an engine enum — there is no alternative engine in the tree.

### NOTICE / license

Add or extend a root-level `NOTICE` (or a `third_party/wasm3/NOTICE.md` section) stating: "wasm3 — Copyright (c) 2019 Steven Massey, Volodymyr Shymanskyy — MIT License — see `third_party/wasm3/LICENSE`", and note the embedded subset (interpreter only, no WASI). This satisfies MIT's attribution condition; the project stays Apache-2.0.

### Offline verification (must be demonstrated in the vendoring commit, all runnable in THIS environment)

1. Fresh configure + build with no network: no `FetchContent`, no `ExternalProject`, no download step. The simde-style failure I hit with WAMR must be impossible by construction.
2. `AHFL_ENABLE_BACKEND_WASM=ON` and `=OFF` both configure + build clean.
3. `-Wall -Wextra -Werror` for all AHFL C++; vendored wasm3 C isolated under the third-party warning profile (antlr4 precedent), not the project's `-Werror`.
4. ASan preset builds and runs clean with the embedded host.
5. Determinism: same module bytes + same input + same host callbacks → byte-identical host observation across two runs.

---

## 5. C++ production host architecture

### Placement — a NEW peer-tier directory, tier-honest

The embedded execution engine is a peer of the compiler backend and of the existing resume/wire runtime, not a "util". Create **`src/runtime/wasm_host/`** (C++ host + the engine adapter), with public surface in **`include/ahfl/runtime/wasm_host/`** only where tooling needs it.

Rationale: `src/runtime/engine/` today holds the workflow/agent/capability/resume machinery and the module-inspection/wire authorities. The embedded VM adapter is a distinct, weighty subsystem (engine lifecycle + frame pack/encode + import dispatch) and burying it among the `core_wasm_*` inspectors would let the directory lie about the architecture — which the project's directory-honesty rule forbids. It is the direct implementation of the already-defined engine port (`src/runtime/engine/core_wasm_resume_engine.hpp`), so it lives under `src/runtime/` as a sibling, `wasm_host/`, links the vendored `wasm3` target, and is consumed by `ahfl_runtime_engine`.

*Alternative considered and rejected:* `src/runtime/engine/wasm/` — rejected because `engine/` is already the consumer and mixing the VM driver in makes the peer weight invisible.

New CMake target e.g. `ahfl_runtime_wasm_host` (STATIC), PRIVATE-linked to `wasm3`, PUBLIC-linked to `ahfl_compiler_ir` + the survivor host-value target (see §6) + `ahfl_runtime_engine` (for capability transport / resume authorities).

### Per-responsibility mapping: `tests/conformance/node_embedded_host.mjs` → C++

| # | Responsibility | mjs evidence | C++ home | Status |
|---|---|---|---|---|
| 1 | Engine instantiation, fresh instance, one framing | `makeInstance` `:448` | `wasm3_engine.{hpp,cpp}` implementing/extending `CoreWasmResumeEngine` (`core_wasm_resume_engine.hpp:121`) | **MISSING — the genuinely new runtime dep** |
| 2 | Export surface: `alloc`/`dealloc`/`run`/`run2`/`runv`/`step`/`current_state` + globals via `FindGlobal`/`GetGlobal` + memory via `GetMemory`/`GetMemorySize` | `:1706-1710` (alloc), `:1138,1271,1398` (run2), `:1006,1485,1577` (runv), `:1046-1119` (step/current_state) | same adapter | **MISSING** |
| 3 | `ahfl_cap.cap_<id>` binding, opaque `(i32,i32)->(i32,i32,i32)` and bridge `(i32)->(i32,i32)` | `makeBridgeCallback` `:303-426` | `capability_import.{hpp,cpp}` via `m3_LinkRawFunction`/`Ex` | **MISSING in C++** |
| 4 | Raw-ABI slot discipline: one arg at `stack[numResults]`, results at `stack[0..n-1]`; bounds-check every guest pointer | (verified by probe: `0 0 0 42 0 0` for a `(i32)->(i,i,i)` call) | same adapter; mirror `m3ApiCheckMem` discipline | **MISSING — highest safety sensitivity** |
| 5 | Module admission + Memory-section / fixed-page cross-check + SHA-256 binding | — | **REUSE** `make_verified_core_wasm_schema_module` (`src/runtime/engine/core_wasm_schema_module.hpp`) | EXISTS |
| 6 | Wire-schema transport admission | — | **REUSE** `core_wasm_schema_transport.{hpp,cpp}` (note: it checks only the opaque tuple — ADD the bridge `(i32)->(i32,i32)` functype cross-check) | EXISTS + needs one addition |
| 7 | Core-layout admission / fail-closed unknown functype at instantiation | — | **REUSE** `admit_core_wasm_frame_sections` (`src/runtime/engine/core_wasm_frame_module.hpp`) | EXISTS |
| 8 | Capacity accounting | — | **REUSE** `core_wasm_resume_capacity.{hpp,cpp}` | EXISTS |
| 9 | Input packer: JSON/Value → P4-D dense bytes, schema+layout guided | `packP6Input` `:985-1000`, `packValue` `:637-786`, `packSlots` `:788-799`, backing `:618-625`, String arena `:689-696` | **NEW** `frame_packer.{hpp,cpp}` | **MISSING** |
| 10 | Output reader: P4-D bytes → `Value` → canonical `value_to_json`; runv root auth (identity 1024 / computed 12288); String region authorization; fail-closed family | `encodeP6Output` `:1003-1026`, `readValue` `:805-980`, base auth `:1008-1013` | **NEW** `frame_reader.{hpp,cpp}` | **MISSING** |
| 11 | Capability arg walk + envelope + result validate + result pack | `makeBridgeCallback` block resolve `:319-392`, envelope `:435-446`, opaque cb `:458-507` | **NEW** `capability_import.{hpp,cpp}`, ending at the existing `serialize_args_for_wire_json` SSOT (`src/runtime/engine/wire_value.cpp:14-34`) + `core_wire_codec::validate_value`/`decode_json` | **MISSING** |
| 12 | Production capability execution over the LIVE transport (no mocks) | — | **REUSE** `CapabilityInvoker` / `ContextualCapabilityInvoker` (`capability_bridge.hpp:91-96`) + `CapabilityTransportAdapter` (`capability_transport_adapter.hpp`) + the registries assembled in `src/tooling/cli/workflow_run.cpp` (HTTP `:1187-1190`, LLM tool-call loop, standard Clock/Uuid) | EXISTS |
| 13 | Node-event log decode | `readEventRecords` `:1162-1228` | **REUSE** `decode_node_events` (`src/runtime/engine/core_wasm_node_events.hpp`) — 8-byte header + 40-byte records at 1032 | EXISTS |
| 14 | Node-event join vs scheduled-node facts (ordinal / source symbol / order) | same | **NEW** (small conformance-shaped join) | MISSING |
| 15 | State-entry trace-ring decode (u32 count + 8-byte `(runner,state)` records) | `:1314-1339` | **NEW** — no runtime decoder exists today | **MISSING** |
| 16 | Step-walk with `current_state` consistency and exactly-once `transition_count` (separate fresh instance for bridge agents so effects fire once) | `collectStatesViaStep` `:1035-1065` | **NEW** `agent_session.{hpp,cpp}` | **MISSING** |
| 17 | Workflow run2 schedule + node-completed counter + event log | `runWorkflowP6` `:1238`, `runWorkflow` `:1355` | **NEW** `workflow_session` | **MISSING** |
| 18 | Canonical observation document emission (status / state_sequence / capability_sequence / capability_arguments / output_json) | `emitObservation` `:531`, `emitStateSequence` `:524`, `normalizeStatus` `:551` | **NEW** facade producing the SAME doc shape the comparator consumes (`tests/conformance/observation_compare.hpp:10-17`) | **MISSING** |
| 19 | Hook-compatible runtime facade for tooling | — | **NEW** `WasmWorkflowRuntime` / `WasmAgentRunner` exposing `state_entered_hook`, `capability_invoked_hook`, `node_completed_hook`, `agent_input_hook` | **MISSING** |
| 20 | ABI-matrix probes (pending latch, corrupt count, legacy-run trap, checked allocator, idempotent identity replay) | `runAbiProbes` `:1463-1711` | become **host unit tests**, not production code | reclassified |

### Survivor host wire types (do NOT delete)

`value.{hpp,cpp}`, `value_json.{hpp,cpp}`, `scalar_spelling.{hpp,cpp}` are interpreter-free and landed at `src/runtime/value/` (target `ahfl_runtime_value`, WH-S `51ba45f8`). The include graph is exactly what the host needs: `value.hpp` includes only standard headers and keeps the interpreter closure descriptor (`InterpreterClosure`) incomplete — forward-declared, defined in `src/runtime/evaluator/evaluator.hpp`; `value_json.hpp` includes `base/json/json_value.hpp` + `runtime/value/value.hpp`; `scalar_spelling.hpp` includes only standard headers. They are the canonical wire SSOT the new host explicitly terminates at.

**Amendment (2026-09-29, WH-S adversarial review fix-forward): the closure arm is RETAINED, not dropped.** The original plan below ("Drop the interpreter-only `CallableValue` variant arm") was rejected by a dedicated decision agent after the review proved closures genuinely live in `Value`-typed scope maps (higher-order apply, `option.map`, `collections.fold`/`filter`). Deleting the arm would require a parallel scope type in the evaluator. The trust-boundary goal is instead enforced by construction:

- the arm is `InterpreterClosureHandle { std::uint64_t id; shared_ptr<const InterpreterClosure> descriptor }`. The monotonic `id` (atomic counter starting at 1, assigned in `make_interpreter_closure`) IS the identity (Principle 2): equality and the total order used by Set/Map canonicalization compare only the id, never the heap address. The shared_ptr is pure lifetime management;
- TWO JSON projections exist. `try_value_to_json` / the optional-returning `hash_values` are the strict wire encoders: a closure anywhere in the value tree (top level or nested in list/struct/enum payload) makes them fail closed (nullopt), and every trust-boundary site — capability HTTP/gRPC transport, native host binding, memo arg-hash, durable snapshot, resume digest — converts that into a typed failure instead of emitting bytes. `value_to_json` is the observation-only projector (DAP, traces, tool output) and renders a closure as the fixed opaque object `{"_callable":"runtime"}` so observation output stays syntactically valid JSON;
- `ahfl_base_support` is a PRIVATE link edge of `ahfl_runtime_value` (build-tree-src-only include), `ahfl_base_json` stays PUBLIC (JsonValue is in the installed header interface).

### Production capabilities reuse the LIVE transport — no mocks in production

The import handler calls the same Value-based capability surface used today: `CapabilityInvoker` / `ContextualCapabilityInvoker` (`capability_bridge.hpp:91-96`), the registries assembled in `workflow_run.cpp` (HTTP capability, LLM tool-call loop, `with_standard_capabilities` Clock/Uuid), over the real `CapabilityTransportAdapter` (HTTP/gRPC). Conformance mocks live ONLY in the test adapter that builds a mock invoker; the production host never links a mock table. Durable memo / injected / ReadyForLive decisions stay owned by the EXISTING D1b controller (`core_wasm_resume_controller.{hpp,cpp}`) + resume host (`core_wasm_resume_host.{hpp,cpp}`), which already drives an engine port — wasm3 simply becomes the real implementer instead of the test Fake or the Node subprocess, closing the "FOUNDATION, no real Wasm VM" caveat stated honestly at `src/runtime/engine/core_wasm_resume_host.hpp:42-44`. Fresh-instance replay maps 1:1: a new wasm3 runtime/module per resume, with host replies (memo/injected frames) delivered synchronously nested inside the import callback.

---

## 6. Cutover ladder (bounded slices → ONE atomic evaluator-deletion slice)

### Survivors extracted first (independent slice, no behaviour change)

Create a host-value target, e.g. `src/runtime/value/` (tier-honest: these are the host wire/value layer used by engine, providers, tooling), containing `value`, `value_json`, `scalar_spelling`. A single big-bang rename to `ahfl::runtime::Value` flipping all includes in the same change is preferred over an alias (Principle 1 disfavours shims). **(LANDED as WH-S `51ba45f8`.)** Original draft said: "Drop the interpreter-only `CallableValue` variant arm — closures never cross the frame boundary." The first half of that sentence was WRONG and was overturned by the 2026-09-29 review amendment in §5 above: the arm is retained as the id-identified opaque `InterpreterClosureHandle` (closures must be representable in the interpreter's `Value`-typed scope maps); only the second half stands — closures never cross the frame boundary, enforced by the fail-closed strict encoder, not by deleting the arm. Consumers flipped: `capability_bridge.hpp:24`, `agent_runtime`, `core_wire_codec.*`, `wire_value.*`, `workflow_runtime`, providers/llm, tooling, DAP. `scalar_spelling` detaches from `builtins.cpp` (its only in-evaluator consumer) and stays with `core_wire_codec.cpp`.

### Ladder

Each slice is independently reviewable, keeps ctest green, and introduces no user-facing flag.

- **WH-0 — Vendor wasm3 v0.9.0** (§4): static target, WASM=ON/OFF matrix, NOTICE/LICENSE, offline + ASan + `-Werror` clean; smoke test via wasm3's own self-test. No production caller.
- **WH-1 — Engine adapter + admission wiring**: `wasm3_engine` implementing the extended engine port; run an identity module (no imports) and an import-free workflow end-to-end through C++; reuse schema/layout/capacity admission; unit-test raw ABI slot offsets, trap mapping, fresh-instance, whole-64KiB read. Delete nothing.
- **WH-2 — Frame packer + reader** over verified bindings/layout; p6 scalar/aggregate/collection + rodata String modules execute in-process and encode canonical output; region-authorization + fail-closed family unit tests.
- **WH-3 — Capability imports (opaque + bridge)** against the LIVE `CapabilityInvoker`/transport for production and a mock invoker only for tests; multi-arg envelope; pending/error → trap; wire-result validate + pack; wire the D1b resume controller to the real engine (memo/injected replay), retiring the FOUNDATION caveat in `core_wasm_resume_host.hpp:42-44`.
  - 2026-09-30 clarification (BUILDER agent, no human gate): "pending/error → trap" is the **BRIDGE lane's guest contract only**. The bridge `(i32)->(i32,i32)` module traps (unreachable) on any non-zero raw status; the opaque `(i32,i32)->(i32,i32,i32)` lane has graceful ERROR/PENDING arms that return `(1,0,0)`/`(2,0,0)` and never traps on a non-OK reply. The engine carries `ImportReply.raw_status` verbatim into the import's first result slot and **never classifies either lane** — the guest's compiled code is the classifier (2026-09-30 DECISION: `ImportReply` gains `raw_status`, `ImportAbort` stays host-DECISION-failure-only).
- **WH-4 — Sessions, events, traces + observation facade**: step-walk (separate effects instance), node-event join, state-trace ring decoder, workflow scheduling/counters, hook-compatible runtime facade; ABI-matrix unit tests.
- **WH-5 — NATIVE CONFORMANCE LANE**: add a native engine adapter to the engine-agnostic conformance harness (the mirror of `tests/conformance/evaluator_engine.cpp` and `node_embedded_host.mjs`) producing the SAME canonical observation doc; add ctest `ahfl.conformance.wasm_native_differential` that runs the full 66-scenario census (current pin `kExpectedAgreed = 66`, `kExpectedSkipped = 0`, `tests/integration/conformance_wasm_node_runner.cpp:147-148`) on the embedded wasm3 host. **Gate the retirement on 66 agreed / 0 skipped natively, including the 7 node-only stems.** Node is retained in the same test as an independent cross-engine oracle for parity.
  - **What replaces the evaluator side of `observation_compare`:** native-vs-blessing and native-vs-Node, both engine-agnostic documents. The comparator (`tests/conformance/observation_compare.hpp`) and the 59 checked-in observation files under `tests/conformance/observations/` are engine-independent and survive unchanged — they already compare documents, not implementations. The five compared dimensions stay: `status`, `state_sequence`, `capability_sequence`, `capability_arguments`, `output_json` (`observation_compare.hpp:10-17`). Blessing verify / determinism / mutation modes run against the native lane too.
- **WH-6 — `ahflc run` cutover**: `src/tooling/cli/workflow_run.cpp` constructs the WASM-backed runtime instead of `WorkflowRuntime` at its single execution call site (`:1849` `WorkflowRuntime runtime(program, ...)`, `:1850` `runtime.run(workflow_name, ...)`); capability registries/transport, input decode, output rendering are reused unchanged. The tree-walk internals reached from the agent/runtime layer are deleted ONLY in the final slice.
- **WH-7 — REPL cutover**: replace the only tree-walk call sites in `src/tooling/repl/repl.cpp:297-298` (`ahfl::evaluator::EvalContext ctx;` / `ahfl::evaluator::eval_expr(...)`) with compilation to wasm + embedded evaluation of the synthetic `const __repl_result__`; `print_value` survives. Drop the direct evaluator link (`src/tooling/repl/CMakeLists.txt`, `PUBLIC ahfl_runtime_evaluator`).
- **WH-8 — DAP cutover**: largely mechanical — DAP never calls the evaluator. It drives `WorkflowRuntime` purely through hooks (`src/tooling/dap/debug_session.cpp` installs `state_entered_hook` at `:218`, `capability_invoked_hook` at `:224`, `node_completed_hook` at `:234`, and calls `runtime.run(...)` at `:371`). Point it at the WASM-backed facade exposing the identical hooks. State-level stepping semantics (`step()` / `current_state` + breakpoints) map onto the WH-4 step-walk; verify setBreakpoints / next / stepIn / stepOut / evaluate / stackTrace / variables behaviour unchanged.
- **WH-9 — THE SINGLE ATOMIC RETIREMENT SLICE (P8)** — one commit, `BREAKING CHANGE:`:
  - delete `src/runtime/evaluator/evaluator.{hpp,cpp}`, `executor.*`, `eval_context.*`, `pattern_match.*`, `builtins.*`, `runtime_fn_table.*` and the `ahfl_runtime_evaluator` target (`src/runtime/evaluator/CMakeLists.txt`); remove the directory (the three survivor modules already moved in the survivor-extraction slice);
  - delete the evaluator conformance engine adapter (`tests/conformance/evaluator_engine.{hpp,cpp}` engine-run parts; extract the shared mock + observation-render helpers used by the native runner into a conformance-common TU in the SAME slice), the evaluator runner (`tests/integration/conformance_evaluator_runner.cpp`) and its ctest identity;
  - retire evaluator-only unit tests: `tests/unit/runtime/evaluator/{evaluator,executor,evaluator_generics}.cpp` are deleted; `set_map_uuid_timestamp.cpp` and `value_json.cpp` are split — the Value/value_json assertions move to the survivor target's tests, the interpreter assertions go;
  - rework the shared test-support assertion helpers and the suites that drive `RuntimeFunctionTable`/`exec_block`/`eval_expr` — these become compile/lower checks or move to native conformance;
  - remove the evaluator link edges: `src/runtime/engine/CMakeLists.txt` (the `PUBLIC ahfl_runtime_evaluator` entry), `src/CMakeLists.txt:27` (`add_subdirectory(runtime/evaluator)`) and `:70` (the bundle entry); update the install export set;
  - with native AND node both green at 66/0 and no remaining production caller (grep-zero non-test callers, per Principle 1), demote the Node subprocess lane: keep exactly one node-parity ctest as an OPTIONAL, clearly-labelled external-oracle test (skip-77 without Node); delete all node machinery that duplicated production driving. The production binary has zero Node dependency;
  - **retire the node-only stem concept**: the `kExpectedNodeOnlyStems` exact-set pin (`tests/integration/conformance_wasm_node_runner.cpp:158-166`, currently 7 entries: `fb1_aggregate_direct_call`, `fb1_direct_call`, `fb3_byvalue_capture`, `fb3_higher_order`, `fb3_nested_activation`, `fb3_nested_lambda_flow`, `fb4_effect_clause_pure_body`) and the `evaluator_surface_awaits_kr68` skip reason are removed, because the evaluator surface they awaited no longer exists — those 7 stems must run natively in WH-5 first;
  - full offline ctest + ASan + `-Werror` green; RFC + roadmap flip KR6.8 to done.

**The atomic guarantee (RFC P8):** users never experience a broken `ahflc run`. WH-6..WH-8 each keep the old path compiling until WH-9 flips all call sites and deletes the implementation in the SAME commit; there is no long-lived dual-engine state, because the native lane is built behind the conformance harness, not behind a user flag.

---

## 7. Sequencing vs FB-5 corpus expansion

**FB-5 is COMPLETE — it precedes and gates this work, as the RFC always required.** Verified at HEAD `acc8d197` (2026-09-29): the FB-5 sub-slices A–E landed, widening the corpus to **66 agreed / 0 skipped** — the pin is `constexpr int kExpectedAgreed = 66;` / `constexpr int kExpectedSkipped = 0;` at `tests/integration/conformance_wasm_node_runner.cpp:147-148`. There are 58 case manifests under `tests/conformance/cases/` and 59 checked-in observation files under `tests/conformance/observations/` (the extra file covers a node-only stem that has no evaluator blessing). The node-only stem set is now 7, pinned as an exact set at `:158-166` — these are evaluator-surface gaps blessed against Node directly; they are exactly the functions/closures the WASM path covers but the tree-walk conformance adapter cannot express.

Sequencing:

1. FB-5 done (✓).
2. Survivor extraction and wasm3 vendoring (WH-0) can start immediately; they are independent.
3. WH-1..WH-5 build the native host up to a 66/0 native census — **the 7 node-only stems must ALSO pass natively at WH-5**, since they are the strongest argument that native supersedes both the evaluator and the node-only escape hatch.
4. Only then WH-6..WH-9.

So corpus expansion does not merely precede deletion; the native census must reach ≥ the full FB-5-expanded set (66 scenarios including the 7 node-only stems) before the atomic slice. No NEW corpus expansion is a prerequisite beyond FB-5. The f64 / map / decimal-source-literal lanes remain explicit non-goals, and their wasm-eligibility skip lane stays a live, machine-verified non-emission classification rather than a hand-maintained list.

---

## 8. Acceptance / verification criteria

Must all hold on the ATOMIC slice commit:

1. **Native embedded-host conformance census: 66 scenarios, 66 agreed, 0 skipped**, including the 7 formerly node-only fn/closure stems, on vendored wasm3 — a new hard pin parallel to `kExpectedAgreed = 66`. Observation dimensions unchanged: `status`, `state_sequence`, `capability_sequence`, `capability_arguments` (canonical envelope bytes), `output_json` (canonical re-serialize).
2. **Cross-engine parity:** native-vs-Node agrees on the full census where Node is present (optional skip-77 elsewhere) — the independent-oracle guard against an interpreter-specific miscompile; and native-vs-blessing verify / determinism (two runs byte-identical) / mutation (tampered status, state_sequence, or output rejected) modes all bite.
3. **Feature coverage on real modules** (already proven today on both engines, must stay green): Table/Element/`call_indirect` closures, active Data rodata String modules, mutable globals, `i64.extend_i32_s`, bounded recursion, the bridge `(i32)->(i32,i32)`, the opaque 3-result import, `run`/`run2`/`runv`/`step`/`current_state` plus the exported globals, and p6 collections.
4. **Full offline ctest** from a network-disabled configure + build (no FetchContent / download; the failure mode demonstrated with WAMR/simde must be structurally impossible); both `AHFL_ENABLE_BACKEND_WASM` ON/OFF matrices configure + build; dev and release presets.
5. **ASan preset** clean for the embedded host, especially the raw-callback pointer/bounds wrappers and the packer/reader; no leaks on repeated fresh-instance resume.
6. **`-Wall -Wextra -Werror`** for all AHFL code; vendored wasm3 C isolated under the third-party warning profile (antlr4 precedent).
7. **License/offline gates:** MIT `LICENSE` + NOTICE attribution present and complete; the install/export set includes `wasm3`; a deployed `ahflc` is a standalone binary with NO Node/PATH requirement — `ahflc run`, `ahfl-repl`, `ahfl-dap` work on a machine with no Node.
8. **Single-binary determinism:** same module + input + capabilities → identical observation; no wall-clock / pid / host-path / allocator-order leakage; capabilities execute over the live transport in production — verify a real capability call round-trips through the embedded import in an integration test (no production mock).
9. **Deletion completeness (Principle 1):** after WH-9, a grep for the evaluator target/symbols and tree-walk entry points in non-test production code is empty; no engine-select flag or env var exists; evaluator-specific tests/adapters/CMake edges are removed in the same commit; a `BREAKING CHANGE:` footer is present.

### Honest claims boundary — what stays UNPROVEN after this decision

- **JIT/AOT performance is not claimed.** wasm3 runs its portable threaded interpreter; no benchmark parity with Wasmtime or V8 is asserted. Workloads are tiny fixed-64-KiB deterministic agent runs. If measured performance later demands a change, fast-JIT or an engine revisit is a FUTURE decision (and would re-open the WAMR three-result question upstream).
- **Real Wasmtime parity is not proven.** We prove native wasm3 execution and retain optional Node/V8 parity; wasmtime remains absent offline. wasm3 + V8 agreement on 66 scenarios is strong but is not a third-engine proof.
- **wasm3 upstream durability** is a mitigated risk, not an eliminated one: a healthy-but-smaller project. Mitigations: full vendoring (we own the source regardless), MIT licence, and the conformance census pinning behaviour. A future wasm spec-feature need (a true f64 numeric ladder, GC, threads, memory64) would trigger a fresh engine decision; none are required by the current emitter.
- **f64 arithmetic, runtime string concatenation, map / decimal-source-literal / timestamp / uuid frame walks, and closure values crossing frame boundaries** remain non-emitting lanes with fail-closed eligibility classification — out of scope, unchanged.
- **DAP instruction-level debugging is not introduced.** State-transition-level stepping (the existing debugger contract) is preserved via the step-walk.
- **Durable replay of bridge results and KMS-protected stores** remain the separate D2b authority; this decision supplies its missing real engine port but does not itself close confidential-store / key-authority work.

---

## 9. Proposed dated RFC 0026 Decision History entry

*(To be appended to `docs/rfcs/0026-ir-tower-and-execution-model.zh.md`; Chinese prose matching the surrounding entries. Nothing was committed by the decision agent.)*

```
- 2026-09-29: **KR6.8 生产执行引擎决策门落定(docs only,设计,未实现;专用决策代理,无人类 owner 门)**。为替换 tree-walking evaluator 的编译后 WASM 生产宿主选定引擎并锁定原子退役阶梯。**决策:vendor wasm3 v0.9.0(MIT,2026-08-24)为 third_party/wasm3/ 下的 C 源码静态库,走 portable 解释器、不引 WASI/JIT、经 raw-function ABI 注册 ahfl_cap 宿主函数;新建 peer-tier 目录 src/runtime/wasm_host/(引擎适配 + 帧 pack/read + 能力导入 + 会话/观察),实现并扩展既有 CoreWasmResumeEngine 端口。** 决定性事实(决策代理在本机离线实测,非营销口径):① 每个带能力模块的 opaque 导入 functype 硬钉 (i32,i32)->(i32,i32,i32) **三结果**(`core_wasm_codegen.cpp:14093`/`:16339` 导入面;kTypeCapabilityTuple `:298`/`:4017`/`:14098`/`:14115`/`:16344`),这是密封的 E2/B1/B2/D2a 线级 ABI(`core_wasm_resume_engine.hpp:59-73`);手写 67 字节三结果导入模块实测 **wasm3 v0.5.0 与 v0.9.0 端到端回传全部三个结果**(raw 栈:参数在 stack[result 数]、结果在 stack[0..n-1],探针钉死 `0 0 0 42 0 0` -> `100 8 7`),而 **WAMR 2.4.5 与上游 main(b70d708d,2026-09-21)的 fast/classic 解释器均硬编码 argv_ret[2]、只回写 1/2 cell 臂**(`wasm_interp_fast.c:1204,1295-1301`;`wasm_interp_classic.c:1213,1307-1314`;main 同),第三结果 ptr/len 被静默截断(同一 fixture:回调收到正确参数 42,结果恒 0/0/0,raw 与 C-API 两条路径一致);C-API 内部虽支持 4 结果(`wasm_runtime_common.c:7227`)但解释器帧在回写前截断,非配置项。改 ABI 为 ≤2 结果会重开已密封的 resume/wire 契约、维护 WAMR fork 补丁违反 Principle 1,故 WAMR 拒绝;上游修复 >2-cell 宿主导入后可由新决策代理重审。② 其余发射特性两引擎实测等价并全部通过:Table/Element/call_indirect 闭包、主动 Data(11) rodata(v2b_*string*,runv->(0,12288))、mutable globals、i64.extend_i32_s、有界递归、p6 集合;模块仅导入 ahfl_cap.cap_<id>(全树无 wasi/env),无 memory.grow、无 f32/f64 数值指令(f64 `:4488` fail-closed)、单页 64KiB 无 max(`core_wasm_abi_constants.hpp:28-31`)。③ wasm3 离线 vendor 形状干净:核心 11 个 C 文件(上游 `source/CMakeLists.txt:1-17` 去掉 libc/wasi/uvwasi/meta_wasi/tracer),无 FetchContent(WAMR 默认 configure 实测会 git clone simde,须 -DWAMR_BUILD_SIMD=0 且裁剪 27MB 树),纯 C99 无平台汇编;MIT 与 Apache-2.0 兼容(NOTICE 归属)。④ "wasm3 停更"直觉已过期:v0.9.0 发布 2026-08-24(v0.5.0..v0.9.0 共 214 commits)、v0.9.1-beta.1 2026-09-10、clone HEAD 提交 2026-09-28(克隆核实)。**显式拒绝**:WAMR(三结果宿主导入截断,唯一硬伤);Wasmtime C API(离线构建不可能 + 预编译平台二进制违背 vendored-only 与单二进制模型);Node 子进程作为生产引擎(违背可嵌入单二进制北极星、依赖 PATH,降级为 skip-77 的可选跨引擎 oracle);自研解释器与 evaluator 双引擎共存(Principle 1 / RFC Alternative 4、5);内嵌 V8(重量/JIT)。**架构**:宿主 packer/reader/bridge 全部终止于存活的 host 线级类型 evaluator::Value/value_to_json/scalar_spelling(computed-output-frame v1 SSOT),复用 core_wasm_schema_module/frame_module/schema_transport/node_events/canonical_size 等既有 C++ 权威(其中 schema_transport 需补一条 bridge `(i32)->(i32,i32)` functype 对拍),能力执行复用 LIVE CapabilityInvoker + CapabilityTransportAdapter(workflow_run.cpp 的 HTTP/LLM/standard 注册),mock 仅存于测试适配器,D2b memo/injected/fresh-instance replay 由既有 controller 经真实引擎端口收口(`core_wasm_resume_host.hpp:42-44` 的 FOUNDATION 缺口关闭)。**存活/删除**:value/value_json/scalar_spelling 先移出 evaluator 目录为独立 host-value 目标并删 CallableValue 臂;evaluator/executor/eval_context/pattern_match/builtins/runtime_fn_table 为删除目标。**阶梯**:WH-0 vendor -> WH-1 引擎适配+admission -> WH-2 帧 pack/read -> WH-3 opaque/bridge 能力导入+resume 真实引擎 -> WH-4 会话/node-event/state-trace/hook facade -> WH-5 原生 conformance 车道(硬门:**原生 66 场景 66 agreed/0 skipped,含 FB-5 后 7 个 node-only fn/闭包 stem**,并退役该 skip 概念)-> WH-6 ahflc run -> WH-7 REPL -> WH-8 DAP -> **WH-9 单一原子切片**翻转全部调用点并同 commit 删除 evaluator 目标/专用测试/CMake 边、BREAKING CHANGE,用户不经历中间损坏态;Node 仅保留为 skip-77 可选外部 parity ctest,生产二进制零 Node 依赖。**FB-5 已完成并为前置**(HEAD acc8d197,kExpectedAgreed=66/kExpectedSkipped=0,conformance_wasm_node_runner.cpp:147-148)。**验收**:离线 ctest、ASan、-Werror、WASM ON/OFF 矩阵、MIT/NOTICE、install export 含 wasm3、单二进制无 PATH 依赖、确定性。**诚实边界**:不主张 JIT/AOT 性能与真实 Wasmtime 证据;f64 算术语/运行时字符串拼接/map 等仍非发射车道;DAP 保持状态迁移级步进。**本片仅决策门,无生产代码,RFC 0026 保持 implementing,KR6.8 原子退役门未到。**
```

---

## 10. WH-1 实现决策记录(2026-09-30,BUILDER 代理,无人类 owner 门)

WH-1 落地首个真实 WASM 执行引擎(wasm3 后端的 `CoreWasmResumeEngine` 端口实现)+ CMake 准入 + 单元测试。实现过程中遇到的设计岔路,按 senior Rust/Clang 工程师方式决策并记录如下。

### 10.1 A2 准入无法覆盖无导入模块 → 引擎自行施加 F3 等价的固定单页内存交叉校验

`make_verified_core_wasm_schema_module`(A2)硬性要求 capability Import 段 + exec-manifest + wire-schema;无导入模块(identity 工作流)在结构上就不满足 A2 准入。**决策**:引擎在 `fresh_instance` 内对所有模块(无论是否经 A2)自行施加与 F3 等价的固定单页内存交叉校验——对照 `core_wasm_resume_capacity::fixed_single_page_capacity()` 验证:恰好一个内存、无 declared max、`min_pages == 1`、声明字节数 == 容量(65536)。capability 模块在测试中走完整 A2 → 引擎路径;无导入模块由引擎直接校验。这不是绕过 A2,而是把"固定单页内存"这一 F3 容量权威下沉到引擎层,使端口对两类模块都成立。

### 10.2 A2 只准入 2 参数 opaque 形状;1 参数 §9 探针形状为引擎-only

`spans_are_capability_tuple` 要求恰好 2 个 i32 参数 + 3 个 i32 结果。设计文档 §9 的 `(i32)->(i32,i32,i32)` 单参数形状是 raw-ABI 槽位探针,不经过 A2。**决策**:引擎同时支持 1 参数与 2 参数 functype(3 个 i32 结果);2 参数时 `param_frame = mem[ptr..+len)`,1 参数时 `param_frame` 为空。1 参数形状仅在引擎单元测试中直接验证(不经过 A2),2 参数 opaque 形状走完整 A2 准入。2 结果 bridge `(i32)->(i32,i32)` 属 WH-3,不在本片。

### 10.3 wasm3 生命周期:必须先 m3_LoadModule 再 m3_LinkRawFunctionEx

实测 wasm3 v0.9.0 要求 `m3_LoadModule`(转移模块所有权到 runtime)**之后**才能 `m3_LinkRawFunctionEx`;在 load 之前 link 返回 `m3Err_moduleNotLinked`。WH-0 smoke 测试也遵循此序。**决策**:生命周期固定为 parse → new runtime(userdata=impl)→ **m3_LoadModule** → **m3_LinkRawFunctionEx**(逐导入)→ m3_GetMemory 校验 → m3_FindFunction(急切编译,编译错误与未解析导入在此暴露)。所有权以 wasm3.h 明文为准:**load 成功**后 runtime 拥有模块(挂入 `runtime->modules`,`m3_FreeRuntime` 经 `ForEachModule` 释放),teardown 只 free runtime;**load 失败**时模块视为 unloaded(未挂入 `runtime->modules`,且 `module->runtime` 置 NULL),必须 free runtime 与模块**两者**(runtime 释放 `ResizeMemory` 已分配的线性内存,模块释放解析结构)。teardown 由 `Wasm3EngineImpl` 析构函数单一拥有(RAII):`fresh_instance` 每条错误路径只返回、不手工 free,失败后重试以全新 Impl 起步,不泄漏首次尝试的残余;默认的 move 构造/赋值因此安全(移后源持 null impl_,析构为空操作;move 赋值的旧会话由析构函数收口)。

### 10.4 host-abort 信号:文件局部哨兵指针 + trap 指针恒等映射

wasm3 把 raw 回调的返回指针作为 trap 向上传播(`forwardTrap`,指针恒等)。`M3Result` 是 `const void*`(指向静态字符串),14 个 `m3Err_trap*` 常量按指针比较。**决策**:用文件局部 `const char[]` + `const void* const kHostAbortSentinel`(与所有 wasm3 错误指针都不同)作为 host-abort 信号;trampoline 内 ImportAbort 或 C++ 异常(跨 C 边界 fail-closed)返回该哨兵。`invoke_run2` 按指针恒等映射:哨兵 → `Run2HostAborted`;14 个 trap 指针之一 → `Run2Trapped`;其余 wasm3 错误 → `InstanceUnavailable`。

### 10.5 alloc 策略:调用模块导出的 alloc(Node-lane 忠实,ABI 正确)

**决策**:引擎不自行在 guest 内存里维护 bump allocator,而是调用模块导出的 `alloc(len)`(经 m3_Call + m3_GetResults)。模块的 checked allocator 在 OOM 时返回 0 → 引擎映射为 `MemoryCapacityExceeded`,且 bump 不前进(无部分突变)。这与 Node lane 的行为一致,ABI 正确。

### 10.6 目标不进 ahfl_runtime_bundle / install 集

WH-1 无生产调用方。**决策**:`ahfl_runtime_wasm_host` 静态库**不**加入 `ahfl_runtime_bundle`(否则 WASM=OFF 时 bundle 断裂),header 为 src-internal BUILD_INTERFACE only,不进 install/export 集。WH-3/WH-6 有生产调用方时再评估提升。

### 10.7 CMake 注册:遵循 gated 先例,不进全局 foreach

`tests/cmake/TestTargets.cmake` 的全局 foreach 是 ungated 的;把 wasm-gated 目标放进 foreach 会在 WASM=OFF 时断裂。**决策**:遵循 `ahfl_core_wasm_codegen_tests` 先例——目标定义放在 `if(AHFL_ENABLE_BACKEND_WASM)` 块内,不进全局 foreach 列表。三文件模式(TestTargets / ProjectTests / LabelTests)均在 WASM 块内注册,`wasm-host` label。

### 10.8 端口头零改动

实现未发现 `core_wasm_resume_engine.hpp` 端口缺陷。Fake 与 Node 端口无需同改。**端口头零改动。**

## 11. WH-2 实现决策记录(2026-09-30,BUILDER 代理,无人类 owner 门)

WH-2 落地帧 packer/reader + 薄宿主驱动 + schema_transport bridge functype 准入硬化。实现过程中的设计岔路,按 senior Rust/Clang 工程师方式决策并记录如下。

### 11.1 P6-frame 车道的引擎组合:具体引擎加法式扩展,端口头零改动

P6-frame 模块的入口是 `runv() -> (status:i32, value_ptr:i32)`,不是 `run2`。`CoreWasmResumeEngine` 端口只暴露 `run2`/`alloc_then_write`/`read_whole_memory`,且 `read_whole_memory` 返回 const span——packer 必须把输入帧写入**固定地址**(input 1024、backing 16384+、arena),这两个能力端口都不提供。**决策**:在**具体** `Wasm3ResumeEngine` 上加法式新增两个方法——`mutable_whole_memory()`(read_whole_memory 的可变对应物,供 packer 直接写入存活实例内存,与 JS oracle 写 `e.memory.buffer` 同构)和 `invoke_runv()`(惰性 m3_FindFunction + 校验 `() -> (i32,i32)` + 调用 + trap 映射 + 固定单页不变量复查)。端口头 `core_wasm_resume_engine.hpp` **零改动**;WH-1 既有方法语义**零变更**(纯加法)。拒绝另起并行 wasm3 驱动(Principle 1:并行实现)。`invoke_runv` 与 `invoke_run2` 共享 `run_started` 一次性闸门:一个会话只走一条车道(run2 或 runv),第二次调用为 InvalidSequence。

### 11.2 final_kind 来源:codegen C++ 描述符,非常量推断

runv 根授权需要 final_kind(identity → value_ptr 必须 == input_base 1024;computed → == output_base 12288)。准入的 `CoreFrameLayoutSection` **不携带** final_kind;input_layout == output_layout 也不能推断——`v2b_string_passthrough` 输入输出同名(Frame)但 final 是 computed(物化到 12288)。**决策**:final_kind 取自 codegen 的 C++ `CoreWasmExecutionDescriptor.frame->final_kind`(与模块同一次 `emit_core_wasm` 产出,是受信的我方编译器输出,非攻击者可控),**不**从常量推断,**不**从 JSON 描述符读取(那是 oracle/测试产物)。reader 只把 final_kind 用于在两个 ABI SSOT 常量基之间二选一;基本身来自 `core_wasm_abi_constants.hpp`。1024 与 node-event 区共享基的歧义(p6 agent 帧 vs capability-workflow 事件区)由 frame_contract(P6Frame vs WireJson)在驱动层消解:WH-2 只对 P6Frame 模块调用 `execute_p6_frame`,此上下文 1024 无歧义地是输入帧基。

### 11.3 schema_transport bridge functype 白名单:两种 ABI functype 皆合法,模式对拍在帧段准入

`CoreWireCapabilitySchema` 不携带 mode(opaque/bridge),schema_transport 看不到帧段的 `bridge_call_sites`。**决策**:schema_transport 把导入 functype 白名单扩展为两种 AHFL ABI functype——opaque `(i32,i32)->(i32,i32,i32)` 与 bridge `(i32)->(i32,i32)`;任一导入的 functype 不在白名单即拒绝(typed fixed diagnostic)。这是传输检查器在无 mode 信号下能施的最强检查:bridge 导入用错 functype(如 `(i32)->(i32)` 或 `(i32,i32)->(i32,i32)`)被拒,用对被接受。"哪个导入是 bridge"的模式对拍属于帧段准入(`verify_frame_bridge_sites`,它能看到 `bridge_call_sites`),不在 schema_transport 重复。A2(`core_wasm_schema_module.cpp`)的相同 opaque-only 检查不在本片改动(bridge 工作流模块走帧段准入,不走 A2;任务明确只点名 schema_transport)。

### 11.4 WH-2 对抗评审 fix-forward(2026-09-30,BUILDER 代理,无人类 owner 门)

独立对抗评审返回 LGTM + 8 项 P2;两项为 WH-3 门禁,全部在本次单一 bounded change 中修复。

**P2-1(WH-3 门禁):重复 placement layout-id 在准入时拒绝。** JS oracle `backingByLayout` 用 `Map.has` 拒绝重复 layout-id;C++ 侧此前只在 `verify_local()` 的 placement 循环中检查 edge_index/valid_id/container 非空,未检查同一 `container_layout` 是否被多次 placement。**决策**:在 `core_frame_layout.cpp` 的 placement 循环中新增 `placed_layouts` 位图(vector<bool>,按 `CoreLayoutId.value` 索引),每个 placement 的 `container_layout` 已置位即 fail-closed("frame-layout backing placement names a container layout that already has a placement")。这是 admission-time 不变量,不是 pack-time 检查——与 JS oracle 的 `Map.has` 语义对齐,且在帧段准入(transport)层就拒绝,不留给运行时。

**P2-2(WH-3 门禁)+ P2-8:runv 结果模型修正——host-abort 与 trap 分离,runv 执行错误不再归类为 FrameReadError。** 评审前 `RunvOutcome` 是二臂 `variant<RunvResult, Run2Trapped>`,host-abort 被映射为 trap;`P6FrameError` 把 runv trap/non-ok-status 塞进 `FrameReadError` 枚举。**决策**:(1) `RunvOutcome` 改为三臂 `variant<RunvResult, Run2Trapped, Run2HostAborted>`,`kHostAbortSentinel` 映射到 `Run2HostAborted` 而非 `Run2Trapped`;(2) 新增 `RunvError{Kind, raw_status}` 结构(Kind = Trapped | HostAborted | NonOkStatus),`P6FrameError` 改为四臂 `variant<FramePackError, FrameReadError, RunvError, EngineError>`;(3) `FrameReadError` 枚举移除 `RunvTrapped`/`RunvNonOkStatus`——帧读取错误与 runv 执行错误是不同抽象层,混在一个枚举里违反 Principle 4(variant 分层)。所有 visitor/test 同步更新。

**P2-3/4/5/6 + hygiene**:runv root-auth 测试强化(identity strict success + 8 边界探针 + computed+12288 happy-path);嵌套 closure pack 拒绝测试(Option/Struct field/List element/Enum payload 四种形状,各验证零部分帧字节);region-zeroing 断言精确到 2 字节 + 派生偏移;`pack_p6_input` 前置条件从 `>= 16384` 收紧为恰好 `fixed_single_page_capacity().value`(65536);移除未使用 include/using-decl(frame_walk.hpp 的 `core_wasm_abi_constants.hpp` + `<cstddef>`;frame_reader.cpp 的 `CoreWireSchemaField` using-decl;frame_packer_reader.cpp 的 `value_to_json` using-decl + `<variant>`;native_wasm_differential.cpp 的 `ir.hpp` include)。

---

### Load-bearing file references (all absolute)

- Emitted ABI / features: `/home/zhangdi.zode/Develop/AHFL/src/compiler/backends/wasm/core_wasm_codegen.cpp` (`:298`, `:4017`, `:4488`, `:14086-14115`, `:14146-14295`, `:16328-16427`); `/home/zhangdi.zode/Develop/AHFL/include/ahfl/compiler/ir/core_wasm_abi_constants.hpp` (`:28-31`, `:117`, `:136-141`, `:188-207`)
- Engine port + resume host: `/home/zhangdi.zode/Develop/AHFL/src/runtime/engine/core_wasm_resume_engine.hpp` (`:19-31`, `:59-73`, `:121`), `core_wasm_resume_host.hpp` (`:42-44`), `core_wasm_schema_module.hpp`, `core_wasm_frame_module.hpp`, `core_wasm_schema_transport.hpp`, `core_wasm_node_events.hpp`, `core_wasm_resume_controller.hpp`
- JS oracle + harness: `/home/zhangdi.zode/Develop/AHFL/tests/conformance/node_embedded_host.mjs` (1736 lines; `:303-507`, `:637-1026`, `:1035-1065`, `:1162-1339`, `:1355`, `:1463-1711`); `/home/zhangdi.zode/Develop/AHFL/tests/integration/conformance_wasm_node_runner.cpp` (`:147-148`, `:158-166`)
- Comparator / blessings: `/home/zhangdi.zode/Develop/AHFL/tests/conformance/observation_compare.hpp` (`:10-17`); `tests/conformance/observations/` (59 files); `tests/conformance/cases/` (58 manifests); `tests/conformance/evaluator_engine.{hpp,cpp}`
- Consumers / cutover: `/home/zhangdi.zode/Develop/AHFL/src/tooling/cli/workflow_run.cpp` (`:1849-1850`), `/home/zhangdi.zode/Develop/AHFL/src/tooling/repl/repl.cpp` (`:297-298`) + `src/tooling/repl/CMakeLists.txt`, `/home/zhangdi.zode/Develop/AHFL/src/tooling/dap/debug_session.cpp` (`:218`, `:224`, `:234`, `:371`)
- Deletion targets / survivors: `/home/zhangdi.zode/Develop/AHFL/src/runtime/evaluator/` (19 files); `/home/zhangdi.zode/Develop/AHFL/src/runtime/engine/CMakeLists.txt` (`PUBLIC ahfl_runtime_evaluator`); `/home/zhangdi.zode/Develop/AHFL/src/CMakeLists.txt` (`:27`, `:70`)
- Vendor precedent: `/home/zhangdi.zode/Develop/AHFL/third_party/antlr4/CMakeLists.txt`, `/home/zhangdi.zode/Develop/AHFL/third_party/doctest/CMakeLists.txt`, `/home/zhangdi.zode/Develop/AHFL/cmake/modules/AhflInstall.cmake:56`, root `CMakeLists.txt:145`, `:149-150`

### Reproduction kit for the two decisive findings (all outside the repo, in `/tmp`)

- `wasm3` clone with tags: `/tmp/wasm3_probe/m3git` (v0.9.0 = tag, HEAD `0cd38327`, v0.9.1-beta.1 `ac3c1dd1`, all-branches HEAD `0228c022` "Exception handling API" 2026-09-28)
- WAMR trees: `/tmp/wamr_probe/wasm-micro-runtime-WAMR-2.4.5` (version.h 2.4.5), `/tmp/wamr_probe/wamr-main` (`b70d708d`, 2026-09-21)
- Fixture: `/tmp/tri.wasm` (67 bytes: `ahfl_cap.cap_0` with functype `(i32)->(i32,i32,i32)`, exported `call`)
- wasm3 probes: `/tmp/wasm3_probe/tri9` (v0.9.0, source `tri3.c`) → `wasm3 results: 100 8 7`
- WAMR probes: `/tmp/wamr_probe/tri` (raw native), `/tmp/wamr_probe/tri_capi` (C API) → callback receives 42, results `0 0 0`

---

## 12. WH-4 facade decisions (2026-09-30, dedicated decision agent, no human gate)

WH-4 builds the session/event/trace/observation layer + the hook-compatible runtime facade consumed later by WH-6 (`ahflc run`) / WH-7 (REPL) / WH-8 (DAP). Two coupled decisions, both committed below. Verified facts this section rests on:

- `WorkflowResult` (`src/runtime/engine/workflow_runtime.hpp:45-64`) is a bag of evaluator-FREE fields (`ExecutionMetadataStore` / `ExecutionEventStore` / `ExecutionReport` / `vector<Value>` / `DiagnosticBag` / `optional<WorkflowRecoverySnapshot>`); the evaluator coupling lives in `WorkflowRuntime::run`'s CONSTRUCTION (`eval_workflow_expression` returning `evaluator::EvalResult`, `workflow_runtime.hpp:22-23,173`), not in the struct. `WorkflowStatus::EvalError` is mapped from `WorkflowFailureKind::EvaluationFailed` (`workflow_runtime.cpp:569-570`).
- The renderer + projections consume only the neutral fields (`execution_renderer.cpp` accesses `metadata/events/report/values/diagnostics/output()/has_errors()`; `execution_renderer.hpp:9` forward-declares `WorkflowResult`; `execution_projection.cpp:7` includes `workflow_runtime.hpp` only for the complete type, uses no evaluator symbol).
- `workflow_runtime.hpp` includes `runtime/evaluator/evaluator.hpp` + `runtime/evaluator/eval_context.hpp` (`:22-23`) — a PUBLIC header that drags the evaluator into every TU including it.
- The engine target PUBLIC-links `ahfl_runtime_evaluator` (`src/runtime/engine/CMakeLists.txt:47`) because of that public-header include.
- The DAP installs `state_entered_hook` / `capability_invoked_hook` / `agent_input_hook` / `node_completed_hook` / `capability_result_observer` on `WorkflowRuntimeConfig` (`debug_session.cpp:217-291`), calls `runtime_->run(...)` on a worker thread (`:371`), and BLOCKS inside `on_state_entered`/`on_capability_invoked` on `resume_cv_` (`pause()`, `:660-680`). `capability_invoked_hook` fires BEFORE dispatch (`workflow_runtime.cpp:1096-1098` vs `:1100-1105`) — capability breakpoints depend on the pre-call pause.
- The wasm3 `ImportObservation.whole_memory` is the whole fixed page at the moment of EVERY import (`core_wasm_resume_engine.hpp`), so the host can decode the state-trace ring prefix while `invoke_run2` is still running.
- The state-trace ring: dynamic base from `CoreFrameLayoutSection.state_trace_base/state_trace_capacity` (`core_frame_layout.hpp:168-169`), u32 count at base + 8-byte `(runner,state)` records, guest TRAPS on overflow (`core_wasm_codegen.cpp:14597-14656`). No runtime decoder exists today.
- Workflow modules' `step`/`current_state` TRAP by contract (`make_trapping_i32_body`, `core_wasm_codegen.cpp:~16440`); the JS oracle asserts `expectTraps`. Workflow `run2` is ONE guest invocation with the schedule baked in-guest (`append_workflow_schedule`, `core_wasm_codegen.cpp:15826`: per-node frame materialization via `WorkflowFrameMaterializer`, runner invocation, output-block checks, node-event records, completed counter).
- The JS oracle `collectStatesViaStep` (`node_embedded_host.mjs:1035-1065`) drives a SEPARATE effects-free instance ("states" mode: bridge replays served, events discarded, only state names are evidence); the canonical instance runs effects exactly once.
- `decode_node_events` exists (`core_wasm_node_events.hpp`); the observation emitter (`ahfl.node-observation.v1`) does NOT exist yet (only the oracle emits it, `node_embedded_host.mjs:531-549`).

### 12.1 Decision 1 — hook timing / source of truth: **Option A (dual-mode facade)**

**Chosen.** The facade guarantees two hook-timing classes, split by module kind.

**AGENT modules** (export `step`/`current_state`/`transition_count`): the facade drives a SEPARATE effects-free wasm3 instance for the state step-walk, mirroring `collectStatesViaStep` exactly. The effects-free instance's imports are served by a states-mode invoker that replays the scenario results and discards its events; only state names are evidence. The CANONICAL instance runs `runv`/`run2` exactly once for effects + output.

- `state_entered_hook` fires LIVE per `step()` transition on the effects-free instance — host-driven, exact; the DAP's pause/step semantics map 1:1 (the walk is host-driven, so pausing between steps is natural).
- `capability_invoked_hook` + `capability_result_observer` fire LIVE at the canonical instance's imports (pre-call / post-invoker, truly live — the `ImportCallback` blocks on the host's return before the guest resumes).
- `agent_input_hook` fires LIVE before the walk; `node_completed_hook` fires LIVE after the canonical run with the output read from the output frame.
- For an agent WITHOUT capability call sites, the facade drives the CANONICAL instance via `step()` directly (no effects to double-fire), matching the oracle's non-bridge path.

**WORKFLOW modules** (`step`/`current_state` trap by contract): `run2` is one guest invocation; the host gets control ONLY at capability imports and after `run2` returns. The facade wraps the WH-3 `capability_import` executor:

- at EACH import, BEFORE invoking the capability, decode the state-trace ring PREFIX from `whole_memory` and fire `state_entered_hook` per NEW record since the last boundary (import-boundary-live; the guest is genuinely stopped inside the `ImportCallback`, so a blocking hook pauses the run);
- fire `capability_invoked_hook` LIVE at the import, PRE-call — genuinely live for BOTH lanes (the DAP's capability breakpoints work exactly);
- fire `capability_result_observer` LIVE post-invoker;
- after `run2` returns: decode the FULL trace + node-event buffer, fire `state_entered_hook` for the remaining records (after the last import), and fire `node_completed_hook` per node in schedule order with the output read from the node's fixed `O_k` block.

`agent_input_hook` is NOT fired for workflow nodes: the in-guest materialized node input (`WorkflowFrameMaterializer`) is not host-observable without re-implementing the frame materializer — the rejected Option C. The host packs the workflow input into the entry `I_k` block, but the evaluated/projected node input is a guest-internal value. WH-8's DAP adapts (Node frame pushed at `node_completed_hook`). Documented, honest degradation.

**Hook timing GUARANTEE table:**

| Hook | Agent (no cap sites) | Agent (with cap sites) | Workflow |
|---|---|---|---|
| `agent_input_hook` | LIVE before step-walk | LIVE before step-walk | not fired (in-guest input not host-observable) |
| `state_entered_hook` | LIVE per `step()` on canonical instance | LIVE per `step()` on effects-free instance | IMPORT-BOUNDARY-LIVE (trace prefix at each cap import) + POST-run (remainder) |
| `capability_invoked_hook` | (none) | LIVE at canonical `runv`/`run2` import, PRE-call | LIVE at `run2` import, PRE-call |
| `capability_result_observer` | (none) | LIVE at import, post-invoker | LIVE at import, post-invoker |
| `node_completed_hook` | LIVE after canonical run | LIVE after canonical run | POST-run (schedule order, `O_k` output) |

**2026-10-03 dated 修订:** 上表 workflow `state_entered_hook` 的 "IMPORT-BOUNDARY-LIVE + POST-run" 单格保证已被 §12.9.14 修订(WH-5c.4 `251ebdb7` 证明单 hook 无法在 hybrid 模块同时服务 live 调试与 schedule 序观察):拆为 `state_entered_hook`(POST-run schedule 序全序列,observation 通道)+ 新增 `state_entered_live_hook`(IMPORT-BOUNDARY-LIVE,P6 only,debug 通道);原行保留作历史档案。

**Rejected:**

- **Option B (post-run replay only for ALL hooks):** rejected — it makes DAP over workflows post-mortem, contradicting WH-8's promise (decision doc §6: "DAP drives through the SAME hooks with state-level stepping mapped to WH-4 step-walk") and discarding the genuinely-live capability pre-call hook the `ImportCallback` makes possible. The capability hook CAN be truly live at the import for both lanes; throwing that away is dishonest.
- **Option C (host-side workflow scheduling):** rejected — the schedule is baked into `run2` (`append_workflow_schedule:15826`: per-node frame materialization, runner invocation, output-block checks, node-event records, completed counter). Host-side scheduling requires (a) codegen changes to export per-node runners or a dispatch export, (b) host re-implementation of the frame materializer (project/construct/normalize node input frames — hundreds of lines of codegen logic), (c) re-defining node-event/counter/trace semantics around host-driven scheduling, (d) diverging from the JS oracle which drives `run2` as one invocation. Massive blast radius across node events/counters/trace; it trades a documented import-boundary-liveness limitation for a full scheduler re-implementation. Not clearly superior.

**Reconciliation with JS oracle `collectStatesViaStep`:** the native facade mirrors the oracle exactly — a separate effects-free wasm3 instance for the state step-walk when the agent has capability call sites (the oracle's "states" mode), the canonical instance running effects exactly once. The bounded-walk guard (`states.size() + 2`), `current_state` consistency, and exactly-once `transition_count` bump are replicated. The `(runner,state) → (agent_name,state_name)` join for the workflow trace ring uses the `ir::Program` (runner → workflow node → target agent; state id → `FlowDecl` state-handlers dense order, the same ordering the codegen uses to assign dense state ids), pinned by a byte-parity test against the oracle's `all_states`.

### 12.2 Decision 2 — facade result type: **Option Z (neutral core extraction)**

**Chosen.** Extract the evaluator-free `WorkflowResult` + `WorkflowStatus` into a neutral header/TU that BOTH the evaluator runtime and the wasm facade produce. The renderer + projections already consume only the neutral fields (verified); the extraction is a mechanical MOVE, not a redesign.

- NEW `include/ahfl/runtime/workflow_result.hpp`: `WorkflowResult` + `WorkflowStatus` (moved from `workflow_runtime.hpp:34-64`). Includes only evaluator-free headers: `execution_event.hpp`, `execution_report.hpp`, `execution_metadata.hpp`, `runtime/value/value.hpp`, `ahfl/base/support/diagnostics.hpp`, `workflow_recovery.hpp` (the D1b durable-resume authority — evaluator-free, includes only `execution_event`/`atomic_file`/`value`).
- NEW `src/runtime/engine/workflow_result.cpp`: the 4 methods (`has_errors`/`status`/`value`/`output`, moved from `workflow_runtime.cpp:552-589`). Stays in the engine target — the survivor home (the engine holds the resume/wire authorities and survives WH-9).
- `workflow_runtime.hpp`: includes the neutral header; keeps `WorkflowRuntime`/`WorkflowRuntimeConfig` (the evaluator-specific `eval_workflow_expression` + evaluator includes stay).
- `execution_renderer.cpp` + `execution_projection.cpp`: include `ahfl/runtime/workflow_result.hpp` instead of `runtime/engine/workflow_runtime.hpp` (they use only the neutral type).
- The wasm facade produces neutral `WorkflowResult` WITHOUT including `evaluator.hpp` — the wasm host's HEADER dependency on the evaluator is structurally impossible.

**`WorkflowStatus::EvalError`:** stays in the enum (the wasm lane never produces it — a wasm trap/host-abort/non-OK maps to `NodeFailed`). It is deleted at WH-9 with its only producer (`WorkflowRuntime::run`'s `EvaluationFailed` path). A documented WH-9 deletion, not hidden coupling.

**Rejected:**

- **Option X (reuse `WorkflowResult` in place):** rejected — `workflow_runtime.hpp` includes `runtime/evaluator/evaluator.hpp` + `runtime/evaluator/eval_context.hpp` (`:22-23`) for the private `eval_workflow_expression` method. The wasm facade including `workflow_runtime.hpp` would drag the evaluator into `ahfl_runtime_wasm_host` — a NEW dependency edge in the WRONG direction (the replacement depending on the replaced). The "zero renderer churn" benefit is real but doesn't justify the inverted edge; the churn is a mechanical include-update, not a redesign. The "hidden evaluator coupling surviving until WH-9" risk is precisely this header edge.
- **Option Y (new `WasmWorkflowResult` + own renderer):** rejected — two result types + two renderers until WH-9, duplicating the event/report/projection machinery; violates "one facade not two parallel dialects" and Principle 1.

**Note on the engine's PUBLIC evaluator link edge:** `ahfl_runtime_engine` PUBLIC-links `ahfl_runtime_evaluator` (`src/runtime/engine/CMakeLists.txt:47`) because `workflow_runtime.hpp` includes `evaluator.hpp` in a PUBLIC header. This edge stays until WH-9 deletes `WorkflowRuntime`; the wasm host transitively links the evaluator but NO wasm-host TU includes it (the extraction breaks the header coupling). The link edge is a WH-9 deletion, not a WH-4 concern.

### 12.3 Exact C++ shapes

Files (all under `src/runtime/wasm_host/` unless noted):

1. **`state_trace_decoder.{hpp,cpp}`** (NEW) — pure structural decoder, mirrors `decode_node_events`' fail-closed style:
   ```cpp
   struct StateTraceRecord { std::uint32_t runner; std::uint32_t state; };
   enum class StateTraceError : std::uint8_t {
       Truncated, BadCapacity, CountExceedsCapacity, /* ... */
   };
   [[nodiscard]] std::expected<std::vector<StateTraceRecord>, StateTraceError>
   decode_state_trace(std::span<const std::uint8_t> whole_memory,
                      std::uint32_t trace_base, std::uint32_t trace_capacity);
   ```
   u32 count at `trace_base`; `count*8 + 8 <= capacity`; records at `trace_base + 8`.

2. **`agent_session.{hpp,cpp}`** (NEW) — the step-walk driver for AGENT modules. Owns the effects-free wasm3 instance + the states-mode invoker. Drives `step()` in a bounded loop (guard = `states.size() + 2`), asserts `current_state` consistency + exactly-once `transition_count`, fires `state_entered_hook` per transition.

3. **`workflow_session.{hpp,cpp}`** (NEW) — the workflow `run2` driver + trace/node-event decode + hook firing. Wraps the WH-3 `capability_import` executor with trace-prefix decode + hook firing (import-boundary-live). Drives `run2`, fires hooks per the guarantee table, builds the neutral `WorkflowResult` (events from decoded trace/node-events, metadata from the `ir::Program`, values from frame reads, diagnostics from trap/abort classification).

4. **`observation_emitter.{hpp,cpp}`** (NEW) — emit the canonical `ahfl.node-observation.v1` document (the SAME shape `node_embedded_host.mjs:531-549` emits and `observation_compare.hpp` consumes). Pure canonical JSON, sorted fields, byte-identical to the oracle's `emitObservation`.

5. **`wasm_workflow_runtime.{hpp,cpp}`** (NEW) — the hook-compatible runtime facade:
   ```cpp
   struct WasmWorkflowRuntimeConfig {
       // production invoker / native host binding (same seam as WorkflowRuntimeConfig)
       std::function<void(AgentId, std::string_view, std::string_view, std::string_view)>
           state_entered_hook;
       std::function<void(AgentId, std::string_view)> capability_invoked_hook;
       std::function<void(AgentId, std::string_view, std::string_view, const Value &)>
           agent_input_hook;
       std::function<void(AgentId, std::string_view, const Value &)> node_completed_hook;
       std::function<void(const CapabilityInvocationContext &, const CapabilityCallResult &)>
           capability_result_observer;
       std::function<std::chrono::steady_clock::time_point()> monotonic_clock;
       // ...
   };
   class WasmWorkflowRuntime {
     public:
       WasmWorkflowRuntime(const ir::Program &program, WasmWorkflowRuntimeConfig config);
       // Compiles the workflow to wasm (wasm backend), admits the module,
       // dispatches on module kind (agent -> agent_session + canonical
       // runv/run2; workflow -> workflow_session), returns the neutral result.
       [[nodiscard]] WorkflowResult run(const std::string &workflow_name, Value input);
   };
   ```
   The hook signatures are IDENTICAL to `WorkflowRuntimeConfig` (`workflow_runtime.hpp:113-136`) so WH-8's DAP swaps `WorkflowRuntime` → `WasmWorkflowRuntime` with zero hook-code changes.

6. **Neutral result extraction** (Decision 2): `include/ahfl/runtime/workflow_result.hpp` (NEW) + `src/runtime/engine/workflow_result.cpp` (NEW) + include updates in `workflow_runtime.hpp` / `execution_renderer.cpp` / `execution_projection.cpp` + CMake registration of the new TU in `ahfl_runtime_engine`.

### 12.4 Acceptance / test criteria

1. **State-trace decoder KAT**: hand-built memory spans with known trace rings decode byte-exactly; corrupt count (exceeds capacity) / truncation fail closed. Mirrors the node-event decoder KAT style.
2. **Agent step-walk liveness + effects-once**: an agent with a bridge capability; the states-mode invoker counts calls (served, discarded); the production invoker counts calls; assert production count == 1 and states-mode count == walk count. A blocking `state_entered_hook` mutates a counter BEFORE the canonical run; prove the hook fires from the effects-free instance.
3. **Capability pre-call hook liveness**: a blocking `capability_invoked_hook` sets a flag; the invoker asserts the flag is set (the hook fired BEFORE the effect). A capability breakpoint pauses the run inside the `ImportCallback` (the guest is genuinely stopped — `run2` has not returned when the hook blocks).
4. **Workflow import-boundary state hook**: a workflow with a capability call after N state transitions; `state_entered_hook` fires at the import boundary with the trace prefix (records 0..N-1) BEFORE `capability_invoked_hook`; assert the hook ordering log.
5. **Post-run replay ordering byte-parity with observation blessings**: the full decoded sequence (import-boundary prefix + post-run remainder) equals the JS oracle's `state_sequence`; the emitted `ahfl.node-observation.v1` document is byte-identical to the checked-in blessing for the WH-4 conformance subset (common-KAT fixture + a few p6 cases). The WH-5 gate's WH-4 precursor.
6. **Breakpoint-pause semantics on agent step**: emulate the DAP's `pause()` — a `state_entered_hook` blocking on a condition variable; a separate thread resumes; prove the step-walk pauses between steps and resumes exactly (the next `step()` fires after resume).
7. **Hook ordering guarantee**: for a workflow, the hook firing order matches the guarantee table (capability hooks live at imports interleaved with state-prefix hooks; post-run: remaining state hooks + `node_completed_hook` in schedule order). A hook log asserts the exact order.
8. **Neutral result extraction**: no wasm-host TU includes `evaluator.hpp` (compile-time check — the wasm host's header dependency on the evaluator is structurally impossible); `render_execution_result` renders a wasm-produced `WorkflowResult` identically to an evaluator-produced one for the same case (byte-parity of human/json output for a no-capability case).

---

## 12.5 WH-4 fix-forward decisions (2026-09-30 revision, dedicated decision agent, no human gate)

The WH-4 implementation (commits `0bfc56ac..534217eb`) landed the sessions /
decoder / emitter / facade, but the adversarial review returned 3 P0s + 7 P1s.
This section records the five decisions that resolve them. The original §12.1 /
§12.2 / §12.3 text above is PRESERVED; this is a dated revision, not a rewrite.
The review's findings are restated inline where a decision turns on them.

### D-A. Facade shape — compile-to-wasm + class facade in a NEW peer-tier runtime component (resolves P1-2)

**Chosen: Option B.** The session free functions (`run_wasm_agent` /
`run_wasm_workflow`, bytes + descriptor in, results out) STAY in
`ahfl_runtime_wasm_host` as the engine-session layer. The compile-to-wasm +
class facade promised by §12.3 moves into a NEW peer-tier directory
**`src/runtime/wasm_runner/`** (target **`ahfl_runtime_wasm_runner`**), gated
behind `AHFL_ENABLE_BACKEND_WASM`, linking `ahfl_compiler_backend_wasm` +
`ahfl_runtime_wasm_host` + `ahfl_compiler_ir` + `ahfl_runtime_engine`.

The new target hosts:

- **`class WasmWorkflowRuntime(const ir::Program &, WasmWorkflowRuntimeConfig)`**
  — compiles each named workflow in the constructor (`lower_ahfl_to_core` →
  `compute_core_layouts` → `resolve_core_wasm_entry` with a `PackageMetadata`
  built from the workflow name, mirroring `tests/conformance/wasm_engine.cpp:587-592`
  → `emit_core_wasm`), admits the module, and on `run(name, input) -> WorkflowResult`
  dispatches on module kind (agent → agent session + canonical runv/run2;
  workflow → workflow session) and returns the neutral result.
- **`class WasmAgentRunner`** — one-shot agent runs for REPL / DAP: compile a
  single-agent program → drive the agent session → return the output Value.

The compile pipeline (4 calls + diagnostic mapping + `PackageMetadata`
construction) lives ONCE in this target. CLI (WH-6), REPL (WH-7), and DAP
(WH-8) each construct the facade through one shared header — no triplication.

**Rejected:**

- **Option A (class inside `ahfl_runtime_wasm_host`; link `ahfl_compiler_backend_wasm`).**
  Rejected — it inverts the established layering invariant. Verified today: ZERO
  runtime targets link a compiler backend (`grep ahfl_compiler_backend_wasm
  src/runtime/**/CMakeLists.txt` is empty; `ahfl_runtime_engine` links
  `ahfl_compiler_ir` PUBLIC but never a backend). The engine-session layer
  consumes wasm bytes; it must not produce them. The reference hierarchy
  agrees: Rust's `miri` does not link `rustc_codegen_llvm`; the execution engine
  and the compiler backend are peers composed by a driver, not dependent.
  Making `wasm_host` link the backend makes the engine depend on a specific
  producer — an inverted producer/consumer edge.
- **Option C (move / duplicate the compile pipeline into the runtime).**
  Rejected — two compile paths to keep in sync (Principle 1 parallel
  implementation).
- **`src/tooling/` shared adapter.** Rejected — the facade is a RUNTIME
  component (it runs programs), not a tool. Placing it in `tooling/` lets the
  directory lie about the architecture (Principle 1 directory honesty). Tooling
  is the consumer tier; the facade is consumed BY tooling.

**AHFL-specific rationale:** the existing evaluator-backed `WorkflowRuntime`
lives in `src/runtime/engine/` and takes a `Program` — but it runs via the
tree-walking evaluator (runtime-tier), never via a compiler backend. The wasm
path fundamentally requires the wasm backend (compiler-tier) to compile. The
composition of compile + run is a NEW role that did not exist when the engine
was evaluator-only. It deserves its own peer tier rather than being forced into
the engine (inverted edge) or tooling (directory dishonesty). The wasm_host
target's own CMakeLists comment already calls it "a peer execution engine of
src/runtime/engine"; the wasm_runner is the peer that composes it with the
backend.

**WASM=OFF gating:** `ahfl_runtime_wasm_runner` is built only inside
`if(AHFL_ENABLE_BACKEND_WASM)`. The `#ifdef` gate lives in the TOOLING
consumers (CLI / REPL / DAP), which conditionally construct the facade. The
facade itself is unconditionally compiled within its gated target. Under
WASM=OFF the tools either refuse with a diagnostic (CLI `ahflc run`) or disable
the wasm-only feature (REPL eval) — decided per-tool at WH-6 / WH-7 / WH-8.

**Costs:** one new CMake target + directory; the compile pipeline moves from
each tool into the facade; `PackageMetadata` construction from the workflow
name (mirrors the conformance producer).

**Acceptance:**

1. `ahfl_runtime_wasm_runner` links `ahfl_compiler_backend_wasm`;
   `ahfl_runtime_wasm_host` does NOT (grep-zero — the engine stays backend-free).
2. `WasmWorkflowRuntime(program, config).run(name, input)` returns a neutral
   `WorkflowResult`; no wasm_host TU includes `evaluator.hpp`.
3. CLI / REPL / DAP each construct the facade through one shared header (no
   triplication of `lower_ahfl_to_core` → `emit_core_wasm`).
4. WASM=OFF configures + builds clean (facade target absent; tools carry the
   `#ifdef`).

### D-B. D1 revision — WireJson workflow state evidence from node-event reconstruction (resolves P0-1)

**Chosen.** For WireJson (non-P6) workflows the session reconstructs
`state_sequence` and fires `state_entered_hook` from decoded node-event records
joined to the descriptor agent walks — EXACTLY as the JS oracle does at
`node_embedded_host.mjs:1413` (`records.map(record => recordStates(lane,
record)).flat()`). P6-frame workflows keep the trace-prefix-at-imports +
post-run union (unchanged from §12.1).

The P0 root cause: `workflow_session.cpp:96-100` gates trace decode on
`is_p6`, so WireJson workflows produce an EMPTY `state_sequence` (9 census
scenarios blocked). The trace ring exists only on P6 modules; WireJson
workflows carry the node-event buffer instead.

**Rules:**

- **Join source of truth:** the descriptor's `agents[].walk` (the declared
  state names entered on one runner call) for WireJson — NOT `all_states`
  (which indexes the P6 trace ring's dense state ids). The oracle uses
  `agentWalk.walk` for WireJson (`recordStates:1235`) and `all_states ?? walk`
  for P6 (`:1328`). The session mirrors this.
- **Ordering:** node-event records are in schedule order (record `i` has
  `schedule_pos == i`, enforced by the oracle `:1193` and the C++ decoder).
  For each record the session finds the node by `node_id`
  (`descriptor.nodes[].node_id`), gets the runner (`node.runner`), and emits
  `agents[runner].walk` states in order. The flat sequence is schedule-order
  walks — byte-identical to the oracle's `records.map(...).flat()`.
- **Identity workflows (no imports, no event region):** the oracle reconstructs
  from `lane.nodes.map(...)` (`:1227`) — schedule-order walks from the
  descriptor. The session mirrors: for a workflow with no event region, emit
  each node's runner walk in schedule order.
- **Import-boundary live hooks on WireJson: NONE.** WireJson node events are
  decoded POST-RUN only (the guest writes the event buffer during `run2`; the
  host has no mid-run access without the P6 trace ring). `state_entered_hook`
  for WireJson workflows fires POST-RUN, in schedule order. **Honest liveness
  consequence for WH-8:** WireJson state hooks are post-run (post-mortem), NOT
  live. The §12.1 guarantee table's "IMPORT-BOUNDARY-LIVE" cell for workflows
  applies ONLY to P6-frame workflows (which have the trace ring). This is a
  DATED REVISION of D1: the original table did not distinguish P6 vs WireJson
  liveness; the revision states that WireJson state hooks are post-run.
- **OOR / corruption fail-closed:** the session MUST call
  `validate_state_trace_bounds` (P6) and the equivalent node-event bounds check
  (WireJson: `runner >= agents.size()` or `state >= walk.size()` → fail-closed,
  NOT the silent `continue` at `workflow_session.cpp:69-75`). The oracle fails
  at `:1329-1331` (OOR runner/state) and `:1318-1320` (count exceeds capacity).
  The session deletes the silent `continue` and returns a terminal `NodeFailed`
  + diagnostic on any OOR / corrupt record.

**Acceptance:**

1. A WireJson workflow with capabilities produces a non-empty `state_sequence`
   byte-identical to the oracle's (the 9 blocked census scenarios unblock).
2. An identity workflow (no imports) produces schedule-order walks from the
   descriptor.
3. A hand-corrupted node-event record (OOR runner / state) yields `NodeFailed`
   + diagnostic, NOT a silent empty sequence.
4. P6 workflows keep the import-boundary-live + post-run union (unchanged).

### D-C. Agent capability integration — big-bang collapse onto the WH-3 executor (resolves P0-2, P0-3, P1-1)

**Chosen: Option A (big-bang collapse).**

1. **Codegen emits the AHFLXM exec-manifest for capability AGENT modules.**
   New `kEntryKindAgent = 1` in both codegen (`core_wasm_codegen.cpp:181`) and
   A2 (`core_wasm_schema_module.cpp:60`). Agent manifest grammar:
   `magic(6) + version(1) + entry_kind=1 + agent_id(4) + capability_count(4) +
   capabilities[] { capability_id(4) + source_symbol(8) }`. This mirrors the
   workflow manifest but with a flat capability list (an agent has no workflow
   schedule / nodes).
2. **A2 admission gains the agent entry kind.** `decode_exec_manifest`
   decodes the agent arm; the `SchemaModulePayload` builds `call_sites` from
   the capability list (each capability = one call site, with `import_ordinal`
   resolved from the wire-schema / imports cross-check, exactly as the workflow
   path does for cap nodes). Real agent modules admit WITHOUT synthetic
   manifests (the WH-3 `a5a6d1b0` synthetic-injection technique was test-only
   evidence; production must not inject).
3. **BOTH the canonical instance AND the effects-free step instance reuse
   `make_capability_import_callback`** (the WH-3 executor: schema-bound opaque
   + bridge lanes, `capability_import.cpp`). The simplified handler
   (`wasm_agent_runner.cpp:247-337`) is DELETED entirely.
4. **Effects-free step instance: scripted replay.** The WH-3 executor runs on
   the effects-free instance with a SCRIPTED-REPLAY invoker (the caller's
   states-mode invoker). Bridge control-block resolution and schema validation
   still run (host-side, not effects), but the invoker's results are scripted —
   never a live side effect. The facade config gains `states_invoker`
   (optional; defaults to the canonical invoker for side-effect-free mock
   invokers — the conformance case). For WH-4 conformance the harness supplies
   the mock as both.
5. **Canonical name (P0-3):** the WH-3 executor uses
   `config.name_resolver(source_symbol)` → canonical name. The
   `local_capability_name` strip (`wasm_agent_runner.cpp:51-58`) is deleted with
   the simplified handler. The `e2_capability_agent.echo` blessing expects
   `wasm::e2_capability::Echo` (verified in
   `tests/conformance/observations/e2_capability_agent.echo.json`) — the
   canonical name the executor produces. The simplified handler's comment
   ("the evaluator's capability name is the local declaration name") was WRONG:
   the evaluator blessing carries the canonical name.

**Rejected:**

- **Option B (separate descriptor-based executor).** Rejected — a parallel
  implementation (Principle 1). The WH-3 executor already handles both lanes
  with schema-bound validation; a second executor duplicates the envelope /
  validate / pack logic and is precisely what introduced the P0-3 canonical-name
  bug and the P0-2 missing-bridge-lane gap.

**Acceptance:**

1. All 4 v2c bridge fixtures run through the WH-3 executor on wasm3 (no
   simplified handler; grep-zero for `local_capability_name`).
2. `e2_capability_agent.echo` produces `capability_sequence
   ["wasm::e2_capability::Echo"]` (canonical, not stripped).
3. Malformed params / results fail closed (`ParamSchemaInvalid` /
   `ResultSchemaInvalid` → `ImportAbort`).
4. Closure results fail closed pre-call (`ResultEncodeFailed`).
5. The effects-free step instance's invoker is called with scripted results;
   the canonical instance's invoker is called exactly once per capability
   (effects-once).
6. A2 admits a real emitted agent module with the agent manifest (no synthetic
   injection in production code).

### D-D. Fix-forward scope boundaries

**IN the fix-forward (gate / honesty):**

- **P0-1** (D-B): WireJson state reconstruction + fail-closed OOR.
- **P0-2 / P0-3 / P1-1** (D-C): collapse onto WH-3 executor + canonical names.
- **P1-6 (node-name semantics):** add `std::string name` to
  `CoreWasmNodeDescriptor` (codegen populates from the `WorkflowNodeDecl`
  source name). `node_completed_hook` fires with the REAL node name +
  `AgentId{node.runner}` (not `agent_name` in the node slot, not `AgentId{0}`).
  `state_entered_hook` for workflows resolves `node_name` from the current
  context (import boundary: call site → node; post-run: runner → schedule →
  node). `capability_invoked_hook` / observer fire with the real `AgentId`
  (resolved per-import from the call site's node runner, not a fixed
  `AgentId{0}`). The evaluator passes `node.source->name`
  (`workflow_runtime.cpp:1456`); the wasm lane matches.
- **P1-7** (D-E): install-neutering.
- **P2-1:** OOR fail-closed (covered by D-B).
- **P2-3:** ABI constants (use `kCoreWasmFixedLinearMemory*` SSOT, not magic
  numbers).
- **P2-4:** include cleanup.
- **P2-6:** strong oracles (assert WireJson states + canonical names in the
  conformance adapter).
- **P1-5 (populate stores):** the session populates
  `ExecutionMetadataStore` (workflow + agents + nodes with display names),
  `ExecutionReport` (status, workflow, `nodes[]` with node / agent / status /
  output, `execution_order`, `output`, `failure_kind`), and
  `ExecutionEventStore` (the `RunStarted` / `NodeScheduled` / `NodeStarted` /
  `NodeCompleted` / `RunCompleted` lifecycle events so the replay + audit
  projections in `execution_renderer.cpp:333-396` produce correct counts). The
  session emits these from decoded node-events + trace + capability
  observations. This is the minimum the renderer + audit projections consume.
- **Terminal mapping:** sessions yield `WorkflowResult` with
  `Completed` / `NodeFailed` + diagnostics (trap / host-abort / non-OK →
  `NodeFailed` + a fixed-code diagnostic carrying the `CapabilityImportError`
  enum name or trap classification). NOT bare error strings. Host-abort / trap
  diagnostic shape: `DiagnosticBag` with a fixed code (`wasm.host-abort` /
  `wasm.trap`) + the `CapabilityImportError` name. **NO `Suspended` in the
  fix-forward** (see WH-4b below).
- **`agent_input_hook`:** added to `WasmRuntimeHooks`; fires LIVE on the agent
  lane before the step-walk (the D1 guarantee table promised it; the delivered
  facade omitted it). Workflow lane stays unfired (recorded D1: in-guest
  materialized input is not host-observable).

**OUT (deferred):**

- **Suspended snapshot origination (P1-4) → WH-4b slice, gated before WH-6.**
  The fix-forward maps Pending → `NodeFailed` (honest: the wasm lane does not
  suspend yet) with a diagnostic. WH-4b adds: opaque-lane Pending observation
  (the import callback returns `raw_status=2`; the session observes it via
  `import_state`), `Suspended` status, and `WorkflowRecoverySnapshot`
  construction (node input + memo table + pending cap_id / ordinal).
  **CRITICAL fixture finding:** the `durable_resume_cli_smoke.py` suspend
  fixture uses a single-Param `Echo` capability
  (`tests/scripts/durable_resume_cli_smoke.py:58`) → the BRIDGE lane
  `(i32)->(i32,i32)` → Pending TRAPS (the bridge has no graceful PENDING arm;
  WH-3 D3 sealed contract). WH-4b / WH-6 MUST switch the suspend fixture to an
  OPAQUE-lane capability (2+ params, which has the graceful `(2,0,0)` PENDING
  arm) or add an opaque-lane suspend fixture. The bridge-lane trap is a
  sealed-ABI contract, not a bug.
- **`recovery_snapshot` / `resume_pending_result_wire_json` /
  `durable_write_intent_sink`:** WH-6 (CLI config fields; consumed with the
  suspend / resume path).
- **`cancellation_requested`:** WH-8 (checked at import boundaries; never
  thread-kill wasm3 — per the wh8 prep map).
- **`monotonic_clock`:** WH-8 or later (DAP timing; the WH-6 CLI does not set
  it — the CLI sets only four config fields per the wh6 prep map).

### D-E. Install neutrality — move workflow_result.hpp to src-internal (resolves P1-7)

**Chosen: Option B.** Move `include/ahfl/runtime/workflow_result.hpp` to
**`src/runtime/engine/workflow_result.hpp`** (its survivor home, next to
`workflow_result.cpp`). Flip all include sites from
`ahfl/runtime/workflow_result.hpp` to `runtime/engine/workflow_result.hpp`
(the build-tree `src/` path, matching the engine's other internal headers —
e.g. `capability_transport_adapter.hpp:3-4`). Include sites:
`workflow_runtime.hpp:15`, `execution_renderer.cpp:11`,
`execution_projection.cpp:7`, `wasm_agent_runner.hpp:24`,
`workflow_session.hpp:28`.

**Rejected:**

- **Option A (promote `value.hpp` + `workflow_recovery.hpp` +
  `atomic_file.hpp` into installed `include/`).** Reasons:
  1. `value.hpp` is the survivor host wire type, deliberately kept src-internal
     by WH-S ("Internal src/ headers ... NOT part of the installed SDK
     surface", `src/runtime/value/CMakeLists.txt:28-30`). Promoting it ships
     the Value type as SDK surface — a far bigger commitment than fixing one
     header.
  2. `workflow_recovery.hpp` includes `base/support/atomic_file.hpp`
     (src-internal) — promoting it drags `atomic_file.hpp` too, and that may
     cascade further.
  3. `ahfl_runtime_value` is in the INTERNAL install targets
     (`AhflInstall.cmake:108`, only with `AHFL_INSTALL_INTERNAL_TARGETS=ON`)
     and its headers are BUILD_INTERFACE-only. Promoting `value.hpp` to
     `include/` while the target's headers stay src-internal is inconsistent.
  4. No external SDK consumer needs `WorkflowResult` today: the installed
     `execution_renderer.hpp` / `execution_projection.hpp` only
     FORWARD-DECLARE it (they take it by reference). An SDK consumer calling
     `render_execution_result` needs the complete type — but that is an
     in-tree tooling scenario (CLI / REPL / DAP), not an external SDK
     scenario.
  5. `ahfl_runtime_wasm_host` is NOT installed (WH-1 decision 10.6). Its
     headers are src-internal. `workflow_result.hpp` is consumed by wasm_host +
     engine + tooling — all in-tree.

**AHFL-specific rationale:** the installed SDK surface is the compiler
frontend + IR + pipeline (the `ahflc` / `ahfl-lsp` tools and the CMake target
graph). The runtime execution result is an in-tree implementation detail of the
tools, not a public API. Keeping it src-internal is honest: the directory
structure reflects that the runtime result is internal, not SDK surface.

**Costs:** one file move + 5 include-site flips. No compat shim (Principle 1:
big-bang).

**Acceptance:**

1. `include/ahfl/runtime/workflow_result.hpp` is deleted;
   `src/runtime/engine/workflow_result.hpp` exists.
2. No installed header (under `include/ahfl/`) includes a src-internal header
   (grep `#include "runtime/` or `#include "base/` in `include/ahfl/` → zero;
   today `workflow_result.hpp:34-35` is the sole violator).
3. `cmake --install` + an external compile test succeeds (the installed SDK
   has no dangling includes).
4. All in-tree consumers build unchanged (include path flip only).

### D-F. Branching-walk honesty — fail-closed rejection of WireJson agents with computed-goto terminals (2026-09-30 addendum, P2-6)

**Problem.** The D-B WireJson state reconstruction joins node-event records to
the descriptor's `agents[].walk`. The walk is a LINEAR chain of `GotoAction`
edges built by `workflow_initial_transitions` / `runner_walk_names`. When the
walk reaches a `ComputedGotoAction` (a branching / computed-routing terminal),
the walk stops and treats that state as a terminal — but the agent actually
takes a runtime-dependent branch the linear walk cannot represent. The D-B
reconstruction would emit a dishonest `state_sequence` (truncated at the branch
point, missing the states the agent actually entered).

**Chosen: fail-closed rejection at codegen.** `encode_workflow_module` checks
each non-P6 (WireJson) runner's walk terminal: if the terminal action is a
`ComputedGotoAction`, the module is NOT emitted and a
`wasm.UNSUPPORTED_ORCHESTRATION` diagnostic is raised ("WireJson agent has a
computed-goto (branching) terminal; the linear walk cannot represent
runtime-dependent branches. Use a P6-frame agent for computed routing.").

**Why not emit a walk covering branch joins:** the walk is a static descriptor
structure; it cannot know which branch the agent takes at runtime. Emitting a
walk that covers all possible branches would produce a state_sequence that
includes states the agent never entered — equally dishonest. The honest options
are (a) reject the module (chosen) or (b) make the agent P6Frame (which has the
trace ring for real runtime state observation).

**Scope:** the check applies ONLY to non-P6 (WireJson) runners. P6Frame agents
use the P6 frame runner (`make_workflow_p6_runner_body`), which has the trace
ring and does not rely on the linear walk for state observation. The
`runner_walk_names` function is unchanged: for P6Frame agents the walk is a
best-effort prefix (unreachable if-else arms are never observed, as documented
in the comment at `core_wasm_codegen.cpp:16805-16810`); for WireJson agents the
module is rejected before the descriptor is built, so `runner_walk_names` is
never called for a branching WireJson agent.

**Acceptance:**

1. A WireJson agent with a `ComputedGotoAction` terminal is NOT emitted; the
   codegen result carries `wasm.UNSUPPORTED_ORCHESTRATION`.
2. P6Frame agents with `ComputedGotoAction` (e.g. `e2e_multi_agent`) are
   unaffected — they use the P6 frame runner.
3. The 4 WireJson reconstruction fixtures (`e3_capability_workflow`,
   `e3_capability_workflow_resume`, `e3_identity_workflow`, `float_output_e2e`)
   have elementwise `state_sequence` assertions through the facade
   (`wasm_runner.cpp`).

### Resulting ordered fix-forward worklist

1. **D-E install move** (unblocks everything; mechanical): move
   `workflow_result.hpp` → `src/runtime/engine/`; flip 5 includes; verify
   install + build.
2. **D-C codegen + A2 agent manifest**: emit AHFLXM for capability agents
   (`kEntryKindAgent`); A2 decodes the agent arm + builds call sites.
3. **D-C collapse**: delete the simplified handler; both canonical +
   effects-free instances use `make_capability_import_callback`; add
   `states_invoker`; delete `local_capability_name`.
4. **D-B WireJson state reconstruction**: node-event → descriptor-walk join;
   identity-workflow descriptor reconstruction; delete silent `continue`;
   fail-closed OOR.
5. **P1-6 node-name + AgentId semantics**: add `CoreWasmNodeDescriptor.name`;
   real node name in `node_completed_hook`; real `AgentId` in capability hooks;
   node_name resolution in `state_entered_hook`.
6. **P1-5 store population + terminal mapping + diagnostics**: lifecycle events
   → report / metadata / event stores; trap / abort → `NodeFailed` +
   `DiagnosticBag`; add `agent_input_hook` to `WasmRuntimeHooks` (agent lane).
7. **D-A wasm_runner facade**: new peer target; `WasmWorkflowRuntime` +
   `WasmAgentRunner`; compile pipeline once; gated CMake.
8. **P2-3 / P2-4 / P2-6**: ABI constants, include cleanup, strong oracles.
9. **Tests**: all acceptance criteria above; the 9 blocked WireJson census
   scenarios unblocked; 4 v2c fixtures through the WH-3 executor;
   `e2_capability_agent.echo` canonical name; OOR fail-closed; store
   population byte-parity with the evaluator renderer.

**Prep-map impacts (flagged for the WH-5 / 6 / 7 / 8 surveys):**

- **WH-5:** the native adapter drives through the NEW `WasmWorkflowRuntime`
  facade (not the free functions). The compile-then-drive pattern
  (`wasm_engine.cpp:554-619`) moves into the facade. WireJson state
  reconstruction (D-B) is required for the 4 WireJson workflow cases
  (e3_capability_workflow, e3_capability_workflow_resume,
  e3_identity_workflow, float_output_e2e). e2e_multi_agent is P6Frame, not
  WireJson, so it uses the P6 trace ring, not node-event reconstruction.
  The 4 v2c bridge fixtures need the D-C collapse.
- **WH-6:** the CLI flips to `WasmWorkflowRuntime`. The four config fields map
  to `WasmWorkflowRuntimeConfig`. Suspend / resume waits for WH-4b. The suspend
  fixture MUST switch to an opaque-lane capability (bridge Pending traps).
- **WH-7:** the REPL uses `WasmAgentRunner` (one-shot compile + run). The
  compile pipeline is in the facade (no triplication).
- **WH-8:** the DAP uses `WasmWorkflowRuntime`. Hook semantics are fixed in
  this fix-forward (node_name, AgentId, agent_input_hook). Cancellation is
  WH-8. WireJson state hooks are post-run (D-B liveness consequence — the DAP's
  `verified:true` breakpoint claim on a no-import WireJson node would lie; the
  DAP must classify breakpoints as live / post-run using descriptor
  observability).

## 12.6 WH-4b decisions (2026-09-30, dedicated decision agent, no human gate)

WH-4b 在 wasm 车道起源挂起(suspension)并打通 suspend/resume 往返,门控于 WH-6(`ahflc run` 切换)之前。prep map(`$CLAUDE_JOB_DIR/tmp/wh4b-prep-map.md`)的事实已对 HEAD `161666db` 逐条复核。本节是一个连贯设计,不是菜单。

### 12.6.0 决策摘要

**两个引擎共用一个挂起工件:`WorkflowRecoverySnapshot` v2 JSON(`src/runtime/engine/workflow_recovery.hpp:109-118`),wasm 车道由 `run_workflow_session` 内一个新的会话级 memo-replay 层消费——不走 WH-3 `run_resume` / D1b controller driver。** wasm 车道起源与 evaluator 相同的快照,对 v2 做一处 append-only 扩展:`CapabilityMemoEntry` 增加可选 `node` 坐标(§12.6.3),把 memo 从"单挂起节点"泛化到"整工作流"——因为 wasm resume 是 FRESH instance 重跑整个模块,需要 pending 之前跨所有节点全部能力调用的 memo。HMAC 二进制机制(`CoreWasmResumeRecord` / `IntegrityPayloadStore` / D1b controller / `run_resume`)保持 test-only FOUNDATION,并绑定日落条款(§12.6.9):D2b 切片必须给它第一个生产调用者,否则一次性 big-bang 删除。

**ReadyForLive 在 WH-4b 范围内且可用**——通过会话设计而非 D1b driver,见 §12.6.8 的诚实裁决。

### 12.6.1 Fork 1+2 — 快照格式与 resume 消费者:复用 JSON v2;新建会话级消费者;HMAC 机制保留为 test-only FOUNDATION

**选择。**
- **格式**:`WorkflowRecoverySnapshot` v2 JSON + `WorkflowRecoveryStore`(单原子文件,`workflow_recovery.cpp:244-271`)是两个引擎唯一的挂起工件。wasm 车道既起源(§12.6.4)也消费(§12.6.5)它。
- **消费者**:`run_workflow_session`(`src/runtime/wasm_host/workflow_session.cpp`)内新的 memo-replay 层。run 开始时若带 `recovery_snapshot`,会话把它翻译为内存 memo map,键为 `(WorkflowNodeId, 节点内 ordinal)`。每个同步 import 边界用 import 的 `source_symbol` O(1) 导出 `(node, ordinal)`(复用 P2-2 的 `symbol_to_runner` 并扩展到 schedule position,§12.6.5),提供 memo 字节 / 注入 / 放行 live。无 HMAC store、无密钥管理、无 `run_resume`。
- **HMAC 机制**:维持 test-only FOUNDATION 现状,日落条款见 §12.6.9。

**拒绝。**
- **转码 JSON 快照为 `CoreWasmResumeRecord` 并委托 `run_resume`(fork 2b)。** 三个 HEAD 事实击败它:① `run_resume` 对 `ReadyForLive` fail-closed(`core_wasm_resume_host.hpp:145` `ReadyForLiveBlocked`;driver 注释 :30-31),pending 之后还有能力调用的工作流无法经它 resume——evaluator 对这类调用是 LIVE 执行的(`workflow_runtime.cpp:1016` 置 `replaying=false`,live 路径在 `:1025+`),委托会造成相对 evaluator 的回归;② 本地单用户文件需要 `IntegrityPayloadStore` root + HMAC 密钥,而该威胁模型(对抗性篡改者)不是今天的;③ JSON/HMAC 双快照并行,违反 Principle 1。
- **JSON + HMAC 双写(fork 1c)。** 拒绝——双工件一致性负担 + Principle 1 并行实现。
- **按 Principle 1 立即删除 HMAC 机制。** 在 WH-4b 中考虑并拒绝:它是已落地的 D2b 基底(record codec + 机密 payload store + decision-only controller + `run_resume` driver + WH-3 真实 wasm3 引擎端口),注释明确命名未来消费者("the first is the future B2-D host",`core_wasm_resume_record.hpp:12`)。D2b(durable-effect authority / KMS store)是 WH-6 切换后的紧邻前沿;WH-4b 删除、D2b 重建纯属 churn;永久不链接则被日落条款禁止。这是带死线的判断,不是"以防万一"的无限保留。

**AHFL-specific 理由。** recovery store 今天是本地单用户文件;JSON v2 已在损坏时 fail-closed(id/闭包/memo 交叉校验 `workflow_runtime.cpp:82-144`;binding 门控解码 `:871-888`)。HMAC/SHA-256 防的是对抗性 store 写入者——那是 D2b durable-effect-authority 前沿的威胁模型。CLI 既有标志契约与 smoke 都讲 JSON v2;单一格式让 WH-6 切换成为 CLI 契约不变下的纯引擎替换。参照层级一致:event-sourced checkpoint(Temporal/Cadence/Restate 主流持久工作流模式)在宿主原生 store 持久化 replay log,盘上编码是宿主关切。

### 12.6.2 身份元组与等价性证明

memo 键与 pending 坐标与 evaluator 使用同一元组,每个分量在 HEAD 都是引擎中立的:

| 分量 | Evaluator | Wasm 会话 | 等价证据 |
|---|---|---|---|
| node | `WorkflowNodeId`(稠密 source-order 索引) | `WorkflowNodeId{descriptor.nodes[K].node_id}` | `wasm_lifecycle.hpp:96-97` 注释:node_id 按序分配,等于 evaluator 稠密索引 |
| ordinal | 节点内计数器,dispatch 前自增(`workflow_runtime.cpp:780`) | import 边界的节点内计数器 | 两者都按程序序计数每节点能力调用,节点间重置 |
| cap_id | `context.source_capability_symbol_id`(`:782`) | `call_site.source_symbol()` | codegen `core_wasm_codegen.cpp:9917` 写同一 SymbolId 空间 |
| arg_hash | `runtime::hash_values(arguments)`(`:787`) | `wrapped_invoker` 对解码后的 Value 参数 `hash_values(args)` | 结构相等 Value 上的同一函数(opaque 车道在 wire binding 下解码参数,`capability_import.cpp:294`) |

**等价性证明。** 两引擎都按稠密 Kahn schedule 序执行节点(node-event buffer 是稠密前缀:记录 `i` 的 `schedule_pos == i`,由 D1b join `core_wasm_resume_controller.cpp:641` 与会话 post-run 交叉检查强制)。节点内能力调用按程序序发生,两引擎都按 `0,1,2,…` 计数。故 wasm 会话节点 K 的第 N 个能力 import 与 evaluator 节点 K 的第 N 次 dispatch 是同一 `(node, ordinal)` 调用。`cap_id` 与 `arg_hash` 是每次 memo 命中都断言的完整性交叉检查;不匹配 fail-closed,绝不静默重新 live 调用。

**跨引擎 resume 出范围且 fail-closed。** 引擎 X 起源的快照只能由引擎 X 消费。wasm 消费者要求每条 memo 带 `node` 坐标;evaluator 起源的快照没有 → wasm 消费者在加载时以定向诊断拒绝(WH-6 后 evaluator 被删除,反向不可能发生;WH-6 前 CLI 无法产生 wasm 起源的快照)。无兼容 shim。

### 12.6.3 Fork 5 — 粒度:用一个 append-only v2 字段把 memo 泛化到整工作流

**已验证的问题。** evaluator 恢复已完成节点、只重跑挂起节点(`workflow_runtime.cpp:724-740` NodeRestored),其 memo 只覆盖挂起节点。wasm resume 是 FRESH instance 重跑整个模块(wasm3 无法停泊调用栈)。已完成节点的 guest 内计算是确定性且无副作用的,但其能力调用是带效果的宿主 import——resume 时必须 memo 供给,绝不重新 live 调用(重放 `durable_write` 违反 exactly-once)。JSON v2 的 `SuspendedNodeState.memo` 没有节点坐标,无法放置已完成节点的 memo 条目。

**选择。** 给 `CapabilityMemoEntry` 增加一个 append-only 字段:

```cpp
// WH-4b: 该 memo 条目所属节点。缺省(nullopt)表示挂起节点——evaluator 的
// 每节点 memo 语义,不变。wasm 车道总是设置它:wasm resume 在 fresh
// instance 上重跑整个模块,memo 覆盖 pending 之前每个节点的调用。
std::optional<WorkflowNodeId> node{};
```

append-only v2 演进(`workflow_recovery.hpp:27-31` 的稳定规则:新字段可选,旧读取者忽略)。wasm 消费者要求每条都带(缺失则加载 fail-closed);evaluator 消费者不变(从不设置,全 nullopt = 挂起节点,即今天的行为)。HMAC record 已正确建模这一点——`CoreWasmResumeRecord.nodes` 是所有节点的逐节点 memo 向量(`core_wasm_resume_record.hpp:80-87`)——证实 whole-workflow memo 是正确的 wasm 语义。

**拒绝:** 仅挂起节点 memo(已完成节点调用 resume 时 live 重跑——重放效果,违反 exactly-once);快照上并行 `wasm_memo` 结构(双 memo 向量,Principle 1);新 schema v3 字符串(一个可选字段应走 v2 append-only)。

**wasm 车道上的 `completed_nodes`:** 为记录与闭包校验而写,但不用于跳过节点:wasm 消费者重跑全部节点并 memo 供给其调用;记录的 `output` 仅信息性(evaluator 恢复它,wasm 车道从 memo 供给的输入确定性重算)。

### 12.6.4 起源 — 会话在何处捕获挂起坐标(fork 4)

**选择:会话级 recorder 包裹 import callback(fork 4c)。** 捕获点是 `wrapped_callback`(`workflow_session.cpp:451-476`)——唯一同时看到 `ImportObservation`(import ordinal、`whole_memory`)与 `ImportReply`(status + `result_ptr`/`result_len`)的最外层;经其内部同步调用的既有 `wrapped_invoker`(`:304-355`)还能看到解码后的 `args`、`CapabilityCallResult` 与 `ctx.source_capability_symbol_id`。

新文件 `src/runtime/wasm_host/wasm_resume_recorder.{hpp,cpp}` 中的会话局部 `WasmResumeRecorder` 持有:逐节点 import 计数器(按 schedule position 索引的 `vector<uint64_t>`);在途 memo 向量 + pending 坐标;"当前 import 坐标"(wrapper 在 `inner(obs)` 前设置,供 `wrapped_invoker` 消费 args→arg_hash/result/cap_id,wrapper 在 `inner` 返回后从 `obs.whole_memory` 的 `(result_ptr, result_len)` 读取回复的精确 wire 字节)。

每个 OK opaque 调用,recorder 追加 `CapabilityMemoEntry{ node=<当前 WorkflowNodeId>, ordinal=<计数器值>, cap_id=source_symbol, arg_hash=hash_values(args), result=<native 投影,clone>, source=ExactSidecar, authoritative_json=<精确回复字节>, result_present=<来自调用结果> }`。精确字节即 opaque 车道 `serialize_value_for_wire_json` 的输出(`capability_import.cpp:327-342`)——逐字捕获,绝不重新序列化(无拼写漂移);这正是 v2 sidecar 字段为之设计的 `ExactSidecar` 状态(`workflow_recovery.hpp:54-56`)。

invoker 返回 Pending 时,`handle_opaque` 回复 `(AHFL_CAP_PENDING=2, 0, 0)`(`capability_import.cpp:309-313`),guest latch 且 run2 返回 `(2,0,0)`。recorder 在该 import 盖戳 pending 坐标 `(node, ordinal, cap_id, arg_hash)`;会话 run2 臂(`workflow_session.cpp:597-608`)增加 `raw_status == 2` 分支:从 recorder 构造 `SuspendedNodeState`,`node_input = nullopt`(宿主不可观察——workflow 车道不发 `agent_input_hook`,`workflow_session.hpp:13`;evaluator 中该字段也仅信息性,`workflow_recovery.hpp:96-102`),以 Suspended 终止。

**facade 产出:** `WorkflowResult` 的 `report.status = Suspended`、`result.suspended` 填充,生命周期事件 `NodeSuspended` / `WorkflowSuspended` / `RunCompleted{Suspended}`(`execution_event.hpp:218-269`)经扩展了 Suspended 终止臂的既有 `finalize_wasm_workflow_run` 路径发出。渲染器已中立(`execution_renderer.cpp:31-32,49-50,121,127,231-236,254-259`)。挂起时保存:会话经 `WorkflowSessionConfig::recovery_store` 保存(镜像 evaluator `workflow_runtime.cpp:1582-1603`:保存失败把 Suspended 降级为 `NodeFailed`)。

**拒绝:** 在 `handle_opaque` 内捕获(车道特定管道穿过必须保持引擎会话中立的 WH-3 executor,且看不到逐节点计数器);仅在 `wrapped_invoker` 捕获(看不到 `ImportReply` 字节——结果帧在 invoker 返回后由 `handle_opaque` 写出,重新序列化 Value 有拼写漂移风险)。

**仅 opaque 车道。** bridge 车道 Pending 按密封 ABI trap(无优雅 PENDING 臂;`capability_import.cpp:537-542` 返回原始状态后 guest trap),WH-4b 不在 bridge 车道起源挂起,trap 路径不变并记录。

### 12.6.5 wasm 车道重放语义(精确算法)

resume run(带 `recovery_snapshot`)时会话:

1. **按 descriptor 校验快照**(evaluator `workflow_runtime.cpp:82-144` 的 wasm 镜像):workflow id 匹配;`suspended.node` 在 descriptor 中存在且 agent 匹配;与 `completed_nodes` 不相交;`completed_nodes` 依赖闭包;每条 memo 带 `node` 坐标;逐节点 memo ordinal 唯一且挂起节点上 `< pending_ordinal`;pending `cap_id` 在 wire schema 可解析。失败 → 失败的 `WorkflowResult`,绝不静默全新运行。
2. **构造 memo map** `(WorkflowNodeId, ordinal) -> CapabilityMemoEntry`,标记前沿 `(suspended.node, pending_ordinal)`。
3. **Fresh instance**(会话每 run 已新建;pending latch 是 per-instance 的,`core_wasm_codegen.cpp:13905-13909`,fresh 即清零)。
4. **逐 import**(`wrapped_callback` 中,`inner` 之前):O(1) 导出 `(node, ordinal)`——call site 的 `source_symbol` → runner → schedule position(把既有 `symbol_to_runner` `workflow_session.cpp:260-293` 扩展为 `symbol_to_schedule`);`ordinal = per_node_counter[schedule]++`。然后:
   - **Memo 命中**(`(node, ordinal)` 在 map 且位于该节点前沿之前):对 live 调用交叉校验 `cap_id` + `arg_hash`;不匹配 → fail-closed `"durable resume replay diverged from the recorded memo (ordinal N)"`(引擎中立消息,`workflow_runtime.cpp:832`);匹配 → 逐字 `alloc_then_write` `authoritative_json` 字节,回复 `(AHFL_CAP_OK, ptr, len)`。绝不调用 invoker → 零 live 副作用、零 intent 重发(§12.6.7)。
   - **前沿**(`(node, ordinal) == (suspended.node, pending_ordinal)`):先身份门(`cap_id == pending_cap_id`,镜像 `:930-940` P0-15)→ 解析 pending 能力的结果 binding → 在该 binding 下 parse + `decode_json` `resume_pending_result_wire_json`(镜像 `:981-989`)→ 逐字供给;清除该节点前沿(后续调用 live)。缺注入结果 → `"durable resume is missing the pending capability result"`(`:967`)。
   - **前沿之后 live**(schedule 中挂起节点之后的节点,或同节点 ordinal > pending):放行 `inner` → 正常 live invoker 路径,即 ReadyForLive(§12.6.8)。
   - **前沿之前未命中**:分歧 → fail-closed。
5. **run2 返回**:OK 且前沿被注入 → 正常 Completed;OK 但前沿从未被命中(fresh instance 走了不同分支、未发生 pending 调用)→ 分歧 fail-closed(镜像 `workflow_runtime.cpp:1422-1434`);trap/error → 既有失败臂。

**不做逐 import node-event 前缀 join。** 会话从 import 自身的 `source_symbol` O(1) 导出 `(node, ordinal)`,而非每个 import 重解 node-event buffer。D1b controller 需要 O(N²) 前缀 join(`core_wasm_resume_controller.cpp:619-658`,复杂度注释 `:394-396`)是因为它是没有 invoker 上下文的纯决策权威;会话有上下文。node-event buffer 仍是 run 后完成性权威(P2-3 交叉检查不变)。共享原语是 `core_wasm_node_events::decode_node_events`——两层使用的 SSOT,无重复解码器。

### 12.6.6 Fork 3 — CLI / fixture / 标志:本切片不加 CLI 路由;facade 字段与 fixture 迁移现在落地

**选择 option (a):facade 配置字段 + WH-4b 新增 facade 级 ctest;fixture 立即迁移;`--recovery-store` 端到端 CLI 证明随 WH-6 切换落地。零并行 CLI 路径。**

- **WH-4b 不加任何 CLI 路由**(无 `--runtime` flag、无 `run-wasm` 子命令)。CLI 在 WH-6 big-bang 前继续构造 evaluator(`workflow_run.cpp:1849`)。
- **facade 配置字段**(`WasmWorkflowRuntimeConfig`,`src/runtime/wasm_runner/wasm_workflow_runtime.hpp:36-43`)对齐 `WorkflowRuntimeConfig`(`workflow_runtime.hpp:52-77`)增加:`recovery_snapshot`、`recovery_store`(裸指针)、`resume_pending_result`(native Value,表面对齐)、`resume_pending_result_wire_json`、`durable_write_intent_sink`。会话配置透传前三 + intent sink。
- **Fixture big-bang 迁移(现在)**:
  - `tests/scripts/durable_resume_cli_smoke.py` 内嵌 `SOURCE`(`:52-86`):`Echo` 现在在非 final 的 `Init` handler 调用 = bridge 车道。迁为 opaque:`context: Unit`,`state Init { goto Done; }`,`state Done { return Echo(Request { value: input.value }); }`;workflow 返回 `Response { value: only.value }` 不变。evaluator smoke 保持绿(opaque 形状 evaluator 兼容)。
  - rich package(`:96-159`)已是 opaque(`DraftReply` 在 final `Done`),不动。
  - `examples/durable-resume/src/main.ahfl`:`DraftReply` 在非 final `Compose` handler = bridge。big-bang 迁为 `state Compose { goto Done; }`、`state Done { return DraftReply(DraftInput { id: input.id, question: input.question }); }`、`context: Unit`;`effect: durable_write` 声明、安全/活性规约、README 两步法不变。WH-6 若不改它,该示例在 wasm 上必坏(bridge Pending trap),故在 WH-4b 改。
- **`--recovery-store` 端到端 CLI 证明落在 WH-6**:CLI 切到 wasm facade 时,迁移后的 smoke 场景(2b/2c/2d/4)在不变标志契约下驱动 wasm 车道。WH-4b 的门是 facade 级 ctest。

**拒绝:** WH-4b 临时 CLI 路由(过渡性 coexistence,Principle 1 禁止);把 fixture 迁移推迟到 WH-6(切换 + 迁移 + CLI 证明挤在一片,且示例此前在 wasm 上是坏的)。

### 12.6.7 `--suspend-capability` 语义 + intent-log 对齐 + 失败矩阵

**`--suspend-capability`:** 最外层 force-Pending 包装器与 evaluator 同构地包在 contextual invoker 外(`workflow_run.cpp:1830-1847`):按 canonical 名匹配、返回 `CapabilityCallStatus::Pending`、最外层先见名。wasm 车道上它位于 facade(包 `WasmWorkflowRuntimeConfig::invoker`),WH-4b facade 测试直接安装。加载快照时**惰性**:memo/inject 路径在 import 边界供给,从不到达 invoker。

**Intent-log 对齐(已核实 evaluator 行为):** write-ahead intent 只在 live dispatch 路径触发(`workflow_runtime.cpp:1050-1058`),在 replay 分支之后——memo 命中在 `:902-910` 返回,consult 不到 intent sink。wasm 车道:① 会话盖戳 `invocation_context.idempotency_key`(同一 `compute_idempotency_key(workflow, node, ordinal, cap_id, arg_hash)`,`:274`,`:1032-1035`;workflow 索引取程序中稠密索引,同一程序跨运行稳定);② FACADE 用 intent-emitting 包装器包用户 invoker(镜像 `:1050-1058`:在 `ir::Program` 查能力 effect kind,盖戳时对 `DurableWrite`/`FinancialWrite` 发到 `durable_write_intent_sink`)。memo 命中不调用 invoker → 包装器不运行 → 零 intent 重发。CLI 在 WH-6 接 `--intent-log`(场景 4 证明挂起时恰好一条 intent、resume 时零条)。

**失败/UX 矩阵(evaluator 对齐):** 挂起无 `--recovery-store` → exit 1(`:1865-1868`);保存失败 → 降级 NodeFailed,exit 1(`:1593-1603`);挂起并持久化成功 → stderr note、stdout 干净报告、exit 0(`:1893-1894`);`--resume-pending-result` 畸形/重复键 → Phase A.3 定向诊断;快照损坏/id 不匹配/闭包失败 → 失败结果;memo cap_id/arg_hash 不匹配 → "durable resume replay diverged" fail-closed;注入结果缺失/类型错/身份不匹配 → fail-closed;重放分歧(未命中 pending ordinal 就完成)→ fail-closed;双重 resume → 重跑(memo+注入+前沿后 live),与 evaluator 同——JSON store 无 consume-once CAS(那是 D2b 前沿);bridge Pending → trap,exit 1。审计投影无 suspended 计数器,挂起经 `run.status` + 事件观察,不变。

### 12.6.8 ReadyForLive 的诚实裁决

**D1b `run_resume` driver 按设计阻塞 `ReadyForLive`,而 WH-4b 不使用该 driver——故 ReadyForLive 在范围内且可用。**

已核实:evaluator resume 在前沿下 memo 重放、前沿注入,然后**前沿之后全部 LIVE**——`workflow_runtime.cpp:1016` 注入后立即置 `node_memo.replaying = false`,`:1025+` 是正常 live dispatch(intent 触发、效果发生)。挂起后这些调用从未执行过,必须 live,语义正确。D1b controller **能决定** `ReadyForLive`(`core_wasm_resume_controller.hpp:256-260`,携带 call site + arg_hash + 解码参数),但 `run_resume` driver 对其 fail-closed(`core_wasm_resume_host.hpp:145`;`:30-31` 注释 "ReadyForLive / dedup arms -> fail closed";`:54-55` "this driver never invokes a live capability")。该 driver 的职责是零 live 调用(其 memo 字节来自机密 `IntegrityPayloadStore`,live 供给需要尚不存在的 D2b durable-effect authority + 幂等令牌去重)。

**WH-4b 会话级消费者是不受该职责约束的不同设计**:其 memo 字节来自已在手的 JSON 快照,前沿后路径就是会话既有 live invoker(全新运行的同一路径)。注入前沿后,后续 import 放行 `inner` → `wrapped_invoker` → contextual invoker,与非 resume 运行完全一致;ReadyForLive 不需要新机制,它就是"不拦截"。D1b driver 的阻塞前沿保持原样;解封它(`AwaitingLiveResult` 状态 + live-response API + D2b-4 去重)是 D2b durable-effect authority 前沿——关于崩溃/重开下 exactly-once live 效果的独立议题,与"前沿后能否运行"无关。

**范围含义:** WH-4b 支持一般 opaque 车道工作流的 suspend/resume——包括 pending 之后(挂起节点内或后续节点)还有能力调用的工作流。e3 fixture(pending 调用是唯一能力调用)证明 inject-to-completion;新 e4 fixture(§12.6.10/12)证明 memo-replay + 注入 + 前沿后完成。唯一硬限制是车道:仅 opaque(WireJson)。

### 12.6.9 HMAC 机制的命运(test-only FOUNDATION + 日落条款)

`CoreWasmResumeRecord` codec、`IntegrityPayloadStore`、D1b controller、`run_resume` 在 HEAD 零生产调用者(仅 `core_wasm_resume_wasm3_e2e.cpp` 与 F4/F5 测试)。WH-4b 不新增对它们的依赖。它们是已落地的 D2b 基底:机密 payload store + HMAC record + decision-only controller 正是 D2b durable-effect authority(幂等令牌去重、机密静态结果、consume-once CAS)的构建件,WH-3 wasm3 引擎端口是其生产引擎适配器。

**日落条款(约束性):** D2b 切片(WH-6 切换后的紧邻 durable-resume 前沿)必须 (a) 接入 `run_resume` 的第一个生产调用者(durable-effect authority 下供给 live 效果的 D2b host),或 (b) 在一个 big-bang 变更中删除 record codec、payload store、controller、driver 及其专属测试。没有第三选项。

### 12.6.10 Fork 6 — agent 车道:不在 WH-4b 范围

**仅 workflow 车道。** `WasmAgentRunner` 的 run2 Pending 臂(`src/runtime/wasm_host/wasm_agent_runner.cpp:544,560-564` 非零 → `NodeFailed`)保持不变并加诚实注释:agent 车道挂起不是 CLI 契约(CLI 跑 workflow;`--recovery-store` 驱动 `WorkflowRuntime`),agent runner 没有 recovery-snapshot 消费者,conformance 普查有零个 pending mock(无对齐压力)。evaluator 的 agent suspend(`agent_runtime.cpp:172-176`)服务 REPL/DAP agent-runner 契约,那是 WH-7/WH-8 的领地,由它们决定 agent 车道挂起。

### 12.6.11 WASM=OFF

WH-4b 全部新增位于 gated 目标内:会话 memo 层在 `ahfl_runtime_wasm_host`,facade 字段在 `ahfl_runtime_wasm_runner`,新 ctest 在 `if(AHFL_ENABLE_BACKEND_WASM)` 内。WH-4b 不改 CLI(CLI `#ifdef` 分叉由 WH-6 决定)。smoke fixture 迁移是源码级、引擎中立的(evaluator smoke 在 WASM=OFF 下保持绿)。

### 12.6.12 有序工作清单

1. `workflow_recovery.{hpp,cpp}`:`CapabilityMemoEntry` 加 `std::optional<WorkflowNodeId> node`;序列化/解析(evaluator 条目写出时缺省;读入缺省 → nullopt);更新 three-state 注释。v2 append-only,不改 schema 字符串。
2. 新 `src/runtime/wasm_host/wasm_resume_recorder.{hpp,cpp}`:逐节点计数器、memo 向量、pending 坐标、当前 import 关联。
3. `workflow_session.{hpp,cpp}`:`WorkflowSessionConfig` 加 recovery/intent 透传;`wrapped_callback` 经 `symbol_to_schedule` 导出 `(node, ordinal)`、驱动 recorder、resume 时按 §12.6.5 供给 memo/inject/live;run2 臂加 `raw_status == 2` → Suspended(构造快照、`recovery_store` 保存、失败降级);finalize 加 Suspended 终止臂;盖 idempotency_key。
4. `wasm_lifecycle.{hpp,cpp}`:`WasmWorkflowRunFacts` 加 Suspended 路径;发 NodeSuspended/WorkflowSuspended/RunCompleted{Suspended}。
5. `wasm_workflow_runtime.{hpp,cpp}`:config 加五个 recovery/intent 字段;`run()` 透传;用 intent-emitting 包装器包用户 invoker(镜像 `workflow_runtime.cpp:1050-1058`,effect kind 在 `ir::Program` 查)。
6. 新 `tests/golden/wasm/e4_capability_workflow_resume_memo.ahfl`:2 节点 opaque 流水线——`first` 调 `A`(OK,被 memo),`second` 调 `B`(经计数包装器 Pending);有界 `String(0,64)`;`context: Unit`;两个调用都在 final state。
7. 新 `tests/integration/wasm_workflow_resume_e2e.cpp`(TestTargets.cmake WASM gate 注册):facade 级往返家族(§12.6.13)。
8. `tests/scripts/durable_resume_cli_smoke.py`:SOURCE 迁 opaque;rich package 不动。
9. `examples/durable-resume/src/main.ahfl`:DraftReply 迁 final `Done`,`context: Unit`;README 不变。
10. 本决策节 + RFC 0026 Decision History 条目。

### 12.6.13 验收 / 验证标准

1. **e3 往返**(`e3_capability_workflow_resume.ahfl`):计数 invoker;强制 Echo Pending → `Suspended` + 快照存入临时 `WorkflowRecoveryStore` + `workflow_completed == 0`;以注入 wire JSON resume → `Completed`,输出等于注入结果,计数 invoker 记录**零次 live 调用**。
2. **e4 memo 往返**:`A` OK 然后 `B` 强制 Pending → Suspended,memo 含 `(first, 0, A, …)`;resume → Completed,`A` 零 live(memo 供给)、`B` 零 live(注入)——证明 memo ordinal 零 live + 恰好一次注入;若 e4 有前沿后调用,它恰好 live 一次。
3. **Fail-closed 家族**:损坏快照 JSON;workflow-id 不匹配;挂起节点不在 descriptor;memo cap_id 不匹配;memo arg_hash 不匹配;注入结果缺失;注入 wire 类型错;pending 身份不匹配;重放分歧(快照的 pending 调用在未被走取的分支上)——各自产出带引擎中立诊断的失败 `WorkflowResult`,绝不静默新跑或 live 重调。
4. **Intent 对齐**:e4 resume 时 intent sink 记录零条(memo 的 `A` 与注入的 `B` 从不到达 invoker);挂起运行对 live 的 `A`(durable_write)恰好一条。
5. **Smoke 迁移**:迁移后 evaluator CLI 上 `durable_resume_cli_smoke.py` 场景 1/2/2b/3/3b 绿;rich package 2c/2d 不变绿。
6. **示例**:`examples/durable-resume` 迁移后 evaluator CLI 场景 4 往返(挂起一条 intent、resume 零条)。
7. **WASM=OFF**:configure + build 干净(facade 与新 ctest 缺席;evaluator smoke 绿)。
8. **ASan**:新 ctest + wasm 阶梯绿。
9. **全量 ctest** 绿,含既有 `ahfl_core_wasm_resume_wasm3_e2e`(HMAC FOUNDATION 证据不动)。
10. **零新增 HMAC 依赖**:WH-4b diff 中改动的 wasm_host/wasm_runner TU 对 `core_wasm_resume_host` / `core_wasm_resume_controller` / `payload_store` 的 include 为零。

### 12.6.14 诚实声明边界 — WH-4b 后仍未证明 / 推迟的事项

- CLI 端到端 wasm suspend/resume 随 WH-6 切换落地;WH-4b 仅在 facade 级证明。
- Bridge 车道 suspend/resume 不支持(bridge Pending 按密封 ABI trap)。
- 跨引擎 resume 不支持且 fail-closed;WH-6 前 evaluator 快照不能在 wasm 上 resume。
- Consume-once / 崩溃重开下 exactly-once(`IntegrityPayloadStore` generation CAS、幂等令牌去重、机密静态结果)是 D2b 前沿;JSON store 无 consume-once CAS(双重 resume 重跑,与 evaluator 今天一致)。
- Agent 车道挂起属 WH-7/WH-8。
- HMAC 机制的生产命运由 §12.6.9 日落在 D2b 绑定。

## 12.7 WH-6 decisions (2026-09-30, dedicated decision agent, no human gate)

WH-6 把 `ahflc run` 的唯一生产构造点(`src/runtime/cli/workflow_run.cpp:1849-1850`)从 evaluator-backed `WorkflowRuntime` 切到 WH-4 `WasmWorkflowRuntime` facade。prep map(`$CLAUDE_JOB_DIR/tmp/wh6-prep-map.md`)的事实已对 HEAD `6b5374a5` 逐条复核。本节是一个连贯设计,不是菜单。WH-4b 的 facade recovery/intent 字段(§12.6.6)与 fixture 迁移是 WH-6 的硬前置;WH-5 原生 conformance 普查(66/0)是符合性地板。

### 12.7.0 决策摘要

**`ahflc run` 在 WASM=ON 下无条件切到 wasm facade;WASM=OFF 下 `run` 以可行动诊断拒绝(exit 1),其余编译/验证命令照常工作。** facade config 是**诚实子集**(非逐字段镜像):CLI 设的 4 个字段直接映射,hooks/name_resolver 留空,quota/non-contextual-invoker/native-host-binding/checkpoint 省略,cancellation/interruption/monotonic_clock 推迟到 WH-8。切换是一个 big-bang commit(include/using/ctor + CMake gated link);evaluator 目标保留到 WH-9(DAP 仍需)。LLM tool-calling 重入接受(smoke 门控)。挂起/恢复标志零改动。facade ctor 编译失败语义(与 WH-7 共享,§12.7.8):保留 ctor + run()-surfaces-failure 形状,把 `compile_error_` 从 `optional<string>` 升级为 `DiagnosticBag`(Principle 5)。

### 12.7.1 跨切决策 — WASM=OFF 产品策略(与 WH-7 联合;WH-8 继承)

**选择 option (a):构建工具与运行时动词;WASM=OFF 下运行时动词以可行动诊断拒绝,非运行时编译命令照常工作。evaluator fallback 永久禁止(Principle 1)。**

**终态(WH-9 后):** evaluator 在 WH-9 一次性 big-bang 删除。此后 WASM=OFF 不为 `run`/REPL eval/DAP launch 留下任何执行引擎。WASM=OFF 构建产出完整编译器/验证器/LSP(`ahflc check`/`compile`/`verify`/`emit`/`format` 等全部可用),但三个运行时动词拒绝:

| 动词 | WASM=OFF 行为 | 退出码 / 输出 |
|---|---|---|
| `ahflc run` | stderr 诊断 + 拒绝 | exit 1 |
| `ahfl-repl` eval | handler 返回 Error 字符串 | REPL 继续 |
| `ahfl-dap` launch/execute | DAP error 事件 | WH-8 继承 |

**精确诊断(CLI):**
```
error: ahflc run requires the embedded wasm engine; this build was configured with -DAHFL_ENABLE_BACKEND_WASM=OFF. Rebuild with the default (ON) to run workflows.
```
退出码 1(`ExitCode::CompileError`,`cli_driver.cpp:3987` 把 `run_workflow_with_llm` 非零映射为 CompileError)。无诊断 code(这是构建配置拒绝,不是编译器诊断);消息命名重建 flag,可行动。

**门控位置(组合边缘,零业务逻辑 #ifdef):** CMake 把 `AHFL_ENABLE_BACKEND_WASM=1` 作为 compile-definition 传播给 `ahflc`(WASM=ON 时)。`workflow_run.cpp` 的 `run_workflow_with_llm` 函数体整体在 `#ifdef AHFL_ENABLE_BACKEND_WASM` ... `#else` stub(打印诊断 + return 1)... `#endif` 内。函数签名、`cli_driver.cpp:3986` 的 dispatch、usage 校验(在 gate 内)不变。业务逻辑(LLM 配置、能力绑定、输入解码、恢复存储、intent sink、suspend 包装器)全部在 gate 内,零 #ifdef 散落。`ahflc` 目标始终链接(无 target 缺席导致的链接断裂);WASM=OFF 下函数是 stub,满足 §8 acceptance 4(两矩阵 CONFIGURE+BUILD)。

**过渡期(WH-6..WH-8):** evaluator 仍链接(DAP 直到 WH-8;`ahfl_runtime_engine` PUBLIC 边 `src/runtime/engine/CMakeLists.txt:47` 保留)。WH-6 后 `ahflc run` 是 wasm-only;evaluator 不再是 `run` 的 fallback。WASM=OFF 下 `run` 立即拒绝(stub),不触碰 LLM/能力路径。

**REPL 因子:** `ahfl-repl` 今天不作为 RUNTIME 安装(`cmake/modules/AhflInstall.cmake:57-61` 仅安装 `ahflc` + `ahfl-lsp`;`ahfl_tooling_repl` 在 INTERNAL 安装集 `:118`,仅 `AHFL_INSTALL_INTERNAL_TARGETS=ON`)。故 WASM=OFF 下 `ahfl-repl` 的 eval 拒绝是开发工具便利,不是部署契约——但策略一致(构建 + eval 拒绝),因为 §8 #4 要求两矩阵 CONFIGURE+BUILD,且 REPL 的 `:type`/`:verify`/`:simulate`/`:help` 在 WASM=OFF 下必须可用(它们不依赖执行引擎)。

**DAP(WH-8)继承同一策略:** `ahfl-dap` 也不安装;WH-8 切换后 WASM=OFF 下 launch/execute 拒绝(DAP error 事件),断点/栈/变量等非执行特性照常。

**拒绝:**
- **option (b)(WASM=OFF 下不构建 run-capable 二进制 / ahfl-repl)。** 击败证据:① §8 acceptance 4 明文要求 `AHFL_ENABLE_BACKEND_WASM` ON/OFF 两矩阵都 CONFIGURE+BUILD——`ahflc` 是安装的 RUNTIME 工具,WASM=OFF 下链接断裂直接违反;② `ahflc` 的 `check`/`compile`/`verify` 等命令与 `run` 同一二进制,不构建 `ahflc` 等于 WASM=OFF 下无编译器——不可接受;③ REPL 虽不安装,但其 `:type`/`:verify` 是编译器前端消费者,不应因 eval 缺席而整体不构建。
- **option (c)(evaluator fallback)。** 永久禁止——Principle 1 禁止 coexistence/parallel path;evaluator 在 WH-9 删除,fallback 会无限期延长其生命;引擎决策(2026-09-30,RFC 0026 History)明文 "no engine-select flag ever"。

**AHFL-specific 理由:** AHFL 的北极星是可嵌入的可验证 agent-workflow DSL(RFC 0020);执行引擎是 wasm3 单一引擎(引擎决策 2026-09-30:wasm3 走完 WH-9,无 engine-select flag)。WASM=OFF 是"只要编译器"的构建配置,不是"另一个引擎"的配置。运行时动词在 WASM=OFF 下拒绝是诚实的:该构建不含执行引擎,不应假装含。参照层级一致:rustc 的 `--emit=metadata` 产出编译器产物但不运行;一个不含 codegen 的 rustc 不能 `cargo run`。

### 12.7.2 Facade config 形状 — 诚实子集,非逐字段镜像(fork 2)

**选择:诚实子集。** `WasmWorkflowRuntimeConfig`(HEAD `src/runtime/wasm_runner/wasm_workflow_runtime.hpp:36-43` 有 `hooks`/`invoker`/`name_resolver`;WH-4b 按 §12.6.6 加 `recovery_snapshot`/`recovery_store`/`resume_pending_result`/`resume_pending_result_wire_json`/`durable_write_intent_sink`)携带 CLI/DAP 实际使用的字段,不携带 evaluator-only 概念。

逐字段裁决(evaluator `WorkflowRuntimeConfig` 全表面,`workflow_runtime.hpp:38-114`):

| evaluator 字段 | facade 裁决 | 理由 |
|---|---|---|
| `contextual_capability_invoker` | **映射为 `invoker`** | CLI 唯一设的 invoker;wasm 车道的 dispatch seam |
| `recovery_snapshot` | **映射(WH-4b)** | CLI `:1789` 设 |
| `recovery_store` | **映射(WH-4b)** | CLI `:1785-1791` 本地 optional,取裸指针 |
| `resume_pending_result_wire_json` | **映射(WH-4b)** | CLI `:1796` 设 |
| `durable_write_intent_sink` | **映射(WH-4b)** | CLI `:1813` 设 |
| `resume_pending_result`(native Value) | **映射(WH-4b,表面对齐)** | CLI 不设(只用 wire_json 形式);facade 保留供其他 host |
| `state_entered_hook` | **在 `WasmRuntimeHooks` 内** | DAP WH-8 接线;CLI 留空 |
| `capability_invoked_hook` | **在 `WasmRuntimeHooks` 内** | 同上 |
| `agent_input_hook` | **在 `WasmRuntimeHooks` 内** | 同上 |
| `node_completed_hook` | **在 `WasmRuntimeHooks` 内** | 同上 |
| `capability_result_observer` | **在 `WasmRuntimeHooks` 内** | 同上 |
| `name_resolver` | **保留(optional),CLI 不设** | 会话从 descriptor 建 fallback(`workflow_session.cpp:398-403`);CLI 的 invoker 链按 canonical 名匹配,fallback 产生的 canonical 名与 codegen descriptor 一致 |
| `cancellation_requested` | **推迟到 WH-8** | DAP 需要;CLI 不设(同步运行,无取消) |
| `interruption_requested` | **推迟到 WH-8** | 同上;wasm 车道在 import 边界检查 |
| `monotonic_clock` | **推迟到 WH-8** | DAP 时序;CLI 不设 |
| `default_agent_quota` | **省略** | evaluator 的 host 侧步数配额;wasm 车道无 host 侧配额(wasm3 跑模块,受固定 64KiB 页 + 有界递归约束)。无 wasm 等价物 |
| `capability_invoker`(non-contextual) | **省略** | evaluator 时代无上下文 invoker;wasm 车道的 import 回调总有上下文,只用 contextual 形式 |
| `native_host_binding` | **省略** | RFC 0021 slice 2 的 C ABI 函数指针表,服务 evaluator 嵌入;wasm 车道的 host binding 就是 contextual invoker(std::function seam)。C ABI 表是 evaluator 时代嵌入,WH-9 随 evaluator 删除 |
| `resume_checkpoint` / `checkpoint_after_node` | **省略** | evaluator 时代检查点机制;wasm 车道的 resume 是 `recovery_snapshot`(WH-4b)。无 wasm 等价物 |

**拒绝逐字段镜像:** 镜像会携带 evaluator-only 概念(quota、non-contextual invoker、C ABI binding、checkpoint),它们在 wasm 车道无意义——一个对引擎撒谎的 config(Principle 1:目录/结构诚实)。D-A 已裁决 facade 在独立 peer 目标;config 形状应反映 wasm 车道的真实表面,不是 evaluator 表面的回声。

**切换的类型兼容性:** CLI 把 `runtime_config` 从 `WorkflowRuntimeConfig` 改为 `WasmWorkflowRuntimeConfig`,设 4 个字段(`invoker` = 原 `contextual_capability_invoker` 链;`recovery_snapshot`;`recovery_store` = `recovery_store.has_value() ? &*recovery_store : nullptr`;`resume_pending_result_wire_json`;`durable_write_intent_sink`),`hooks`/`name_resolver` 留空。suspend 包装器(`:1830-1847`)包 `config.invoker`(原 `config.contextual_capability_invoker`);`with_standard_capabilities`(`:1778`)同理。字段语义不变,仅结构体/字段名变更。

### 12.7.3 Big-bang 切换面 + 链接边编排(WH-6 → WH-7 → WH-8 → WH-9)

**WH-6 切换 diff 面(一个 commit):**

1. `src/runtime/cli/workflow_run.cpp`:
   - `:13` include `runtime/engine/workflow_runtime.hpp` → `runtime/wasm_runner/wasm_workflow_runtime.hpp`
   - `:72-73` using-decls `WorkflowRuntime`/`WorkflowRuntimeConfig` → `wasm_runner::WasmWorkflowRuntime`/`WasmWorkflowRuntimeConfig`
   - `:1849-1850` 构造 + run:类型替换(§12.7.2 字段映射)
   - 函数体整体 `#ifdef AHFL_ENABLE_BACKEND_WASM` gate(§12.7.1)
2. `src/runtime/cli/CMakeLists.txt`:`ahflc` 在 `if(AHFL_ENABLE_BACKEND_WASM)` 内链接 `ahfl_runtime_wasm_runner` + `target_compile_definitions(ahflc PRIVATE AHFL_ENABLE_BACKEND_WASM=1)`。
3. 无 evaluator TU 删除(保留到 WH-9)。

**链接边编排:**

| 切片 | `ahflc` 链接变化 | `ahfl_tooling_repl` | `ahfl_tooling_dap` | `ahfl_runtime_engine` evaluator 边 |
|---|---|---|---|---|
| WH-6 | +gated `ahfl_runtime_wasm_runner`;evaluator 经 `ahfl_runtime` bundle 保留 | 不变(仍 DIRECT 链接 evaluator) | 不变 | PUBLIC 保留(DAP `debug_session.hpp:205` 仍 include `workflow_runtime.hpp`) |
| WH-7 | 不变 | **DROP direct evaluator**;+gated `ahfl_runtime_wasm_runner` | 不变 | PUBLIC 保留 |
| WH-8 | 不变 | 不变 | 翻转为 `WasmWorkflowRuntime`(hooks 经 `WasmRuntimeHooks` 同签名) | **PUBLIC → PRIVATE**(无外部生产 includer;`workflow_runtime.hpp` 仅 engine 内部 + 测试 include) |
| WH-9 | bundle 去 evaluator | 传递依赖消失 | 传递依赖消失 | **删除** evaluator 目标 + `WorkflowRuntime`/`agent_runtime`/`capability_eval` + 专用测试/CMake 边,一个原子 big-bang + `BREAKING CHANGE:` |

**关键事实(HEAD 复核):** 生产 TU 中 evaluator 的直接构造点仅两个——`workflow_run.cpp:1849`(WH-6)与 `repl.cpp:297-298`(WH-7);DAP 持有 `unique_ptr<WorkflowRuntime>`(`debug_session.hpp:205`,构造于 `debug_session.cpp:291`,WH-8)。`ahfl_runtime_engine` PUBLIC 链接 evaluator(`src/runtime/engine/CMakeLists.txt:47`)因 `workflow_runtime.hpp:20-21` include `evaluator.hpp`;WH-8 后无外部生产 includer,边可降 PRIVATE;WH-9 删除。`ahfl_runtime_wasm_runner` PUBLIC 链接 `ahfl_runtime_wasm_host` + `ahfl_runtime_engine`(`src/runtime/wasm_runner/CMakeLists.txt:21-27`),故 WASM=ON 下 evaluator 经 engine 传递链接到 WH-9——这是预期的(evaluator 仍存活供 DAP),不是泄漏。

### 12.7.4 退出码 / 报告 / 错误对齐

**`render_execution_result` 原样复用**(中立 `WorkflowResult`,`src/runtime/engine/execution_renderer.cpp`;WH-4 D-E 已把 `workflow_result.hpp` 移 src-internal,渲染器只消费中立字段)。

**状态臂映射(evaluator → wasm):**

| evaluator `WorkflowStatus` | wasm 车道 | 备注 |
|---|---|---|
| `Completed` | `Completed` | 会话 finalize 设 `RunTerminalStatus::Completed` |
| `NodeFailed` | `NodeFailed` | trap/host-abort/non-OK → `NodeFailed` + `wasm.trap`/`wasm.host-abort`/`wasm.run-failed` 诊断(D-D) |
| `DependencyFailed` | **不产生** | wasm 车道只报根因节点失败(`NodeFailed`);下游节点不标记 DependencyFailed(它们未运行)。接受的语义收窄:wasm 诚实报告根因,不标记未运行的下游。CLI 退出码不变(非 Completed 即 1) |
| `EvalError` | **不产生** | wasm 车道无 evaluator;`EvalError` 枚举保留到 WH-9(`workflow_result.hpp:48` 注释明文) |
| `Suspended` | `Suspended`(WH-4b) | 会话 `raw_status==2` 臂(§12.6.4);`result.suspended` 填充 |

**编译错误(facade ctor):** CLI 在 `cli_driver.cpp:3958-3963` 已把源码 lower 到 IR(parse/resolve/typecheck 错误更早以 SourceRange 诊断 + exit 1 暴露)。facade ctor 的编译是 IR→Core→wasm(`lower_ahfl_to_core` → `compute_core_layouts` → `emit_core_wasm`),其失败是 lowering/codegen 错误(如 wasm 不支持的构造),携带 Core IR SourceRange。按 §12.7.8 裁决,ctor 把诊断存入 `DiagnosticBag`,run() 返回带该 bag 的失败 `WorkflowResult`;CLI 经 `:1888-1890` `diagnostics.render` 渲染(SourceRange 保留,Principle 5),exit 1。evaluator ctor 假设已编译 Program、不失败——wasm ctor 可能失败,但失败在 run() 诚实暴露(与 evaluator 的 run()-surfaces-failure 形状对齐)。

**compile-all 语义:** facade ctor 编译程序中**所有** workflow(`wasm_workflow_runtime.cpp:47-70` 循环)。若 sibling workflow 用了 wasm 不支持的构造,整个 run 失败(诊断命名失败的 workflow)。接受:① 程序是一个编译单元,typechecker 已拒绝坏掉的 sibling,wasm codegen 子集更窄,诚实报告 codegen 失败而非静默跳过;② per-entry 过滤(只编译目标)是隐藏 sibling 失败的并行路径,不诚实。参照 Rustc(编译整个 crate)。

**`ahfl.run-report` v1 JSON 字节对齐:** 事件/元数据/审计存储由会话填充(D-D P1-5);WH-5 普查钉死观察对齐。WH-6 用 `ahfl.run-report` JSON 测试验证字节对齐(risk #2)。

### 12.7.5 LLM 重入 — 接受,smoke 门控

**接受,无设计变更。** LLM tool-calling 循环(`src/runtime/providers/llm/llm_capability_provider.cpp:1303` 同步 round 循环;tool_executor `std::function` `:171` 经 `install_runtime_tools` `workflow_run.cpp:1506-1548` 安装)在 contextual invoker 内运行。wasm 车道上 invoker 从 `m3_Call` 的 import 回调内调用——阻塞 HTTP + tool registry 分发在 wasm3 解释器栈上同步执行。

**约束(明文记录):** contextual invoker **不得重入同一 wasm3 runtime 实例**(`m3_Call` 在同一 `IM3Runtime` 上不可重入)。LLM/tool 路径是纯宿主侧(HTTP/gRPC 能力、tool registry),不触碰 wasm 实例——满足约束。tool registry 调用的是宿主能力(`make_http_capability`/`make_grpc_json_transcoding_capability`),不是 wasm。

**门控:** `llm_provider_runtime.smoke`(mock HTTP LLM + tool 往返)是重入门。若 smoke 暴露 wasm3 重入问题,再开决策门;当前不预判。

### 12.7.6 测试门控顺序 + 切换 commit 形状

**门控顺序(逐级解锁):**

1. **WH-4b 落地**(facade recovery/intent 字段 + fixture 迁移到 opaque 车道)——硬前置。
2. **WH-5 原生普查 66/0**(`ahfl.conformance.wasm_native_differential`,`ProjectTests.cmake:1352+`)——符合性地板。
3. `ahflc.run.manifest.entry_workflow_default`(`ProjectTests.cmake:963-972`,human regex `ir::workflow_value_flow::ValueFlowWorkflow  completed`,两空格,`execution_renderer.cpp:427`)+ `ahflc.run.default_manifest.entry_workflow_default`(`:974-981`)——基本 run。
4. `durable_resume_flags.smoke`——硬 suspend/resume 往返(WH-4b 后 opaque fixture;钉 `audit.workflow_completed` 0/1 + `result.value "resumed-ok"`,`durable_resume_cli_smoke.py:234-274`)。
5. `llm_provider_runtime.smoke`——LLM 重入(§12.7.5)。
6. `capability_bindings`——能力跨 wasm 边界。
7. `profile_and_output_contract`——输出契约。
8. `llm_failure_matrix`——失败映射。
9. `fail_*` 预运行时测试(`SingleFileCliTests.cmake:353-471`:LLM secret/budget/vault/mock/input-schema)——不受引擎切换影响,保持绿。
10. `ahfl.run-report` v1 JSON——字节对齐(§12.7.4)。
11. 全量 ctest + ASan + `-Werror` WASM=ON;WASM=OFF configure+build(§12.7.1)。

**切换 commit 形状:** 一个 big-bang flip + 测试更新。evaluator TU 保留到 WH-9。无并行 run 路径,无 flag。commit 内含:workflow_run.cpp(include/using/ctor/gate)+ cli/CMakeLists.txt(gated link + compile-def)+ §12.7.8 facade DiagnosticBag 升级 + 本决策节 + RFC 0026 Decision History 条目。

### 12.7.7 挂起路径对齐 + `--wasm-profile` 裁决

**挂起/恢复标志零改动。** `--recovery-store`/`--resume-pending-result`/`--suspend-capability`/`--intent-log`(`workflow_run.cpp:1785-1847`)是引擎中立的:它们操作 contextual invoker 与 config 字段,不触碰引擎。WH-4b 后 facade 消费这些字段(§12.6.6);CLI 把它们映射到 `WasmWorkflowRuntimeConfig`(§12.7.2)。迁移后的 smoke 场景(2b/2c/2d/4)在不变标志契约下驱动 wasm 端到端。`--suspend-capability` 包装器(`:1830-1847`)包 `config.invoker`,在 wasm import 边界强制 Pending;会话观察 `raw_status==2` 挂起(§12.6.4)。`--intent-log` sink 经 facade 的 intent-emitting 包装器(§12.6.7)。

**`--wasm-profile` 裁决:不接线到 `run`;facade 硬编码 `WasmProfileKind::Wasi`。** 理由:① 内嵌 wasm3 宿主只承认 Wasi/内嵌 ABI 形状(引擎决策:模块仅导入 `ahfl_cap`,无 WASI env;browser profile 面向外部宿主,不能在内嵌 wasm3 上运行);② `--wasm-profile` 今天是 `emit wasm` 的选项(`option_table.cpp:293` 帮助文本 "WASM deployment profile for emit wasm";`cli_analysis_helpers.cpp:197-201` 仅 `emit wasm` 消费),`run` 从不消费(evaluator 不编译 wasm)——切换后 `run` 仍不消费,无行为变更;③ 接线它到 facade 会暗示 `run --wasm-profile browser` 有意义,但 browser 模块在内嵌宿主上不可部署——不诚实。facade config 不加 `profile` 字段(YAGNI,诚实子集)。

### 12.7.8 共享决策 — facade ctor 编译失败语义(与 WH-7 联合)

**选择:保留 ctor + run()-surfaces-failure 形状;把 `compile_error_` 从 `optional<string>` 升级为 `DiagnosticBag`。**

**现状(HEAD):** `WasmWorkflowRuntime` ctor 编译所有 workflow,失败时存 `std::optional<std::string> compile_error_`(`wasm_workflow_runtime.hpp:70`),run() 返回带 `wasm.compile-failed` + 拼接字符串的失败 `WorkflowResult`(`wasm_workflow_runtime.cpp:76-85`)。字符串拼接丢失 `CoreWasmDiagnostic.source_range`(`core_wasm_codegen.hpp:58-62` 携带 SourceRange)。

**升级(big-bang,现在,在 WH-6/WH-7 两个调用者存在之前):**
- `compile_error_` 改为 `DiagnosticBag`(或等价);ctor 把 lowering/layout/codegen 诊断(每条带 code + message + SourceRange)存入 bag。
- run() 的失败臂返回带该 bag 的 `WorkflowResult`(不再拼接字符串)。
- CLI 经 `:1888-1890` `diagnostics.render` 渲染——SourceRange 保留(Principle 5)。

**`WasmAgentRunner` 保留 `expected<_, std::string>`。** REPL handler 契约是 string-in/string-out;字符串已拼接 `[code] message`;REPL 按 code 模式匹配产出不支持 UX(§12.8.2)。`DiagnosticBag` 会被 REPL 拍平成字符串,无收益。agent runner 的消费者是 REPL(WH-7)+ conformance 测试;string 是 one-shot runner 的诚实契约。

**拒绝工厂模式(`static expected<WasmWorkflowRuntime, DiagnosticBag> create(...)`):** 它偏离 facade 镜像的 evaluator 表面(evaluator ctor 不失败;失败在 run() 暴露)。失败已在 run() 诚实暴露,每个调用者都处理 run() 的失败结果(CLI:渲染 + exit 1;REPL:error 字符串)。工厂要求每个调用者重构(先查工厂再 run),更大 diff,无诚实收益。

**不对称的理由:** workflow facade 镜像 evaluator 的 `WorkflowRuntime`(ctor + run → `WorkflowResult` 带 `DiagnosticBag`),CLI 渲染 bag 的 SourceRange;agent runner 是 one-shot 自由函数,消费者拍平成字符串。不同消费者契约,不同诚实形状。

### 12.7.9 有序工作清单

1. `wasm_workflow_runtime.{hpp,cpp}`:`compile_error_` 升级为 `DiagnosticBag`(§12.7.8);run() 失败臂带 bag。
2. `workflow_run.cpp`:include/using/ctor 切换(§12.7.2 字段映射);函数体 `#ifdef` gate(§12.7.1);suspend/standard 包装器改包 `config.invoker`。
3. `src/runtime/cli/CMakeLists.txt`:gated `ahfl_runtime_wasm_runner` 链接 + `AHFL_ENABLE_BACKEND_WASM=1` compile-definition。
4. 测试:§12.7.6 门控顺序全绿;WASM=OFF configure+build;ASan。
5. 本决策节 + RFC 0026 Decision History 条目。

### 12.7.10 验收 / 验证标准

1. WASM=ON:`ahflc run` 经 wasm facade 运行;`entry_workflow_default` human "  completed" regex 绿;`durable_resume_flags.smoke` 场景 1/2/2b/2c/2d/3/3b/4 绿(opaque fixture)。
2. WASM=OFF:`ahflc run` 打印 §12.7.1 诊断 + exit 1;`ahflc check`/`compile`/`verify`/`emit` 等照常;configure+build 干净。
3. `llm_provider_runtime.smoke` 绿(LLM 重入)。
4. `ahfl.run-report` v1 JSON 字节对齐(event/metadata/audit 存储)。
5. facade ctor 编译失败:lowering/codegen 错误以 SourceRange 诊断渲染,exit 1(非拼接字符串)。
6. 零业务逻辑 #ifdef(gate 仅在 `run_workflow_with_llm` 函数体边界)。
7. ASan + `-Werror` 绿。
8. evaluator 目标/TU 保留(WH-9 删除);无 engine-select flag/env var。

### 12.7.11 拒绝的替代方案(按 fork)

- **fork 1 option (b)(WASM=OFF 不构建 run-capable 二进制):** 见 §12.7.1——违反 §8 #4 两矩阵构建 + `ahflc` 是安装的编译器工具。
- **fork 1 option (c)(evaluator fallback):** 永久禁止(Principle 1;引擎决策 no engine-select)。
- **fork 2 逐字段镜像:** 见 §12.7.2——携带 evaluator-only 概念,config 对引擎撒谎。
- **工厂 ctor:** 见 §12.7.8——偏离镜像表面,无诚实收益。
- **`--wasm-profile` 接线到 run:** 见 §12.7.7——browser 模块在内嵌宿主不可部署,接线不诚实。
- **per-entry 编译(只编译目标 workflow):** 见 §12.7.4——隐藏 sibling codegen 失败,不诚实。

## 12.8 WH-7 decisions (2026-09-30, dedicated decision agent, no human gate)

WH-7 把 REPL 的唯一 tree-walk 调用点(`src/tooling/repl/repl.cpp:297-298` `EvalContext ctx; eval_expr(...)`)替换为 compile-to-wasm + 内嵌求值合成 agent;`print_value` 存活;drop direct evaluator 链接。prep map(`$CLAUDE_JOB_DIR/tmp/wh7-prep-map.md`)的事实已对 HEAD `6b5374a5` 逐条复核。WH-7 门控于 WH-6 之后(阶梯 §6),依赖 WH-4 `WasmAgentRunner`。

### 12.8.0 决策摘要

**REPL eval 用源码级合成 agent 包装用户表达式(A1),经 `WasmAgentRunner` 编译 + 运行;不支持的种类由 codegen fail-closed 诊断直接暴露(B1),无预分类门、无 evaluator fallback。** 包装器是两态 agent(`Init → Done`,`return <expr>` 在 `Done`),匹配已证的 `e3_identity_workflow` agent 形状;输出类型经第一轮 const 包装 typecheck 推断 + `describe()` 拼写(语法验证可 round-trip)。P6/WireJson 车道由 codegen `frame_contract` 决定,REPL 不选。`print_value` 存活;闭包输出是 codegen 拒绝(非 print_value 情形)。WASM=OFF 继承 §12.7.1(eval 拒绝,非 eval 命令可用)。

### 12.8.1 Fork A — 包装器放置:源码级合成 agent(A1)

**选择 A1:源码级合成 agent 字符串,复用 parser/typechecker。**

**包装器形状(两态,匹配 `tests/golden/wasm/e3_identity_workflow.ahfl` 的 `FirstAgent`):**
```
agent __repl__ {
    input: Unit;
    context: Unit;
    output: <describe(inferred_type)>;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    transition Init -> Done;
}
flow for __repl__ {
    state Init { goto Done; }
    state Done { return <expr>; }
}
```

**求值流程:**
1. 第一轮:`const __repl_result__ = <input>;`(现状 `:272`)→ `run_pipeline` → typecheck + 推断类型(从 `typecheck_result.typed_program.expressions.back().type`,与 `:type` handler `:83-91` 同路径)。
2. `describe()` 拼写输出类型(§12.8.1 类型 round-trip 验证)。
3. 构造合成 agent 源码(上形状,`<expr>` = 用户原文)。
4. 第二轮:`run_pipeline` 合成 agent → `lower_program_ir` → `wasm_runner::run_wasm_agent(program, "__repl__", Value{UnitValue{}}, config)`。
5. `result.result.output()` → `print_value`(`:301` 存活)。

**主流先例:** Swift REPL 把表达式包进合成上下文;GHCi 绑 `it`;rustc `--extern`。REPL 已在 `:type`(`:76` `const __repl_expr__ = <input>`)与现状 eval(`:272`)做源码级包装——A1 是既有 REPL 模式的延续,不是新发明。

**诊断保真度(Principle 5):** 用户表达式的类型错误由第一轮 const 包装的 typechecker 捕获(用户原文在已知偏移),REPL 返回 `Error: <type error>`(现状行为,不回归)。codegen 拒绝(字符串拼接、f64 算术等)由第二轮 wasm 路径捕获(§12.8.2)。REPL 的 `run_pipeline` 把诊断拍平成字符串(`:40-67`,不渲染 SourceRange)——这是 REPL 既有行为,A1 不回归也不改善。合成 SourceRange 不丢失 REPL 当前有的保真度。

**不变量安全:** A2(IR 级注入 AgentDecl/FlowDecl)要求手工构造 agent/flow/const 的全部不变量(initial/final/transition/context/input/output 类型),是与 parser 并行的 IR 构造,会漂移。A1 复用 parser(agent/flow 不变量的 SSOT)。A3(Core 级注入)最接近 emitter 但最 opaque,跳过 typechecker——类型错误无法在用户原文上捕获。

**双解析成本:** 接受。REPL 输入小,parser 快;GHCi 也重解析。第一轮是类型推断(必须,因 grammar `outputDecl` 强制显式类型,`grammar/AHFL.g4:219`);第二轮是 codegen。第一轮成功则第二轮必成功(同一表达式,同一类型规则)。

**REPL 无状态:** 确认(`repl.cpp` 每输入 fresh `EvalContext` + fresh pipeline;仅 `history_` 持久)。one-shot wasm 模型精确匹配;无状态累积。

**类型 round-trip 验证(已对 HEAD grammar 核实):** `describe()`(`include/ahfl/compiler/semantics/types.hpp:232-330`)产出:`Unit`/`Bool`/`Int`/`String`/`Float` → `primitiveType`;`Int(lo,hi)`/`String(lo,hi)`/`Decimal(scale)` → `primitiveType` 带界;`Request`/`module::Request` → `qualifiedIdent`;`Option<Int>`/`List<Int>`/`Set<Int>`/`Tuple<...>` → `qualifiedIdent<type_>`;`List<Int>(16)` → `qualifiedIdent<type_>(INT)`。grammar `type_`(`AHFL.g4:89-93`)= `primitiveType | fnType | qualifiedIdent ('<' type_ (',' type_)* '>')? collectionCapacity?`。P6 子集(Unit/None/Bool/Int/String/Struct/Option/Enum/List/Set/Tuple)的 `describe()` 拼写全部 round-trip。闭包类型(`Fn(...) -> ...`)可解析但 codegen 拒绝闭包跨 fn 边界(§12.8.2)——在 wasm 路径捕获,不在拼写阶段。

**拒绝:**
- **A2(IR 级注入):** 手工 IR 构造与 parser 并行,不变量漂移风险;类型错误的 SourceRange 需手工映射回用户原文(易错)。
- **A3(Core 级注入):** 跳过 typechecker,类型错误无法在用户原文上捕获(违反 Principle 5);最 opaque。
- **单态零过渡 agent(`states: [Done]; initial: Done; final: [Done]`):** 拒绝作为主形状——golden fixture 中无此形状先例(全部两态+),codegen planner 对零过渡 agent 的支持未证;两态形状(`Init → Done`)是 `e3_identity_workflow`/`e2_capability_agent` 已证形状,风险最低。实现时若探针证明零过渡可行,可简化,但不在本决策预设。

### 12.8.2 Fork B — 不支持种类:可行动 codegen 诊断(B1),无预分类门

**选择 B1:让 codegen 说话。** REPL 捕获 `run_wasm_agent` 的 `unexpected(string)` 错误,字符串已拼接 `[code] message`;REPL 打印可行动消息,不 trap 字符串,不 fallback。

**今天 wasm 上工作的(P6 车道):** Unit/None/Bool/Int/String/Struct/Option/Enum/List/Set/Tuple。

**codegen fail-closed 拒绝的(HEAD file:line):**
- f64 算术:`core_wasm_codegen.cpp:1992`(`p6_scalar_kind` 对 F64 返回 nullopt)
- 字符串拼接/比较:`:7851-7857`(`emit_binary` 拒绝 `P6ScalarKind::String`:"binary arithmetic/comparison is defined for Int/Bool only on the P6 frame lane")
- 无界集合:`:2009`(`p6_container_layout` 返回 null → `core.layout.UNBOUNDED`)
- Map/Decimal/Duration/Timestamp/Uuid 帧走:`frame_packer.cpp:470-472`(`ValueNotWireEncodable`)
- 闭包跨 fn 边界:`core_wasm_codegen.cpp:4188`(call argument)/`:4363`(call result)/`:4288`(capture);`frame_packer.cpp:140-141`(packer 拒绝闭包)
- f64/多词跨 fn 边界:同 `:4188`/`:4363`

**UX 方向(精确措辞由实现定,方向约束):** REPL 打印 `Error: ` + codegen 诊断消息(已具体,如 "binary arithmetic/comparison is defined for Int/Bool only on the P6 frame lane"),可选稳定后缀指向跟踪 KR(如 "; see RFC 0026 KR6.8 (wasm-gc post-WH-9)")。不硬编码 kind 名(codegen 消息已命名构造)。不暴露 wasm3 trap 字符串。

**拒绝 B2(预分类 eligibility 门):** ① `wasm_eligibility` 分类器在 `tests/conformance/`(测试侧机械),拖入生产工具会反转分层(测试 conformance → 生产);② 其自身头注释明文 "never a second, parallel re-derivation of the codegen subset predicates — CLAUDE.md forbids a second SSOT"——REPL 预分类门正是第二个 SSOT;③ codegen 已是 fail-closed 的权威,分类器会漂移。

**拒绝 B3(evaluator fallback):** 永久禁止(Principle 1;引擎决策 no engine-select)。

### 12.8.3 车道选择 — codegen `frame_contract` 决定,REPL 不选

P6 vs WireJson 由 codegen 的 `CoreWasmFrameContract`(`core_wasm_codegen.hpp:195`)决定,descriptor 携带。`wasm_host::run_wasm_agent` 按车道驱动(runv for P6,run2 for WireJson)。REPL 不选择、不感知车道。

**零能力 agent 的 invoker:** REPL 传 fail-closed noop invoker(若被调用则返回错误——但零能力 agent 无 import,永不调用)。`wasm_agent_runner.cpp` 的无导入路径不调用 invoker(import 回调仅在模块有 import 时触发)。合成 agent 无 `capabilities:` 声明 → 无 import → invoker 是防御性的,永不触达。

**字符串/struct 往返:** WireJson 车道(零能力 agent 的默认)经 `value_from_json` 解码输出(`wasm_agent_runner.cpp:447` 区域);String rodata 经 Data 段(已证,e2e/`v2b_string`)。REPL 的 `print_value` 渲染解码后的 Value。

### 12.8.4 Fork D — 输出:`print_value` 存活

**`print_value` 存活(阶梯 D1)。** `WasmAgentRunResult.result.output()` 给 Value;REPL 经 `runtime::print_value`(`src/runtime/value/value.hpp:252`)渲染。

**闭包输出:** wasm 车道不能产生闭包输出(codegen 拒绝闭包跨 fn 边界,§12.8.2)——在运行前捕获,不是 `print_value` 情形。`print_value` 的闭包拼写(`<lambda/id>`,`value.cpp:543-546`)在 wasm 车道永不触达。

**不支持的输出种类(Float 等):** 同 §12.8.2——codegen 在发射前拒绝,不产生输出。行为与 B 一致。

**拒绝 `value_to_json`:** 用户可见格式变更(REPL 今天用 `print_value` 的人类拼写);无收益。

### 12.8.5 Big-bang 面 + WASM=OFF(继承 §12.7.1)

**repl.cpp:**
- `:8` 删 `#include "runtime/evaluator/evaluator.hpp"`。
- `:3-7` 加:`ahfl/compiler/ir/core_ir.hpp`、`ahfl/compiler/ir/core_layout.hpp`、`compiler/backends/wasm/core_wasm_codegen.hpp`、`runtime/wasm_runner/wasm_agent_runner.hpp`、`runtime/engine/capability_bridge.hpp`(Value/UnitValue 已在 `value.hpp`)。
- `:270-307` `default_eval_handler` 重写:§12.8.1 流程;`#ifdef AHFL_ENABLE_BACKEND_WASM` 门(组合边缘)。
- `:280-285` 声明 fallback(declaration 输入 → `print_program_ir`)存活。
- `:type`/`:verify`/`:simulate`/`:help` handler 不动。

**CMakeLists.txt:**
- 删 `ahfl_runtime_evaluator` PUBLIC 链接(`:7`)。
- 加 `ahfl_compiler`(frontend/ir/semantics bundle)+ `ahfl_verification_formal` + `ahfl_runtime_value`(print_value;今天经 evaluator 传递,drop 后需显式)。
- `if(AHFL_ENABLE_BACKEND_WASM)`:加 `ahfl_runtime_wasm_runner` + `target_compile_definitions(ahfl_tooling_repl PUBLIC AHFL_ENABLE_BACKEND_WASM=1)`。
- `ahfl-repl` exe 不变(链 `ahfl_tooling_repl`)。

**WASM=OFF(继承 §12.7.1):** `ahfl-repl` 始终构建(不 gated)。eval handler 的 `#else` 臂返回:
```
Error: evaluation requires the embedded wasm engine; this build was configured with -DAHFL_ENABLE_BACKEND_WASM=OFF. Rebuild with the default (ON) to evaluate expressions.
```
非 eval 命令(`:type`/`:verify`/`:simulate`/`:help`/`:quit`)可用(它们只依赖编译器前端 + verification,不依赖执行引擎)。eval handler 是唯一的 `#ifdef` 站点(组合边缘);wasm eval 逻辑在 gate 内,零 #ifdef 散落。

**诚实裁决(无 wasm_runner target 时 exe 不能链接 wasm 符号):** 解法是 compile-definition seam——eval handler 的 wasm 路径整体在 `#ifdef AHFL_ENABLE_BACKEND_WASM` 内,WASM=OFF 下该臂不编译,无 wasm 符号引用,exe 正常链接(链编译器/verification/value,无 wasm_runner)。不需要 skip-77(REPL 不是 ctest;`ahfl.repl.process_smoke` 在 WASM=OFF 下跑 `:help`+`:quit`,仍绿)。

### 12.8.6 测试

新增(`tests/unit/tooling/repl/repl.cpp`,注册于 `ProjectTests.cmake:1880` `ahfl.repl.repl_all`):
1. P6 算术:`1+2` → `3`;`1+2*3` → `7`。
2. struct/enum/option:构造 + 字段访问/模式 → `print_value` 拼写。
3. 字符串字面量 rodata:`"hello"` → `hello`(经 Data 段)。
4. 不支持表达式 UX:`1.5+2.5` → `Error: ...`(codegen 诊断,非 trap);`"a"+"b"` 同。
5. 声明 fallback:`const x = 5;` → `print_program_ir` 输出(存活)。
6. `:type` 回归(`1+2` → `Int`)。

`ahfl.repl.process_smoke`(`repl_smoke.py`):加 eval 用例(WASM=ON 下 `1+2` → `3`;WASM=OFF 下 eval → 拒绝消息)。现状 smoke 仅 `:help`+`:quit`——扩展。

### 12.8.7 有序工作清单

1. `wasm_agent_runner.{hpp,cpp}`:确认 `expected<_, string>` 契约不变(§12.7.8);REPL 按 code 模式匹配。
2. `repl.cpp`:重写 `default_eval_handler`(§12.8.1);`#ifdef` gate(§12.8.5);删 evaluator include。
3. `src/tooling/repl/CMakeLists.txt`:drop evaluator;加 compiler/verification/value;gated wasm_runner + compile-definition。
4. 测试:§12.8.6 新增 + smoke 扩展。
5. WASM=OFF configure+build;ASan;全量 ctest。
6. 本决策节 + RFC 0026 Decision History 条目。

### 12.8.8 验收 / 验证标准

1. WASM=ON:`1+2` → `3`;struct/enum/option/string 字面量经 wasm 求值 + `print_value` 渲染正确。
2. 不支持表达式(`1.5+2.5`、`"a"+"b"`、map 字面量、闭包输出)→ `Error: <codegen 诊断>`,非 trap 字符串,非 evaluator fallback。
3. 声明输入(`const x = 5;`)→ `print_program_ir`(存活)。
4. `:type`/`:verify`/`:simulate`/`:help` 不变。
5. WASM=OFF:eval → §12.8.5 拒绝消息;非 eval 命令可用;configure+build 干净。
6. `ahfl_tooling_repl` 不直接链接 `ahfl_runtime_evaluator`(grep-zero 直接边;传递边经 wasm_runner→engine 保留到 WH-9)。
7. ASan + `-Werror` 绿。
8. 零业务逻辑 #ifdef(gate 仅在 `default_eval_handler`)。

### 12.8.9 拒绝的替代方案(按 fork)

- **fork A2(IR 级注入):** 见 §12.8.1——并行 IR 构造,不变量漂移,SourceRange 手工映射易错。
- **fork A3(Core 级注入):** 跳过 typechecker,类型错误无法在用户原文上捕获。
- **fork B2(预分类门):** 见 §12.8.2——第二个 SSOT,测试侧机械拖入生产。
- **fork B3(evaluator fallback):** 永久禁止。
- **fork D `value_to_json`:** 用户可见格式变更,无收益。
- **单态零过渡 agent:** 无 golden 先例,codegen 支持未证;两态形状已证。

---

**跨切片注记(WH-8 继承):** DAP 的 WASM=OFF 策略(§12.7.1)、facade config 的 hooks/cancellation/monotonic_clock 字段(§12.7.2 推迟项)、ctor 编译失败语义(§12.7.8)均由 WH-8 继承,不再重开决策。DAP 切换(`debug_session.cpp:291` `unique_ptr<WorkflowRuntime>` → `WasmWorkflowRuntime`)是 hooks 同签名的机械替换(`WasmRuntimeHooks` 已携带全部 5 个 hook,`wasm_runtime_hooks.hpp`),加 cancellation 在 import 边界检查。

## 12.8.10 WH-7 §12.8 修订记录(2026-10-03,dedicated decision agent,no human gate)

WH-7 builder 在编码前 STOP(coordinator 已独立复现全部事实):§12.8.1 的逐字合成形状在 HEAD `b9c75736` 无效。专用决策代理对 HEAD 逐条复核,并用 5 个 scratch harness(v1-v5,已全部删除,工作树零残留)在真实 wasm3 车道端到端实证后给出本修订。本节是 §12.8 的权威修订;builder 按 12.8.10.2 / 12.8.10.3 / 12.8.10.9 / 12.8.10.10 实现。

### 12.8.10.1 Coordinator 验证的无效事实(file:line)

**无效 1 — stage-1 const 包装不可解析。** `grammar/AHFL.g4:121` `constDecl: 'const' IDENT ':' type_ '=' constExpr ';';` 强制显式类型标注;`grammar/AHFL.g4:717` `IDENT: LETTER (LETTER | DIGIT | '_')*;` 禁止前导下划线。`const __repl_result__ = <expr>;` lex 为 stray `_`;shipped 的 `:type` handler(`repl.cpp:76`)与 eval handler(`repl.cpp:272`)对真实表达式从未工作过(`:type 1 + 2` 实测报 `mismatched input '_' expecting IDENT`)。git archaeology:自 `62b38665` 起;unit tests 从未驱动 default eval handler。

**无效 2 — agent I/O 为 Unit 被 schema 边界拒绝。** `src/compiler/semantics/typecheck.cpp:2688-2702` `check_schema_boundary_decl_type` 仅 `StructT` 通过;UnitT 唯一例外是 `SchemaBoundaryKind::AgentContextDefault`(仅 `context:` 槽位)。实测 `input: Unit` / `output: Unit` 均报 "must resolve to a struct type"。

**无效 3 — 名称 `__repl__` 本身不合法。** 同 IDENT 规则,前导下划线禁止。

**推论:** §12.8.1 的 `run_wasm_agent(..., Value{UnitValue{}}, ...)` + whole-output `print_value` 不可达。无 wasm golden fixture 使用 Unit I/O(全部 struct Frame);"proven e3 shape" 主张混淆了两态 transition 形状(已证)与 Unit frame(从未尝试)。

### 12.8.10.2 D1 — Stage-1 类型推断机制(替代失效 const 包装)

**FINAL stage-1 包装文本(verbatim):**

```
fn repl_probe() -> Unit { let repl_val = <expr>; return {}; }
```

为何有效(全部 file:line 实证):

1. 语法合法:`fnDecl` 显式 `-> Unit`;无标注 `let`;`return {};` 为 unit literal(`AHFL.g4:366`)。
2. 所有 fn body 均被 typecheck,即使从未被调用:`typecheck.cpp:3448` `check_fns_in_program` 遍历全部 fn,有 body 即 `check_fn_body`(:3472)。
3. 无标注 fn 默认 Pure:`typecheck_decls.cpp:1199` EffectJudgement 默认 `make_pure()`。
4. NO_DECREASES 不触发:`typecheck_decls.cpp:1277-1283` 的检查在 `if (decl.get().effect_clause)`(:1249)内;无标注 fn `effect_clause` 为 nullopt,永不进入。
5. 能力调用在 stage-1 即被拒:body ⊑ declared,Pure declared + CapabilitySet body → EFFECT_UNDERDECLARED(`typecheck_decls.cpp:1371`)。实测 `http_get("http://x")` → "function 'repl_probe' declares effect Pure but its body infers effect CapabilitySet; declared effect must be an upper bound of the body effect"。
6. `?` 在 Unit 返回类型 fn 中被拒:`typecheck_expr.cpp:2630-2631` TRY_REQUIRES_OPTION_OR_RESULT / `:2695-2696` TRY_INCOMPATIBLE_RETURN_TYPE。
7. 不带 `-> Unit` 的包装在 IR lowering 边界 crash("TypedProgram contains an error type");stage-1 只到 typecheck(`run_pipeline` 不 lowering),不受影响,但显式 `-> Unit` 是必须的。

**FINAL TypedProgram 提取路径(file:line):**

```
typed_program.declarations
  -> TypedDecl, std::get_if<FnTypeInfo>(&decl.payload) 且 local_name == "repl_probe"
     (FnTypeInfo: include/ahfl/compiler/semantics/declaration_info.hpp:374-401)
  -> FnTypeInfo::body_block_index                         (:401)
  -> typed_program.blocks[body_block_index]
      .statement_indexes[0]                               (typed_hir.hpp:521)
  -> typed_program.statements[stmt_idx]
  -> 断言 kind == TypedStmtKind::Let
     target_name == "repl_val"                            (:539)
     let_type_ref_strategy == FromInitializerType         (:552; enum :236)
  -> stmt.let_type                                         (:553) = 推断 TypePtr
```

权威节点是 let statement 的 `let_type`,**不是** `expressions.back()`:无标注时 typecheck.cpp:3992-4002 记录 `let_strategy = FromInitializerType`、`let_type = initializer.type->clone()`;typed_hir.hpp:550-551 注释明确 let_type 是 lowering 的权威语义输入;表达式 arena 顺序不保证用户表达式在最后。

**EnumVariantT 特例:** `Shape::Circle(42)` 推断为 `EnumVariantT` 而非 parent enum,`describe()` 产出不可解析的 `Shape::Circle`。必须经 `EnumVariantT::symbol`(`types.hpp:125-131`,= parent enum 的 SymbolId)→ `typed_program.find_symbol(id)`(`typed_hir.hpp:684`)取 parent enum 的 canonical_name + type_args,手工拼写 parent 类型(如 `MyOpt<Int>`)。

### 12.8.10.3 D2 — 合成 agent I/O 形状(替代 Unit frame)

**FINAL 合成源码模板(verbatim;无 module 声明;合成声明全部非 pub):**

```
<prologue>
struct ReplIn {}
struct ReplOut {
    value: <describe(parent_or_inferred_type)>;
}
agent ReplAgent {
    input: ReplIn;
    context: Unit;
    output: ReplOut;
    states: [Init, Done];
    initial: Init;
    final: [Done];
    capabilities: [];
    transition Init -> Done;
}
flow for ReplAgent {
    state Init { goto Done; }
    state Done { return ReplOut { value: <expr> }; }
}
```

设计决策(实证):

1. **Input = 零字段 struct** `struct ReplIn {}`:实证探针确认零字段 struct 存活 Core lowering → `compute_core_layouts` → `emit_core_wasm` → frame_packer → wasm3 run → frame_reader 全管线(packer 遍历零字段无操作)。host 构造空 StructValue(type_name 为空,fields 空;packer 不校验零字段 struct 的 type_name)。
2. **Output = 包装 struct** `ReplOut { value: T; }`:host 侧 `output()` → StructValue → `fields.get("value')` 取内层 Value → `print_value`。任意 T(Int/Bool/String/Struct/Enum/泛型 Enum/Option)实证通过。
3. **无 module 声明**:module 会给全部 nominal 类型加 `repl::` 前缀污染输出(实证 v5:`Color::Green` vs `repl::Color::Green`)。
4. **非 pub**:prologue 类型是 package-internal,pub ReplOut 会报 "public struct exposes package-internal type"(实证 v1)。
5. **名称全部 IDENT 合法**(字母开头):ReplIn/ReplOut/ReplAgent/repl_val/repl_probe。
6. **Unit 表达式短路,不走 wasm**:`value: Unit` struct 字段被 codegen 拒绝("computed final value is not a single-word P6 value on the frame lane");推断类型为 UnitT 时直接 `print_value` Unit → `{}`(`value.cpp:547-549`)。
7. **碰撞语义**:stage-1 无合成声明,用户表达式无法引用合成名;stage-2 若用户 prologue 同名 → 重定义 parse 错误,REPL 诚实返回。危害极低(单次求值,prologue 用户自控)。

### 12.8.10.4 D3 — 实证探针矩阵(真实 wasm3 端到端,observed bytes)

| 用户表达式 | 推断类型 | 输出 |
|---|---|---|
| `1 + 2` / `1 + 2 * 3` / `(1+2)*(3+4)` / `-42` | Int | `3` / `7` / `21` / `-42` |
| `true` / `1 < 2` / `true && false` | Bool | `true` / `true` / `false` |
| `"hello"` | String | `"hello"`(**带引号**,print_value String 拼写) |
| `{}` | Unit | `{}`(短路) |
| `Color::Green` | Color | `Color::Green` |
| `Shape::Circle(42)` / `Shape::Circle { r: 42 }` | Shape | `Shape::Circle(42)` / `Shape::Circle { r: 42 }` |
| `Point { x: 1, y: 2 }` | Point | `Point { x: 1, y: 2 }` |
| `Outer { inner: Inner { v: 7 } }` | Outer | `Outer { inner: Inner { v: 7 } }` |
| `match Color::Green { Red => 1, Green => 2, Blue => 3 }` | Int | `2` |
| `MyOpt::MySome(7)` | MyOpt<Int> | `MyOpt::MySome(7)` |

Codegen 拒绝(B1,预期,均为 actionable 字符串非 trap):

| 表达式 | observed |
|---|---|
| `1.5 + 2.5` | `float literals need the f64 opcode ladder, a later P6 slice` |
| `"a" + "b"` | `binary arithmetic/comparison is defined for Int/Bool only on the P6 frame lane` |
| `\(x: Int) -> x + 1` | `constructor slot is not a single-word P6 value` |

Stage-1 拒绝(预期):能力调用(EFFECT_UNDERDECLARED 全文)、`?`(TRY_REQUIRES_OPTION_OR_RESULT)、`if ... {} else {}` 表达式(if 是 statement;parse error)、`[1,2,3]`(listLiteral 是 dead grammar rule,`AHFL.g4:656`,从未被 primaryExpr 引用)。

声明 fallback 存活:`const x: Int = 5;` / `struct Foo { x: Int; }` / `fn bar() -> Int { return 42; }` 均走 `print_program_ir`。

`:type` 经 fn 包装实证工作:`1+2`→`Int`、`"hello"`→`String`、`true`→`Bool`、`{}`→`Unit`、`1.5+2.5`→`Float`。

### 12.8.10.5 D4 — 输出/错误契约

成功:stage-1 typecheck → 提取 let_type → UnitT 短路 `{}` → EnumVariantT 转 parent 拼写 → describe + 合成源码 → stage-2 pipeline/lower/`run_wasm_agent(program, "ReplAgent", empty StructValue, config)` → Completed + output() 非空 → StructValue.fields 取 `value` → print_value。

失败:非 Completed → `Error: agent did not complete (status: <status>)`;null output → `Error: agent produced no output`;stage-2 pipeline 失败 → `Error: <诊断字符串>`;`unexpected(string)` → `Error: <codegen 诊断>`(B1,不暴露 wasm3 trap 文本)。

`:type` handler 本片一并修复为同一 fn 包装(共享 helper),不再用失效 const 包装。

### 12.8.10.6 对 §12.8.1 其余事实性纠正

1. "第一轮成功则第二轮必成功" 的语境失效(第一轮不再是 const 包装),机制以本节为准。
2. Unit 输出必须短路(§12.8.1 未区分)。
3. §12.8.6 测试 3 字符串期望是**带引号** `"hello"`,不是裸 `hello`。
4. §12.8.6 测试 5 声明语法是 `const x: Int = 5;`(必须带 `: type`)。
5. EnumVariantT 需 parent-enum 转换(§12.8.1 未提及)。

### 12.8.10.7 被取代的 §12.8.1 条目

- 包装器形状(Unit I/O、`output: <describe(T)>`、agent 名 `__repl__`)→ 由 12.8.10.3 模板取代。
- 求值流程第 1 步(const 包装、`expressions.back()`)→ 由 12.8.10.2 取代。
- 第 4 步(`run_wasm_agent(..., "__repl__", Value{UnitValue{}}, ...)`)→ 由 ReplAgent + 空 StructValue 取代。
- 第 5 步(whole-output print_value)→ field-unwrap 后 print_value 取代。

### 12.8.10.8 §12.8.2–12.8.9 重申

- **12.8.2(B1;无预分类门;无 evaluator fallback):重申。** 探针矩阵实证 codegen fail-closed;B2/B3 拒绝理由不变。
- **12.8.3(车道 codegen 决定;零能力 invoker 防御性):重申。**
- **12.8.4(print_value 存活):重申,附修正**——渲染的是 unwrap 后的内层 Value。
- **12.8.5(单 #ifdef 组合边缘;CMake link-edge;WASM=OFF 字节固定拒绝):重申。**
- **12.8.7 有序清单:重申**,第 2 步引用改为 §12.8.10。
- **12.8.9 拒绝的替代方案:重申**(A2/A3/B2/B3/D value_to_json/单态零过渡 agent)。

### 12.8.10.9 §12.8.6 测试列表 delta(替换)

1. P6 算术:`1+2`→`3`;`1+2*3`→`7`。
2. struct/enum/option:`Point { x: 1, y: 2 }`、`Color::Green`、`MyOpt::MySome(7)` 的 print_value 拼写。
3. 字符串:`"hello"` → `"hello"`(带引号)。
4. 不支持 UX:`1.5+2.5`、`"a"+"b"` → `Error:` 前缀 + codegen 消息片段,success==false,无 trap 文本。
5. 声明 fallback:`const x: Int = 5;`(真实语法)→ IR dump。
6. `:type` 回归(fn 包装):`1+2`→`Int`、`"hello"`→`String`、`{}`→`Unit`、`1.5+2.5`→`Float`。
7. 新增 Unit 短路:`{}` → `{}`。
8. 新增泛型 enum 变体:`MyOpt::MySome(7)`(EnumVariantT→parent 路径)。
9. 新增嵌套 struct:`Outer { inner: Inner { v: 7 } }`。

`ahfl.repl.process_smoke`:WASM=ON 加 `1+2`→`3` eval 用例;WASM=OFF eval → 字节固定拒绝。

### 12.8.10.10 §12.8.8 验收 delta(替换为)

1. WASM=ON:`1+2`→`3`;struct/enum/option/string 经 wasm + print_value 正确;`{}`→`{}` 短路。
2. 不支持表达式 → `Error: <codegen 诊断>`,非 trap,非 fallback。
3. `const x: Int = 5;` → print_program_ir。
4. `:type/:verify/:simulate/:help` 不变;`:type` 内部改 fn 包装。
5. WASM=OFF:eval → 12.8.5 拒绝消息;非 eval 命令可用;configure+build 干净。
6. `ahfl_tooling_repl` 无直接 evaluator 链接边(grep-zero;经 wasm_runner→engine 的传递边保留到 WH-9)。
7. ASan + `-Werror` 绿。
8. 零业务 #ifdef(gate 仅 default_eval_handler)。
9. 合成源码无 module 声明(输出无 `repl::` 前缀)。
10. 合成声明非 pub(无 package-internal visibility 违规)。

### 12.8.10.11 拒绝的替代方案

带显式类型的 const 包装(循环依赖:推断前不可能标注类型)、顶层 let(grammar 无)、零过渡 agent(无 golden 先例)、合成 module 声明(prefix 污染)、pub 合成声明(visibility 违规)、output struct 的 Unit 字段(codegen 拒绝)、input 占位字段(零字段已证,多余)。

### 12.8.10.12 WH-7 REPL enum/struct/option 求值面收窄决策(2026-10-03,dedicated decision agent,no human gate)

WH-7 builder 的未提交实现(HEAD `b9c75736` + 工作树改动)把 eval 切到 wasm3 车道后,一个开放范围问题浮出:§12.8.10.4 探针矩阵的 nominal 行(`Color::Green`、`Point { x: 1, y: 2 }`、`MyOpt::MySome(7)` 等)在真实 REPL 二进制中**不可达**。专用决策代理对 builder 未提交树逐条复核源码,并用 build/dev 的 `ahfl-repl`(WASM=ON)端到端实证后给出本节;coordinator 对全部关键事实独立复核,并更正了代理初稿 1.2 的一处事实错误(见下)。**决策:A — WH-7 只交付 scalar/P6 诚实面;enum/struct/option 求值延后到 post-WH-9 后援切片。**

#### 1. 实证发现(file:line + observed bytes)

**1.1 REPL 管线是 detached 单源单元,永不经过 Project 模型层。** `src/tooling/repl/repl.cpp` `run_pipeline` = `Frontend::parse_text("repl", source)` + `Resolver::resolve` + `TypeChecker::check`,单个 `ast::Program`,无 ProjectInput / SourceGraph / package graph。prelude 注入**只**存在于 Project 模型层:`src/compiler/syntax/frontend/project.cpp:23` `kStdPreludeModule = "std::prelude"`、`:255-256` `should_inject_prelude`、`:771-782` 隐式 `import std::prelude` 注入(被 `#![no_prelude]` 抑制)。detached 管线没有任何通道把声明送进 stage-2。

**1.2 std 只在磁盘上,无嵌入;编译期默认 sysroot 是 dev-build-only 的源码树路径,不是可分发契约。** `std/option.ahfl:3` `pub enum Option<T>` 等全部 std 源是仓库根 `std/` 下的磁盘文件;无嵌入机制(无 incbin / 无 std 源码字符串表;`strings build/dev/.../ahfl-repl | grep -c "module std::prelude"` = 0)。sysroot 发现三级:`--sysroot` flag、`AHFL_SYSROOT` 环境变量(`src/tooling/cli/cli_driver.cpp:441-475`)、编译期默认 `AHFL_DEFAULT_SYSROOT`(`src/compiler/project_discovery/discovery.cpp:519-529`)。**coordinator 更正决策代理初稿的事实错误(已复核)**:`AHFL_DEFAULT_SYSROOT` 并非"全仓库 CMake 从未定义"——它在 `src/compiler/project_discovery/CMakeLists.txt:14-18` 以 **PRIVATE** compile definition **无条件**定义为 `${PROJECT_SOURCE_DIR}`,dev build tree 内默认 toolchain profile 可解析到仓库根 `std/`。但它不能解救 Option B:(a) 指向**构建机源码绝对路径**,安装/分发包不携带该目录,不是 install-robust 契约,REPL 依赖它即静默依赖构建树;(b) REPL 的 detached `run_pipeline` **根本不经过 project discovery / Project 模型**(1.1),默认 sysroot 对 REPL 代码路径不可达;(c) 即便经 `ahflc`,detached 文件无 manifest 时既有契约仍是 `note [N::detached_source_unit]` + `create ahfl.toml or pass --manifest to enable std imports and workspace navigation`(`cli_driver.cpp:207-217`)。

**1.3 Project 加载器强制每源恰好一个 module 声明。** `project.cpp:693-698`:零 module 声明即报 "project-aware source file must declare exactly one module"。REPL 合成源按 §12.8.10.3 第 3 条**无 module 声明**(实证:module 会给全部 nominal 类型加 `repl::` 前缀污染输出)——合成源在当前 Project 路径下**不可编译**,Option B 必须重开 §12.8.10.3 已实证 settle 的决策。

**1.4 observed error bytes(builder 未提交树,build/dev ahfl-repl,WASM=ON):**

| 输入 | 通道 | observed(稳定前缀) |
|---|---|---|
| `Option::Some(3)` | eval | `Error: mismatched input 'Option' expecting {<EOF>, '#', 'module', ...}` |
| `Color::Green` | eval | 同形(`mismatched input 'Color' expecting {...}`) |
| `Point { x: 1, y: 2 }` | eval | 同形(`mismatched input 'Point' expecting {...}`) |
| `Some(3)` | eval | 同形(`mismatched input 'Some' expecting {...}`) |
| `Option::Some(3)` | `:type` | `Error: unknown callable 'Option::Some'` |
| `Color::Green` | `:type` | `Error: unknown type 'Color'` |
| `Some(3)` | `:type` | `Error: unknown callable 'Some'` |

eval 路径机制:stage-1 `infer_repl_type` 先失败(resolve 报 UNKNOWN_CALLABLE / unknown type),随后声明 fallback `run_pipeline(input)` 对**原始输入**做顶层 parse 并失败——用户看到的是 fallback 的**顶层 parse 错误**,不是 stage-1 的 resolve 错误。`:type` 只走 stage-1,暴露 resolve 错误。两条都是诚实 Error 字符串,无 trap、无 evaluator fallback。

**1.5 零跨行状态(实证)。** `enum Color { Green, Red }` 一行 → 声明 fallback IR dump(存活);下一行 `Color::Green` → 同 1.4 顶层 parse 错误。`execute_command` 无状态,仅 `history_` 持久。单行内塞 prologue+表达式(`struct Point {...} Point {...}`)同样失败(grammar 顶层不接受 decl 后接 expr)。

**1.6 §12.8.10.4 矩阵 nominal 行的可达性纠正。** 这些行由 §12.8.10.3 harness 证明——prologue 声明与合成 agent 在**同一源码字符串**内。builder shipped 实现的 prologue 是**空的**(合成源码构造处注释:"The prologue is empty: the REPL takes one expression per line")。矩阵行是 harness-proven 的管线能力,不是 REPL-reachable 行为;可达性以 1.4 为准。

**1.7 Option B 的管线成本已量化。** SourceGraph 重载确实存在(resolver/typecheck/lowering 均有 graph 重载),preluded 编译技术上可达——但:(a) std 定位只能靠 `AHFL_SYSROOT` env / 新 REPL flag(CWD/install-tree/构建树静默依赖被禁止,见 1.2);(b) 合成源必须加 module 声明(重开 §12.8.10.3 第 3 条);(c) `:verify` 与 `:simulate` 同样用 `run_pipeline`,B 必须一并改造或留下不一致;(d) REPL 今天把诊断拍平成字符串,project 路径引入全新错误呈现面(package graph / sysroot mismatch / module 声明诊断);(e) `print_value` 对 enum 渲染 `enum_name::variant`(`src/runtime/value/value.cpp:477`),std 类型在 `module repl;` 下的 enum_name 拼写未证。

#### 2. 决策:A — WH-7 交付 scalar/P6 诚实面,nominal 构造子求值延后

**WH-7 的 eval 面 = Int / Bool / String / Unit(经 wasm3 车道 + `print_value`),加声明 fallback IR dump。** enum/struct/option 表达式继续产出 1.4 的既有 Error 字符串;这些字符串诚实(声明不可见 → resolve/parse 失败),不是 trap、不是 fallback。

**为何 A 击败 B:**
1. **B 是半吊子,且没有 C 的 B 不值得做。** B 只修复 std 的 Option/Result 等构造子(且仅限 sysroot 恰好可达时);用户自声明 enum/struct 仍因零跨行状态(1.5)不可用。B 交付的 UX 是"std 构造子仅在 sysroot 恰好配置时工作;你自己的 enum 永远不工作"——比 A 的清晰诚实面更差。明确裁决:**无 C 的 B 不值得做。**
2. **B 的真实成本(1.7)远超半吊子收益:** REPL 管线不经 Project/discovery(1.1/1.2),module 声明要求(1.3)重开已 settle 的 §12.8.10.3 第 3 条;`:verify`/`:simulate` 连带改造;新错误呈现面;std enum 输出拼写未证。
3. **B 不解除任何 WH-8/WH-9 阻塞。** WH-8(DAP 切 wasm facade)与 WH-9(原子删 evaluator)都不依赖 REPL 能求值 `Option::Some`。

**为何 A 击败 C(对 WH-7 而言):** C(会话累积)是真正的主流对齐修复(evcxr/GHCi 式),但它是会话语义项目:重声明/遮蔽规则、`:clear`、错误恢复(坏行不得污染会话)、whole-program typechecker 下逐行重跑 vs 新建增量架构,且 C 与 B 组合才完整(C 单独修用户 enum,不修 std Option)。C 与引擎切换正交,不属于 WH-7,也不开 WH-7b;**C 裁定为 post-WH-9 后援切片。**

**与主流参考(Rust/evcxr)分叉的 AHFL-specific 理由:** Rust 把 std 嵌入 sysroot,rustc 经安装期默认找到;AHFL 无嵌入 std,唯一编译期默认指向构建树(1.2),安装后不可用。今天在 AHFL 做 REPL prelude 模式只能静默依赖 env/CWD/构建树——不诚实的双模 REPL。诚实的主流对齐路径需要(std 嵌入或显式 REPL sysroot 契约)+ 会话累积,两者都大于 WH-7 的引擎切换范围。

**A 的成本:** enum/struct/option 表达式产出 Error 字符串(1.4 稳定前缀)而非值;§12.8.10.4 nominal 矩阵行降级为 harness-proven-only(1.6 已纠正)。**连带代码处置(Principle 1):** `infer_repl_type` 的 EnumVariantT→parent 转换分支在 detached 管线中**不可达**(无任何通道把 enum 声明送入 stage-1 源),WH-7 一并删除;后援切片在通道建成时重新引入。

#### 3. 对 §12.8.10.9 / §12.8.10.10 的收窄

**§12.8.10.9 测试列表 delta — 第 2 条收窄为:** scalar print_value 拼写(经 wasm 车道):`1+2`→`3`、`true`→`true`、`"hello"`→`"hello"`(带引号)、`{}`→`{}`(短路)。enum/struct/option 求值延后(见本节)。

**§12.8.10.9 第 8、9 条(泛型 enum 变体、嵌套 struct)整体延后**,移入后援切片;不在 WH-7 测试列表。

**§12.8.10.9 新增第 10 条(nominal 失败面 pin):** 无 prologue 的 nominal 构造子表达式产出诚实 Error 字符串,success==false,无 trap 文本:`Option::Some(3)`/`Color::Green` eval → `Error: mismatched input '<IDENT>' expecting {...}`(fallback 顶层 parse 错误,1.4);`:type Option::Some(3)` → `Error: unknown callable 'Option::Some'`;`:type Color::Green` → `Error: unknown type 'Color'`。

**§12.8.10.10 验收 delta — 第 1 条收窄为:** WASM=ON:`1+2`→`3`;string 字面量经 wasm + print_value 正确(带引号);`{}`→`{}` 短路。enum/struct/option 求值不在 WH-7 验收面(本节)。

**§12.8.10.10 新增第 11 条:** enum/struct/option 表达式在无 prologue 时产出 1.4 的固定错误前缀(eval 走 fallback 顶层 parse 错误;`:type` 走 resolve 错误),success==false,无 trap、无 evaluator fallback。

**§12.8.10.4 探针矩阵加注:** nominal 行(`Color::Green`、`Shape::Circle(42)`、`Point {...}`、`Outer {...}`、`match Color::Green {...}`、`MyOpt::MySome(7)`)是 §12.8.10.3 harness(prologue + 合成 agent 同一源码字符串)证明的**管线能力**,不是 shipped REPL 的可达行为;可达性以本节 1.4 为准。

#### 4. 后援切片

**切片名:REPL session accumulation + prelude mode(post-WH-9)。** 进入条件:(a) std 嵌入二进制,或显式 REPL sysroot 契约(不静默依赖 env/CWD/构建树);(b) 会话累积(逐行重跑累积源码,或增量 typechecker 架构)——用户自声明 enum/struct 跨行可见;(c) 合成 agent 的 module 声明 / 前缀污染决议(§12.8.10.3 第 3 条重开);(d) `:clear`/`:reset` + 坏行不污染会话的错误恢复语义;(e) `:verify`/`:simulate` 与 eval 的 prelude 模式一致性。主流参考:evcxr(会话累积 + 重声明)、GHCi(`it` 绑定 + `:load`)。

## 12.9 WH-8 decisions (2026-09-30, dedicated decision agent, no human gate)

本节是 WH-8(DAP 从 evaluator `WorkflowRuntime` 切换到 `WasmWorkflowRuntime` facade)的书面决策。决策代理只读地对照 HEAD `9bca5507` 复核了源码,未运行构建/测试。**继承不重议**:WASM=OFF 策略(launch/execute 拒绝、非执行功能可用、gate 只在 composition edge)、facade config hooks/cancellation/monotonic_clock 在 WH-8 引入、facade ctor 编译失败语义 = `run()` 时以 DiagnosticBag 浮现(§12.7.8)、link-edge flip(§12.7.3 表 WH-8 行:`ahfl_tooling_dap` 翻到 `WasmWorkflowRuntime`;`ahfl_runtime_engine` 的 evaluator edge 在 DAP 成为最后一个外部生产 includer 后 PUBLIC→PRIVATE)。

**复核事实(vs HEAD 9bca5507)**:5 个 hook 签名逐字节相同(`wasm_runtime_hooks.hpp:45-75` vs `workflow_runtime.hpp:84-107`);hook 触发语义——P6 `state_entered` = import-boundary trace prefix + post-run,WireJson = post-run only(`workflow_session.cpp:135-220`),capability hooks 在 import 处 LIVE(`wrapped_invoker:495-504`),`node_completed` post-run 且在 state hooks 之后(`:1143-1237`),`agent_input_hook` 在 workflow lane 不触发;evaluator 取消检查在 node loop 顶部(`workflow_runtime.cpp:1180-1200`,Cancelled/Interrupted + `"workflow execution cancelled"`/`"workflow execution interrupted"`);grammar 无用户循环(`AHFL.g4` statement 无 while/for),递归由 `core_recursion.hpp` R1/R2/R3 编译期封闭;DAP 无 pause/terminate/attach(`dap_server.cpp:69-113`);生产 includer of `workflow_runtime.hpp` 仅剩 `dap/debug_session.hpp`(WH-8)与 `cli/workflow_run.cpp`(WH-6);`compile_error_` 仍是 `std::optional<std::string>`(§12.7.8 DiagnosticBag 未落地)。

**wh8-prep-map.md 中的过时声明(stale)**:(1) "session 不填充 diagnostics"——过时,`wasm_lifecycle.cpp:31-39` `add_error` 填充 code+message;(2) "facade 是自由函数不是类"——过时,`WasmWorkflowRuntime` 类已存在(`wasm_workflow_runtime.hpp:82`);(3) "hook 语义 bug:agent_name 占 node 槽、AgentId{0}、node_name 空"——过时,fix-forward 已全部修复(P1-6 agent_name、P2-2 真实 AgentId、P2-7 `runner_to_schedule` 解析 node_name);(4) "monotonic_clock 推迟到 WH-8 供 DAP 时序"——前提过时,DAP 源码中 `monotonic_clock` 引用为零,无物可对齐;(5) "cancellation 缺失"——确认(wasm_host grep 为空),必须新增。

### 12.9.0 决策总览

WH-8 是一次 big-bang 切换:`src/tooling/dap/` 不再 include `runtime/engine/workflow_runtime.hpp`,改为持有 `runtime/wasm_runner/wasm_workflow_runtime.hpp` 的 `WasmWorkflowRuntime`。**不保留 evaluator 回退路径、不并行实现**(Principle 1)。切换后 `src/tooling/dap/` 对 evaluator 的 include 为零,`ahfl_runtime_engine` 的 `ahfl_runtime_evaluator` edge 从 PUBLIC 翻为 PRIVATE(WH-6 已切 CLI,DAP 是最后一个外部生产 includer)。

核心分叉是**断点活性(breakpoint liveness)**:evaluator 在每个 state entry 同步触发 hook,任何 state/line 断点都能 LIVE 暂停;wasm lane 的 host 只能在 import boundary(capability 调用)观察 guest,state 断点的活性取决于该 state 是否在某次 capability 调用之前进入。决策见 12.9.1。其余决策:cancellation 在 import boundary 检查(12.9.2);monotonic_clock 不引入(12.9.3);hook 签名零差异、语义缺口三项、其中 `node_completed` 触发顺序修订(12.9.4);变量/作用域/栈的诚实契约(12.9.5);阻塞 hook/线程机制不变且无死锁(12.9.6);编译/启动错误(12.9.7);CMake link-edge diff(12.9.8);WASM=OFF 策略具体化(12.9.9);测试计划(12.9.10);有序 worklist(12.9.11);验收标准(12.9.12);被否方案(12.9.13)。

### 12.9.1 断点活性模型(核心决策)

**分类权威**:facade descriptor(`CoreWasmExecutionDescriptor`)。新增 facade 访问器 `descriptor_for(workflow_name) -> const CoreWasmExecutionDescriptor*`(当前 descriptor 是 `CompiledWorkflow` 私有成员,需暴露)。DAP 在 `setBreakpoints` 时(launch 之后、run 之前)调用它做精确分类。

**descriptor 扩展(一处)**:`CoreWasmStateWalk` 新增 `std::uint32_t last_cap_walk_index{0}`——该 agent 的 walk 中、包含最后一次 capability 调用的 state handler 的 walk 下标(无 capability 调用时为 0)。codegen 已知道 walk 与 capability call site,填充此字段是举手之劳。这是让断点验证精确的权威来源;DAP 不应近似 codegen 已知的事实(Principle 1/5)。

**分类规则**(`setBreakpoints` 的 `verified` + `message`):

| 断点位置 | 分类 | verified | message(pinned) |
|---|---|---|---|
| capability 调用行 | **LIVE**(总是) | `true` | (无 message) |
| state handler 行,该 state 在 agent walk 中且 walk-index ≤ `last_cap_walk_index`,且该 agent 有 node `has_capability` | **LIVE** | `true` | `"state entered before capability boundary (live pause)"` |
| state handler 行,该 agent 无 node `has_capability` | **POST-MORTEM** | `false` | `"state has no capability boundary in its runner; pauses after run completes (post-mortem)"` |
| state handler 行,walk-index > `last_cap_walk_index` | **POST-MORTEM** | `false` | `"state is entered after the last capability boundary; pauses after run completes (post-mortem)"` |
| state handler 行,state 不在 walk 中(`all_states` 有但 walk 无,computed-goto 未取分支) | **NEVER** | `false` | `"state is not on the runner's walk (untaken branch); never paused"` |
| 非 state-handler 行(agent/workflow/node 声明行) | **NOT BREAKABLE** | `false` | `"line is not a state handler; only state handlers and capability calls are breakable"` |

**`breakable_lines_` 收窄**:当前 `build_breakable_lines`(`debug_session.cpp:406-463`)把 state handler 行 + agent/workflow/node 声明行都加入 `breakable_lines_`,导致声明行断点 `verified:true` 但永不暂停——这是 evaluator 时代就存在的诚实性缺陷。WH-8 收窄为**仅 state-handler 行**(capability 调用行通过 state handler 行映射,因为 capability 调用在 state handler 内)。声明行断点按上表 NOT BREAKABLE 拒绝。

**DAP deferred-binding 诚实**:`setBreakpoints` 在 launch 之前也可能被调用(DAP 客户端常在 initialize 后、launch 前设断点)。此时 session 尚未持有 facade,descriptor 不可用。处理:`setBreakpoints` 先按 `breakable_lines_`(源码级,不依赖 descriptor)做粗筛;launch 完成、facade 构造后,DAP 主动向客户端发送 unsolicited `"breakpoint"` 事件,携带按 descriptor 精化后的 `verified`/`message`。这是 DAP 标准的 deferred breakpoint binding 模式,保证客户端最终看到精确的验证状态。

**POST-MORTEM 断点的暂停语义**:分类为 POST-MORTEM 的断点 `verified:false`,但 DAP 仍注册它。run 完成后(post-run state hooks 触发时),若该 state 在 post-run 重建中出现,DAP 触发 stopped 事件(reason `"pause"`),栈/变量按 12.9.5 的 post-mortem 契约呈现。客户端通过 `verified:false` + message 已知这是 run 完成后的暂停,不被误导。

**单步语义**:`next`/`stepIn`/`stepOut` 的步进目标是下一个 state entry。在 wasm lane 上,"下一个 state entry" 只在 import boundary(trace prefix 的下一条)或 post-run(下一条重建 state)可观察。因此单步在 LIVE 区间(capability boundary 之前)逐 import-boundary 推进;越过最后一个 capability boundary 后,单步直接到 post-run 的下一条重建 state(或 run 结束)。这是诚实的:单步粒度 = host 可观察粒度。`stepIn` 不进入 capability 实现(capability 是 host 侧调用,非 guest 代码);`stepOut` 从当前 state 走到该 node 的下一个可观察点。与 evaluator 时代的差异(evaluator 逐 state entry 同步步进)通过 `verified`/message 与文档说明,不伪装。

**WireJson vs P6 差异**:断点活性分类对两条 lane 相同(都基于 descriptor 的 walk/capability 结构)。差异在变量呈现(12.9.5):P6 lane 的 node 输出在 `node_completed` 时可读(O_k),WireJson lane 为 `NoneValue`(不透明,run 结束才解码)。

### 12.9.2 Cancellation / interruption 检查点 + DAP 请求映射

**新增 config 字段**(顶层,与 evaluator 对齐,**不**放进 `WasmRuntimeHooks`——hook 是值观察通道,不是控制通道;evaluator 也把 cancellation 放在 config 而非 hook):

```cpp
// WasmWorkflowRuntimeConfig + WorkflowSessionConfig
std::function<bool()> cancellation_requested;
std::function<bool()> interruption_requested;
```

**检查点**(wasm lane):
1. **`wrapped_callback` 顶部**(`workflow_session.cpp:643-670`,import boundary):在 trace decode / inner 之前检查。若 `cancellation_requested()` → 置 `cancelled=true`,返回 `eng::ImportAbort{}` 终止 run2;run2 返回后 session 构建 `RunTerminalStatus::Cancelled` + `WorkflowFailureKind::Cancelled` + diagnostic `"workflow execution cancelled"`(与 evaluator `workflow_runtime.cpp:1196-1200` 对齐)。`interruption_requested` 同理 → `Interrupted` + `"workflow execution interrupted"`。此检查覆盖 WH-4b memo/replay boundary(memo 命中也走 `wrapped_callback`,顶部检查在 memo 分类之前)。
2. **`run_workflow_session` 入口**(pre-run):在 admission/instantiation 之前检查,若已请求取消则直接返回 Cancelled 结果(不启动 wasm 实例)。

**无 import 的 workflow**:没有任何 mid-run 取消点。这类 workflow 的 guest 计算是**有界的**——grammar 无用户循环(`AHFL.g4` statement 仅 exprStmt/ifStmt/gotoStmt/returnStmt 等),递归由 `core_recursion.hpp` R1/R2/R3 格编译期封闭(静态深度来自有界容器容量),wasm `kOpLoop` 仅用于有界内部扫描。因此无 import workflow 必然运行到有界完成,cancellation 请求在 run 完成后才被观察(pre-run 检查除外)。这是诚实的:没有可挂起的边界,就没有 mid-run 取消。DAP 文档说明此契约。

**DAP 请求映射**:
- `disconnect` → 置 `stopping_` → `cancellation_requested` 返回 true → wasm lane 在下一个 import boundary 终止(或无 import 时运行到完成)→ `run()` 返回 → `execute()` 见 `stopping_` → 跳过 diagnostics → `terminated` 事件。worker join 及时完成(有界)。
- **不新增** `pause`/`terminate`/`attach` 请求(evaluator 时代 DAP 也没有,`dap_server.cpp:69-113`)。断点/单步的暂停机制(阻塞 hook)是唯一的 pause。
- `continue`/`next`/`stepIn`/`stepOut` → resume/arm step(不变,notify `resume_cv_`)。
- DAP 只接 `cancellation_requested`;`interruption_requested` 默认空(facade 为 parity 与未来 host 保留)。

**结果映射**:wasm lane 的 `Cancelled`/`Interrupted` 终端(`RunTerminalStatus`)映射到 neutral `WorkflowStatus::NodeFailed`(`workflow_result.cpp` 既有映射),renderer 渲染 `RunTerminalStatus::Cancelled`。DAP 在 `!stopping_` 时把 diagnostic 发到 stderr + `terminated`;`stopping_` 路径静默。

### 12.9.3 monotonic_clock

**不引入** facade config。复核确认 DAP 源码中 `monotonic_clock` 引用为零——evaluator 时代 DAP 就从未设置过此字段,§12.7.2 "推迟到 WH-8 供 DAP 时序" 的前提不成立。facade 的事件存储使用 lifecycle helper 内部时钟,DAP 不消费 execution event 流(DAP 用 hook,不用 event store)。若未来有 host 需要确定性 event offset,那是独立 slice,不在 WH-8。此条同时更正 §12.7.2 的过时 deferral 理由。

### 12.9.4 Hooks 签名对齐与语义缺口

**签名**:5 个 hook(`state_entered`、`agent_input`、`capability_invoked`、`capability_result_observer`、`node_completed`)在 `WasmRuntimeHooks` 与 `WorkflowRuntimeConfig` 之间逐字节相同(已复核)。DAP 的 hook lambda 从 `config.*` 平移到 `config.hooks.*`,签名零改动。

**语义缺口与决策**:

1. **`agent_input_hook` 在 workflow lane 不触发**。evaluator 在每个 node 的 agent 运行前触发(`workflow_runtime.cpp:1351-1352`,真实 node_name + node_input);wasm lane 的 node 输入在 guest 内部物化,host 不可观察,故不触发。**决策**:删除 DAP 的 `on_agent_input` + `agent_inputs_` 成员 + hook 安装(workflow lane 上是死代码,Principle 1)。Node frame 改由 `on_state_entered` 合成(见下)。Input/Output 作用域的 `"input"` 不可得(诚实契约,12.9.5)。

2. **`node_completed_hook` post-run 且在 state hooks 之后**(`workflow_session.cpp:1143-1237`)。这导致任何 state 暂停点都拿不到 node 输出(node_completed 在所有 state hook 之后才触发)。**决策:facade 修订触发顺序**——post-run 按 node(schedule 顺序)逐 node 触发:先 `node_completed(node, output)`,再触发该 node 的 `state_entered` hooks。这使 post-mortem 暂停点携带已完成 node 的输出。**§12.1 保证表修订(dated)**:post-run 触发顺序由 "state hooks 全部 → node_completed 全部" 改为 "per node(schedule 顺序):node_completed(node) → 该 node 的 state_entered hooks"。WireJson lane 的 node 输出在 `node_completed` 时为 `NoneValue`(不透明,run 结束才解码);P6 lane 为真实 O_k。suspend 路径同样适用:已完成 node 的 node_completed 在其 state hooks 之前;pending node 无 node_completed(未完成),其 state hooks 从 suspend 点的 trace prefix 触发。

3. **`state_entered_hook` 的 node_name 现已始终解析**(P2-7 `runner_to_schedule`,`workflow_session.cpp:103-113`;WireJson 重建用 `node_desc->name`,`:156,211`)。`wasm_runtime_hooks.hpp` 头注释 "empty for import-boundary workflow fires" 过时,需更正。DAP 可依赖 node_name 合成 Node frame。

4. **`capability_invoked_hook` 携带真实 AgentId**(P2-2,`wrapped_invoker:469-479`)——DAP 的 `agent_debug_id` 可用,无缺口。

5. **`capability_result_observer`** 在 invoker 返回后 LIVE 触发——无缺口。

**Node frame 合成**:`on_state_entered` 中,若 hook 的 node_name 非空且与当前 Node frame 的 node 不同(或无 Node frame),则弹出 Workflow 之上的帧、压入新 Node frame(source 取自 `node_line_map_`),再压入/替换 State frame。这使栈形为 `[Workflow, Node, State]`(capability 暂停时再加 Capability frame),与 evaluator 时代一致。Node frame 在该 node 的首个 state entry 时压入(evaluator 在 agent_input 时压入,时机略早于首个 state,但形状一致;诚实差异:wasm lane 的 Node frame 在首个可观察 state 时出现)。

### 12.9.5 变量 / 作用域 / 栈在暂停点的诚实契约

| 暂停点类型 | Workflow 作用域 (400) | Input/Output 作用域 (300+a) | Context 作用域 | 栈帧 |
|---|---|---|---|---|
| **LIVE capability 暂停** | workflow input(仅) | 空(agent_input 不可得;node_completed 未触发) | 空(不变) | `[Workflow, Node, State, Capability]` |
| **LIVE state 暂停**(import-boundary trace prefix) | workflow input(仅) | 空 | 空(不变) | `[Workflow, Node, State]` |
| **POST-MORTEM state 暂停**(node_completed 重排后) | workflow input + 已完成 node 的输出(node_results_) | `"output"`(该 node 的输出,P6=真实值 / WireJson=null) | 空(不变) | `[Workflow, Node, State]` |
| **run 完成后** | workflow input + 全部 node 输出 + workflow output | `"output"`(末 node) | 空(不变) | `[Workflow]`(worker 已 join) |

**不可得项(诚实)**:
- `"input"`(agent/node 输入):wasm lane 不触发 `agent_input_hook`,node 输入在 guest 内物化,host 不可观察。`evaluate("input")` 返回 `"unknown identifier input"`。**不**从 workflow input 合成(node 输入是投影,非 workflow input,合成即说谎)。
- Handler 局部变量:guest 栈帧内,host 不可见(evaluator 时代也不可见,不变)。
- WireJson lane 的 node 输出:`node_completed` 时为 `NoneValue`(不透明 wire JSON,run 结束才解码)。变量栏显示 `null`。P6 lane 为真实值。DAP 可通过 `descriptor.frame_contract` 告知用户当前 lane。

**evaluate**:identifier-path 解析(不变)。`"input"` → unknown(诚实);node 名 → 该 node 输出(post-mortem,P6=真实/WireJson=null);`"output"` → 当前 agent 输出(post-mortem)。

### 12.9.6 阻塞 hook / 线程机制

**机制不变,且在 wasm lane 上安全**。DebugSession 的 worker 线程运行 workflow;hook 在 worker 上阻塞于 `resume_cv_`(`pause()`,`debug_session.cpp:660-680`);主线程处理 DAP 请求;`mutex_` 保护 frames/variable 状态。wasm lane 的阻塞发生在 `ImportCallback` 内(`wrapped_callback`/`wrapped_invoker`),阻塞的是 worker 上的同步 wasm3 实例——主线程从不触碰 wasm 实例,无竞争。

**死锁检查**:worker 在 CV 等待期间释放 `mutex_`(`pause()` 在持锁期间 check `stopping_`/step 状态后释放锁等待);主线程的 DAP 请求(stackTrace/scopes/variables/continue)获取 `mutex_` 读状态、`continue` notify CV——不与 worker 互锁。`disconnect` 置 `stopping_` + notify + join:worker 见 `stopping_` 后 `pause()` 立即返回(不阻塞),wasm lane 在 cancellation 请求后于下一个 import boundary 终止,join 及时完成。无死锁路径。

**与 evaluator 时代的唯一差异**:evaluator 的 hook 在 node loop 内同步触发(worker 阻塞在 evaluator 调用栈中);wasm lane 的 hook 在 import callback 内触发(worker 阻塞在 wasm3 host callback 中)。两者都是 worker 阻塞、主线程服务 DAP,线程模型相同。

### 12.9.7 编译 / 启动错误

- **前端错误**(parse/resolve/typecheck/validate):不变——`compile_source_file`(`debug_session.cpp:51-94`)失败 → stderr diagnostics + `terminated`(worker 启动前)。
- **facade wasm 编译错误**(lowering/layout/codegen):facade ctor 存 `compile_error_`;`run()` 返回 failed `WorkflowResult`(`wasm.compile-failed`,`wasm_workflow_runtime.cpp:87-96`)+ diagnostics。DAP 的 `execute()` 渲染 `result.diagnostics` → stderr + `terminated`。**§12.7.8 DiagnosticBag 升级是前置**:当前 `compile_error_` 是 `std::optional<std::string>`(无 SourceRange);若 WH-6 未落地 DiagnosticBag,WH-8 worklist 包含它(`compile_error_` → `DiagnosticBag`,`run()` 浮现 bag,DAP 渲染保留 SourceRange)。
- **sibling workflow codegen 失败**(compile-all 语义,§12.7.4):facade ctor 编译**所有** workflow(`wasm_workflow_runtime.cpp:58-81`),任一失败 → `compile_error_` 指名失败 workflow → DAP `run()` 返回 failed → stderr + `terminated`。诚实:程序不可编译,阻塞调试。DAP 不支持"只编译被调试 workflow"(facade 是 compile-all)。
- **workflow 名不存在**:DAP 在 launch 时按 IR 校验名字(`debug_session.cpp:189-196`),不存在 → `"no workflow found"` stderr + `terminated`(facade 构造前)。facade 的 `kWorkflowNotFound` 是防御路径。
- **session pre-run 失败**(admission/instantiation/pack):`run()` 返回 failed(`wasm.session-failed`,`:186-190`)→ stderr + `terminated`。

### 12.9.8 Big-bang + CMake link-edge diff

**源码翻转**(big-bang,同一变更):
- `debug_session.hpp`:include `runtime/engine/workflow_runtime.hpp` → `runtime/wasm_runner/wasm_workflow_runtime.hpp`;成员 `std::unique_ptr<ahfl::runtime::WorkflowRuntime> runtime_` → `std::unique_ptr<ahfl::runtime::wasm_runner::WasmWorkflowRuntime> runtime_`(#ifdef 门控,见 12.9.9)。
- `debug_session.cpp`:`config.state_entered_hook` → `config.hooks.state_entered_hook`(5 个 hook 同理);`config.capability_invoker` → `config.invoker`;新增 `config.cancellation_requested = [this]{ return stopping_.load(); }`;删除 `on_agent_input`/`agent_inputs_`;`on_state_entered` 合成 Node frame;断点分类器接入 `descriptor_for`。
- **删除** evaluator 专属路径:无并行保留。

**CMake diff**:
- `src/tooling/dap/CMakeLists.txt`:`ahfl_tooling_dap` 的 link 从 `ahfl_runtime_engine`(evaluator)翻到 gated `ahfl_runtime_wasm_runner`:
  ```cmake
  target_link_libraries(ahfl_tooling_dap
      PUBLIC
          ahfl_base_public
          ahfl_compiler_ir
      PRIVATE
          ahfl_base_json
          ahfl_compiler_syntax
          ahfl_compiler_semantics
  )
  if(AHFL_ENABLE_BACKEND_WASM)
      target_link_libraries(ahfl_tooling_dap PRIVATE ahfl_runtime_wasm_runner)
      target_compile_definitions(ahfl_tooling_dap PRIVATE AHFL_ENABLE_BACKEND_WASM=1)
  endif()
  ```
  注意:`ahfl_runtime_wasm_runner` PUBLIC 依赖 `ahfl_runtime_engine`(neutral WorkflowResult 类型),故 DAP 仍间接获得 engine 的**类型**头文件,但不再链接 evaluator 实现。`ahfl_runtime_value` 由 wasm_runner PRIVATE 传递——DAP 用 `Value` 类型,需显式 link `ahfl_runtime_value`(wasm_runner 的 PRIVATE 不传播)。
- `src/runtime/engine/CMakeLists.txt:46-54`:`ahfl_runtime_evaluator` 从 PUBLIC 翻为 **PRIVATE**(WH-6 切 CLI + WH-8 切 DAP 后,生产 includer 为零)。
- **测试目标补 explicit evaluator link**(edge flip 后不再传递):`tests/unit/runtime/wasm_runner/wasm_runner.cpp`(evaluator differential)、`tests/integration/durable_resume_capstone.cpp`、`tests/unit/runtime/engine/workflow_runtime.cpp`、`tests/conformance/evaluator_engine.cpp`、`tests/integration/reference_workflow_recovery_worker.cpp`、`tests/integration/core_wasm_e3_probe.cpp`。

### 12.9.9 WASM=OFF 继承策略(DAP 具体化)

继承 §12.7 策略:**launch/execute 拒绝,非执行功能可用,gate 只在 composition edge**。

- `ahfl_tooling_dap` 在 WASM=OFF 下**不链接** `ahfl_runtime_wasm_runner`(gated)。`runtime_` 成员、config+ctor、`execute()` 用 `#ifdef AHFL_ENABLE_BACKEND_WASM` 门控。
- **WASM=OFF launch 拒绝**(pinned diagnostic):
  ```
  ahfl-dap launch requires the embedded wasm engine; this build was configured with -DAHFL_ENABLE_BACKEND_WASM=OFF. Rebuild with the default (ON) to debug workflows.
  ```
  发到 stderr(`emit_output("stderr", ...)`)+ `terminated` 事件,返回 `"{}"`。
- **非执行功能可用**:`initialize`、`setBreakpoints`(BreakpointManager 仍工作;但 `breakable_lines_` 由 session 注册,session 不存在 → `verified:false`,诚实)、`threads`(返回单 ahfl-main)、`disconnect`。`stackTrace`/`scopes`/`variables`/`evaluate` 返回空/错误(无 session)。
- **gate 边界**:仅 `runtime_` 成员 + launch 内 config/ctor + worker 启动 + `execute()`。hook lambda 与 `on_*` 方法编译(不引用 `runtime_`),是 DAP 自身逻辑(WASM=ON/OFF 共享),不散落 `#ifdef`。

### 12.9.10 测试计划

**既有 `dap_basic.cpp`(27 tests)分诊**:

| 类别 | tests | 处置 |
|---|---|---|
| PASS 不变 | 6, 9, 10, 11, 15, 17, 20, 23-27 | 验证通过 |
| PASS-but-degraded(post-mortem state 暂停) | 7, 8, 12, 13, 14, 16 | 重写断言:stopped 事件触发、栈显示 state,但断言这是 post-mortem(run 已完成);16 断言 Node frame 由 `on_state_entered` 合成(形状 `[Workflow, Node, State]`) |
| BREAK(agent input / node 输出) | 18, 19, 21, 22 | 见下 |

**BREAK tests 重写**:
- **18/19**(`variables(301)` 期望 `"input"`):wasm lane 无 `agent_input_hook` → `"input"` 不可得。重写:在 post-mortem 暂停点断言 Input/Output 作用域暴露 `"output"`(node_completed 重排后),或断言 LIVE 暂停点该作用域为空。删除 `"input"` 断言。
- **21**(Workflow 作用域在 second Done 暂停时有 `"first"`):node_completed 重排后,second Done post-mortem 暂停时 `first` 与 `second` 的 node_completed 均已触发 → `"first"` 可用。断言通过。**lane 断言**:通过 `descriptor.frame_contract` 钉住 vars workflow 的 lane——若 P6,断言真实输出值;若 WireJson,断言 `null`。实现者运行时确认 lane 并钉死(codegen 改 lane 决策时测试会破,这是正确的告警)。
- **22**(`evaluate("input.tag")`):`"input"` 不可得。重写:`evaluate("input.tag")` 断言 `"unknown identifier input"`;改断言 `evaluate("first.tag")`(node 输出,post-mortem)或 `"output.tag"`。

**新增测试**:
1. **capability 断点 LIVE 暂停**:`kCapabilityWorkflowSource`,在 `DoWork` 调用行设断点 → `verified:true`,stopped 事件在 capability 调用前触发,栈含 Capability frame,`continue` 后 run 完成。
2. **state 断点分类**:`kStepperAgent`(无 capability)的 state 断点 → `verified:false` + post-mortem message;`kCapabilityWorkflowSource` 的 capability 前 state → `verified:true` + live message;capability 后 state → `verified:false` + post-mortem message。
3. **cancellation**:capability workflow 运行中 `disconnect` → worker join 在有界时间内完成(下一个 import boundary 终止);无 import workflow `disconnect` → run 完成后 join(有界)。
4. **WASM=OFF launch 拒绝**:WASM=OFF 构建下 launch → pinned diagnostic + `terminated`;`setBreakpoints`/`threads` 仍响应。
5. **post-mortem node 输出**:node_completed 重排后,post-mortem 暂停点 Workflow 作用域含已完成 node 输出。
6. **sibling codegen 失败**:程序含一个 codegen 失败的 sibling workflow → launch 后 run 返回 failed,diagnostic 指名失败 workflow。

### 12.9.11 有序 worklist

1. **facade cancellation**:`WasmWorkflowRuntimeConfig` + `WorkflowSessionConfig` 加 `cancellation_requested`/`interruption_requested`;`wrapped_callback` 顶部 + `run_workflow_session` 入口检查;取消时构建 `Cancelled`/`Interrupted` 结果 + pinned diagnostic。(wasm_runner + wasm_host)
2. **facade descriptor 访问器**:`descriptor_for(workflow_name)`。(wasm_workflow_runtime.hpp)
3. **codegen descriptor 扩展**:`CoreWasmStateWalk::last_cap_walk_index` 填充。(core_wasm_codegen.hpp/.cpp)
4. **facade node_completed 重排**:post-run per-node(schedule 顺序)`node_completed` → 该 node 的 state hooks;§12.1 保证表 dated 修订。(workflow_session.cpp:1087-1237)
5. **§12.7.8 DiagnosticBag**(若 WH-6 未落地):`compile_error_` → `DiagnosticBag`,`run()` 浮现。(wasm_workflow_runtime)
6. **DAP 翻转**:include/成员/config hooks/invoker/cancellation;删 `on_agent_input`/`agent_inputs_`;`on_state_entered` 合成 Node frame。(debug_session.hpp/.cpp)
7. **DAP 断点分类器**:`breakable_lines_` 收窄为 state-handler 行;`descriptor_for` + `last_cap_walk_index` 精确分类;`setBreakpoints` verified/message;launch 后 unsolicited breakpoint 事件。(debug_session.cpp + dap_server.cpp)
8. **DAP WASM=OFF gate**:`runtime_`/config/ctor/execute `#ifdef`;launch 拒绝 pinned diagnostic。(debug_session.cpp)
9. **CMake**:`ahfl_tooling_dap` gated wasm_runner link + `AHFL_ENABLE_BACKEND_WASM=1` + explicit `ahfl_runtime_value`;engine evaluator edge PUBLIC→PRIVATE;测试目标补 explicit evaluator link。
10. **测试**:dap_basic 分诊重写 + 6 个新增测试。

### 12.9.12 验收 / 验证标准

- `src/tooling/dap/` 对 `runtime/engine/workflow_runtime.hpp` 的 include 为 **零**(grep 验证);`ahfl-dap` 链接 `ahfl_runtime_wasm_runner`(gated)。
- `ahfl_runtime_engine` 的 `ahfl_runtime_evaluator` edge 为 **PRIVATE**;WH-6+WH-8 后生产 includer of `workflow_runtime.hpp` 为零(grep 验证,测试目标除外)。
- `dap_basic` 全部测试通过(按 12.9.10 重写后的诚实契约);6 个新增测试通过。
- WASM=OFF 构建:launch 拒绝并输出 pinned diagnostic;`setBreakpoints`/`threads`/`disconnect` 仍响应。
- Cancellation:capability workflow 运行中 `disconnect` → worker join 有界完成;无 import workflow → run 完成后 join。
- 断点验证:capability 断点总是 `verified:true`;state 断点按 descriptor 精确分类(live/post-mortem/never),无静默接受不可验证断点(Principle 5)。
- 新鲜构建 `-Wall -Wextra -Werror` 干净;asan 干净。
- `ctest --preset test-dev -L dap` 全绿。

### 12.9.13 被否方案(按分叉)

- **断点活性**:(a) 所有 state 断点 `verified:true`(evaluator 时代的谎言,违反 Principle 5)——否;(b) 保守 LIVE + 运行时纠正(DAP 重新发明 codegen 权威,不精确)——否;(c) dry-run 观察 prefix(副作用、重)——否。**选**:descriptor 扩展 `last_cap_walk_index` + 精确分类。
- **Cancellation**:(a) 放进 `WasmRuntimeHooks`(hook 是值观察通道,非控制通道;evaluator 也在 config)——否;(b) wasm3 指令级中断(wasm3 无 mid-instruction interrupt,需注入轮询,重且不可靠)——否;(c) 不支持 cancellation(disconnect 在长计算上挂起——但无 import 计算有界,可接受;capability workflow 仍需边界取消)——部分否。**选**:config 级 flag,import boundary 检查(与 evaluator 对齐,粒度更细)。
- **node_completed 时机**:(a) 保持 post-run-after-states(暂停点无 node 输出,调试器致盲)——否;(b) LIVE node_completed(不可能——输出在 guest 内直到 run 结束)——否;(c) per-node 重排——**选**。
- **monotonic_clock**:(a) 为"DAP 时序"加入 facade(前提过时,DAP 从不使用)——否;(b) 为 event offset 加入(DAP 不消费 event 流,独立 slice)——否。**选**:不引入。
- **agent_input_hook**:(a) 保留 `on_agent_input`(workflow lane 死代码,Principle 1)——否;(b) 从 workflow input 合成 node 输入(node 输入是投影,合成即说谎)——否;(c) 删除 + 诚实"input 不可得"契约——**选**。
- **WASM=OFF**:(a) evaluator 回退(禁止并行路径,Principle 1)——否;(b) 构建失败(破坏非执行功能)——否;(c) launch 拒绝——**选**(继承)。

### 12.9.14 WH-8 前置决策修订(2026-10-03):state 断点活性与 WH-5c.4 post-run-only hook 冲突裁决

> 专用决策代理,无人类 owner 门;coordinator 独立复核全部承重事实。HEAD `1b8f9cfb`。WH-8 builder 零编辑 STOP 后立项。本节裁决 §12.9.1(2026-09-30,pin `9bca5507`)与 WH-5c.4(commit `251ebdb7`,2026-10-02)之间的冲突:§12.9.1 要求 P6 workflow 的 state 断点在 capability import boundary LIVE 暂停,但 WH-5c.4 把 `state_entered_hook` 在 import boundary 的触发抑制为 post-run-only(为 hybrid P6+opaque 模块的 schedule-order 观察奇偶性),使 §12.9.1 的 LIVE 行、`last_cap_walk_index`、step 语义、§12.9.5 LIVE-state 行、新测试 1-2 按原文不可实现。

### 12.9.14.1 经验事实(file:line at HEAD `1b8f9cfb`)

1. **WH-5c.4 抑制 live 触发的机制与理由**:`workflow_session.cpp:885-887` 注释 "state_entered_hook is NOT captured here";`:911-914`(wrapped_callback import boundary trace decode)与 `:1847-1849`(post-run full decode)传 `StateEnteredHook{}`;`:1911-1912` `rebuild_and_fire_states`(`:316-338` 定义)post-run 按 schedule 序触发一次。理由:hybrid P6+opaque 模块中 opaque node 不写 trace ring,live 触发会 (a) 漏掉 opaque node 状态,(b) 使 P6 状态在 import 时刻相对 schedule 序在前的 opaque node 乱序。conformance census 从 hook 触发收集状态向量并渲染(coordinator 复核:`tests/conformance/native_engine.cpp:61-70` 仅装 `config.hooks.state_entered_hook` 收集 `states`;evaluator 侧 `evaluator_engine.cpp:94-100` 同形),hook 触发序是字节奇偶输入。抑制是 census 73/0 奇偶性的正确修复。
2. **设计意图明确且早于 WH-5c.4**:§12.1 保证表(本文件 :428 附近)选 workflow `state_entered_hook` = "IMPORT-BOUNDARY-LIVE (trace prefix at each cap import) + POST-run (remainder)",并明确**否决** Option B(post-run-only)因为 "it makes DAP over workflows post-mortem, contradicting WH-8's promise"。§12.9.1 在 WH-5c.4 之前写成,假设 P6 live 触发。
3. **evaluator LIVE 触发**:`src/runtime/engine/workflow_runtime.cpp:1268-1278` 在 node loop 内逐 state 同步触发 `state_entered_hook`(Kahn schedule 序);resume 时 node loop 重跑、hook 重触发。
4. **observation 文档与 census 是两个通道**:observation 文档从 `collected_states` 构建(`workflow_session.cpp:2409` 附近 `.states = std::move(collected_states)`,schedule 序),但 census 不消费文档的 state_sequence,而消费 hook 收集的向量——故 hook 通道必须保持 schedule 序。
5. **capability hooks 今日仍 LIVE**:`wrapped_invoker` 内 capability_invoked_hook(pre-call)/capability_result_observer(post-invoker)。cap 行 LIVE 暂停不受本决策影响。
6. **agent lane LIVE per step**:`src/runtime/wasm_host/wasm_agent_runner.cpp:339-343`。不受影响。
7. **suspend 路径**:suspend 时 post-run 段仍执行;P6 rebuild post-suspend 触发 ordered hook;import boundary 的 trace prefix decode 在 capability 调用前运行,live hook 会在 suspend 点触发。
8. **resume/memo**:D1b resume 是 fresh instance whole-module replay;memo 命中的 capability 调用在 import 处从 memo 供给,import callback 仍触发,trace prefix decode(及 live hook)对各 node prefix 状态重触发。与 evaluator 一致(事实 3)。
9. **DAP execute/terminated 流程**:`debug_session.cpp:370-402`,`run()` 在 worker 上,hook 在 `run()` 内触发,`terminated` 在 `run()` 返回后发。故 post-mortem hook 触发也发 `stopped` 且先于 `terminated`——事件序保持,但 run 的工作已完成。
10. **Kahn 序严格顺序**:node 按稠密 Kahn schedule 序执行(node-event buffer 是稠密前缀),无交错。故 P6 node 的 live 触发按 schedule 序;schedule 序问题只关于合并流中的 opaque node。
11. **lane 选择**:`CoreWasmExecutionDescriptor.frame_contract`(`core_wasm_codegen.hpp:223-226`,默认 WireJson,P6Frame 时填充 frame/frame_section)。codegen 端 workflow 为 P6Frame iff 任一 gathered agent 为 p6(p6 含 has_computed_final)。coordinator 复核 dap_basic 两个 fixture:`kSimpleWorkflowSource`(computed literal final、无 cap)与 `kCapabilityWorkflowSource`(computed literal final;`Init` 仅 `goto Done`,`DoWork(arg)` 在 **Done** 中调用——故 `last_cap_walk_index=1`,Init walk-index 0 ≤ 1 判 LIVE)**均为 P6**。WireJson 无 trace ring,live hook 永不触发。
12. **dap_basic.cpp 27 个测试**:Test 7/8/12/13/14 用无 cap 的 kSimpleWorkflowSource(按 §12.9.1 即 POST-MORTEM,last_cap_walk_index=0);Test 9 用 kCapabilityWorkflowSource;Test 18/19/21/22 断言 agent-input/node-output。Test 7 的 LIVE 钉是暂停时 frames 中无 terminated 事件。
13. **业界参考(DAP/LLDB)**:DAP `setBreakpoints` 支持 `verified:false` + `message`(标准 deferred 模式)。debuggee 终止后发 `stopped` 语义不诚实:主流调试器(LLDB/GDB/VS Code)在 run 结束时发 `terminated`;post-mortem/time-travel 调试(LLDB core file;rr/Pernosco)显式标注是录像。DAP 无一等的 "this is a recording" 指示。

### 12.9.14.2 选项与裁决

**(a) 双 hook 解耦** — 新增 `state_entered_live_hook`(P6 import boundary trace prefix,debug 通道)+ `state_entered_hook` 保持(post-run schedule 序全序列,observation 通道,WH-5c.4 行为不变)。DAP 装两个,dedup。
**(b) 全 POST-MORTEM** — 删 `last_cap_walk_index`,分类器 4 条消息,step = post-run 遍历。
**(c) 混合(仅可证明 schedule 序处 live)** — 塌缩为 (a):node 总按 schedule 序执行(事实 10),"可证明 schedule 序" 对 P6 node 恒真;覆盖缺口(opaque/WireJson/no-cap)由 ordered hook post-mortem 补。

**裁决:选 (a)。**

理由:
1. (b) 使所有 workflow state 断点 post-mortem,DAP 退化为录像导航器;§12.1 Decision 1 已明确否决("it makes DAP over workflows post-mortem, contradicting WH-8's promise")。capability hooks 已 LIVE(事实 5),紧邻的 cap LIVE 断点与全 post-mortem state 断点不一致,用户无法理解。
2. (b) 的"终止后 stopped"是语义不诚实的 DAP 构造(事实 13)。
3. (b) 永久丢失:mid-run state 变量(后续 node 失败前)、mid-run disconnect 交互、在 cap 执行前读 state(agent workflow 最有用的断点——"在副作用前暂停")。
4. 双 hook 保留 §12.1 的 P6 live 承诺,同时保留 WH-5c.4 的 census schedule 序奇偶性;WH-5c.4 恰好证明单 hook 无法同时服务两者。
5. (c) 塌缩为 (a)。

**AHFL-specific 分歧理由**:主流调试器调试原生代码,每条指令可观察,断点绑地址同步 trap。wasm lane 的 host 是 **wasm3 host**,不是 ptrace 控制器:guest 是密封 wasm3 实例,host 只在 capability import 与 run2 返回后获得控制。live state 观察天然 import-boundary 粒度、trace-prefix-only,这是嵌入契约而非调试器限制。双 hook 是该契约的诚实表达:要 live 观察(调试器/progress monitor/tracer)用 `state_entered_live_hook`;要 schedule 序观察(conformance/reporting)用 `state_entered_hook`。把两者混进一个 hook 正是 WH-5c.4 乱序/双发问题的根因。参考层级:LLDB 有 live process event 与 structured-data plugin;DAP 有 stopped(live)与 terminated/output(post-run)——两个时刻两个通道是业界标准。**这不是 debug-only flag 或并行路径(Principle 1)**:是 facade 的新事件通道,语义契约独立,任何 host 可消费,无环境开关、无新旧共存。

### 12.9.14.3 机制(具体)

1. `WasmRuntimeHooks`(`src/runtime/wasm_host/wasm_runtime_hooks.hpp`)加 `state_entered_live_hook`,签名与 `state_entered_hook` 逐字节相同。头注释:仅 workflow session 在 P6 import boundary(trace prefix)触发;WireJson 永不触发(无 trace ring);post-run 永不触发;agent runner 不触发(其 `state_entered_hook` 本就 live per step)。
2. `WorkflowSessionConfig` 加 `StateEnteredHook state_entered_live_hook;`。
3. facade 映射 `config.hooks.state_entered_live_hook` → `session_config.state_entered_live_hook`。
4. `workflow_session.cpp` 的 wrapped_callback import boundary 段把 `StateEnteredHook{}` 改为传 `config.state_entered_live_hook`(landing 形态经 fix-forward 后位于 trace prefix decode 处,且在 live hook 阻塞返回后、lane 分派前**二次**检查 cancellation);post-run full decode **保持** `StateEnteredHook{}`(只收集不触发——remainder 定义上 POST-MORTEM)。原 `rebuild_and_fire_states` 在 landing 时更名 `rebuild_states`(只收集不触发);`state_entered_hook` 的触发移入 post-run section 10 的 per-node 交错循环(每个 schedule index i:先 `node_completed(i)`,再触发该 node 的 state hooks;decode 失败 break 后的后缀 index 仅补触发 state hooks),即 §12.9.4 item 2 的落地形态;`reconstruct_wirejson_states` 同样只收集。
5. DAP 装两个 hook:
   - `state_entered_live_hook` → `on_state_entered_live`:帧合成(同 `on_state_entered` 的 Node/State 帧规则)+ 仅对 LIVE 分类(P6 + has_capability + walk-index ≤ `last_cap_walk_index`)的断点检查 + `pause("breakpoint", …)`,并递增 `live_count_[(node_name, state_name)]`(实现形态:计数 map,非集合——见下条)。
   - `state_entered_hook`(ordered,post-run)→ `on_state_entered`:递增 `post_count_[key]`,当 `post_count ≤ live_count_[key]` 时跳过该 occurrence(已 live 暂停);其余帧合成 + POST-MORTEM 断点检查 + `pause("pause", …)`。dedup 身份 `(node_name, state_name, occurrence-within-node)`(key 以 `\x1f` 分隔 node/state)。trace ring 为线性 append-only,live 流是 post-run 全序列的严格前缀,两序列 occurrence 对齐。
   - **dedup 身份**:`(node_name, state_name, occurrence-index-within-node)`。live hook 经 `last_trace_count` 差分按 walk 序触发 prefix,ordered hook 按 walk 序触发全序列,两序列 occurrence 对齐。

### 12.9.14.4 §12.9 delta 枚举

- **§12.9.1 分类表**:LIVE 行加 `frame_contract == P6Frame` 条件;新增 WireJson POST-MORTEM 行。初版 7 行(英文 pinned 消息,逐字);2026-10-04 WH-8 fix-forward F6 追加第 8 行(共线歧义,见下)。

| 断点位置 | 分类 | verified | message(pinned) |
|---|---|---|---|
| capability 调用行 | LIVE(总是) | true | (无 message) |
| state handler 行,P6 + has_cap + walk-index ≤ last_cap_walk_index | LIVE | true | `state entered before capability boundary (live pause)` |
| state handler 行,WireJson lane(任意 state) | POST-MORTEM | false | `state hooks are post-run on the WireJson lane (no trace ring); pauses after run completes (post-mortem)` |
| state handler 行,P6 + 无 node has_capability | POST-MORTEM | false | `state has no capability boundary in its runner; pauses after run completes (post-mortem)` |
| state handler 行,P6 + has_cap + walk-index > last_cap_walk_index | POST-MORTEM | false | `state is entered after the last capability boundary; pauses after run completes (post-mortem)` |
| state handler 行,state 不在 walk 中(untaken branch) | NEVER | false | `state is not on the runner's walk (untaken branch); never paused` |
| 非 state-handler 行 | NOT BREAKABLE | false | `line is not a state handler; only state handlers and capability calls are breakable` |
| 同一物理行被 ≥2 个 state handler 共享且 per-record 裁决不一致(2026-10-04 fix-forward F6 追加) | AMBIGUOUS SHARED LINE | false | `line is shared by multiple state handlers with differing pause behavior; put each handler on its own line to bind a specific one` |

行 8 注:`program`/`flowDecl`/`stateHandler` 之间语法上仅以空白分隔,两个 handler(同/异 agent)可合法共线。分类器收集该行全部 `(agent,state)` 记录逐条裁决:裁决(verified+message)全同则归并为该单一裁决(消息逐字不变);任一不同则走行 8。命中/暂停解析以 `(agent_name,state_name)` 为准,共线断点仍对每个 handler 各自暂停(DAP 正确语义)。回归:T6a 混合(LIVE vs 无 cap)、T6b 一致(双 caps[] WireJson)。

- `last_cap_walk_index` 工单 3 **保留**(option a 下仍是 P6 LIVE/POST-MORTEM 分类权威)。
- step 语义保留并加 lane 注:LIVE 区间(P6、最后 cap boundary 之前)逐 import boundary 推进;其后 / WireJson / 无 cap 走 post-run 重建遍历;DAP dedup live-paused occurrence。
- **§12.9.4 item 2**(node_completed per-node 重排):**不变**(独立)。live hook 在 import 触发(先于任何 node_completed),LIVE 暂停点 node_results_ 为空,与 §12.9.5 LIVE 行一致。
- **§12.9.5**:LIVE-state 行保留(现由 `state_entered_live_hook` 支撑),加注 LIVE state 暂停仅 P6;WireJson 全 POST-MORTEM。
- **§12.9.10**:新测试 2 加 WireJson case;**新增测试 8**(P6 live state 暂停:kCapabilityWorkflowSource 的 Init(walk-index 0,DoWork 前)→ verified:true + live message → stopped 先于 cap 调用(暂停时 capability_invoked 计数 == 0)→ continue → 完成)、**测试 9**(同一 fixture,Init 恰好一次 stopped,钉 dedup 无双发)。
- **§12.9.12 验收**:加 P6 live state 暂停在 cap 调用前发 stopped;debug 通道每 state 恰好一次;census 73/0 不变。
- **§12.1 保证表 dated 修订**:原 workflow `state_entered_hook` 单格保证("IMPORT-BOUNDARY-LIVE + POST-run (remainder)")拆为两行(表下加 dated pointer,原文保留作历史档案,同 §12.15.7 先例):`state_entered_hook` = POST-run schedule 序全序列(observation 通道,WH-5c.4 不变);`state_entered_live_hook`(新)= IMPORT-BOUNDARY-LIVE(trace prefix,P6 only,debug 通道)。

### 12.9.14.5 dap_basic 分诊 delta

§12.9.10 原分诊(6/9/10/11/15/17/20/23-27 不变;7/8/12/13/14/16 post-mortem 改写;16 含 Node-frame 形状;18/19/22 input-unavailable 改写;21 node output + lane pin)整体保留,补一条:Test 7/8/12/13/14 的无 cap fixture(POST-MORTEM)断言改写为 post-mortem 语义(stopped 在 run() 内 post-run、先于 terminated;栈显示 state;run 已完成——断言 workflow 输出已设或 node_completed 已触发);为**不丢失 LIVE 覆盖**,新增测试 8-9 用 P6 kCapabilityWorkflowSource 钉 Init 在 DoWork 前的 LIVE 暂停与 dedup(这是 option b 会永久删除的 LIVE 回归守卫)。6 个新测试最终形态:1 cap LIVE;2 state 分类(P6 + WireJson 两 case);3 cancellation;4 WASM=OFF 拒绝;5 post-mortem node output;6 sibling codegen failure;外加 8 live state 暂停;9 dedup。

### 12.9.14.6 风险与缓解

1. census 73/0:census 只装 ordered `state_entered_hook`,行为不变;验收门 `conformance_wasm_native_runner`(73 agreed/0 skipped)+ `conformance_wasm_node_runner`(69/2)。
2. debug 通道双发:dedup 身份 `(node,state,occurrence)`,测试 9 钉恰好一次。
3. hybrid 语义:live hook 仅对 P6 状态在 import 触发;opaque 状态永不 live;ordered hook 仍发全 schedule 序。wh5b hybrid Kahn-reordered census case 保持绿。
4. suspend/resume:suspend 时 live hook 在 pending cap 前触发,ordered post-suspend,dedup 适用;resume whole-module replay 重触发(与 evaluator 一致)。DAP resume 专项测试属后援切片。
5. step 粒度:LIVE 区间 import-boundary 粒度(粗于 evaluator 逐 state)——诚实的 host 可观察粒度,经 verified/message 文档化。
6. WireJson vs P6:分类器 lane 条件防 false LIVE;测试 2 WireJson case 锁死。
7. 过时头注释:`wasm_runtime_hooks.hpp` 的 state_entered_hook 注释在本 slice 重写为双 hook 准确文档。

### 12.9.14.7 验收 / 验证

- `src/tooling/dap/` 对 `runtime/engine/workflow_runtime.hpp` include grep-zero;`ahfl_tooling_dap` gated 链接 `ahfl_runtime_wasm_runner`。
- conformance_wasm_native_runner 73/0;conformance_wasm_node_runner 69/2;`ctest -L dap`、`ctest -L wasm` 全绿。
- 新测试 8/9 真实 transcript 钉死(暂停时 cap 计数 0;Init 恰好一次 stopped)。
- 测试 2 WireJson case:verified:false + post-mortem WireJson 消息逐字。
- WASM=OFF scratch:launch 拒绝字节 + setBreakpoints/threads/disconnect 仍响应。
- 新鲜构建 -Wall -Wextra -Werror 干净;ASan 干净(dap + wasm 标签,coordinator 独立跑)。
- `grep -n "StateEnteredHook{}" src/runtime/wasm_host/workflow_session.cpp`:仅 post-run full decode 一处,不在 import boundary。


## 12.10 WH-9 decisions (2026-09-30, dedicated decision agent, no human gate)

> Commissioned 2026-09-30; recorded 2026-10-01。HEAD at commission: `85f5132e`。
> WH-6(CLI wasm-only run)、WH-7(REPL wasm cutover)、WH-8(DAP cutover)在该 HEAD **已决策但未实现**。以下 file:line 钉于 `85f5132e`;builder MUST 在 WH-6/7/8 落地后重新核对行号,但**决策本身稳定**——WH-6/7/8 的任何实现选择都不改变本节结论。
>
> **Coordinator sequencing note(2026-10-01):** §12.10.9 staging 第 1 步/W1-W3 提到的 CLI/REPL/DAP 切换**不是** WH-9 commit 的内容——它们是 §12.7/§12.8/§12.9 已决策的独立切片(WH-6/7/8),各自 implement → adversarial review → fix-forward 后独立落地。WH-9 从"零生产调用者"状态开始,只包含删除本身(一个原子 `BREAKING CHANGE` commit)。本节其余内容(删除 census、closure 裁决、EvalError 裁决、Node 降级、测试分诊、验收)不变。

### 12.10.1 Closure arm fork — DECISION: Option (a), big-bang delete

**Chosen: 删除 `Value` closure arm、`InterpreterClosureHandle`、`InterpreterClosure`/`InterpreterClosureRef`、`make_interpreter_closure`、`next_closure_id`、`ValueKind::Callable`、全部 `value.cpp`/`value_json.cpp` closure arms、`frame_packer.cpp` 拒绝检查、`debug_session.cpp` 类型名 lambda——在同一 commit。**

**Rejected: Option (b),保留防御性 opaque-id handle。** 唯一生产者死亡之时,防御性 arm 就是死重。Principle 1 禁止 "just in case" 留代码;Principle 3(hash-consed, flat)被一个无任何路径可构造的 variant arm 违反。若未来 wasm lane 需要一等 callable Value,那是一个新 RFC + 新设计,不是幽灵 arm。

#### 7-stem observation 问题的精确回答

WH-S 复审的阻断问题:*closure arm 删除后,7 个 closure-stem conformance case 怎样?*

**7 个 stem 的 `output_json` 不含 closure。** 已在 HEAD 逐个 manifest 核实:

| Stem | `output_json` 形状 |
|------|---------------------|
| `fb1_direct_call` | `{"_type":"wasm::fb1_direct_call::Frame","value":"identity"}` |
| `fb1_aggregate_direct_call` | plain struct(无 callable) |
| `fb3_byvalue_capture` | plain struct(无 callable) |
| `fb3_higher_order` | plain struct(无 callable) |
| `fb3_nested_activation` | plain struct(无 callable) |
| `fb3_nested_lambda_flow` | plain struct(无 callable) |
| `fb4_effect_clause_pure_body` | plain struct(无 callable) |

这些程序中的闭包是**wasm lane 内部状态**——funcref table 的 `call_indirect`,从不跨 frame 边界。codegen 拒绝闭包跨 fn 边界(`core_wasm_codegen.cpp:4188`、`:4363`、`:4288`);frame packer 拒绝 closure Value(`frame_packer.cpp:140`)。native lane 的输出 Value 由 wire JSON 经 `value_from_json` 解码,该函数**无 closure arm**——结构上不可能产出 closure。`render_observation`(`observation_document.cpp:86`)对解码后的 Value 调 `runtime::value_to_json(*output)`;closure arm 删除后,`value_to_json` 没有可命中的 closure 分支。

**结论:删除 closure arm 改变 ZERO 个 blessing,不需要任何非 Value renderer 源码变更。** 7 个 stem 保留,manifest 的 `node_observation_skip: "evaluator_surface_awaits_kr68"` 退役(见 §12.10.4),改为数据驱动的 blessing-or-expectation 比较(见 §12.10.6)。

#### Closure 删除 census(file:line at HEAD `85f5132e`)

**生产源码:**

| 文件 | 行 | 删除内容 |
|------|-------|-----------|
| `src/runtime/value/value.hpp` | `:49` | `struct InterpreterClosure;` 前向声明 |
| | `:50` | `using InterpreterClosureRef = std::shared_ptr<const InterpreterClosure>;` |
| | `:52-59` | `struct InterpreterClosureHandle { id, descriptor, operator== }` |
| | `:213` | `ValueNode` variant arm `InterpreterClosureHandle` |
| | `:240` | `ValueKind::Callable` |
| | `:417` | `make_interpreter_closure` 声明 |
| | `:20-48` | closure rationale 注释(更新为删除后事实) |
| `src/runtime/value/value.cpp` | `:202-211` | `compare_values` closure arm(id 序) |
| | `:345-351` | `structurally_equal` closure arm |
| | `:400-401` | `value_kind` → `Callable` |
| | `:543-546` | `print_value` `<lambda/id>` |
| | `:559-576` | `next_closure_id` + `make_interpreter_closure` |
| | `:786-791` | `clone_value` closure arm |
| `src/runtime/value/value_json.cpp` | `:45` | `kOpaqueClosureJson = R"({"_callable":"runtime"})"` |
| | `:172-178` | `write_json_impl` closure arm(Strict→拒绝,否则→opaque) |
| | `:268` | `try_value_to_json` strict 拒绝路径(arm 亡 → 路径亡) |
| | `:276` | `hash_values` strict closure 拒绝(arm 亡 → 路径亡) |
| `src/runtime/evaluator/evaluator.cpp` | `:271-283` | `eval_lambda_expr`——**唯一生产 closure 生产者** |
| `src/runtime/evaluator/evaluator.hpp` | `:42-53` | `InterpreterClosure` struct 定义(随目录亡) |
| `src/runtime/wasm_host/frame_packer.cpp` | `:140` | `std::holds_alternative<InterpreterClosureHandle>` 拒绝(arm 亡 → 检查亡) |
| | `:290`,`:356` | 提及 closure 的注释(更新措辞) |
| `src/tooling/dap/debug_session.cpp` | `:119` | `value_type_name` 的 `[](const InterpreterClosureHandle&) → "Callable"` lambda(删除后不可达) |

**测试源码:**

| 文件 | 行 | 删除内容 |
|------|-------|-----------|
| `tests/unit/runtime/engine/wire_value.cpp` | `:39-42` | `make_closure_value()`(需要 evaluator.hpp 的 InterpreterClosure) |
| | `:103-167` | `test_closure_rejected_top_level`、`test_closure_rejected_nested`、`test_closure_observation_spelling`、`test_closure_identity` |
| | `:305-308` | `main()` 中 4 个 closure 测试调用 |
| | `:12` | `#include "runtime/evaluator/evaluator.hpp"` |
| `tests/unit/runtime/wasm_host/frame_packer_reader.cpp` | `:713`,`:866`,`:885`,`:908`,`:977` | 测试专用 closure 构造(packer 拒绝测试) |
| `tests/unit/runtime/wasm_host/capability_import.cpp` | `:713` | 测试专用 closure 构造 |

**Post-cut grep gates(commit 后 builder 执行):**

```
grep -rn "InterpreterClosure" src/ include/ tests/   # must be zero
grep -rn "Callable" src/runtime/value/               # must be zero
grep -rn "_callable" src/ tests/                     # must be zero
grep -rn "make_interpreter_closure" src/ tests/      # must be zero
```

注意:`grep -rn "evaluator" src/` 会有 **const-evaluator 误报**(如 constexpr-evaluator 注释、third_party 内)。builder grep `ahfl::evaluator` / `runtime::evaluator` / `ahfl_runtime_evaluator` / `evaluator::`——`third_party/` 之外必须为零。

### 12.10.2 删除 census 定稿

#### 生产:`src/runtime/evaluator/` — 5840 LOC,整目录删除

| 文件 | LOC |
|------|-----|
| evaluator.cpp | 2261 |
| builtins.cpp | 2014 |
| executor.cpp | 415 |
| runtime_fn_table.cpp | 350 |
| eval_context.cpp | 134 |
| pattern_match.cpp | 188 |
| evaluator.hpp | 123 |
| builtins.hpp | 67 |
| eval_context.hpp | 55 |
| executor.hpp | 117 |
| runtime_fn_table.hpp | 98 |
| pattern_match.hpp | 18 |
| CMakeLists.txt | — |

#### 生产:engine 库 evaluator 驱动 TU

| 文件 | LOC | 命运 |
|------|-----|------|
| `src/runtime/engine/agent_runtime.cpp` | 356 | **删除**(evaluator 驱动 agent 执行) |
| `src/runtime/engine/capability_eval.cpp` | 251 | **删除**(evaluator 驱动 capability 求值) |
| `src/runtime/engine/native_host_binding.cpp` | 115 | **删除**(evaluator 驱动 native host binding) |
| `src/runtime/engine/workflow_runtime.cpp` | 1597 | **删除**(evaluator 驱动 `WorkflowRuntime::run`;WH-6/7/8 后零生产用户——已核:只有 workflow_run.cpp 与 debug_session.cpp include workflow_runtime.hpp,届时均已切换) |
| `src/runtime/engine/workflow_runtime.hpp` | — | **删除**(定义 WorkflowRuntimeConfig + WorkflowRuntime;wasm lane 有自己的 `WasmWorkflowRuntimeConfig`,`wasm_workflow_runtime.hpp:38`) |

**存活 engine TU**(40 个文件,evaluator-free):execution_event.cpp、execution_metadata.cpp、execution_otel.cpp、execution_projection.cpp、execution_renderer.cpp、execution_report.cpp、workflow_result.cpp、workflow_recovery.cpp、capability_bridge.cpp、capability_transport_adapter.cpp、standard_capabilities.cpp、connection_pool.cpp、http_transport.cpp、grpc_transport.cpp、parallel_scheduler.cpp、data_pipeline.cpp、sandbox.cpp、distributed.cpp、wire_capability.cpp、wire_value.cpp、wire_transport_adapter.cpp、core_wire_codec.cpp、core_wasm_schema_transport.cpp、core_wasm_resume_record.cpp、core_wasm_schema_module.cpp、core_wasm_frame_module.cpp、core_wasm_node_events.cpp、core_wire_canonical_size.cpp、core_wasm_resume_controller.cpp、core_wasm_resume_host_codes.cpp、core_wasm_resume_host.cpp、core_wasm_resume_capacity.cpp、core_wasm_idempotency_token.cpp、host_event_envelope.cpp、durable_effect_intent.cpp、durable_effect_authority.cpp、payload_store_codec.cpp、payload_store.cpp。

#### CMake / build edges

| 文件 | 行 | 删除内容 |
|------|------|-----------|
| `src/CMakeLists.txt` | `:28` | `add_subdirectory(runtime/evaluator)` |
| | `:83` | `ahfl_runtime` bundle 中的 `ahfl_runtime_evaluator` |
| `src/runtime/engine/CMakeLists.txt` | — | TU 列表中的 agent_runtime.cpp、workflow_runtime.cpp、capability_eval.cpp、native_host_binding.cpp |
| | — | `PUBLIC ahfl_runtime_evaluator` link edge |
| `cmake/modules/AhflInstall.cmake` | `:109` | install 集中的 `ahfl_runtime_evaluator` |
| `src/tooling/repl/CMakeLists.txt` | `:7` | `ahfl_runtime_evaluator` link(WH-7 先 drop) |

### 12.10.3 EvalError / EvaluationFailed — DECISION: 分裂存活

**这是对 §12.2(line 450)与 §12.7.4(line 1227)"wasm lane 永不产生 EvalError" 声明的 dated revision。该声明在 HEAD `85f5132e` 事实层面已过时。**

**已核实的 wasm-lane `WorkflowFailureKind::EvaluationFailed` 生产者:**

| 文件 | 行 | 上下文 |
|------|------|---------|
| `src/runtime/wasm_host/wasm_lifecycle.cpp` | `:57` | `build_report` fail-closed:event-stream-invalid → `result.report.failure_kind = WorkflowFailureKind::EvaluationFailed` |
| `src/runtime/wasm_host/workflow_session.cpp` | `:1333` | P6 输出解码失败 → `run_failure_kind = EvaluationFailed` |
| `src/runtime/wasm_host/workflow_session.cpp` | `:1355` | WireJson 输出解析失败 → `run_failure_kind = EvaluationFailed` |

`workflow_result.cpp:29-30` 把 `WorkflowFailureKind::EvaluationFailed` 映射为 `WorkflowStatus::EvalError`。wasm lane **确实**经 fail-closed 路径产生 `EvalError`。wasm-lane **测试**钉住这一点:`tests/unit/runtime/wasm_host/workflow_session.cpp:928` 断言损坏输出场景下 `result->result.status() == WorkflowStatus::EvalError`。

**决策:**

| 枚举 arm | 命运 | 理由 |
|----------|------|------|
| `WorkflowStatus::EvalError`(`workflow_result.hpp:50`) | **存活** | wasm lane 经 fail-closed 路径产生 |
| `WorkflowFailureKind::EvaluationFailed`(`execution_event.hpp:107`) | **存活** | wasm lane 产生(上述 3 个生产者) |
| `NodeFailureKind::EvaluationFailed`(`execution_event.hpp:97`) | **删除** | 唯一生产者:`workflow_runtime.cpp:1297,1419,1471`(evaluator 驱动,随删)。grep 已核零非生产者消费者。 |

**同 commit 更正的过时注释:**

- `src/runtime/engine/workflow_result.hpp:14-18`——声称 "wasm lane never produces it … deleted at WH-9"。替换为:"The wasm lane produces `EvalError` through fail-closed paths (event-stream-invalid, output decode failure); see `wasm_lifecycle.cpp:57`, `workflow_session.cpp:1333,1355`."
- `tests/conformance/native_engine.cpp:80`——"never EvalError" 过时;wasm lane 可在 fail-closed 路径产生 `EvalError`。

**`ahfl.run-report` JSON schema 影响:无。** `run_status_name`(`execution_renderer.cpp:21-36`)映射 `RunTerminalStatus`(Completed/Failed/Cancelled/Interrupted/Suspended),无 `EvalError` arm。`WorkflowStatus::EvalError` 是由 report `failure_kind` 计算的高层状态;经 `status_name_workflow`(`observation_document.cpp:25-37`)呈现为 `"failed"`。JSON schema 不变。

### 12.10.4 Node 降级形状

#### Conformance node runner:gut 为 parity-only

`tests/integration/conformance_wasm_node_runner.cpp`(909 行)**gut**:

- **删除**:evaluator 依赖(`:42`、`:53`、`:498-507`、`:575-579`)、`kExpectedAgreed=66`/`kExpectedSkipped=0` census pin(`:147-148`)、`kExpectedNodeOnlyStems`(`:158-166`)、evaluator-vs-node differential 比较、blessing-determinism 模式、mutation 模式。
- **保留**:node-parity smoke——编译一个 fixture,Node 上跑,断言 observation 与 checked-in blessing 一致。这是 "Node 仍工作" 的金丝雀,不是 differential 引擎。
- **重命名**:存活 ctest 名保留 `ahfl.conformance.wasm_node_differential`(名字被 CI 配置钉住;重命名超范围),但语义变为 "node-parity smoke"。

#### Native conformance runner:存活,census pin 更新

`tests/integration/conformance_wasm_native_runner.cpp`(625 行)存活。变更:

- **删除**:evaluator 依赖(`:41`、`:56`、`:198-207`、`:464`)、`kExpectedNodeOnlyStems`(`:73`)、node-only 分支(`:219-234`)。
- **更新**:census pin 为 `kExpectedAgreed=66` / `kExpectedSkipped=0`(全部 66 场景改为 native-vs-blessing 比较;7 个前 node-only stem 经数据驱动 blessing-or-expectation 进入普查——见 §12.10.6)。

#### Node resume port:整删

| 文件 | LOC | 命运 |
|------|-----|------|
| `tests/integration/core_wasm_node_resume_engine.hpp` | 145 | **删除**(Node/V8 resume port) |
| `tests/integration/core_wasm_node_resume_engine.cpp` | 892 | **删除** |
| `tests/integration/core_wasm_resume_node_e2e.cpp` | 428 | **删除**(resume e2e) |

`--self-test-arity` V8 自测(e2e `:600-681`)随删。wasm3 lane 有自己的 resume 测试(`tests/unit/runtime/wasm_host/workflow_session.cpp`、`tests/unit/runtime/engine/core_wasm_resume_*.cpp`)存活。

#### Node-host 脚本与探针:删除

| 对象 | 数量 | 命运 |
|------|-------|------|
| `tests/scripts/wasm_*_node_host.py` | 25 | **删除**(被 66-census + gutted node-parity ctest 取代) |
| `tests/conformance/node_embedded_host_v2*_probe.mjs` | 5 | **删除**(`v2a_probe`、`v2b_probe`、`v2b_passthrough_probe`、`v2b_arena_probe`、`v2b_enum_probe`——仅被将死 py 脚本消费) |
| `tests/conformance/node_embedded_host.mjs` | 1 | **存活**(gutted node-parity ctest 使用,`conformance_wasm_node_runner.cpp:601,687`) |
| `tests/CMakeLists.txt` skip-77 ctests | 30 | **删除**(`:61,91,129,...,989` 依赖 node 的 SKIP_RETURN_CODE 77 ctests) |
| `tests/integration/core_wasm_e1_probe.cpp` | 161 | **删除**(evaluator-forking differential) |
| `tests/integration/core_wasm_e2_probe.cpp` | 247 | **删除** |
| `tests/integration/core_wasm_e3_probe.cpp` | 154 | **删除**(用 WorkflowRuntime 作 "native" 参照) |
| `tests/integration/core_wasm_p6_probe.cpp` | 793 | **删除**(用 AgentRuntime 作 "native" 参照) |
| `tests/integration/core_wasm_capability_workflow_probe.cpp` | 111 | **存活**(已核 evaluator-free) |

#### `EvaluatorSurfaceAwaitsKr68` 枚举退役

`tests/conformance/conformance_case.hpp:126-138` 定义 `WasmNodeObservationSkip::EvaluatorSurfaceAwaitsKr68`。**退役该枚举值**(它等待的 evaluator surface 已亡)。7 个声明 `"node_observation_skip": "evaluator_surface_awaits_kr68"` 的 manifest **移除该字段**(不是改成另一个值——skip 理由已不存在)。`wasm_eligibility.cpp:232-236` 检查删除。parser(`conformance_case.hpp:1068`)不再接受该字符串。

#### `engines.evaluator` 字段退役

`tests/conformance/conformance_case.hpp:153` 的 engines struct 有 `bool evaluator{false}`;`:977-1004` 解析它。**退役该字段**:从 struct、parser 与全部 58 个 manifest 移除(36 个 `"evaluator": true` + 7 个 `"evaluator": false` + 15 个 minified `"evaluator":true`)。唯一消费者是 `conformance_evaluator_runner.cpp:113`(随删)。native 与 node runner 从不检查它。

### 12.10.5 语义 / GUT 测试分诊

#### 分类规则

每个触及 evaluator 的测试文件归入:

- **Class A — 存活,evaluator-free**:无 evaluator 符号依赖;原样存活(或清理 stale include/注释)。
- **Class B — 分裂**:同一文件有存活测试与 evaluator 依赖测试;删除后者及其 `main()` 调用,保留前者。
- **Class C — 整删**:文件唯一主体是 evaluator 或 evaluator 驱动组件;删除文件与其 CMake target。

#### Class C — 整删

`tests/unit/runtime/evaluator/evaluator.cpp`(993)、`evaluator_generics.cpp`(646)、`executor.cpp`(450);`tests/unit/runtime/engine/agent_runtime.cpp`(613)、`workflow_runtime.cpp`(4426)、`native_host_binding.cpp`(222);`tests/conformance/evaluator_engine.{hpp,cpp}`(75+291);`tests/integration/conformance_evaluator_runner.cpp`(448);`core_wasm_node_resume_engine.{hpp,cpp}`(1037);`core_wasm_resume_node_e2e.cpp`(428);`core_wasm_e1_probe.cpp`(161)、`e2_probe.cpp`(247)、`e3_probe.cpp`(154)、`p6_probe.cpp`(793)。连同各自 CMake target。

#### Class B — 分裂

| 文件 | 删除 | 保留 |
|------|--------|--------|
| `tests/unit/runtime/evaluator/set_map_uuid_timestamp.cpp`(567) | `:307-530`(`eval_expr` 驱动测试 + `main()` evaluator 调用)、`:7-8` includes、`:26` using | `:1-303`(Value/json/Set/Map 测试) |
| `tests/unit/runtime/engine/wire_value.cpp`(318) | `:39-42`、`:103-167`(4 个 closure 测试)、`:305-308`、`:12` include | `:45-90`(serialize_args)、`:172-206`(hash_stable)、`:207-298`(parse_args) |
| `tests/unit/runtime/engine/native_wasm_differential.cpp`(1298) | `:1-376`(Part A:4 个 evaluator-differential 函数) | `:378-1288`(Part B:c3 inspector)、`:1289` `main()` |
| `tests/unit/runtime/wasm_runner/wasm_runner.cpp`(1490) | `:1205-1490`(section 8:evaluator vs wasm 字节比较、`eval_runtime`、`fail_byte`) | `:62-1204`(sections 1-7:wasm3 facade 测试) |
| `tests/unit/runtime/engine/core_wire_codec.cpp`(1320) | `:941-960`(`test_builtin_callers` 用 `evaluator::BuiltinTable`/`EvalContext`)、`:7-8` includes、`:944` using | 其余 wire codec 测试 |
| `tests/unit/runtime/engine/capability_bridge.cpp`(2013) | evaluator 依赖测试函数(`:1012`、`:1162` 用 `evaluator::EvalContext`) | evaluator-free 测试(若有存活;builder 核实) |

#### Class A — 存活(含清理)

- 清理 stale 引用:`execution_projection.cpp:14`(`using namespace ahfl::evaluator;` + stale include)、`execution_renderer.cpp:18`(同)、`workflow_recovery.cpp` 测试注释 `:271,294`、`observation_compare.hpp` 措辞/错误串("evaluator" → "engine-agnostic")、`native_engine.cpp:80` 注释、`src/runtime/engine/workflow_recovery.cpp:5` stale include。
- 无需清理(已核 evaluator-free):`observation_document.cpp`、`conformance_case.hpp`(减去退役枚举/字段)、`wasm_eligibility.cpp`(减去退役检查)、`conformance_mock_registry.*`、`resume_test_support.hpp`、engine 下 execution_*/report/event/metadata/otel、sandbox、distributed、parallel_scheduler、connection_pool、http/grpc_transport、host_event_envelope、core_wasm_*、core_wire_*、durable_effect_*、payload_store*、`tests/unit/runtime/wasm_host/*.cpp` 全部、wasm_runner.cpp(分裂后)、`tests/unit/runtime/value/*.cpp` 全部、`tests/unit/compiler/**` 全部、`tests/cmake/WasmTargetTests.cmake`(5 个结构性 gate)。

### 12.10.6 存活保证

| 存活者 | 保证 | 证据 |
|----------|-----------|----------|
| `observation_compare.hpp` | 引擎中立比较器;`observations_agree`(`:154`)与 `node_observation_matches_expectation`(`:215`)比较 observation JSON,不碰 evaluator 内部。注释/错误串更新。 | 无 evaluator include/符号 |
| 59 个 blessings | `tests/conformance/observations/` 下全部 checked-in observation 是 evaluator-free JSON。7 个 closure-stem manifest **无 blessing**(用 manifest-expectation 比较)。 | `ls ... \| wc -l` = 59 |
| `conformance_case.hpp` | 退役 `EvaluatorSurfaceAwaitsKr68` + `engines.evaluator`;其余 manifest schema 存活。 | parser `:977-1004` 更新 |
| `compile_source` | evaluator-free 测试支撑。 | 无 evaluator include |
| `wasm_eligibility.cpp` | 退役 `:232-236` 检查;文件其余 evaluator-free。**命运:存活**(eligibility catalogue 对 wasm lane 仍 load-bearing) | 无 evaluator include |
| `resume_test_support.hpp` | evaluator-free。 | 无 evaluator include |
| `value_json` 测试 | evaluator-free(对非 closure Value 测 value_to_json/try_value_to_json/hash_values)。 | 无 evaluator include |
| `node_observation_matches_expectation` | **存活。** 7 个前 node-only stem 无 blessing;它们与 manifest 声明的 `output_json` 期望经此引擎中立函数比较。这是独立契约(对 Node bless,不对 evaluator)。 | `observation_compare.hpp:215` |
| 7-stem WH-9 后比较 | 数据驱动:blessing 文件存在 → native-vs-blessing(59 stem);不存在 → 经 `node_observation_matches_expectation` 比 manifest 期望(7 stem)。无需 manifest 标记,按 blessing 文件存在性分支。 | — |

**`observation_document.cpp` schema 字符串:保留。** `"ahfl.evaluator-observation.v1"`(`:47`)是 wire-format 标识符,不是对执行引擎的声明。重命名会破坏每个 blessing 的 schema 字段。schema 描述的是引擎中立的 observation JSON 格式。加 dated 注释:"The schema name is historical; the observation format is engine-agnostic and survives the evaluator retirement."

### 12.10.7 Product scope freeze、文档、commit 形状

#### product-scope-freeze.json

**不变。** `config/product-scope-freeze.json` 列的是 CommandKind 与 emit artifact。evaluator 两者皆非。`ahflc run` 命令存活(内部切换引擎)。gate 对本 commit 是 no-op;commit message 明文声明这一点。

#### 同 commit 更新的文档

| 文档 | 更新 |
|-----|--------|
| `docs/design/kr68-wasm3-embedded-host-decision.zh.md` | 加入 §12.10;WH-9 标记 implemented |
| `docs/plans/q4-2026-roadmap.zh.md` | KR6.8 行:⬜ → ✅;注记 evaluator retired at WH-9 |
| `docs/plans/project-status.zh.md` | 加 WH-9 完成条目 |
| `docs/rfcs/0026-ir-tower-and-execution-model.zh.md` | status:`implementing` → `implemented`(KR6.8 是最后开放 KR);更新 evaluator 引用 |
| `docs/plans/issue-backlog-global-gaps.zh.md` | 关闭 evaluator-retirement gap 项 |

#### Commit title 与 footer

```
refactor(runtime)!: retire tree-walking evaluator behind wasm3 embedded host (WH-9)

BREAKING CHANGE: The tree-walking evaluator (src/runtime/evaluator/, 5840 LOC)
is deleted. ahflc run, the REPL, and the DAP now execute exclusively through
the vendored wasm3 embedded host. The Value closure arm (InterpreterClosureHandle,
ValueKind::Callable), NodeFailureKind::EvaluationFailed, the evaluator-driven
WorkflowRuntime/AgentRuntime/capability_eval/native_host_binding, the Node/V8
resume port, 25 node-host scripts, 5 v2 probe mjs files, 30 node-dependent
ctests, and 4 evaluator-forking probes are deleted in the same change.
WorkflowStatus::EvalError and WorkflowFailureKind::EvaluationFailed survive:
the wasm lane produces them through fail-closed paths (event-stream-invalid,
output decode failure). The conformance schema retires the
evaluator_surface_awaits_kr68 skip reason and the engines.evaluator field.
product-scope-freeze.json is unchanged (no CommandKind or emit artifact is
removed).
```

**ASCII-only**(commit-msg hook)。subject/body 无 CJK。

### 12.10.8 验证阶梯

builder 按序执行,全部须通过:

1. **Grep-zero gates**(commit 后,repo root):
   ```
   grep -rn "InterpreterClosure" src/ include/ tests/                                              # zero
   grep -rn "ahfl::evaluator\|runtime::evaluator\|ahfl_runtime_evaluator" src/ include/ tests/     # zero
   grep -rn "evaluator_surface_awaits_kr68" src/ include/ tests/                                   # zero
   grep -rn "engines.evaluator" tests/                                                             # zero
   grep -rn "NodeFailureKind::EvaluationFailed" src/ include/ tests/                               # zero
   ```
   注意 `grep evaluator` 的 const-evaluator 误报,用上面的 qualified patterns。
2. **66/0 census**:`ahfl.conformance.wasm_native` 报告 `66 agreed, 0 skipped`;native runner pin `kExpectedAgreed=66`/`kExpectedSkipped=0` 成立。
3. **Node skip-77 + mutation**:gutted node-parity ctest(`ahfl.conformance.wasm_node_differential`)Node 不可用时 exit 77(SKIP_RETURN_CODE 77 保留),Node 在场时通过。
4. **dev preset**:`cmake --preset dev && cmake --build --preset build-dev && ctest --preset test-dev --output-on-failure` 全绿。
5. **release preset**:preset/build/test-release 全绿。
6. **WASM ON/OFF**:`-DAHFL_ENABLE_BACKEND_WASM=ON`(默认)与 `=OFF` 两矩阵。WASM=OFF 下 `ahflc run` 以可行动诊断拒绝(§12.7.1);无 evaluator fallback;两矩阵构建均成功。
7. **FRESH -Werror build**:`rm -rf build/dev` 后全新 configure+build,零警告(develop 可累积 -Werror 破坏;fresh build 强制)。
8. **ASan**:asan preset build+test 全绿、无 leak。
9. **install/export**:`cmake --install build/dev` 成功;安装的 CMake package 不导出 `ahfl_runtime_evaluator` target。
10. **wasm3 LICENSE**:vendored wasm3 LICENSE 在位、未修改。
11. **LOC-deleted sanity**:`git diff --stat HEAD~1` 净删除 ≥ 12000 LOC(5840 evaluator + ~2000 engine TU + ~5000 tests + ~1000 scripts/probes)。数字是信息性,非 gate。

### 12.10.9 Commit 内编辑顺序

**ONE commit。** 以下编辑顺序保证中间态不累积额外债务;中间态可以不编译,最终态必须全绿。

1. **删除 `src/runtime/evaluator/`**(整目录)。从 engine 删除 agent_runtime.cpp、capability_eval.cpp、native_host_binding.cpp、workflow_runtime.cpp、workflow_runtime.hpp。更新 src/CMakeLists.txt、engine/CMakeLists.txt、AhflInstall.cmake。(前提:WH-6/7/8 已作为独立切片落地,零生产消费者——见顶部 coordinator note。)
2. **删除 closure arm**:value.hpp、value.cpp、value_json.cpp、frame_packer.cpp、debug_session.cpp 的 closure arms。从 execution_event.hpp 删除 `NodeFailureKind::EvaluationFailed`。
3. **更正过时注释**:workflow_result.hpp:14-18、native_engine.cpp:80、workflow_recovery.cpp:5、observation_compare.hpp 措辞、value.hpp:20-48 rationale。
4. **删除 Class C 测试**整文件与其 CMake targets(TestTargets.cmake / ProjectTests.cmake)。
5. **分裂 Class B 测试**:set_map_uuid_timestamp.cpp、wire_value.cpp、native_wasm_differential.cpp、wasm_runner.cpp、core_wire_codec.cpp、capability_bridge.cpp。
6. **清理 Class A stale refs**:execution_projection.cpp、execution_renderer.cpp。
7. **退役 conformance schema**:`EvaluatorSurfaceAwaitsKr68` + `engines.evaluator` 从 conformance_case.hpp 移除;更新全部 58 manifest;退役 wasm_eligibility.cpp:232-236 检查。
8. **删除 node 基础设施**:core_wasm_node_resume_engine.{hpp,cpp}、core_wasm_resume_node_e2e.cpp、25 个 py 脚本、5 个 v2 mjs 探针、tests/CMakeLists.txt 中 30 个 skip-77 ctests、4 个 evaluator-forking probes。
9. **Gut node runner**:conformance_wasm_node_runner.cpp → parity-only;更新 conformance_wasm_native_runner.cpp census pin。
10. **更新文档**(§12.10.7 表)。
11. **执行验证阶梯**(§12.10.8)。

**回滚风险注记:**

- 最高风险步是删除本身(WH-6/7/8 已先独立落地);若 wasm lane 有 66-census 未捕获的潜伏语义缺口,整个 commit revert。无部分回滚——删除是原子的。
- closure arm 步骤安全:存活路径无 closure 生产者(已核:唯一生产者 evaluator.cpp:276 在步骤 1 删除)。
- manifest 批量编辑是机械的:builder 用脚本移除 58 个 manifest 的两个字段,然后 `git diff --stat` 核恰好 58 文件变更。

### 12.10.10 Worklist 与验收标准

**Worklist**(WH-9 本体;WH-6/7/8 由各自切片拥有,不在此列):

| # | 项 | 文件 |
|---|------|-------|
| W4 | 删除 `src/runtime/evaluator/` | 整目录 |
| W5 | 删除 engine evaluator TU | agent_runtime.cpp、capability_eval.cpp、native_host_binding.cpp、workflow_runtime.{cpp,hpp} |
| W6 | 删除 closure arm | value.hpp、value.cpp、value_json.cpp、frame_packer.cpp、debug_session.cpp |
| W7 | 删除 `NodeFailureKind::EvaluationFailed` | execution_event.hpp |
| W8 | 更正过时注释 | workflow_result.hpp、native_engine.cpp、workflow_recovery.cpp、observation_compare.hpp、value.hpp |
| W9 | 删除 Class C 测试 + CMake targets | 15+ 文件 + TestTargets.cmake / ProjectTests.cmake |
| W10 | 分裂 Class B 测试 | 6 文件 |
| W11 | 清理 Class A stale refs | 2+ 文件 |
| W12 | 退役 conformance schema | conformance_case.hpp、58 manifest、wasm_eligibility.cpp |
| W13 | 删除 node 基础设施 | resume port、25 py、5 mjs、30 ctests、4 probes |
| W14 | Gut node runner + 更新 native runner | conformance_wasm_node_runner.cpp、conformance_wasm_native_runner.cpp |
| W15 | 更新 CMake edges | src/CMakeLists.txt、engine/CMakeLists.txt、AhflInstall.cmake、TestTargets.cmake、ProjectTests.cmake、tests/CMakeLists.txt |
| W16 | 更新文档 | 5 个文档 |
| W17 | 执行验证阶梯 | — |

**验收标准:**

1. `grep -rn "InterpreterClosure" src/ include/ tests/` 为零。
2. `grep -rn "ahfl::evaluator\|runtime::evaluator\|ahfl_runtime_evaluator" src/ include/ tests/` 为零。
3. `grep -rn "evaluator_surface_awaits_kr68" src/ include/ tests/` 为零。
4. `grep -rn "NodeFailureKind::EvaluationFailed" src/ include/ tests/` 为零。
5. `src/runtime/evaluator/` 不存在。
6. 任何 CMake 文件中无 `ahfl_runtime_evaluator` target。
7. `ahflc run` 经 wasm3 lane 执行并产出正确输出(66-census 证)。
8. `-DAHFL_ENABLE_BACKEND_WASM=OFF` 下 `ahflc run` 以可行动诊断拒绝;无 evaluator fallback。
9. `ahfl.conformance.wasm_native` 报告 `66 agreed, 0 skipped`。
10. `ahfl.conformance.wasm_node_differential` 无 Node exit 77,有 Node 通过。
11. Fresh `-Werror` dev build 零警告。
12. ASan ctest 全绿、无 leak。
13. `cmake --install` 成功;安装导出无 `ahfl_runtime_evaluator`。
14. `WorkflowStatus::EvalError` 与 `WorkflowFailureKind::EvaluationFailed` 存活;`tests/unit/runtime/wasm_host/workflow_session.cpp:928` 仍通过。
15. 7 个前 closure stem 经 `node_observation_matches_expectation` 与 manifest 期望比较;不需要 blessing 文件。
16. `git diff --stat` 净删除 ≥ 12000 LOC。
17. Commit message ASCII-only、含 `BREAKING CHANGE:`、声明 product-scope-freeze.json 不变。
18. RFC 0026 status 为 `implemented`;KR6.8 roadmap 行为 ✅。

### 12.10.11 被否方案(按分叉)

**Closure arm:**
- **Option (b) 保留防御性 opaque-id handle**——否。无存活路径构造 closure Value;防御性 arm 是死重,违反 Principle 1 与 Principle 3。未来需要 = 新 RFC。

**EvalError:**
- **删除 `WorkflowStatus::EvalError` + `WorkflowFailureKind::EvaluationFailed`**——否。wasm lane 经 fail-closed 路径产生它们(`wasm_lifecycle.cpp:57`、`workflow_session.cpp:1333,1355`)。删除会破坏 wasm lane fail-closed 契约与 `workflow_session.cpp:928` 测试。

**Node 降级:**
- **保留完整 node differential runner 作永久第二引擎**——否。native(wasm3)66-census 是权威契约。Node 是可移植性金丝雀,不是对等引擎。维护对第二个 JS 引擎的完整 differential 线束成本持续、边际信心递减。
- **完全删除 node runner**——否。单个 parity smoke 是防 wasm3 特有 ABI 漂移(wasm3 解释器与 V8 可能在边界情形分歧)的廉价保险。gutted runner 约 100 行。

**测试分诊:**
- **把 evaluator 测试当 "legacy" 测试继续编译**——否。Principle 1 禁止 legacy/compat 目录。evaluator 删除,其测试同 commit 删除或分裂。

**Commit 形状:**
- **把 WH-9 拆成多个 commit(先删 evaluator、再删 closure arm、再清测试)**——否。evaluator 与 closure arm 相互依赖(evaluator 是唯一 closure 生产者),拆开产生不编译/测试失败的中间态。一个原子 commit 是唯一安全形状。
- **保留 `engines.evaluator` 字段作 no-op 兼容**——否。Principle 1 禁止前向兼容 shim。evaluator runner 死后该字段无消费者,同 commit 移除。

## 12.11 WH-5b decisions (2026-10-01, dedicated decision agent, no human gate)

WH-5b 解决 hybrid workflow module(P6 打包节点 + opaque capability-final 节点共存于同一 workflow)在 wasm 车道的 codegen 失败。事实已对 HEAD `1be8bc82` 逐条复现。本节是一个连贯设计,不是菜单。

### 12.11.0 决策摘要

**选择 Option C:big-bang 在 lowerer 层把 opaque capability-final 归一化为 frame-bridge 调用。** opaque 终端形状(`return Cap(args)` 在 final state)从 wasm 车道的 IR 中完全消失;所有能力调用经 bridge 车道(非 final handler 内调用,结果暂存 context,final state 返回构造值)。hybrid module 不再是一个需要"支持"的形状——它在 lowerer 层被归一化为 canonical P6Frame。v2d rejection test 更新为锁定归一化成功。evaluator 的 opaque-final 快路径在同一变更中删除(Principle 1)。

### 12.11.1 事实复现(三个事实)

**Fact 1 — hybrid emit 失败(INVALID_LAYOUT):** fixture `P6BeforeCap`(RouteAgent = computed-goto P6 节点;CapAgent = opaque capability-final 节点,`return A(input)` 在 Done state)。codegen 产出 `wasm.INVALID_LAYOUT`:"a workflow node block size disagrees with the aligned size of its named layout root"。

**Fact 2 — evaluator 车道执行同一源码成功:** evaluator `WorkflowRuntime` 执行 hybrid fixture 到 `Completed`,capability 调用一次,输出 `{"_type":"wasm::scratch_p6f::Frame","n":7}`。这是 WH-6 parity regression:evaluator 接受,wasm 拒绝。

**Fact 3 — all-bridge rewrite 在 wasm 车道运行成功:** 把 CapAgent 的 capability 调用从 final state 移到非 final `Calling` state(`let result = A(input.n); ctx.out_n = result.n; goto Done;`),final state 返回 `Frame { n: ctx.out_n }`。两个节点均 P6 打包。codegen 产出 P6Frame(2 nodes, 1 import),wasm session 执行到 `Completed`,capability 调用一次,输出 `{"_type":"wasm::scratch_p6f_bridge::Frame","n":7}`。

### 12.11.2 根因链

1. `gathered[runner].p6 = has_computed_final || has_computed_goto || !bridge_calls.empty()`(`core_wasm_codegen.cpp:12795`)。opaque capability-final agent 三个条件全 false → `p6 = false`。
2. `plan.has_p6_nodes = any_of(gathered, p6)`(`:12906-12908`)。任一 runner 是 p6 → module 标记 P6Frame。
3. P6 runner 获得真实 node block(`:12254-12317`);opaque runner 保持零值 `WorkflowNodeBlock`(`input_base==0, input_size==0`)。
4. Dense `node_blocks` 发射包含所有 runner(`:13521-13537`),包括零值 opaque block。
5. `verify_workflow_spans`(`core_frame_layout.cpp:1371-1377`)检查 `block.input_size != expected_input`——opaque block 的 `input_size==0` 但 `expected_input = align8(layouts[input_layout.value].size) > 0` → 拒绝。

**共存设计已存在但不完整:** `:12862-12905` 的 `!p6 && region.constructed` 检查拒绝 opaque 节点接收 constructed input,但未覆盖 hybrid module 中 opaque 节点接收 simple input 的情形。这是 Fact 1 暴露的缺口。

**已有相关测试:** `v2d_computed_goto_preamble_reject.ahfl` 锁定单 agent hybrid(computed-goto + opaque final 在同一 agent)的 rejection(`UNSUPPORTED_WORKFLOW_FRAME`)。这是同一根因的不同表现:单 agent hybrid 在 codegen 早期被拒,多 agent hybrid 在 frame-layout 验证期被拒。

### 12.11.3 第二 bug:event_count 不变量破坏

即使修复 Fact 1 的 codegen 缺口使 hybrid module 能 emit,hybrid schedule 在 guest scheduler 层面仍有独立 bug:

1. **P6 节点不写 event record:** guest scheduler(`:15940-16150`)中,P6 节点在 `:16050` 处 `continue`——跳过 event record 写入和 event_count 检查。
2. **Cap 节点检查 event_count:** cap 节点在 `:16127-16137` 检查 `event_count == node.schedule_pos`(dense over ALL nodes),不匹配则返回 `(ERROR,0,0)`。
3. **P6-before-cap 顺序:** event_count=0(P6 节点未写),cap 节点 schedule_pos=1 → guest 返回 `(ERROR,0,0)`。
4. **Host import-boundary 归属:** `workflow_session.cpp:715-764` 用 `event_count` 作为 `schedule_pos` 推导当前节点。注释(`:716-719`)明确假设"scheduler walks nodes in Kahn order and writes event_count = schedule_pos + 1 on each node's completion"——此假设在 P6 节点不写 event record 时不成立。
5. **WH-4b memo 身份破坏:** `(WorkflowNodeId, ordinal)` 坐标和 `wasm_resume_recorder` schedule-space 分类假设每个节点写 event record。hybrid schedule 破坏此假设。

**结论:** hybrid schedule 不是"codegen 有缺口但运行时正确"——它在 scheduler 层面有根本性的 event_count 不变量破坏。修复需要要么(a)让 P6 节点也写 event record(改变 P6 快路径),要么(b)让 cap 节点用不同的 schedule position(破坏 dense 不变量)。两者都是对 scheduler 热路径的侵入式变更。

### 12.11.4 被否方案

**Option A — 使 hybrid 成为一等公民(支持 P6 + opaque 共存):**
- 否。需要:(1) 为 opaque runner 发射真实 node block 或让其共享 P6 frame layout;(2) 修复 event_count 不变量(P6 节点写 event record 或 cap 节点用非 dense position);(3) 修复 host import-boundary 归属;(4) 修复 WH-4b memo 身份。这是在 scheduler 热路径上维护两种物理终端形状,违反 Principle 1(一个 canonical 形状,无 special-case 分支)。event_count 不变量破坏(§12.11.3)使 hybrid schedule 在 replay 层面不健全——不是"更多工程"能修复的,是设计层面不兼容。

**Option B — 拒绝 + 可选归一化:**
- 否。如果归一化是可能的(Fact 3 证明),它应该是自动的,不是可选的。Principle 1 禁止 old-and-new coexistence:保留 opaque 终端形状作为"可选"路径意味着维护两套 codegen/scheduler/replay 逻辑。拒绝诊断对用户无 actionable 价值——用户无法手动修复 codegen 内部缺口。

### 12.11.5 选择方案与 AHFL-specific 分叉理由

**选择 Option C:big-bang 在 lowerer 层把 opaque capability-final 归一化为 frame-bridge 调用。**

**归一化变换(机械、语义保持):**
1. **检测:** final state 的 body 是单个 `return <capability_call>` 语句
2. **合成 context:** 若 agent context 是 Unit,合成含 capability 返回类型字段的 context struct;若已是 struct,添加字段(或复用匹配类型的现有字段)
3. **重命名 final state:** 原 final state 改名为 `Calling`(或 fresh name),移除 `final` 标记
4. **移动 capability 调用:** `Calling` 中 `return A(args)` 替换为 `let result = A(args); ctx.<field> = result; goto Done;`
5. **添加新 final state:** 新 `Done` state 返回 `ctx.<field>`
6. **重连 transitions:** `Start -> Done` 改为 `Start -> Calling`,添加 `Calling -> Done`

**AHFL-specific 分叉理由:** opaque capability-final 形状是 evaluator-only 优化——evaluator 的 tree-walk 可以直接 `return Cap(input)` 而无需 context round-trip。wasm 车道的 ABI 要求 bridge 调用(capability 在非 final handler 内调用,结果经 context 流转)。不在 wasm scheduler 维护两种终端形状,而是在 lowerer 层归一化为 canonical bridge 形状。参考 Rust:一个 canonical IR 形状(MIR),一个 fast path(monomorphization),不为 evaluator 优化保留第二形状。这与 §12.7 的"无 evaluator fallback"决策一致:归一化消除 parity gap,而非在 wasm 车道复制 evaluator 行为。

### 12.11.6 成本与风险

**成本:**
- Lowerer 变换:检测 opaque final、合成 context、移动调用、添加 state——机械但非平凡
- `v2d_computed_goto_preamble_reject.ahfl` 更新:从锁定 rejection 改为锁定归一化成功
- Evaluator 的 opaque-final 快路径在同一变更中删除(Principle 1:grep-zero non-test production callers)
- 现有 all-opaque golden tests(e2/e3/e4/e5/e6/p2_12 等)的 IR 形状变化:lowerer 归一化后,这些 fixture 的 agent 从 opaque 变为 bridge。golden 文件需重新生成

**风险:**
- **Capability 返回类型不可存储:** 若返回类型是 Reject(float/decimal/duration/timestamp/uuid/map/sequence),归一化无法 proceed。诊断:
  ```
  [wasm.UNSUPPORTED_CAPABILITY_FINAL] capability '<name>' returns <type> which cannot be routed through the bridge lane; rewrite as a bridge call in a non-final state with a storable context field
  ```
  这与 bridge 车道的 `bridge_param_kind` Reject 分类一致。
- **State machine 拓扑变化:** 归一化添加一个 state(`Calling`),但 workflow 级 schedule(Kahn order)不变——新 state 在同一 agent 内,不影响节点间依赖。
- **WH-4b resume 兼容性:** 归一化后,capability 调用在 bridge 车道,写 event record,与 WH-4b 的 event_count 不变量兼容。

### 12.11.7 验收标准

1. **P6-before-cap fixture**(`scratch_p6f_mixed.ahfl` 模式):归一化后 emit P6Frame(2 nodes, 1 import),wasm session 执行到 `Completed`,输出 `{"_type":"...Frame","n":7}`,capability 调用一次
2. **Cap-before-P6 fixture**(反转 Kahn 顺序):归一化后 emit 并运行,行为与 #1 一致
3. **Kahn-reordered variant**(3+ 节点混合顺序):归一化后 emit 并运行,所有节点完成
4. **Conformance census:** `ahfl.conformance.wasm_native` 报告 `66 agreed, 0 skipped`(不回归)
5. **WH-4b resume on normalized hybrid:** 在 capability 调用处 suspend,resume 后完成,输出正确;memo 身份 `(WorkflowNodeId, ordinal)` 正确
6. **Run-report byte parity:** evaluator 与 wasm 车道对归一化后模块的 run-report 字节一致
7. **ASan:** ctest 全绿、无 leak
8. **WASM=OFF:** §12.7 策略不变——`ahflc run` 以可行动诊断拒绝,无 evaluator fallback
9. **v2d test 更新:** `v2d_computed_goto_preamble_reject.ahfl` 不再 rejection;锁定归一化后 emit + run 成功
10. **Fresh `-Werror` dev build:** 零警告
11. **精确 gate 列表:**
    - `ahflc run` on hybrid module → wasm lane 成功
    - `ahflc build --target wasm` on hybrid module → emit P6Frame
    - `grep -rn "opaque.*final\|capability.*final.*return" src/compiler/ir/` 为零(opaque 终端形状从 IR 消失)
    - `grep -rn "UNSUPPORTED_WORKFLOW_FRAME.*opaque\|opaque.*UNSUPPORTED" src/` 为零(v2d rejection 路径删除)

### 12.11.8 先前决策保留声明

- **§12.6(WH-4b suspend/resume):** 保留,不重写。归一化使 hybrid schedule 与 event_count 不变量兼容——capability 调用在 bridge 车道,写 event record,memo 身份 `(WorkflowNodeId, ordinal)` 成立。
- **§12.7(WH-6 ahflc run cutover,无 fallback):** 保留,不重写。归一化消除 evaluator/wasm parity gap——wasm 车道不再拒绝 evaluator 接受的模块,无需 evaluator fallback。WASM=OFF 策略不变。

## 12.12 WH-5b decision revision (2026-10-01, dedicated decision agent, no human gate)

§12.11 的 Option C(big-bang 归一化 opaque capability-final 为 bridge 调用)经协调者审计后被发现**致命缺陷**。本节是修订记录,§12.11 原样保留作历史档案。事实已对 HEAD `f42842ba` 逐条复核。

### 12.12.0 修订摘要

**§12.11 Option C 被否决。选择 Option A':修复 hybrid 的两个机械缺陷,保留两种终端形状。** 两种终端形状不是冗余 special-case——它们编码不同的 ABI 契约:opaque terminal = pending-capable(D2b suspend 权威);in-handler bridge = single-run(组合)。修复:(a) frame section 诚实性(混合模块中 opaque runner 不发 node_blocks 条目,加 per-node `is_p6` 标志);(b) 混合模块中 P6 scheduler 节点也写 tag-0 event record 并发布 event_count,使 event_count == dense schedule_pos 在每个 cap 节点成立。**Bridge-pending parity gap 是独立的 mandatory pre-WH-6 slice(WH-5b.2),不在本切片范围。**

### 12.12.1 Bridge PENDING trap 复现(runtime + source)

**Runtime 复现:** bridge fixture(`scratch_p6f_bridge.ahfl`,两个 P6 节点,capability `A(n: Int) -> Frame` 在非 final `Calling` state 调用)在 Pending invoker 下运行:wasm session 返回 `NodeFailed`(status=1),**不是** `Suspended`(status=4)。guest 在 bridge 调用处 trap。

**Source 证据链:**
1. `core_wasm_codegen.cpp:6703-6710`:bridge guest arm 编译 `if (status != 0) unreachable`——任何非 OK 状态 trap。注释原文:"Single-run contract: any non-OK status traps (durable replay / pending arm stays the D2b authority)."
2. `capability_import.cpp:292-296`:host 把 `CapabilityCallStatus::Pending` 映射为 `AHFL_CAP_PENDING`(非零 raw)。
3. `workflow_session.cpp:708-713`:host 对 bridge imports(`param_frame.empty()`)直接 `return inner(obs)`,**跳过 WH-4b memo/replay 逻辑**。注释原文:"WH-4b: opaque lane only. Bridge imports (empty param_frame) pass through to inner live: the bridge ABI traps on Pending, so there is no suspend/resume on that lane."

**结论:** bridge 车道是 single-run-only,无 durable suspend/resume。Option C("opaque 终端形状消失,所有能力调用走 bridge 车道")会**摧毁所有 capability workflow 的 durable suspend/resume**。

### 12.12.2 Evaluator non-final pending semantics(source 验证)

**关键发现:evaluator 对 Pending 的处理不是 terminal-structural 的——它对 ANY statement position 的 capability 调用(包括 in-handler non-final 调用)统一挂起节点。**

1. `capability_eval.cpp:108-116`:Pending → `EvalResult::suspension`(携带 `pending_cap_id` + `pending_ordinal`),`has_errors()` 保持 false。注释原文:"a pending capability call suspends evaluation. Carry the suspension (which call, which ordinal) up the eval recursion instead of an error diagnostic, so has_errors() stays false and the workflow node loop persists a resume record rather than terminating."
2. `workflow_runtime.cpp:1057-1064`:Pending → `node_memo.suspended = true`,stamp `(pending_cap_id, pending_ordinal)`,return BEFORE failure-classification loop。注释原文:"a pending call suspends the node... Pending is not a failure, so no CapabilityFailed event and no node_capability_failures entry."

**结论:bridge suspension 是 NEW CODEGEN,不是 new semantics。** evaluator 已经支持在 non-final capability 调用处挂起;wasm bridge 车道的 trap 是 codegen 限制,不是语义差异。

### 12.12.3 更广泛的 WH-6 parity gap(独立于 hybrid 问题)

Bridge-pending trap 影响**所有 bridge workflow**,不仅是 hybrid:
- 一个纯 P6/bridge workflow(无 opaque 节点)中,in-handler capability 返回 Pending → guest trap(`NodeFailed`),evaluator 会挂起(`Suspended`)。
- **Latent 原因:** conformance census 的 58 个 manifest 中没有 scenario 期望 `suspended` 状态——所有 scenario 期望 `completed`。e2e_multi_agent 的 bridge 调用(`ClassifyMessage`、`HandleGeneral` 等)在 census 中全部返回 Success。没有 fixture 触发 bridge-pending 路径。

**Bridge-pending rung 是 mandatory pre-WH-6 slice,在 Option A' 或 C' 下都需要。** Option B'(只拒绝 hybrid)不关闭此 gap——一个纯 computed agent 在 mid-handler 调用 capability 并 pending 时,不经过任何 hybrid 节点就会 hit 此 trap。

### 12.12.4 被否方案(修订)

**Option C(§12.11,big-bang 归一化):** 致命缺陷——bridge 车道在 Pending 上 trap(§12.12.1),归一化摧毁所有 capability workflow 的 durable suspend/resume。§12.11.8 声称"归一化后与 WH-4b 兼容"是错误的:normalized call 在旧 call 挂起的地方 trap。

**Option B'(slice gate:拒绝 hybrid + 后续 KR):** 否。(1) 不关闭 hybrid parity gap(evaluator 接受,wasm 拒绝);(2) 不关闭 bridge-pending parity gap(§12.12.3);(3) WH-6 gated on parity——拒绝不是 parity。B' 是最低风险但留下最多 gap。

**Option C'(full normalization + generalized suspension):** 否。(1) 把两个独立问题(hybrid codegen 缺陷 + bridge-pending codegen 限制)捆绑成一个大变更;(2) 需要泛化 WH-4b recorder 到 bridge ordinals(bridge 调用在 runner 内部,不写 node-event record),改变 recorder 的 node-boundary identity 模型;(3) 最高 regression 风险;(4) evaluator 消费 AHFL-IR(`workflow_runtime.hpp:120,126` `const ir::Program&`,从不调用 `lower_ahfl_to_core`),Core-IR 归一化不能删除 evaluator 路径——evaluator 继续 tree-walk AHFL-IR capability 调用直到 WH-9。C' 是正确的长期方向但不是本切片。

### 12.12.5 选择方案:Option A'(修复 hybrid,保留两种终端形状)

**两种终端形状编码不同的 ABI 契约,不是冗余:**

| 形状 | ABI | Pending 行为 | WH-4b | 用途 |
|------|-----|-------------|-------|------|
| Opaque terminal | `(i32,i32)->(i32,i32,i32)` | graceful PENDING latch + null-result suspension | 完整支持(memo + replay) | D2b suspend 权威 |
| In-handler bridge | `(i32)->(i32,i32)` | trap(unreachable) | 不支持(skip) | single-run 组合 |

**修复 (a) — frame section 诚实性:** `node_blocks` 数组只包含 P6-packaged runner 的条目(packaged-instance order,与 header 注释 "parallel to the workflow module's sorted packaged-instance table" 一致)。opaque runner 不发条目。在 `CoreWasmNodeDescriptor` 加 `bool is_p6` 标志(P6-packaged 节点为 true,opaque 节点为 false),host 用此标志守卫 `node_blocks[runner]` 访问(`workflow_session.cpp:1298`)。verifier 无需 special-case——它只看到 P6 条目的非零 size。

**修复 (b) — P6 节点写 event record:** 在混合模块中(`!plan.imports.empty()` => event region 存在,`core_wasm_codegen.cpp:12176`),P6 scheduler 节点在 `continue`(`:16050`)之前调用 `append_event_record_write`(tag 0 = identity,status = OK)并发布 `event_count = schedule_pos + 1`。纯 P6 模块(无 import,无 event region)保持当前行为。`append_event_record_write`(`:15830`)已支持 tag 0(zero capability fields + status OK)。

**Reference Hierarchy 依据:** Rust(extern "C" vs Rust ABI)、Swift(ownership conventions)、Clang(calling conventions)都为不同目的维护不同 ABI 契约。两种终端形状是不同的 ABI 契约,不是 old-and-new coexistence。

### 12.12.6 成本与风险

**成本:**
- `CoreWasmNodeDescriptor` 加 `is_p6` 字段(descriptor + codegen 填充 + host 消费)
- `node_blocks` 发射跳过 opaque runner(codegen `:13521-13537` 改为只发 P6 runner)
- host `node_blocks[runner]` 访问改用 `is_p6` 守卫 + P6-runner ordinal 映射
- P6 节点在混合模块中写 tag-0 event record + 发布 event_count(codegen `:16050` 前插入)

**风险:**
- **Host node_blocks 索引:** `workflow_session.cpp:1298` 当前用 `runner` 索引 `node_blocks`。改为 P6-only 后需要 per-node `is_p6` + P6-runner ordinal。影响面:post-run event decoding 的 node output 读取。
- **Event record layout:** tag-0 record 的 `capability`/`source_symbol`/`invocation_ordinal` 字段为零(已由 `append_event_record_write` 处理)。`decode_node_events` 的 `IdentityFieldsNonZero` 检查确保 tag-0 record 无 capability 字段。
- **e3/e4/e5/e6 resume 测试:** 不受影响——它们是 all-opaque workflow(无 P6 节点,无 tag-0 record)。event_count 行为不变。
- **v2d 测试:** 不受影响——single-agent hybrid 是不同问题(见 §12.12.9)。

### 12.12.7 验收标准(修订)

1. **P6-before-cap fixture:** emit P6Frame(2 nodes, 1 import),wasm session `Completed`,输出 `{"_type":"...Frame","n":7}`,capability 调用一次
2. **Cap-before-P6 fixture**(反转 Kahn 顺序):emit 并运行,行为与 #1 一致
3. **Kahn-reordered variant**(3+ 节点混合顺序):emit 并运行,所有节点完成
4. **Event record 完整性:** 混合模块中 P6 节点写 tag-0 record,cap 节点写 tag-1 record;`decode_node_events` 验证通过;每个 cap 节点的 event_count == dense schedule_pos
5. **WH-4b resume on hybrid:** 在 cap 节点的 PENDING latch 处 suspend,resume 后完成,输出正确;memo 身份 `(WorkflowNodeId, ordinal)` 正确
6. **Frame section 诚实性:** `node_blocks` 只含 P6 条目;opaque runner 无条目;host 用 `is_p6` 守卫;`verify_workflow_spans` 无 special-case
7. **Conformance census:** `ahfl.conformance.wasm_native` 报告 `66 agreed, 0 skipped`(不回归)
8. **e3/e4/e5/e6 resume 测试:** 不变(all-opaque,无 P6 节点)
9. **v2d 测试:** 不变(single-agent hybrid 保持 rejection)
10. **ASan:** ctest 全绿、无 leak
11. **WASM=OFF:** §12.7 策略不变
12. **Fresh `-Werror` dev build:** 零警告
13. **精确 gate 列表:**
    - `ahflc run` on hybrid module → wasm lane 成功
    - `ahflc build --target wasm` on hybrid module → emit P6Frame
    - `grep -rn "is_p6" src/compiler/backends/wasm/core_wasm_codegen.hpp` 非零(新字段存在)
    - `grep -rn "node_blocks\[runner\]" src/runtime/wasm_host/` 为零(旧索引消除)

### 12.12.8 Fixture/blessing/census 影响

| 类别 | 文件 | 影响 |
|------|------|------|
| Golden wasm tests(14 个 opaque-terminal) | e2/e3/e4/e5/e6/p2_12/p2_15/fb4_* | **不变**——all-opaque,不是 hybrid |
| v2d rejection test | `v2d_computed_goto_preamble_reject.ahfl` | **不变**——single-agent hybrid 保持 rejection |
| Conformance manifests(58 个) | `tests/conformance/cases/*.json` | **不变**——census 中无 hybrid case |
| Observation blessings(59 个) | `tests/conformance/observations/*.json` | **不变**——census 中无 hybrid case |
| 新 hybrid fixtures | P6-before-cap, cap-before-P6, Kahn-reordered | **新增**——3 个测试 fixture(不加入 census,作为独立 ctest) |

**净影响:零现有 fixture/blessing/census 变更。** 3 个新 fixture 作为独立 ctest 加入,不改变 66-census 基线。

### 12.12.9 v2d fixture 命运

**保持 rejection test,不变。** v2d fixture 是 single-agent hybrid(computed-goto + opaque capability final 在同一 agent)。这与 multi-agent hybrid(P6 agent + 独立 opaque agent)是不同问题:

- **Multi-agent hybrid(本切片修复):** P6 agent 和 opaque agent 是独立 runner,各自有正确的 ABI。缺陷在 frame section 发射和 event record 发布。
- **Single-agent hybrid(v2d,保持 rejection):** 同一 agent 同时有 computed-goto(P6 打包)和 opaque capability final。P6 runner 的 ABI 是 `(i32)->(i32,i32)`,opaque terminal 的 ABI 是 `(i32,i32)->(i32,i32,i32)`——P6 runner 无法调用 opaque terminal import(函数签名不匹配)。修复需要:(a) P6 runner 支持 opaque terminal 调用(新 ABI),或 (b) lowerer 在同一 agent 内把 opaque final 归一化为 bridge 调用。这是 follow-up KR,不在本切片范围。

### 12.12.10 Bridge-pending parity gap(独立 mandatory slice)

**WH-5b.2(独立切片,mandatory pre-WH-6):** 泛化 bridge 车道的 PENDING/ERROR 处理,使 in-handler capability 调用返回 Pending 时 guest 优雅挂起(而非 trap)。范围:

1. Guest status dispatch(`core_wasm_codegen.cpp:6703-6710`):非 OK 状态不再 `unreachable`,改为 PENDING latch + null-result suspension(与 opaque terminal `:16080-16101` 对齐)
2. Scheduler-level suspension at in-handler boundaries:bridge 调用在 runner 内部,scheduler 需要感知 bridge-level pending
3. WH-4b recorder 泛化:bridge 调用不写 node-event record,recorder 的 `(schedule_pos, ordinal)` identity 模型需要扩展到 bridge ordinals
4. Fresh-instance replay across bridge sites
5. Snapshot v2 schema 影响(若 bridge suspension 需要新字段)

**此切片独立于 WH-5b.1(hybrid 修复)。** WH-5b.1 不依赖 WH-5b.2;WH-5b.2 不依赖 WH-5b.1。但 WH-6 gated on 两者:WH-5b.1 关闭 hybrid parity gap,WH-5b.2 关闭 bridge-pending parity gap。

### 12.12.11 先前决策保留声明

- **§12.6(WH-4b suspend/resume):** 保留,不重写。Option A' 保持 opaque terminal 的 PENDING latch 和 WH-4b memo/replay 完整。bridge-pending 泛化(WH-5b.2)是独立切片,不改变 §12.6 的 recorder 设计。
- **§12.7(WH-6 ahflc run cutover,无 fallback):** 保留,不重写。Option A' 关闭 hybrid parity gap;WH-5b.2 关闭 bridge-pending parity gap。WASM=OFF 策略不变。
- **§12.11(Option C):** 原样保留作历史档案。其事实复现(§12.11.1-12.11.3)和根因分析(§12.11.2)仍然正确;致命缺陷在 §12.12.1-12.12.3 中记录。

## 12.13 WH-5b.3 decision: hybrid P6/opaque encoding-boundary transcode (2026-10-01, dedicated decision agent, no human gate)

本节修订 §12.12。§12.12 修复了 hybrid 模块的 event-record 归属(tag-0/tag-1)和 is_p6 分类,但**遗漏了编码边界**:P6 节点与 opaque 节点之间的数据流不能直接传递 P4-D frame words。本节关闭该 gap,作为新切片 WH-5b.3。

### 12.13.1 §12.12 遗漏了什么

§12.12 的修复 (a)/(b) 是必要的:tag-0 identity record(P6 节点)与 tag-1 capability record(opaque 节点)的 event_count 归属、is_p6 分类、wrapped_callback 的 node attribution。但这些修复只解决了**控制面**(event record、memo identity、schedule position)。**数据面**未解决:

- **Opaque 节点**消费/生产 wire-JSON:`capability_import.cpp:269` `parse_args_from_wire_json`、`:310` `serialize_value_for_wire_json`。opaque terminal ABI `(i32,i32)->(i32,i32,i32)` 的参数是 wire-JSON buffer (ptr, len)。
- **P6 节点**消费/生产 P4-D binary frame:`frame_packer.hpp:75` `pack_value_at`(Value -> P4-D INLINE)、`frame_reader.hpp:86` `read_value_at`(P4-D INLINE -> Value)。P6 runner 的参数是 P4-D frame address。

两种编码在字节层不兼容。当 hybrid workflow 的 crossed edge(P6->opaque 或 opaque->P6)直接传递 frame words 时:

- **P6-before-cap**:opaque 节点收到 P4-D words,`parse_args_from_wire_json` 解析失败 -> ImportAbort -> NodeFailed。
- **cap-before-P6**:materializer 把 wire-JSON 当作 P4-D 读取 -> garbage -> computed-final materialization 产出错误输出。
- **cross-node NodeOutput edge**:`append_workflow_source`(`core_wasm_codegen.cpp:15864-15865`)push upstream (ptr, len) locals,在 crossed lane 上传递错误编码的 words。

§12.12 的 (a)/(b) 是 WH-5b.3 的**必要前置**(is_p6 flag 用于 crossed-edge 分类,tag-0/event_count 用于 wrapped_callback attribution),但不充分。

### 12.13.2 选择:Option A - host boundary transcode

**在 host 侧、wasm 边界处做编码转换。** host 已经拥有全部 codec(P4-D pack/read、wire-JSON serialize/parse)。新增一个 wasm import namespace `ahfl_xcode`,每个 crossed edge 一个 import `xcode_<N>`,functype 复用 opaque terminal 的 3-result 形状 `(i32,i32)->(i32,i32,i32)`。scheduler 在每个 crossed edge 处插入一次 transcode call。

**为什么是 host 侧而不是 module 侧:** module 内没有 JSON codec(P4-D 是唯一的 in-module 编码);在 module 内实现 JSON parser 是重新发明 host 已有的轮子,违反 Principle 1。Rust/Clang 的参考模型:host(编译器/runtime)拥有 ABI 边界处的序列化,module 只处理一种内部编码。

### 12.13.3 Transcode import ABI

```
namespace: ahfl_xcode
field:     xcode_<N>          (N = transcode-site ordinal, decimal, compiler 按 schedule 顺序分配)
functype:  (i32, i32) -> (i32, i32, i32)
           params:  src_ptr, src_len
           results: status (0=OK, nonzero=error), dst_ptr, dst_len
```

两个方向:

| direction | src 编码 | dst 编码 | host handler |
|-----------|----------|----------|--------------|
| `P4D_TO_JSON` | P4-D INLINE frame | wire-JSON | `read_value_at` -> Value -> `validate_value` -> `serialize_value_for_wire_json` -> `alloc_then_write` -> reply (0, json_ptr, json_len) |
| `JSON_TO_P4D` | wire-JSON | P4-D INLINE (shadow) | `decode_json` -> Value -> `pack_value_at`(INLINE)into shadow -> reply (0, shadow_base, shadow_extent) |

**Frame section 扩展:** `CoreFrameLayoutSection` 新增 `transcode_sites` table。每个 entry:

```
struct TranscodeSite {
    uint32_t import_ordinal;      // 对应 ahfl_xcode.xcode_<N> 的 import ordinal
    enum Direction { P4D_TO_JSON, JSON_TO_P4D } direction;
    enum Source { ENTRY, NODE_OUTPUT } source;
    uint32_t source_node_ordinal; // 若 source=NODE_OUTPUT
    uint32_t target_node_ordinal;
    TypeLayout layout;             // P4-D layout(用于 read_value_at / pack_value_at)
    FramePlacement shadow;         // JSON_TO_P4D 的 shadow region placement
}
```

**Admission(安全门):** `wasm3_engine.cpp:509-554` 的 admission path 扩展,接受 `ahfl_xcode.xcode_<decimal>` + opaque 3-result functype。tight check:namespace == `ahfl_xcode`、field prefix `xcode_`、decimal suffix、functype `(i32,i32)->(i32,i32,i32)`。与 `ahfl_cap` 相同的 sealed-functype discipline。

**Linking:** `m3_LinkRawFunctionEx`,signature `"iii(ii)"`(与 opaque lane 相同)。

**Routing:** `workflow_session.cpp` 的 `wrapped_callback` 在 memo/replay/event_count 逻辑**之前**路由 transcode ordinal 到 `handle_transcode`(与 bridge skip `:708-713` 同构,但针对 transcode ordinal)。session 从 frame section 的 `transcode_sites` table 解析 import ordinal -> transcode site;未找到则 fail closed(ImportAbort)。

**`handle_transcode` 永远:**
- 不调用 capability invoker
- 不触碰 memo(不读不写)
- 不 fire `capability_invoked` hook
- 不写 event record
- 是纯确定性 codec adapter

### 12.13.4 Shadow ingress spans

JSON_TO_P4D 方向需要一个 in-page 的 shadow region 作为 P4-D INLINE 的落地空间。设计:

- **单一复用 static region** `[shadow_base, shadow_base + max_shadow_extent)`:所有 JSON_TO_P4D transcode 复用(每次 overwrite)。`max_shadow_extent` 由 compiler 从所有 crossed edge 的 layout 计算最大值。
- **Payload arena** `[shadow_payload_base, shadow_payload_base + capacity)`:String bytes 的落地空间(`pack_value_at` 把 String bytes 写入 arena,shadow frame 里的 (ptr, len) 指向 arena)。
- **Entry shadow** `[entry_shadow_base, +entry_extent)`:当存在 source=ENTRY 的 P4D_TO_JSON transcode site 时,host 把 entry pack 成 INLINE 到 entry shadow(永不 normalize),再 memcpy 到 I_k。transcode 读 entry shadow(inline 完整)。scalar entry(fixture 场景)下 I_k 本就 inline,entry shadow 是一次小的额外 memcpy,但统一正确。

**Verifier 扩展:** `core_frame_layout.cpp:1325-1442` `verify_workflow_spans` 扩展,检查 shadow region / payload arena / entry shadow 与 node_blocks、bridge_control、entry_payload、workflow_output、state_trace 互不相交。5b.1 承诺 verifier 不变;**本切片扩展 verifier**,因为 shadow spans 是新的 in-page region,必须验证 disjointness(否则 shadow overwrite 会 corrupt node block)。

### 12.13.5 Materializer 交互

**JSON_TO_P4D(opaque source -> P6 target):**
1. scheduler call `xcode_N`(src = opaque node 的 wire-JSON output buffer)
2. host `handle_transcode`:`decode_json` -> Value -> `pack_value_at`(INLINE)into shadow -> reply (0, shadow_base, shadow_extent)
3. scheduler check status;若 nonzero -> NodeFailed(fail closed)
4. scheduler call `normalize_inline_input(shadow)`(`core_wasm_codegen.cpp:15034`):把 shadow 的 INLINE aggregate 重写为 pointer-tree(src==dst, in-place)
5. materializer 通过 `emit_root_base`(`:15403`)读 shadow:对 crossed source,`emit_root_base` 返回 `shadow_base`(static constant,不是 local)——materializer 从 shadow 读取 pointer-tree 字段,materialize 到 I_k
6. `copy_aggregate`(`:15697`)读 pointer-tree、写 INLINE 到 I_k

**P4D_TO_JSON(P6 source/entry -> opaque target):**
1. scheduler `append_workflow_source`(`:15853`)push (src_ptr, src_len):
   - source=NODE_OUTPUT:src = O_k base(computed-final materializer `:7311-7313` 保证 O_k 是 INLINE)
   - source=ENTRY:src = entry_shadow_base(INLINE,永不 normalize)
2. scheduler call `xcode_N`
3. host `handle_transcode`:`read_value_at`(INLINE)-> Value -> `validate_value` -> `serialize_value_for_wire_json` -> `alloc_then_write` -> reply (0, json_ptr, json_len)
4. scheduler check status;results (json_ptr, json_len) 成为 opaque runner 的参数

**关键 form 约束:** `read_value_at` 只读 INLINE(`frame_reader.cpp:209-235` 字段在 `addr + field_offsets[i]` 处)。P4D_TO_JSON 的 source 必须是 INLINE。O_k 是 INLINE(computed-final 保证);entry I_k 在 normalize 后是 pointer-tree,所以需要 entry shadow。

**String payload:** `pack_value_at` 把 String bytes 写入 shadow payload arena;shadow frame 里的 PtrLen (ptr, len) 指向 arena。`read_value_at` 读 PtrLen 时用 `string_regions` 解析。transcode 的 P4D_TO_JSON 方向读 O_k/entry_shadow 的 String 时,String bytes 在 entry payload arena 或 node block 的 string region 内(已有的 string_regions 机制)。

**Fan-out:** 一个 crossed edge 一个 transcode site;shadow region 复用(每次 transcode overwrite)。若一个 opaque node 的 output 被多个 P6 node 消费,每个消费 edge 一个 JSON_TO_P4D transcode site(各自 normalize 各自的 shadow 副本——但 shadow 复用,所以 scheduler 必须在每个 consumer 的 materialize 之前重新 transcode+normalize)。

**Workflow output crossing:** 若 workflow return 是 opaque node,scheduler 插入一个 JSON_TO_P4D transcode(opaque output -> workflow_output region)。host post-run 用 `read_value_at` 读 workflow_output(INLINE)。fixture 场景(return P6 node)不触发此路径。**注意:此 shape 在 hybrid workflow 中当前不可表达** —— hybrid(p6=true)的 output 走 materializer path(`:16282-16297`),`emit_root_base`(`:15410-15412`)对 opaque-rooted return region 返回 false(comment `:15399-15402`)。WH-5b.3 的 transcode(JSON_TO_P4D into workflow_output shadow,materializer 从 shadow 读)使其可表达;opaque-return fixture 是 WH-5b.3 的 scope(见 §12.13.9 item 4、§12.13.10 AC1(b))。

### 12.13.6 Value round-trip fidelity

transcode 是纯 Value->Value round-trip,经过 host 已有的 codec:

- **P4D_TO_JSON:** `read_value_at`(P4-D INLINE->Value)-> `serialize_value_for_wire_json`(Value->wire JSON)。fidelity 由 `core_json_round_trip` corpus 保证。
- **JSON_TO_P4D:** `decode_json`(wire JSON->Value)-> `pack_value_at`(Value->P4-D INLINE)。fidelity 由同一 corpus 保证。

类型覆盖(hybrid workflow 真实 crossed edge 的 e2e 覆盖,2026-10-01 校正):
- struct(扁平 wire 字段集)/tag-only enum/Int/Bool/String:P4-D binary layout(CoreLayoutTable)<-> wire JSON object,两个方向均有 real-wasm3 e2e + evaluator parity 覆盖(`wh5b_hybrid_rich_fidelity` / `wh5b_hybrid_rich_p6_to_opaque`)。enum discriminant 在 P4-D 里是 integer tag,在 wire JSON 里是 enum object field。
- String:PtrLen <-> wire JSON string。JSON_TO_P4D 方向 String bytes 落地 transcode payload arena;P4D_TO_JSON 方向 PtrLen 可指向 rodata / entry-payload arena / bridge result arena / transcode payload arena,host 的 String region 表必须全部承认。
- **payload-bearing enum / nested struct / std Option / std Result:codec 支持,workflow 帧边界 e2e 不覆盖。** packer/reader 的 payload-enum/Option arm 由 `frame_packer_reader.cpp` 合成 pin 与 `core_json_round_trip`(经 `@repo-std` project parse 的真实 std Option corpus)覆盖;但 P6 workflow lane 的 scheduler materializer 当前拒绝 node input / workflow return 中的 payload-bearing enum 与嵌套 struct(kUnsupportedWorkflowFrame),computed final 也不能从 host-packed INPUT 转发 aggregate/enum 字段。这是 P6 workflow lane 的既有边界,不是 transcode codec 限制;扩展 materializer 是独立后续 rung。
- **Decimal/Duration/Timestamp/Float/Uuid/Map:当前 fail closed。** WH-2 的 P4-D 帧子集在 host 侧 pack/read 穷尽分支对这些 wire schema 节点返回 `ValueNotWireEncodable`(`frame_packer.cpp` / `frame_reader.cpp` 的 frame-subset fallthrough)。wire schema 本身接受这些节点,P6 codegen 也能为字面量发射 i64,但 host 侧 P4-D pack/read 不支持。这是 WH-2 帧子集的既有边界,transcode 复用同一 codec 故继承之;`wh5b_hybrid_decimal_fail_closed.ahfl` pin 该安全行为(entry pack 在 run2 之前失败,零 capability invocation)。帧子集扩展是独立后续 KR,不属于 WH-5b.3。
- **Closure:fail closed。** closure 在 wire-schema 构造期即被拒绝(`core_wire_schema.cpp` ws_Closure:"closure values are not supported by value_json"),任何 source-level 程序都无法产生带 Closure 的 wire schema,故 transcode 边界对 Closure 不可达;codec 级拒绝由 `core_json_round_trip` 与 `frame_packer_reader.cpp` pin。transcode 不引入新的 closure 支持。

### 12.13.7 Determinism + WH-4b replay

**transcode 是确定性的:** 纯 codec,无 invoker、无 side effect、无 heap allocation 影响语义(alloc_then_write 的 buffer 地址不进入 memo identity)。

**WH-4b resume/replay:**
- fresh-instance replay 时,transcode 被重新执行(同样的输入 -> 同样的输出)。
- memo identity tuple `(WorkflowNodeId, per-node ordinal, cap SymbolId, arg_hash)` 不变。transcode 不写 memo,不读 memo。arg_hash 从 decoded Value 计算(不是 pointer)。
- transcode 不写 event record -> `event_count` invariant 不变。tag-0/tag-1 归属仍由 §12.12 (a)/(b) 保证。
- Pending 不会在 transcode 中产生(无 invoker)。transcode 的 status 只有 OK / error(fail closed)。
- `wrapped_callback` 在 memo/replay/event_count 之前路由 transcode(与 bridge skip 同构)。

**ExactSidecar:** transcode 不影响 opaque node 的 ExactSidecar(verbatim wire bytes)。transcode 的输出是 opaque node 的输入(wire JSON),opaque node 的 memo 仍记录其 verbatim wire bytes。

### 12.13.8 Kahn reordering 与 crossed-edge 分类

**分类规则(显式):** 一个 crossed edge 存在当且仅当 (target node 的 lane) != (其 source 的编码)。其中:
- ENTRY source 的编码 = `schedule.front()` 的 lane 编码(host entry-pack 规则:`workflow_session.cpp:1017-1018` 按 schedule 顺序取 `nodes[0]`,host 按 front node 的 lane pack entry)。
- NODE_OUTPUT source 的编码 = producing node 的 lane。
- 同 lane 的 ENTRY consumer(如 kahn_reordered 的 echo2)永远不获得 transcode site。

entry lane 由 `schedule.front()` 决定。transcode sites 从 schedule 计算,不是 declaration order。

**三个 fixture 的正确 walkthrough(全部节点消费 ENTRY,无 NodeOutput edge):**

`wh5b_hybrid_p6_before_cap.ahfl`:schedule [compute(P6), echo(opaque)],both consume ENTRY,return compute。
- schedule.front() = compute(P6) -> entry lane = P6(P4-D)。host pack entry 成 P4-D 到 I_0。
- compute:source=ENTRY,lane=P6,entry 编码=P4-D。同 lane -> 无 transcode。compute 从 host-packed I_0 运行。
- echo:source=ENTRY,lane=opaque,entry 编码=P4-D。crossed(opaque != P4-D)-> P4D_TO_JSON transcode,source=ENTRY。host pack entry_shadow(inline P4-D,因 schedule.front() 是 P6,entry I_k 可能被 normalize);transcode 读 entry_shadow,产出 wire-JSON 给 echo runner。

`wh5b_hybrid_cap_before_p6.ahfl`:schedule [echo(opaque), compute(P6)],both consume ENTRY,return compute。
- schedule.front() = echo(opaque) -> entry lane = opaque(wire-JSON)。host pack entry 成 wire-JSON。
- echo:source=ENTRY,lane=opaque,entry 编码=wire-JSON。同 lane -> 无 transcode。echo 直接在 (0,1) wire-JSON 上运行。
- compute:source=ENTRY,lane=P6,entry 编码=wire-JSON。crossed(P6 != wire-JSON)-> JSON_TO_P4D transcode,source=ENTRY。transcode 把 wire-JSON 解成 Value,pack 成 INLINE P4-D 到 shadow;bare-forward materialize 读 shadow result words(non-front node 今天就 emit 自己的 region),写入 I_k。

`wh5b_hybrid_kahn_reordered.ahfl`:declared [echo2, compute, echo1],Kahn schedule [echo1(opaque), compute(P6), echo2(opaque)],all consume ENTRY,return compute。
- schedule.front() = echo1(opaque) -> entry lane = opaque(wire-JSON)。host pack entry 成 wire-JSON。
- echo1:source=ENTRY,lane=opaque。同 lane -> 无 transcode。
- compute:source=ENTRY,lane=P6。crossed -> JSON_TO_P4D transcode,source=ENTRY。
- echo2:source=ENTRY,lane=opaque,entry 编码=wire-JSON。同 lane(schedule.front() 也是 opaque)-> 无 transcode,直接 (0,1) passthrough。
- return compute(P6)-> host read O_k(P4-D)。

transcode site 的 ordinal 按 schedule 顺序分配,与 Kahn 重排一致。

### 12.13.9 Evaluator parity tests

evaluator 今天就能跑 hybrid workflow(Value transport,无编码边界)。wasm host 必须匹配。测试:

1. **3 个 entry-consumer fixture 作为 standalone ctest**(Completed + value assertion):
   - `wh5b_hybrid_p6_before_cap.ahfl`:schedule [compute(P6), echo(opaque)],return compute
   - `wh5b_hybrid_cap_before_p6.ahfl`:schedule [echo(opaque), compute(P6)],return compute
   - `wh5b_hybrid_kahn_reordered.ahfl`:Kahn [echo1, compute, echo2],return compute
   这三个 fixture 全部节点消费 ENTRY(无 NodeOutput edge),覆盖 ENTRY crossing 的两个方向(P4D_TO_JSON from entry_shadow、JSON_TO_P4D into shadow)和 same-lane passthrough。

2. **NodeOutput cross-lane fixture(P6->opaque 方向):** 一个 workflow,opaque node 消费 P6 node 的 O_k(NodeOutput source)。覆盖 P4D_TO_JSON 从 O_k words 读取(§12.13.5 的 NODE_OUTPUT path)。**此 shape 在当前 language/IR 中可表达:** `check_path`(`core_wasm_codegen.cpp:11815-11856`)对 packaged P6 source 通过(dispatch_types.size()==3、payload 非 null);`append_workflow_source`(`:1859-1866`)push P6 node 的 (ptr,len) locals。当前 runtime blocker(opaque runner 把 P4-D 当 wire-JSON 解析失败)正是 WH-5b.3 transcode 修复的目标。

3. **NodeOutput cross-lane fixture(opaque->P6 方向):** 一个 workflow,P6 node 消费 opaque node 的 output(NodeOutput source),input region 为 constructed(V2D-CTX 支持 capability-result -> constructed node input)。覆盖 JSON_TO_P4D into shadow + normalize + materialize(constructed region)。**此 shape 在当前 codegen 中不可表达:** `emit_root_base`(`core_wasm_codegen.cpp:15410-15412`)对 opaque upstream 返回 false(comment `:15399-15402`:"An upstream OPAQUE node owns no fixed output block... a frame region that roots an opaque output is not materializable on this rung")。WH-5b.3 的 transcode 设计(shadow 作为 materializer root,`emit_root_base` 对 crossed source 返回 shadow_base)使其可表达。**此 fixture 是 WH-5b.3 的 scope,不是 pre-existing capability。**

4. **Opaque-return fixture:** 一个 workflow,return 是 opaque node。覆盖 JSON_TO_P4D into workflow_output region + host post-run read path。**此 shape 在 hybrid workflow 中当前不可表达:** hybrid workflow(p6=true)的 output 走 materializer path(`:16282-16297`),materializer 的 `emit_root_base` 对 opaque-rooted return region 返回 false(同 :15410-15412)。WH-5b.3 的 transcode(JSON_TO_P4D into workflow_output shadow)使其可表达。**此 fixture 是 WH-5b.3 的 scope。**

5. **Hybrid WH-4b resume test:** hybrid workflow suspend/resume,验证 transcode 在 fresh-instance replay 中重新执行、memo identity 不变、event_count invariant 保持。

6. **core_json_round_trip corpus pin:** 把 transcode 的 Value round-trip 加入 corpus,保证 P4-D<->wire-JSON fidelity。

7. **Fail-closed tests:**
   - schema mismatch(wire JSON 与 P4-D layout 不匹配)-> transcode status nonzero -> NodeFailed
   - oversized frame(shadow extent 超过 max_shadow_extent)-> fail closed
   - corrupt shadow(normalize 后 materializer 读到非法 pointer)-> fail closed
   - transcode abort attribution(import ordinal 不在 transcode_sites table)-> ImportAbort

### 12.13.10 Acceptance criteria

- [ ] AC1:hybrid fixture 作为 standalone ctest 通过,workflow status == Completed,output value 与 evaluator 一致。fixture 分两类:
  - **(a) Entry-consumer(3 个,当前可表达):** `wh5b_hybrid_p6_before_cap`、`wh5b_hybrid_cap_before_p6`、`wh5b_hybrid_kahn_reordered`。全部节点消费 ENTRY,覆盖 ENTRY crossing 两方向 + same-lane passthrough。
  - **(b) NodeOutput cross-lane + opaque-return(3 个,WH-5b.3 scope):**
    - P6->opaque NodeOutput fixture(当前可表达,runtime blocker 由 transcode 修复)。
    - opaque->P6 NodeOutput constructed-region fixture(当前不可表达:`emit_root_base` :15410-15412 拒绝 opaque upstream;WH-5b.3 shadow-root 使其可表达)。
    - opaque-return fixture(当前不可表达:hybrid output 走 materializer path,`emit_root_base` 拒绝 opaque-rooted return;WH-5b.3 JSON_TO_P4D into workflow_output shadow 使其可表达)。
  - (b) 类 fixture 的 .ahfl 源文件与 ctest 在 WH-5b.3 实现中一并落地;若实现中发现 (b) 类某 shape 仍不可表达,必须以 file evidence 记录并在 AC1 中标注为 deferred,不得静默丢弃。
- [ ] AC2:hybrid WH-4b resume test 通过:suspend -> resume -> fresh-instance replay -> Completed,event_count invariant 保持,memo identity 不变。
- [ ] AC3:core_json_round_trip corpus 包含 transcode 的 P4-D<->wire-JSON round-trip,全绿。(2026-10-01 outcome:扁平 wire 字段集 struct/tag-only enum/Int/Bool/String 两个方向 real-wasm3 e2e + evaluator parity 覆盖;payload-enum/Option arm 由 codec 合成 pin 覆盖;Decimal/Duration 为 frame 子集 fail-closed pin,不是 fidelity。详见校正后的 §12.13.6。)
- [ ] AC4:fail-closed tests 全绿(schema mismatch / oversized / corrupt shadow / transcode abort attribution / descriptor zeroed span —— JSON_TO_P4D shadow+payload 与 P4D_TO_JSON entry shadow 两个方向均 pin)。
- [ ] AC5:`verify_workflow_spans` 扩展后,所有 hybrid fixture 的 shadow spans disjointness 验证通过。
- [ ] AC6:ASan build 全绿(无 heap-buffer-overflow / use-after-free,shadow region 复用安全)。
- [ ] AC7:WASM=OFF build 全绿(transcode 是 wasm-host-only,不影响 WASM=OFF)。
- [ ] AC8:census:66 manifests / 0 blessing changes(hybrid fixture 是新的,不改变现有 blessing)。
- [ ] AC9:fresh `-Werror` build 全绿(develop 分支)。
- [ ] AC10:`ctest -L wasm` baseline:84 pass / 4 skip(与 5b.1 后一致,新增 hybrid tests 计入 pass)。
- [ ] AC11:full dev `ctest`:570/573,仅 3 个已知 beta/install/readme env failure。
- [ ] AC12:admission path 的 `ahfl_xcode` sealed-functype check 有 unit test(namespace / prefix / decimal / functype 缺一不可)。

### 12.13.11 Costs / LOC

估算(基于代码阅读):

| 组件 | LOC | 说明 |
|------|-----|------|
| `core_wasm_codegen.cpp` scheduler | ~150 | crossed-edge 分类、transcode call 插入、shadow region 分配、entry shadow |
| `core_frame_layout.cpp` verifier | ~40 | shadow spans disjointness |
| `core_frame_layout.hpp` frame section | ~30 | TranscodeSite table |
| `workflow_session.cpp` routing | ~80 | wrapped_callback transcode routing、handle_transcode、entry shadow pack |
| `capability_import.cpp` / 新 `transcode.cpp` | ~120 | handle_transcode 两个方向 |
| `wasm3_engine.cpp` admission | ~30 | ahfl_xcode sealed-functype |
| tests | ~350 | 3 entry-consumer fixture ctest + 3 NodeOutput/opaque-return fixture ctest + hybrid resume + fail-closed + admission unit |
| **合计** | **~800** | |

### 12.13.12 Rejected alternatives

**Option B(unify encoding)- REJECTED。**
- all-JSON:module 内需要 JSON codec(P4-D 是唯一 in-module 编码)。在 module 内实现 JSON parser 是重新发明 host 已有的轮子,违反 Principle 1。且 P4-D 的 CoreLayoutTable 是 zero-copy 的,JSON 解析引入运行时开销。
- all-P4-D:opaque node 的 WH-4b ExactSidecar 是 verbatim wire bytes(`§12.6`),wire-schema 是 capability transport 的契约。改成 P4-D 会破坏 ExactSidecar、wire-schema、evaluator parity(evaluator 用 Value,不是 P4-D)。
- AHFL-specific reason:host 已经拥有全部 codec;在 module 内重新实现是 duplicate ownership,违反 Rust/Clang 的"host owns ABI serialization"模型。

**Option C(§12.11 normalization)- REJECTED(已在 §12.12 否决,此处简述)。**
- bridge ABI `(i32)->(i32,i32)` 在 Pending 时 trap,不能用于 suspend-capable opaque node。
- evaluator 消费 AHFL-IR,不是 Core-IR;normalization 是 wasm 侧概念,evaluator 无对等物。
- 详见 §12.12.1-12.12.3。

**Option D(defer / reject hybrid)- REJECTED。**
- evaluator 今天就能跑 hybrid workflow;WH-6 把 `ahflc run` 切到 wasm 后,hybrid 会 parity regression。
- WH-9 删除 evaluator 后,hybrid 永久不可用 = 语言收窄,与 north-star(embeddable verifiable agent-workflow DSL,RFC 0020)矛盾。
- census 当前无 hybrid case(58 manifests),但 north-star 是通用 embeddable DSL,hybrid 是基本能力(P6 computed-goto + opaque capability 在同一 workflow)。
- host 已经拥有全部 codec,transcode 是自然的边界适配,不是新能力。

### 12.13.13 Sequencing + gate revision

**顺序:**
1. **WH-5b.1**(§12.12 修复 (a)/(b))- 立即落地,作为独立 commit。这是 WH-5b.3 的必要前置(is_p6 flag 用于 crossed-edge 分类,tag-0/event_count 用于 wrapped_callback attribution)。
2. **WH-5b.3**(本节,transcode)- 在 5b.1 之后。
3. **WH-5b.2**(#78,bridge-pending 泛化)- 在 5b.3 之后。
4. **WH-6**(ahflc run cutover)- 在 5b.1 + 5b.3 + 5b.2 之后。

**Gate revision:** §12.12.10 的 gate set 是 "WH-6 gated on 5b.1 + 5b.2"。**修订为:WH-6 gated on 5b.1 + 5b.3 + 5b.2。** 理由:5b.1 关闭 hybrid 控制面 gap,5b.3 关闭 hybrid 数据面 gap(本节),5b.2 关闭 bridge-pending gap。三者缺一,WH-6 cutover 会有 parity regression。

### 12.13.14 Prior-decision preservation statement

- **§12.6(WH-4b suspend/resume):** 保留,不重写。transcode 不改变 opaque terminal 的 PENDING latch、memo/replay、ExactSidecar。transcode 是确定性的,在 fresh-instance replay 中重新执行。
- **§12.7(WH-6 ahflc run cutover,无 fallback):** 保留,不重写。WASM=OFF 策略不变。gate set 修订(§12.13.13)。
- **§12.11(Option C):** 原样保留作历史档案。
- **§12.12(Option A'):** 保留作历史档案。其修复 (a)/(b) 是 WH-5b.3 的必要前置,作为 WH-5b.1 落地。§12.12 的遗漏(编码边界)在 §12.13.1 中记录,由本节关闭。

## 12.14 WH-5b.2 decision: bridge-lane graceful Pending/suspend parity (2026-10-01, dedicated decision agent, no human gate)

本节关闭 §12.12.10 记录的 bridge-pending parity gap:in-handler bridge capability call 返回 Pending 时,guest trap 导致 `NodeFailed`,而 evaluator 在同一位置 suspend。WH-5b.2 使 bridge lane 的 Pending 语义与 evaluator 对齐:graceful suspend + fresh-instance replay,复用 WH-4b 的 memo/replay 基础设施。

### 12.14.1 问题复现与证据链

**Runtime 复现:** bridge fixture(`scratch_p6f_bridge.ahfl`,两个 P6 节点,capability `A(n: Int) -> Frame` 在非 final `Calling` state 调用)在 Pending invoker 下运行:wasm session 返回 `NodeFailed`(status=1),**不是** `Suspended`(status=4)。guest 在 bridge 调用处 trap。

**Source 证据链(HEAD 8dbd2570):**

1. `core_wasm_codegen.cpp:6727-6734`:bridge guest arm 编译 `if (status != 0) unreachable`——任何非 OK 状态 trap。注释原文:"Single-run contract: any non-OK status traps (durable replay / pending arm stays the D2b authority)."
2. `capability_import.cpp:520-525`:`handle_bridge` 在非 Success 时返回 `ImportReply{raw_status, GuestPointer{0}, 0}`,guest trap。
3. `workflow_session.cpp:756-761`:wrapped_callback 对 bridge import(`obs.param_frame.empty()`)直接 `return inner(obs)`,跳过 WH-4b 的 memo/replay  machinery。注释原文:"WH-4b: opaque lane only. Bridge imports (empty param_frame) pass through to inner live: the bridge ABI traps on Pending, so there is no suspend/resume on that lane."
4. `core_wasm_codegen.cpp:16491-16498`:scheduler 的 P6 node dispatch 检查 `status != AHFL_CAP_OK -> unreachable`,无 PENDING arm。
5. **对比 — evaluator**:`capability_eval.cpp:108-116` Pending -> `EvalResult::suspension`(无 error,`has_errors()` 保持 false);`workflow_runtime.cpp:1057-1064` Pending -> `node_memo.suspended = true`,stamp `pending_cap_id` / `pending_ordinal`,在 failure-classification 之前 return。
6. **对比 — opaque lane wasm**:`core_wasm_codegen.cpp:16577-16595` opaque terminal 有完整的 PENDING arm(check `status == AHFL_CAP_PENDING`、check `ptr == 0`、set `kWorkflowGlobalPendingLatched`、return `(PENDING,0,0)`)。bridge lane 缺少对应 arm。

**Parity gap 的后果:** WH-6(`ahflc run` cutover)后,一个在 evaluator 下 suspend 的 workflow 在 wasm 下 `NodeFailed`。这是 terminal-status 级别的 parity regression,不是 cosmetic 差异。

### 12.14.2 决策摘要

**ONE coherent big-bang change:** bridge lane 的 Pending 从 trap 改为 graceful suspend,复用 WH-4b 的 `(node, per-node ordinal)` memo identity、`WasmResumeRecorder` classify/memo/replay、`WorkflowRecoverySnapshot` v2 持久化。具体:

1. **Snapshot schema: stays v2。** `CapabilityMemoEntry` 的 `(optional<WorkflowNodeId> node, ordinal per-NODE, cap_id, arg_hash)` identity 已覆盖 in-handler call。per-node ordinal 计数 node 内所有 capability call(evaluator:`workflow_runtime.cpp:754`;wasm:recorder `per_node_counters_`),不区分 opaque terminal 还是 in-handler bridge。无需新字段。
2. **Manifest extension: exec manifest v2。** scheduler-boundary per-node byte(`cap_call_count`)保持 binary(0 = identity,1 = opaque cap site)。P6 bridge node 发 `cap_call_count=0`(identity)加空 `capabilities`。新增 per-node in-runner bridge-site table:`(ordinal: u8, call_site_id: u32, capability_id: u32, source_symbol: u64)`。A2 admission-set equality 变为:union of (opaque sites from `cap_call_count=1` nodes) + (bridge sites) == wire schema capabilities == module imports。manifest version 从 1 升到 2,decoder 只接受 v2(big-bang,无 coexistence)。
3. **Guest ABI: graceful PENDING return。** bridge status arm 从 `if (status != 0) unreachable` 改为 `if (status == PENDING) return (PENDING,0,0) from runner; if (status != OK) unreachable`。scheduler 的 P6 node dispatch 在 tag-0 write 之前加 PENDING arm:latch + return `(PENDING,0,0)` from run2。ERROR 保持 trap(parity:evaluator `CapabilityFailed -> NodeFailed`;wasm trap -> `NodeFailed`——observation 相同)。
4. **Host suspension coordinate:** run2 tuple `(PENDING,0,0)` 信号 suspend。scheduler 在 runner suspend 时不写 tag-0 event record,`event_count` 保持 node 的 `schedule_pos`。snapshot 的 `suspended.node` / `suspended.agent` / `suspended.pending_ordinal` 从 recorder 的 stamped coordinate 推导。
5. **Fresh-instance replay:** suspended node 之前的 node replay;suspended runner 内更早的 bridge call 从 per-node memo 供给:host 把 recorded P4-D frame + String payload arena bytes 写回 `site.result_base` + `site.result_payload_base`(与 live `handle_bridge` placement 完全一致)。
6. **Composition with WH-5b.3 xcode:** xcode 在 wrapped_callback 中 route FIRST(pure codec,no memo),bridge/opaque 走 memo/replay。无 contamination。
7. **Census + tests:** 新增 `suspended` terminal scenario;evaluator parity of suspended terminal + resumed completion;enumerated fail-closed cases。
8. **Agent-lane scope:** WH-5b.2 是 workflow-lane only。agent lane(`WasmAgentRunner`)无 recovery-snapshot consumer,保持 `NodeFailed`。

### 12.14.3 Q1: Snapshot schema — v2 stays

**决策:不升 v3。`ahfl.workflow-recovery.v2` 保持不变。**

理由:

- `CapabilityMemoEntry`(`workflow_recovery.hpp:81-94`)的 identity 是 `(optional<WorkflowNodeId> node, ordinal per-NODE, cap_id, arg_hash)`。bridge call 发生在 node 的 agent 内部;per-node ordinal 已计数 node 内每个 call,不论 statement position。
- evaluator 的 per-node ordinal:`workflow_runtime.cpp:754` `const std::uint64_t memo_ordinal = node_memo.next_ordinal++`——在 node 的 capability-call loop 内递增,覆盖 in-handler call。
- wasm 的 per-node ordinal:`wasm_resume_recorder.hpp:155` `std::vector<std::uint64_t> per_node_counters_`,按 `schedule_pos` index,在 `begin_import` 中递增。bridge import 走同一 `begin_import`(删除 skip 后),ordinal 空间自然扩展。
- 一个 node 要么是 P6(bridge calls),要么是 opaque(一个 opaque terminal call),不会同时是两者。因此 `(node, ordinal)` 无歧义。
- `authoritative_json` 是 `optional<string>`,可存任意 bytes(P4-D frame + String payload arena)。ExactSidecar trust state 不变。
- `SuspendedNodeState`(`workflow_recovery.hpp:113-127`)的 `pending_cap_id` / `pending_ordinal` 直接复用。`node_input` 在 wasm lane 保持 `nullopt`(in-guest materialized input 不是 host-observable)。

**无需新 snapshot 字段。** 缺失的 piece 纯粹是 host-side:wrapped_callback 跳过 bridge import 的 memo/replay(§12.14.1 证据 3)。删除 skip、把 bridge import 路由进 memo/replay machinery 后,identity 空间自然覆盖。

### 12.14.4 Q2: Manifest extension — exec manifest v2 with in-runner bridge-site table

**决策:exec manifest 从 v1 升到 v2,新增 per-node in-runner bridge-site table。scheduler-boundary byte 保持 binary。**

**当前 v1 格式(`core_wasm_schema_module.cpp:909-958` encode,`:703-852` decode):**

```
magic "AHFLXM" + version(u8) + entry_kind(u8) + workflow_id(u32) + node_count(u32)
per node:
  workflow_node_id(u32) + schedule_pos(u32) + cap_call_count(u8) + capabilities[] {
    capability(u32) + source_symbol(u64)
  }
```

**v1 的 latent gap(`core_wasm_codegen.cpp:14770-14776` 注释原文):**

> "KNOWN latent gap (owned by WH-5b.2): a bridge P6 node's scheduler-level completion is an identity event (design 12.12.5: tag-0 record, zero capability fields), yet its per-node manifest cap byte below counts its in-handler bridge imports. The tag-0 record vs cap-byte distinction is a resume-coordinate inconsistency. WH-5b.2 must introduce a separate in-runner site table (the scheduler-boundary byte stays binary) jointly with admission-set semantics and the resume classifier (design 12.12.10)."

**v2 格式(big-bang,decoder 只接受 v2):**

```
magic "AHFLXM" + version=2(u8) + entry_kind(u8) + workflow_id(u32) + node_count(u32)
per node:
  workflow_node_id(u32) + schedule_pos(u32) + cap_call_count(u8) + capabilities[] {
    capability(u32) + source_symbol(u64)
  }
  bridge_site_count(u8) + bridge_sites[] {
    ordinal(u8) + call_site_id(u32) + capability(u32) + source_symbol(u64)
  }
```

**字段语义:**

- `cap_call_count`:scheduler-boundary byte,保持 binary。0 = identity node(P6 bridge node 或纯计算 node),1 = opaque cap site。P6 bridge node 发 0 + 空 `capabilities`。
- `bridge_sites[]`:in-runner bridge call 的 per-node table。`ordinal` 是 per-node bridge call ordinal(0-based,execution order,与 recorder 的 `per_node_counters_` 对齐)。`call_site_id` join 到 frame section 的 `bridge_call_sites[call_site_id]`(`core_frame_layout.hpp:60-92`)用于 result placement。`capability` + `source_symbol` 满足 A2 admission-set equality。
- `bridge_site_count`:per-node bridge site 数量。一个 P6 bridge node 可在不同 branch 调多个 capability(`core_wasm_codegen.cpp:12985-12993` 已收集 ALL reachable capabilities)。

**A2 admission-set equality 修订(`core_wasm_schema_module.cpp:1200-1354`):**

当前:manifest 的 `capabilities` union == wire schema capabilities == module imports。

修订后:union of (opaque sites from `cap_call_count=1` nodes 的 `capabilities`) + (bridge sites from `bridge_sites[]`) == wire schema capabilities == module capability imports。transcode imports(`ahfl_xcode`)排除(不 name capability,`core_wasm_schema_module.cpp:1168-1177` 已有排除逻辑)。

**Host-side trust checks(WH-5b.3 lesson:session admits modules without verified-schema gate):**

session 交叉检查 manifest bridge-site table 与 frame section:
- 每个 bridge site 的 `call_site_id` 存在于 frame section 的 `bridge_call_sites`。
- frame section 的 `source_symbol` 匹配 manifest 的 `source_symbol`。
- `(result_base, result_extent)` 和 `(result_payload_base, result_payload_capacity)` in-bounds。
- per-node `ordinal` dense 且 unique(0, 1, ..., N-1)。
- `call_site_id` 在 workflow 内 unique。

**D5 instance-reuse 不受影响:** `core_wasm_codegen.cpp:12997-13001` 确保每个 P6 node 有自己的 runner。in-runner bridge-site table 是 per-node 的,每个 node 的 runner unique,因此 `(node, ordinal) -> call_site_id` 映射无歧义。

**Agent arm:** agent manifest 保持 flat capability list(无 bridge sites),但 version 升到 2。decoder 对 agent arm 不读 `bridge_site_count`。

### 12.14.5 Q3: Guest ABI — graceful PENDING return

**决策:bridge `(i32)->(i32,i32)` 的 PENDING 从 trap 改为 runner-level `return (PENDING,0,0)`。scheduler P6 node dispatch 加 PENDING arm。ERROR 保持 trap。**

**Bridge status arm 修订(`core_wasm_codegen.cpp:6727-6734`):**

当前:
```
if (status != 0) unreachable
```

修订后:
```
if (status == AHFL_CAP_PENDING) {
    // Runner returns (PENDING, 0, 0). The `return` instruction bypasses
    // handler block structure and returns from the runner function.
    push AHFL_CAP_PENDING; push 0; push 0; return;
}
if (status != AHFL_CAP_OK) unreachable
```

WebAssembly `return` 从 function(runner)返回,绕过 handler block 结构。runner 返回 `(i32,i32,i32) = (status, O_k, output_size)`。PENDING 时返回 `(PENDING, 0, 0)`。

**Scheduler P6 node dispatch 修订(`core_wasm_codegen.cpp:16491-16498`):**

当前:
```
// status must be OK.
if (runner_status != AHFL_CAP_OK) unreachable
```

修订后(在 tag-0 write 之前):
```
if (runner_status == AHFL_CAP_PENDING) {
    // Same shape as the opaque lane's PENDING arm (line 16577-16595).
    set kWorkflowGlobalPendingLatched;
    return (AHFL_CAP_PENDING, 0, 0) from run2;
}
if (runner_status != AHFL_CAP_OK) unreachable
```

这与 opaque lane 的 PENDING arm(`core_wasm_codegen.cpp:16577-16595`)形状完全一致:check `status == PENDING`、check `ptr == 0`、set latch、return `(PENDING,0,0)`。

**Run2 first-instruction gate 不变(`core_wasm_codegen.cpp:16689-16693`):** 如果 `kWorkflowGlobalPendingLatched` 已 set,run2 第一条指令 trap。fresh instance 清除 latch。这复用现有机制,无需新 global。

**ERROR 保持 trap 的 parity 论证:**

- evaluator:capability failure -> `CapabilityFailed` -> `NodeFailed`(`workflow_runtime.cpp` failure-classification loop)。
- wasm bridge lane:ERROR -> trap -> `Run2Trapped` -> `NodeFailed`(`workflow_session.cpp:1320-1326`)。
- terminal status 相同(`NodeFailed`)。trap 是 wasm 对 evaluator error-propagation 的等价表达。无需 graceful ERROR arm。

**PENDING with non-null result_ptr:** 与 opaque lane 一致(`core_wasm_codegen.cpp:16588-16593`):PENDING + non-null ptr 是 malformed -> ERROR,不 latch。bridge status arm 的 PENDING 分支只在 `status == PENDING` 时触发,`ptr` 和 `len` 被忽略(runner 直接返回 `(PENDING,0,0)`)。host 的 `handle_bridge` 在 Pending 时返回 `ImportReply{AHFL_CAP_PENDING, GuestPointer{0}, 0}`(`capability_import.cpp:520-525`),所以 `ptr` 总是 0。

**与 xcode status word 的共存:** xcode 返回 `(0, json_ptr, json_len)` 或 `(AHFL_CAP_ERROR, 0, 0)`(`transcode.cpp:63-65`)。xcode 从不 Pending(不 invoke capability)。bridge PENDING arm 在 runner 内部;xcode call 在 scheduler 内(runner 之前)。无 interference。

### 12.14.6 Q4: Host suspension coordinate

**决策:host 通过 run2 tuple `(PENDING,0,0)` 区分 mid-runner pending 与 completed。suspend 时不写 tag-0 event record,`event_count` 保持 node 的 `schedule_pos`。**

**Mid-runner pending:**
1. bridge call 返回 PENDING -> runner 返回 `(PENDING,0,0)`。
2. scheduler P6 node dispatch 检测 PENDING -> latch + return `(PENDING,0,0)` from run2。
3. scheduler 在 tag-0 write(`core_wasm_codegen.cpp:16519-16527`)之前 return,所以 **不写 tag-0 event record**。
4. `event_count` 保持 node 的 `schedule_pos`(P6 node 未完成)。
5. host 的 run2 pending arm(`workflow_session.cpp:1327-1333`)检测 `(PENDING,0,0)` -> `run_status = Suspended`。
6. recorder 在 bridge import callback 中 stamp pending coordinate(`workflow_session.cpp:1014-1018` `recorder.stamp_pending()`)。
7. snapshot construction(`workflow_session.cpp:1521-1591`):`suspended.node = pending->node`,`suspended.agent = descriptor node's runner`,`suspended.pending_cap_id = pending->cap_id`,`suspended.pending_ordinal = pending->ordinal`。

**Completed:**
1. runner 返回 `(OK, O_k, output_size)`。
2. scheduler 写 tag-0 record + publish `event_count = schedule_pos + 1`(`core_wasm_codegen.cpp:16519-16527`)。
3. host 检测 `(OK, ...)` -> `run_status = Completed`。

**Frontier classification:** recorder 的 `classify(schedule_pos, ordinal)`(`wasm_resume_recorder.hpp:134`)对 bridge call 与 opaque call 一视同仁。frontier 是 `(suspended.node, pending_ordinal)`。

**D1b resume controller(test-only FOUNDATION)修订(`core_wasm_resume_controller.cpp:315-392`):**

`coordinate_gate` 当前要求 frontier 是 `NodeKind::Capability`(`cap_call_count=1`)。v2 manifest 中 P6 bridge node 的 `cap_call_count=0`(identity)。controller 必须接受 P6 bridge node 作为 frontier:
- frontier node 的 `cap_call_count == 0` 且 `bridge_sites` 非空 -> 合法 frontier。
- `event_join` 检查:`cap_call_count=1` != tag-1 record -> mismatch;P6 bridge node `cap_call_count=0` 且 tag-0 record(identity)-> match。
- frontier 的 pending coordinate 与 `bridge_sites[ordinal]` 交叉检查(`capability` + `source_symbol`)。

D1b controller 是 test-only FOUNDATION(sunset clause);production resume path 是 session memo layer。

### 12.14.7 Q5: Fresh-instance replay and String payload arena

**决策:fresh instance;suspended node 之前的 node replay;suspended runner 内更早的 bridge call 从 per-node memo 供给。memo entry 存储 P4-D frame + String payload arena bytes。**

**Replay 算法(§12.6.5 泛化到 bridge call):**

1. Fresh instance(session 每次 run 创建新 wasm3 instance)。
2. Nodes < suspended replay:其 capability call(opaque 或 bridge)从 memo 供给。
3. Suspended node 的 runner 内:
   - 更早的 bridge call(`ordinal < pending_ordinal`):MemoHit,从 per-node memo 供给。
   - pending bridge call(`ordinal == pending_ordinal`):Frontier,inject。
   - 更晚的 bridge call(`ordinal > pending_ordinal`):PostFrontier,live(`ReadyForLive`)。

**Bridge memo capture(origination):**

wrapped_callback 在 `inner(obs)` 返回后(`workflow_session.cpp:1014-1072`):
- `ImportReply{AHFL_CAP_OK, GuestPointer{site.result_base}, site.result_extent}`(`capability_import.cpp:556-557`)。
- host 从 `result_base` 读 `result_extent` bytes(P4-D frame)。
- host 还需读 String payload arena bytes:`result_payload_base` 起 `result_payload_capacity` bytes。
- wrapped_callback 通过 control-block 解析 bridge site(与 `handle_bridge` 的 `capability_import.cpp:332-366` 相同逻辑:`obs.scalar_arg -> block_ptr -> block_index -> bridge_call_sites[block_index]`),获取 `result_payload_base` + `result_payload_capacity`。
- memo entry 的 `authoritative_json = [P4-D frame (result_extent bytes)][String payload arena (result_payload_capacity bytes)]`。

**Bridge memo injection(replay, MemoHit):**

wrapped_callback 在 `recorder.classify` 返回 `MemoHit` 后:
1. Cross-check `cap_id` + `arg_hash`(与 opaque lane 相同,`workflow_session.cpp:855-880`)。
2. 从 `(node, ordinal)` 解析 bridge site:manifest bridge-site table -> `call_site_id` -> frame section `bridge_call_sites[call_site_id]`。
3. 把 recorded bytes 拆成两段:`[0, result_extent)` = P4-D frame,`[result_extent, result_extent + result_payload_capacity)` = String payload。
4. 写 P4-D frame 到 `site.result_base`(bounds check:`result_base + result_extent <= page->size()`)。
5. 写 String payload 到 `site.result_payload_base`(bounds check:`result_payload_base + result_payload_capacity <= page->size()`)。
6. 返回 `ImportReply{AHFL_CAP_OK, GuestPointer{site.result_base}, site.result_extent}`。

这与 live `handle_bridge` 的 result placement(`capability_import.cpp:535-557`)完全一致:zero-fill `result_base` 起 `result_extent` bytes,然后 `pack_value_at` 写 frame + String bytes 到 payload arena。replay 直接写 recorded bytes,跳过 `pack_value_at`(bytes 已 packed)。

**String payload arena 的必要性:**

bridge result 的 String 类型有 `PtrLen (ptr, len)` 指向 per-call-site `result_payload` arena(`frame_packer.cpp:202-239`:String bytes bump-allocated in payload arena via `arena_cursor`)。`e2e_multi_agent.ahfl` 的 bridge agent 有 String result(`ClassifyResult.confidence`、`SupportResult.response`、`SummaryResult.summary`)。如果 memo 只存 P4-D frame bytes,String 的 `PtrLen` 指向 arena 但 arena bytes 未记录,replay 时 guest 读到 stale/zero bytes -> divergence。因此 memo 必须同时 capture frame + arena。

**arg_hash 计算:**

bridge call 的 arg_hash 需要 decode P4-D args 成 Values,然后 `runtime::hash_values(decoded_args)`。这与 evaluator 的 arg_hash 一致。wrapped_callback 需要一个 `decode_bridge_import_args` helper(从 `handle_bridge` 的 argument decoding 部分 `capability_import.cpp:418-511` 重构)。origination 和 replay 都用同一 helper,确保 arg_hash 一致。

**Determinism for data-dependent later calls:**

`(node, ordinal)` identity 确保 ordinal 0 在 origination 时 live 供给、replay 时从 memo 供给。memo 存储 exact P4-D frame + String payload bytes。replay 时 host 把这些 bytes 写回同一 result region。guest 读到相同 bytes,产生相同 control flow。ordinal 1(依赖 ordinal 0 的 result)被确定性地到达并 pend。这证明了 data-dependent later calls 的 determinism。

### 12.14.8 Q6: Composition with WH-5b.3 xcode

**决策:xcode 在 wrapped_callback 中 route FIRST(pure codec,no memo),bridge/opaque 走 memo/replay。无 contamination。**

**Routing order(`workflow_session.cpp:770-786`):**

1. xcode imports(`ahfl_xcode.xcode_<N>`):route to `handle_transcode`。pure codec,不 invoke capability,不 touch memo,不写 event record。replay 时重新执行(确定性)。
2. Bridge imports(empty `param_frame`):memo/replay machinery(WH-5b.2 新增,删除 `workflow_session.cpp:756-761` 的 skip)。
3. Opaque imports(non-empty `param_frame`):memo/replay machinery(WH-4b 现有)。

**无 memo/xcode contamination:**

- xcode 从不 touch memo(不读、不写)。
- bridge/opaque memo 从不 touch xcode(xcode 在 memo machinery 之前 route)。
- xcode 的 `transcode_by_ordinal` map 与 bridge/opaque 的 import ordinal 空间不相交(xcode 有自己的 import namespace `ahfl_xcode`)。

**Shadow/payload arena behavior under replay:**

- **xcode shadow region:** reused(每次 overwrite)。replay 时 xcode 重新执行并 overwrite shadow。确定性。
- **xcode payload arena:** bump-allocated within a run。replay 时 arena cursor reset(fresh instance)。确定性。
- **bridge result regions:** per-call-site disjoint(`core_frame_layout.hpp:60-92` 的 `result_base` / `result_payload_base` 是 compile-time constant,pairwise disjoint)。replay 时 memo 写入同一 region。与 xcode shadow 不 interference。

**P6 runner that both transcodes and bridges:**

scheduler 在 runner 之前 call xcode(transcode input),然后 call runner。runner 内部 call bridge imports。xcode call 在 scheduler 内(memo-replay machinery 跳过它)。bridge calls 在 runner 内(memo-replay machinery 处理)。无 interference。

**xcode fail-closed 不变:** `transcode.cpp:199-222` 的 JSON_TO_P4D shadow/payload bounds guards 保持不变。xcode failure 返回 `ImportReply{AHFL_CAP_ERROR}`(`transcode.cpp:63-65`),guest scheduler trap -> `NodeFailed`。这与 WH-5b.2 无关。

### 12.14.9 Q7: Census, tests, and scope

**Census 新增 `suspended` terminal scenario:**

当前 census(`conformance_wasm_native_runner.cpp:64-65`):`kExpectedNativeAgreed = 66`,`kExpectedNativeSkipped = 0`,全部 expect `"completed"`。WH-5b.2 新增:

- 新 manifest(如 `wh5b2_bridge_pending.case.json`):bridge workflow 在 in-handler bridge call 处 pend。scenario expect `run_status: "suspended"`。
- mock capability 对 bridge call 返回 Pending。
- census runner 处理 `"suspended"` terminal(`conformance_case.hpp:935` 已支持 `"suspended"` 作为 valid `run_status`)。
- census pin 更新:`kExpectedNativeAgreed` 从 66 升到 67(或更多,取决于 scenario 数量),deliberately blessed。

**Evaluator parity:**

- evaluator 在同一 fixture 上 suspend(`capability_eval.cpp:108-116`)。
- census 比较 wasm observation 与 evaluator observation。两者都应 report `"suspended"`。
- resumed completion:separate ctest(不是 census)驱动 suspend/resume cycle:suspend -> snapshot -> resume with injected result -> `Completed`。resumed completion 与 evaluator 的 resumed completion 比较。

**Enumerated fail-closed cases:**

1. **Ordinal tamper:** memo entry 的 ordinal 不匹配 live call 的 ordinal -> divergence(`workflow_session.cpp:855-860` `entry->cap_id != cap_id` 同类检查)。
2. **Arg_hash mismatch:** memo entry 的 arg_hash 不匹配 live call 的 arg_hash -> divergence(`workflow_session.cpp:876-880`)。
3. **Site OOB:** bridge site 的 `result_base + result_extent` 超出 page -> fail closed(`capability_import.cpp:535-536` 同类 bounds check)。
4. **Unknown site:** manifest bridge site 的 `call_site_id` 不在 frame section 的 `bridge_call_sites` -> fail closed。
5. **ERROR status word:** bridge call 返回 ERROR -> trap -> `NodeFailed`(parity with evaluator)。
6. **PENDING carrying non-empty result:** bridge call 返回 PENDING with non-null `result_ptr` -> host `handle_bridge` 总是返回 `GuestPointer{0}` for Pending,所以这种情况不可达;defense-in-depth check 保持。
7. **Tag-0 published while suspended:** scheduler 在 runner pend 时不写 tag-0 record。test 验证 `event_count` 保持 `schedule_pos`。
8. **Manifest v1 rejection:** decoder 拒绝 v1 manifest(`*version != kExecManifestVersion` -> error)。
9. **Bridge site ordinal gap:** per-node ordinals 不 dense(0, 2, ...) -> fail closed。
10. **Bridge site call_site_id duplicate:** 同一 workflow 内两个 bridge site 有相同 `call_site_id` -> fail closed。

**Agent-lane vs workflow-lane scope:**

WH-5b.2 是 **workflow-lane only**。agent lane(`WasmAgentRunner`)无 recovery-snapshot consumer、无 conformance census pressure for pending。agent lane 的 run2 Pending arm 保持 `NodeFailed`。这与 §12.6.10 的 scope 一致。

**D5 instance-reuse 影响:**

无。D5 rule(`core_wasm_codegen.cpp:12997-13001`)确保每个 P6 node 有自己的 runner。in-runner bridge-site table 是 per-node 的,`(node, ordinal) -> call_site_id` 映射无歧义。无需 D5 修订。

### 12.14.10 Q8: Acceptance criteria

1. - [ ] Fresh `-Werror` dev build:zero warnings(`cmake --preset dev && cmake --build --preset build-dev`)。
2. - [ ] `ctest --preset test-dev --output-on-failure -L wasm`:all pass,including new WH-5b.2 tests。
3. - [ ] ASan build & test(`cmake --preset asan && cmake --build --preset build-asan && ctest --preset test-asan`):clean on touched targets。
4. - [ ] `WASM=OFF` build:clean(无 wasm backend 时 codegen 不编译)。
5. - [ ] Census:existing 66 agreed / 0 skipped 保持;new suspended scenarios agree(wasm == evaluator == `"suspended"`);pin deliberately blessed。
6. - [ ] E3-E6 resume tests(`wasm_workflow_resume_e2e.cpp`):unchanged(all-opaque,无 P6 nodes)。
7. - [ ] Bridge suspend/resume e2e:suspend -> snapshot -> resume with injected result -> `Completed`;resumed output 与 evaluator 一致。
8. - [ ] Fail-closed family:all 10 enumerated cases(§12.14.9)fail closed with actionable diagnostics。
9. - [ ] Evaluator parity:suspended terminal + resumed completion match(`run_status`、`pending_cap_id`、`pending_ordinal`、output bytes)。
10. - [ ] Determinism:data-dependent later calls(ordinal 0 live->memo,ordinal 1 pending)在 replay 时确定性到达。
11. - [ ] No tag-0 on suspend:`event_count` 保持 `schedule_pos`;snapshot 的 `completed_nodes` 不含 suspended node。
12. - [ ] Manifest v2:codegen emits v2;decoder rejects v1;A2 admission-set equality(opaque + bridge == wire schema == imports)holds;transcode imports excluded。
13. - [ ] Host-side trust checks:manifest bridge-site table 与 frame section 交叉检查(call_site_id、source_symbol、bounds、ordinal density、call_site_id uniqueness)。
14. - [ ] D1b resume controller(test-only):P6 bridge node 作为 frontier 被接受;coordinate_gate 通过。
15. - [ ] No old/new coexistence:v1 manifest decoder 删除;bridge trap arm 删除;wrapped_callback bridge skip 删除;`cap_call_count > 0` for P6 bridge nodes 删除。

### 12.14.11 Q9: Rejected alternatives

**Option A: Mid-expression frame parking — REJECTED。**

wasm3 interpreter 不能 park a call stack。node 是 re-run unit(evaluator parity:`workflow_runtime.cpp` re-evaluates node-input expression on resume)。fresh-instance replay 是 WH-4b 的模型,已落地、已测试。Reference:Rust async/await parking 是 language-level feature(state machine transformation),不是 runtime trick;AHFL 的 wasm3 interpreter 无 frame parking 能力。在 wasm3 中实现 frame parking 需要保存/恢复整个 interpreter stack,这是重新发明一个 debug info-based unwinder,违反 Principle 1(industry-standard approach = fresh-instance replay,同 GHC 的 IO replay)。

**Option B: Keep the trap — REJECTED。**

evaluator 在 ANY position 的 Pending 都 suspend(`capability_eval.cpp:108-116`)。wasm bridge lane trap -> `NodeFailed`。这是 WH-6 cutover 后会暴露的 parity regression。trap 是 codegen limitation,不是 semantic difference。§12.12.10 已记录为 mandatory pre-WH-6 slice。

**Option C: Key memos by guest pointer — REJECTED。**

guest pointer 在 fresh-instance replay 中不稳定(wasm3 分配新 linear memory)。`(node, ordinal)` identity 是 stable key。Reference:Rust 的 `Pin`/pointer identity 不用于 replay key;index-based identity 是 standard(AHFL Principle 2:Index-Based, Not String-Based;同理 pointer-based 也不适合 replay identity)。

**Option D: Per-import global ordinal — REJECTED。**

evaluator 的 ordinal 是 per-node(`workflow_runtime.cpp:754`)。global ordinal 不匹配 evaluator 的 identity,破坏 parity。per-node ordinal 是正确 key。Reference:Rust 的 `Substs` 按 parameter position index,不按 global declaration order——per-scope index 是 type-system 的 standard。

**Option E: Compat flag / dual implementation — REJECTED by CLAUDE.md Principle 1。**

无 transitional state,无 old/new coexistence。big-bang change flip every call site:codegen emits v2 manifest,decoder accepts only v2,bridge status arm changes,old trap path deleted,wrapped_callback bridge skip deleted,`cap_call_count > 0` for P6 bridge nodes deleted。`BREAKING CHANGE:` footer 标记这些删除。

**Option F: Put in-runner bridge-site table in frame section instead of exec manifest — REJECTED。**

frame section(`ahfl.core-layout.v1`)是 P4-D layout authority,不携带 capability identity。A2 admission 读 exec manifest 做 set equality;把 bridge-site table 放 frame section 会迫使 admission 同时读两个 payload,增加 attack surface。exec manifest 是 import authority 的 natural home(A2 已读它)。`call_site_id` join 到 frame section 的 `bridge_call_sites` 用于 result placement,这是 table join,不是 duplication。

### 12.14.12 Costs / LOC

| Component | LOC delta | Notes |
|-----------|-----------|-------|
| `core_wasm_codegen.cpp` | ~150 | bridge status arm PENDING(~20)、scheduler P6 PENDING arm(~20)、manifest v2 emission(~50)、bridge-site table emission(~60) |
| `core_wasm_schema_module.cpp` | ~160 | manifest v2 decode(~80)、A2 admission-set equality(~40)、bridge-site table in payload(~40) |
| `workflow_session.cpp` | ~350 | bridge memo/replay routing(~100)、bridge memo capture + String arena(~60)、bridge memo injection(~80)、bridge frontier injection(~60)、host-side trust checks(~50) |
| `capability_import.cpp` | ~80 | refactor argument decoding + packing into shared helpers |
| `wasm_resume_recorder.hpp/cpp` | ~30 | generalize to bridge calls(per_node_counters_ already covers) |
| `core_wasm_resume_controller.cpp` | ~80 | D1b controller bridge-site support(test-only FOUNDATION) |
| Tests | ~450 | census scenario、e2e suspend/resume、fail-closed family、determinism |
| Docs | ~100 | 本节 |
| **Total** | **~1400** | |

### 12.14.13 Sequencing + gate revision

**顺序:**
1. **WH-5b.1**(§12.12 修复 (a)/(b))- 已 landed(commit 7813556c)。
2. **WH-5b.3**(§12.13,transcode)- 已 landed(commit 8dbd2570)。
3. **WH-5b.2**(本节,bridge-pending parity)- 立即开始。
4. **WH-6**(ahflc run cutover)- gated on 5b.1 + 5b.3 + 5b.2。

**Gate revision:** §12.13.13 的 gate set 是 "WH-6 gated on 5b.1 + 5b.3 + 5b.2"。本节不改变该 gate set(5b.2 已在其中)。WH-5b.2 落地后,gate set 的三个前置全部满足。

### 12.14.14 Prior-decision preservation statement

- **§12.6(WH-4b suspend/resume):** 保留,不重写。WH-5b.2 复用 WH-4b 的 memo/replay 基础设施(`WasmResumeRecorder`、`WorkflowRecoverySnapshot` v2、ExactSidecar),不改变 opaque terminal 的 PENDING latch、memo/replay、snapshot schema。bridge lane 是 WH-4b machinery 的新 consumer,不是新机制。
- **§12.7(WH-6 ahflc run cutover,无 fallback):** 保留,不重写。WASM=OFF 策略不变。gate set 不变(§12.14.13)。
- **§12.12(Option A'):** 保留作历史档案。§12.12.10 记录的 bridge-pending parity gap 由本节关闭。
- **§12.13(WH-5b.3 transcode):** 保留,不重写。WH-5b.2 不改变 transcode 的 routing order、shadow/payload arena、fail-closed guards。xcode 与 bridge memo/replay 无 contamination(§12.14.8)。

## 12.15 WH-5c decisions: cutover-blocking guest codegen gaps (2026-10-02, dedicated decision agent, no human gate)

本节关闭 WH-6(`ahflc run` cutover,工作树中 uncommitted)暴露出的 9 个 product-test red。独立验证(逐 test 运行 + 读 ahflc stderr + code-section byte dump + host 内存 dump)确认这 9 个 test 分裂为 **FIVE 个独立机制**,不是 builder 初步诊断的四个。所有五个 shape 都是 evaluator 已 shipped 的行为,CLI contract 要求 wasm lane 对齐——不接受 "honest limitation"、不弱化 fixture、不引入 fallback。

### 12.15.1 问题复现与证据链

**Runtime 复现:** WH-6 cutover 后 `ahflc run` 驱动 wasm lane。9 个 product test 失败(#213/#214/#421/#422/#423/#424/#425/#426/#427),外加 #79(runtime_evidence_smoke)作为 #421 的纯 cascade。baseline 585/588(#81/#84/#85 是无关的 pre-existing pnpm env failure)。

**逐 test 的精确 ahflc stderr(2026-10-02 在 uncommitted cutover 上运行):**

| Test | 机制 | 精确 error |
|------|------|-----------|
| #213 #214 #421 #422 | GAP 1 | `error [wasm.RESOURCE_EXHAUSTED]: two workflow nodes reuse one packaged agent instance; V2-D assigns one node-frame block per instance in a P6-frame workflow and rejects reuse (re-use analysis pending)` |
| #423 | GAP 2 | `error [wasm.UNSUPPORTED_CAPABILITY_FRAME]: KR6.5 E2 capability final must contain canonical input, one call, and return` |
| #424 | GAP 5 | `error [wasm.UNSUPPORTED_CAPABILITY_FRAME]: RFC 0026 P6 scalar codegen cannot lower body 'Evaluate': a String literal is constructible only inside a P6-7 frame-bridge v2 computed final: this builder owns no in-module rodata region (the E1-E3/FB lanes never build one, and V2-B does not construct String literals in a non-final frame handler)` |
| #425 #426 #427 | GAP 3 | `error [wasm.host-abort]: run_workflow_session: run2 host-aborted (capability import failure) (CapabilityImportError=ArgDecodeFailed)` |
| (parity) | GAP 4 | `ahfl.run-report` v1 的 `output_value_id` 在 wasm lane 为 null,evaluator 为 0/1 |

**Source 证据链(HEAD c960bff0 + uncommitted cutover):**

1. **GAP 1(instance reuse):** `core_wasm_codegen.cpp:13078-13088` D5 lifecycle rule——`if (plan.has_p6_nodes) for (instance_users) if (users > 1) -> kResourceExhausted`。`node_blocks` 按 packaged-instance dense runner index keyed(`p6_block_by_runner` 12348-12357;`node_blocks` P6-dense 13878-13894)。runner code **not frame-base-reentrant**:`P6FrameRelocation` 把 `I_k/C_k/scratch_k/O_k` bake 成 `i32.const` immediate(13455-13463);runner 忽略其 `(ptr,len)` args 做 addressing(15351-15353);每个 runner 有 private `current_state` global(globals (5/6)+runner);scheduler 验证 returned `O_k` 命名 runner 自己的 block(16706-16721)。
2. **GAP 2(opaque capability-final canonical-only):** `validate_capability_final`(1428-1517)要求 canonical 3-statement shape `let in=input; let r=Cap(in); return r;`。fixture `state Done { return Echo(Request{value: input.value}); }` 构造 aggregate arg,被拒。第二道 gate:scheduler materializer 拒绝 constructed P4-D input 到 opaque node(13041-13046 "add the frame-bridge packaging")。§12.12 forbids normalizing opaque-final to bridge。
3. **GAP 5(#424 String literal in non-final handler):** `examples/execution-demo/src/decision.ahfl` 的 `state Evaluate`(non-final)赋值 String literal(`ctx.reason = "high severity incident";`)。P6 scalar/FB builder 不 own rodata Data region——V2-B 的 `shared_rodata_pool` + active Data(11) segment 只在 P6-7 frame-bridge v2 computed-final 路径 build。
4. **GAP 3(null-root descriptor):** host 侧 `build_bridge_root_regions`(180-238)**已**包含每个 node block 的 `scratch_k`(workflow lane);`build_bridge_string_regions`(129-172)已包含 entry payload arena `[1136,3184)`。host authorization **正确且 fail-closed**。`capability_import.cpp:674` 的 `len==layout->size` check **PASS**(8-byte inline Request root);`:678` 的 root-region check **FAIL**,因为 guest 写的 descriptor 是 `(ptr=0, len=8)`——constructed aggregate 的 root address 是 NULL。**关键经验注记:** code-section byte dump 显示 relocated Init handler 发射了正确的 construct address(`i32.const 1104; local.set 2; local.get 2; i32.store @1080`),但 runtime descriptor 读到 0。builder 必须 pin 精确机制(见 §12.15.6)。
5. **GAP 4(output_value_id parity + fail-open):** canonical identity agent 分类为 `IdentityAction` 在 OPAQUE WireJson lane(`validate_identity_final` 1354-1394;p6 requires computed final/goto/bridge at 12964-12966;module contract WireJson 17756-17758)。该 lane 的 node output **deliberately never decoded**(`workflow_session.cpp:1789-1815` guard `is_p6` false);completion 从 `workflow_completed_count` global 推导(2012-2027);identity runner 原样返回 host-packed input tuple(15121-15128,scheduler `append_workflow_source` 16352-16366),只有 workflow final output 被 decode + id 0。evaluator 存 node ids 0,1 然后 workflow id 2(`workflow_runtime.cpp:1427-1433` `add_runtime_value` + `emit(NodeCompleted{.output=output_id})` at 1480)。byte-parity harness **deliberately strips** `output_value_id`(`wasm_runner.cpp:1356-1366`);CLI run-report **不 strip**。**真实 asymmetry:** node-output decode failure **fail OPEN**(silent `NoneValue` 1808-1810),workflow-output decode failure **fail CLOSED**(`kOutputDecodeFailed` 1965-1974)。

### 12.15.2 决策摘要

**ONE coherent big-bang change(WH-5c):** wasm lane 的 guest codegen + host session 对齐 evaluator 的五个 shipped 行为。具体:

1. **GAP 1 → per-node frame blocks。** `node_blocks` 从 per-packaged-instance 改为 per-node。runner 变为 frame-base-reentrant:不再 bake `I_k/C_k/scratch_k/O_k` 为 `i32.const`,而是接收 frame-base `i32` arg 并 relative addressing(`i32.add`)。per-node `current_state` slot(按 node ordinal index)。scheduler 在 dispatch 时传 node 的 block base。删除 D5 reuse rejection(big-bang)。
2. **GAP 2 → opaque capability-final 接受 constructed aggregate arg。** 在 opaque lane 上 in-module 构造 aggregate(复用 V2-B bump scratch arena + rodata Data),host 把构造的 P4-D span transcode 成 wire JSON 喂给 capability。relax `validate_capability_final` 的 canonical shape 与 scheduler materializer 的 constructed-input gate。**不**转 bridge(§12.12 保留)。
3. **GAP 5 → P6 lane 全 handler rodata。** 把 V2-B 的 `shared_rodata_pool` + active Data(11) emission 从 computed-final-only 扩展到所有 P6 handler(含 non-final state)。String literal 的 PtrLen 命名 rodata region(已在 `string_regions` via `rodata_base/extent`)。**不 duplicate** machinery——扩展现有 scope。
4. **GAP 3 → guest descriptor 命名 construct root。** 修复 guest codegen 使 bridge descriptor 的 ptr 字段命名 construct 的 scratch address(不是 0)。host 保持 fail-closed 不变。diagnostic 携带精确 sub-reason(root-outside-region / length-mismatch / spill-oob / string-leaf-region)。
5. **GAP 4 → node output decode + fail-closed alignment。** wasm lane 在 node completion 时 decode node output(WireJson lane 也 decode)并分配 sequential `RuntimeValueId`(node completion order,然后 workflow output),与 evaluator 对齐。node-output decode failure 从 fail-OPEN 改为 fail-CLOSED(对齐 workflow-output path)。byte-parity harness 的 `output_value_id` stripping **删除**(big-bang)。

### 12.15.3 Q1: GAP 1 — per-node frame blocks (instance reuse)

**决策:`node_blocks` 改为 per-node;runner 变为 frame-base-reentrant;删除 D5。**

**Root cause:** V2-D 把 node-frame block 按 packaged-instance 分配(`p6_block_by_runner`),一个 instance 一个 block。fan-out(多个 node 复用一个 agent type)是 core DAG pattern,evaluator 天然支持(每个 node invocation 有独立的 frame)。wasm lane 的 D5 rule 直接 reject。

**Design:**

- **`CoreFrameLayoutSection::node_blocks`**(`core_frame_layout.hpp:183`)从 P6-dense(per packaged instance)改为 per-node(per workflow node)。cardinality = `workflow.nodes.size()`(P6 node),不是 `packaged_instances.size()`。
- **Runner frame-base parameterization:** `P6FrameRelocation` 不再 bake `input_base/context_base/scratch_base/output_base` 为 `i32.const`。runner 的 functype 增加一个 `i32 frame_base` param;handler 内所有 frame addressing 变为 `frame_base + offset`(`i32.add`)。scheduler 在 dispatch node 时把该 node 的 `block.input_base`(或 block base)作为 `frame_base` 传入。
- **Per-node state slots:** 每个 runner 的 private `current_state` global 从 single global 改为 per-node slot(按 node ordinal index 的 dense array,或 scheduler 在 dispatch 时 write/read 的 per-node state word)。scheduler 维护 `node_state[node_ordinal]`。
- **Scheduler re-addressing:** scheduler 的 node dispatch(`core_wasm_codegen.cpp:16706-16721` 附近)不再验证 returned `O_k` 命名 runner 自己的 block(因为 block 是 per-node 的);改为验证 returned `O_k` 命名 **dispatched node** 的 block。
- **删除 D5:** `core_wasm_codegen.cpp:13078-13088` 的 `instance_users` reuse rejection **删除**(big-bang)。`instance_users` map 本身删除。
- **Re-use analysis 保证:** 不再需要 "observable overlap" 分析——per-node block 天然 non-overlapping(每个 node 有自己的 fixed block)。scheduler 串行执行 node(DAG topological order),但 block 是 fixed address,不依赖时间复用。

**Reference Hierarchy:** 这偏离 Clang 的 monomorphization 模型(per-instance baked address),转向 Swift 的 generic runtime 模型(per-node frame-base parameter + witness-table 式 dispatch)。AHFL-specific divergence reason:fixed 64KiB page 无法为 fan-out workflow 的每个 node 存一个 baked block;runtime frame-base parameterization 让 N 个 node 共享一个 runner code body,page 只存 N 个 block 的 data。Rust 的 monomorphization 是 compile-time code duplication(不占 data page);AHFL 的 wasm3 单 page 约束使 code duplication 不可行(一个 runner body 已占 code space),因此选 runtime parameterization。

**Stale comment 修正(builder 必做):**
- `core_frame_layout.hpp:165-166`("parallel to the workflow module's sorted packaged-instance table")——false,改为 per-node。
- `core_wasm_codegen.cpp:15246-15250`(oversells reset safety)——修正为 per-node 语义。

### 12.15.4 Q2: GAP 2 — opaque capability-final constructed aggregate arg

**决策:opaque lane 支持 in-module aggregate 构造 + host wire-JSON encoding;relax canonical shape gate;不转 bridge。**

**Root cause:** `validate_capability_final`(1428-1517)只接受 `let in=input; let r=Cap(in); return r;`——arg 必须是 bare `input` reference。`Echo(Request{value: input.value})` 构造新 aggregate,被拒。第二道 gate(scheduler materializer 13041-13046)拒绝 constructed P4-D input 到 opaque node。

**Design:**

- **In-module 构造(opaque lane):** opaque capability-final 的 arg 如果是 construct expression,在 module 内用 V2-B 的 bump scratch arena(`scratch_addr_cursor_` + `scratch_capacity()`)构造 aggregate。construct 的 field 从 input frame / context / literal 读取。这复用 `plan_construct`(4704-4808)+ `emit_construct`(7014-7121)的现有 machinery——agent/P6 lane 已在用。
- **Host wire-JSON encoding:** opaque lane 的 capability arg 是 wire JSON。host 在 call opaque capability 前,把构造的 P4-D span 用 `ahfl_xcode` 的 P4D_TO_JSON transcode(WH-5b.3 已有)encode 成 wire JSON。transcode site 的 source 是 construct 的 scratch span,target 是 capability 的 wire param。
- **Relax `validate_capability_final`:** canonical shape 从 "bare input reference" 扩展为 "input reference OR in-module construct whose leaves are input/context/literal references"。仍拒绝跨 lane 的 heap edge(由 WH-5b.3 xcode 处理)。
- **Relax scheduler materializer gate(13041-13046):** opaque node 的 constructed input 在 in-module 构造 + xcode 后变为 wire JSON,gate 从 "reject constructed P4-D input" 改为 "accept in-module construct + xcode"。
- **不转 bridge:** §12.12 的 prohibition 保留。opaque lane 是 wire-JSON boundary;把 opaque-final 转 bridge 会 blur lane discipline。construct 在 opaque lane 内 in-module 完成,不经过 bridge ABI。

**Reference Hierarchy:** Rust 的 monomorphization——opaque lane 为每个 concrete construct type 生成 specialized in-module construction code(不引入 runtime type info)。AHFL-specific divergence:opaque lane 的 wire-JSON boundary 要求 construct 在 module 内完成(不能依赖 host-side heap allocation),这与 Rust 的 compile-time specialization 一致——construct 的 layout 是 compile-time 已知的 P4-D layout。

### 12.15.5 Q3: GAP 5 — rodata for non-final handler String literals

**决策:把 V2-B rodata machinery 从 computed-final-only 扩展到所有 P6 handler;不 duplicate。**

**Root cause:** `#424` 的 `state Evaluate`(non-final)赋值 String literal(`ctx.reason = "high severity incident";`)。P6 scalar/FB builder 不 own rodata Data region——`shared_rodata_pool` + active Data(11) segment 只在 P6-7 frame-bridge v2 computed-final 路径 build。error message 明确:"this builder owns no in-module rodata region"。

**Design:**

- **扩展 `shared_rodata_pool` scope:** 从 "computed-final only" 扩展到 "all P6 handlers"。任何 P6 handler(Init/Evaluate/Done/...)中的 String literal 都 intern 到 `shared_rodata_pool`,emitted 到 active Data(11) segment(base 256,capacity 768)。
- **PtrLen naming rodata:** String literal 的 PtrLen `(ptr, len)` 命名 rodata region(`rodata_base=256, rodata_extent=<Data length>`)。host 的 `build_bridge_string_regions` 已包含 rodata(129-172),所以 bridge call 用 String literal arg 时 authorization 已覆盖。
- **不 duplicate machinery:** 复用现有 `shared_rodata_pool` + Data(11) emission。唯一改动是 scope 从 computed-final 扩展到 all handlers。
- **Capacity accounting:** rodata region `[256, 1024)` 是 768 bytes。String literal 的总 byte length 不能超过。超限 fail-closed with `kResourceExhausted` + SourceRange diagnostic(Principle 5)。

**Reference Hierarchy:** Swift 的 string-literal interning(compile-time uniquing into a shared rodata pool)。AHFL-specific divergence:wasm3 单 page 的 rodata region 固定在 `[256, 1024)`,不能像 Swift 那样动态扩展;capacity 是 compile-time checked 的。

### 12.15.6 Q4: GAP 3 — null-root descriptor for constructed aggregate bridge arg

**决策:修复 guest codegen 使 bridge descriptor 命名 construct root;host 保持 fail-closed;diagnostic 携带 sub-reason。**

**Root cause(经验证):** host 侧 authorization **正确**——`build_bridge_root_regions` 已包含 `scratch_k [1104,1120)`,`build_bridge_string_regions` 已包含 entry payload arena `[1136,3184)`。`capability_import.cpp:674` 的 `len==layout->size` PASS(8-byte inline Request root)。`:678` 的 root-region check FAIL,因为 guest 写的 descriptor 是 `(ptr=0, len=8)`。

**未解决的机制矛盾(builder 必须 pin):** code-section byte dump 显示 relocated Init handler 发射了 `i32.const 1104; local.set 2`(construct address)和 `local.get 2; i32.const 1080; i32.store`(descriptor ptr = 1104)。emitted wasm bytes 是正确的。但 runtime `whole_memory[1080]` 读到 0。同样,construct store `i32.const 1104; local.get 0; i32.store` 应写 `[1088]=1136` 到 `[1104]`,但 runtime `[1104]=1080`。len store(`[1108]=local 1=5`)正确。这指向 local-read 路径在 workflow relocated lane 上的 defect(construct-result local binding / relocation / wasm3 0.9.0 interaction 三者之一)。

**Builder 必做的 pinning steps(在 scratch build 中):**

1. 在 `emit_bridge_call` 的 Ptr-arg branch(6663-6685)加 compile-time assertion:descriptor ptr 必须等于 `scratch_base() + construct_addrs_[id.value]`(对 construct arg)。如果 assertion 失败,codegen 路径有 defect。
2. 在 host `decode_bridge_import_args` 加 scratch-build-only check:descriptor ptr 必须在 `root_regions` 内且非 0;如果 emitted bytes 正确但 runtime 读到 0,用 wasm3 execution trace 或 isolated module run 定位是 wasm3 mis-execution 还是 module/instance lifecycle mismatch。
3. 对比 agent-lane(non-relocated)handler body 与 workflow-lane(relocated)body 的 construct+bridge pattern,找 differentiator。agent lane 是 older tested path,如果 agent lane 的 construct+bridge 正常,defect 是 relocation-specific。

**Fix direction(guest-side):** 无论 pinning 结果是 codegen 还是 wasm3 interaction,fix 在 guest 侧:
- 如果是 construct-result local binding 在 relocated builder 中丢失/rebind:修复 `readable_local(arg)` / `bind_value` 在 workflow packaging lane 的 local 分配。
- 如果是 relocation 没 apply 到 descriptor root:修复 `P6FrameRelocation` 使其覆盖 construct-result local 的 address。
- 如果是 wasm3 0.9.0 mis-execution(block-wrapped handler + `local.tee` + `i32.load` pattern):升级 wasm3 或改写 handler body 避免触发 pattern(后者是 workaround,需注释说明)。

**Host 侧不变:** `decode_bridge_import_args` 的 fail-closed 保持原样。唯一 host 改动是 diagnostic sub-reason(下条)。

**Diagnostic sub-reason(Principle 5):** `ArgDecodeFailed` 当前 swallow sub-reason。改为携带 enum:
- `kRootOutsideRegion`(root-region check fail,:678)
- `kLengthMismatch`(len != layout->size,:674)
- `kSpillOutOfBounds`(spill window check fail,:664-668)
- `kStringLeafOutsideRegion`(string-leaf region check fail,`read_value_at` 内)
- `kNullDescriptor`(ptr==0,新增——明确区分 "guest 写了 null" 与 "guest 写了 valid-but-unauthorized address")

`CapabilityImportError` enum 增加 `ArgDecodeFailedRootOutsideRegion` 等 sub-code,或 `ArgDecodeFailed` 携带一个 `ArgDecodeSubReason` field。diagnostic message 用 human-readable 语言(不 echo address bytes,per `core_frame_layout.hpp:20` 的 fixed-diagnostic discipline——但 sub-reason enum 是 schema-only string,允许)。

**Reference Hierarchy:** Clang 的 diagnostic infrastructure(`err_fe_backend_unsupported` 携带 sub-reason)。AHFL-specific divergence:wasm3 单 page 的 region authorization 是 security boundary,diagnostic 不能 echo address bytes(防 leak),但 sub-reason enum 是安全的。

### 12.15.7 Q5: GAP 4 — output_value_id parity + fail-closed alignment

**决策:wasm lane decode node output 并分配 sequential RuntimeValueId;node-output decode failure 改 fail-closed;删除 parity harness 的 output_value_id stripping。**

**Root cause:** 两个独立问题:
1. **Parity:** WireJson lane 的 node output deliberately never decoded(`workflow_session.cpp:1789-1815` guard `is_p6` false)。evaluator 给每个 node output 分配 sequential `RuntimeValueId`(`workflow_runtime.cpp:1427-1433`)。wasm lane 只给 workflow final output 分配 id 0。CLI run-report 不 strip,所以 `output_value_id` 在 wasm 为 null、evaluator 为 0/1。
2. **Fail-open asymmetry:** node-output decode failure silent `NoneValue`(1808-1810),workflow-output decode failure fail-CLOSED(`kOutputDecodeFailed` 1965-1974)。

**Design:**

- **Node output decode(WireJson lane):** `workflow_session.cpp:1789-1815` 的 guard 从 `if (node_desc->is_p6 && ...)` 扩展为 **all nodes**(P6 + WireJson)。WireJson node 的 output 从 `workflow_completed_count` global + identity runner 的 host-packed tuple 推导:identity node 的 output = 其 input tuple(host 已 packed);capability node 的 output = capability result(wire JSON)。host 用 `value_from_json` decode wire JSON output(与 workflow-output path 1960-1974 相同),`add_runtime_value` 注册,分配 sequential id。
- **Sequential id ordering:** node output id 按 node completion order 分配(scheduler 的 `schedule_pos` order),与 evaluator 的 node loop order 一致。workflow output id 在所有 node output 之后分配。这匹配 evaluator 的 0,1,...,N-1(node),N(workflow)。
- **Fail-closed alignment:** node-output decode failure 从 silent `NoneValue` 改为 `run_ok = false; run_failure_code = kOutputDecodeFailed`(与 workflow-output path 1965-1974 相同)。node-output 与 workflow-output 的 decode failure 用同一 fail-closed path。
- **删除 parity harness stripping:** `wasm_runner.cpp:1356-1366` 的 `output_value_id` stripping **删除**(big-bang)。harness 比较 full parity(含 `output_value_id`)。这是 test-paper 的 removal,不是 weakening——wasm lane 现在产生正确的 id。

**Reference Hierarchy:** Dafny 的 decreases/termination——fail-closed alignment 确保 wasm lane 不能 silently 产生与 evaluator 不同的 output set。AHFL-specific divergence:evaluator 的 `add_runtime_value` 是 in-process 的;wasm lane 的 host 必须跨 wasm3 boundary decode wire JSON,但 id 分配逻辑必须与 evaluator 一致(sequential,completion order)。

> **机制修订(2026-10-02,§12.15.19):** 本节的 per-node output host 可读性机制——"identity node 的 output = 其 input tuple(host 已 packed)"与"capability node 的 output = capability result"——经独立复核证伪(仅单节点 pure-identity workflow 成立;多节点 chain / canonical compute / cap-then-compute 均不成立,per-node tuple 活在 wasm scheduler local 里、host 不可读)。机制改由 guest stash table 承担,见 §12.15.19。本节的 id 分配规则、fail-closed alignment、stripping 删除决策**不变**。

### 12.15.8 Q6: Wire/manifest verdict — no version bump

**决策:不升任何 wire-format version。AHFLXM v2 不变;core-layout section 保持 format_version=3;recovery snapshot 保持 v2。**

理由:

- **AHFLXM v2 manifest bytes 不变:** GAP 1 的 per-node block 不增加 manifest 字段(`ManifestNode` at `schema_module 672-684` 无 ordinal/block field)。node_blocks cardinality 变化是 core-layout section 的语义变化,不是 manifest 的 wire-format 变化。
- **Core-layout section format_version=3 不变:** `node_blocks` field 已存在(`core_frame_layout.hpp:183`);GAP 1 只改其 cardinality(per-instance → per-node)和 host descriptor join(node ordinal → block mapping)。GAP 2/3/5 不增加新 field——construct scratch、rodata、spill window 都在现有 field 内。
- **Recovery snapshot `ahfl.workflow-recovery.v2` 不变:** per-node frame base 是 runtime scheduling concern,不是 snapshot concern。snapshot 的 `suspended.node` / `per_node_counters_` 已按 node ordinal index,不依赖 block 是 per-instance 还是 per-node。
- **Big-bang,无 compat flag:** 不引入 dual encoding、不保留 per-instance block path、不增加 env var / flag。`node_blocks` 的 cardinality 变化是 big-bang:host decoder 只接受 per-node cardinality(与 module emitter 同步)。

### 12.15.9 Q7: Page capacity accounting + user-facing diagnostic

**决策:64KiB page 的 capacity check 覆盖 per-node blocks + rodata;超限 fail-closed with SourceRange diagnostic。**

- **Per-node blocks(GAP 1):** page 必须 hold N 个 node block(每个 `I_k + C_k + scratch_k + O_k`)。`core_wasm_codegen.cpp` 的 page layout planner 计算 `sum(node_blocks) + bridge_control + spill + rodata + payload arena + state_trace + event_log <= 65536`。超限 `kResourceExhausted` + diagnostic 命名 page capacity(65536)与 required bytes + SourceRange(Principle 5)。
- **Rodata(GAP 5):** rodata region `[256, 1024)` 是 768 bytes。String literal 总 byte length 超限 fail-closed。
- **Construct scratch(GAP 2/3):** V2-B bump scratch arena(`scratch_base=7168, capacity=5120`)的 high-water 检查已存在(`scratch_high_water`);GAP 2 的 opaque-lane construct 用同一 arena,capacity check 自然覆盖。
- **Diagnostic 语言:** human-readable,actionable,命名 frame/page 与两个 size(与 `fits_frame_region` 1971-1988 同 style)。不 echo address bytes。

### 12.15.10 Q8: Census, tests, conformance

**新增 conformance case(mandatory):**

1. **GAP 1 — fan-out instance reuse:** 一个 agent type 被 2+ node 复用的 workflow(如 `llm_provider_smoke` 的 SmokeWorkflow)。census scenario + golden fixture。验证 per-node block 不 overlap、runner frame-base reentrant。
2. **GAP 2 — opaque capability-final constructed arg:** `state Done { return Cap(Struct{field: input.x}); }` 的 opaque workflow。验证 in-module construct + xcode + wire-JSON capability arg。
3. **GAP 3 — constructed aggregate bridge arg with String leaf(mandatory per coordinator):** `state Init { let r = Cap(Struct{value: input.value}); ... }` 其中 `input.value` 是 String。验证 String leaf 从 input frame inline word → payload arena 的 walk end-to-end 成功。**现有 conformance corpus 没有任何 case 把 String field thread 过 constructed aggregate bridge arg——此 case 必须新增。**
4. **GAP 5 — String literal in non-final handler:** `state Mid { ctx.reason = "literal"; goto Done; }`。验证 rodata Data emission 在 non-final handler。
5. **GAP 4 — node output_value_id parity:** 多 node workflow(identity + capability node),验证 wasm lane 的 `output_value_id` sequence 与 evaluator 一致(0,1,...,N)。

**Fail-closed mutation cases:**

- GAP 3:guest 写 null descriptor(ptr=0)→ `kNullDescriptor` sub-reason。
- GAP 3:guest 写 valid-but-unauthorized address → `kRootOutsideRegion`。
- GAP 3:String leaf payload 在 scratch/heap(非 rodata/arena)→ `kStringLeafOutsideRegion`。
- GAP 4:node output wire JSON corrupt → fail-closed `kOutputDecodeFailed`(不是 silent NoneValue)。
- GAP 1:page capacity 超限 → `kResourceExhausted`。

**Census pin movements(predicted):**

- `kExpectedAgreed`:67 → **72**(+5 个新 case 全部 agree)。
- `kExpectedSkipped`:0 不变。
- `kExpectedNodeOnlyStems`:增加新 stem(若适用)。

**Golden fixtures:** 每个新 conformance case 有 golden `.ahfl` + expected run-event sequence。GAP 4 的 golden 包含 `output_value_id` field(不 strip)。

### 12.15.11 Q9: Acceptance criteria (1:1 to 9 tests)

| Test | 机制 | Acceptance |
|------|------|-----------|
| #213 `ahflc.run.manifest.entry_workflow_default` | GAP 1 | exit 0,manifest 正确 |
| #214 `ahflc.run.manifest.entry_workflow_default` variant | GAP 1 | exit 0 |
| #421 `llm_provider_runtime.smoke` | GAP 1 | exit 0,SmokeWorkflow fan-out 正常 |
| #422 `real_llm.evidence` | GAP 1 | exit 0(AHFL_LLAMA_SERVER-gated) |
| #423 `durable_resume_flags.smoke` | GAP 2 | exit 0,suspend/resume contract 保持 |
| #424 `profile_and_output_contract` | GAP 5 | exit 0,Evaluate state String literal 正常 |
| #425 `llm_failure_matrix` | GAP 3 | exit 0,HTTP 401 diagnostic 出现 |
| #426 `llm_secret_manager` | GAP 3 | exit 0,vault secret success |
| #427 `capability_bindings` | GAP 3 | exit 0,bridge construct arg 正常 |
| #79 `runtime_evidence_smoke` | cascade of #421 | exit 0(自动解除) |

**Additional gates:**
- Conformance census:`kExpectedAgreed=72`,`kExpectedSkipped=0`。
- ASan build & test clean(`cmake --preset asan && ctest --preset test-asan`)。
- WASM=OFF build clean(KR6.8 WASM=OFF 策略不变)。
- `ahfl.run-report` v1 byte parity:wasm lane 与 evaluator 的 `output_value_id` sequence 一致。
- Fresh build `-Werror` clean(CLAUDE.md memory:develop 可积累 -Werror breakage,commit 前 fresh build)。

### 12.15.12 Q10: Rejected alternatives

1. **弱化/重写 CLI smoke fixture 以 dodge limitation — REJECTED。** 所有五个 shape 都是 evaluator shipped 的合法行为;grammar + spec 确认合法(evaluator 编译并执行它们)。弱化 fixture 是 falsify conformance,不是 fix。
2. **接受 "honest limitation" 并长期 refuse 这些 program — REJECTED。** KR6.8 的 north-star 是 evaluator retirement behind wasm3 host;这五个 shape 是 CLI contract 的一部分。"Honest limitation" 在无 human-gate 的 autonomous repo 中不是可接受的终态。
3. **Evaluator fallback — REJECTED。** KR6.8 retire evaluator;fallback 是 dual path,违反 Principle 1(no old-and-new coexistence)。evaluator 在 WH-6 cutover 后不可达。
4. **Engine-selection flag(wasm vs evaluator per-invocation)— REJECTED。** 同上,dual path + compat flag,big-bang ethos 禁止。
5. **Over-broad region authorization(授权整个 scratch/heap arena)— REJECTED。** Security boundary。host 已正确授权 `scratch_k`(root)与 payload arena(string leaf);widening 到 whole arena 会授权 unrelated data,破坏 fail-closed precision。GAP 3 的 defect 是 guest 写 null,不是 host 授权不足。
6. **Normalize opaque-final to bridge(§12.12)— REJECTED。** Opaque lane 是 wire-JSON boundary;转 bridge 会 blur lane discipline。GAP 2 的 fix 在 opaque lane 内 in-module construct,不经过 bridge ABI。
7. **在 CLI run-report 中 strip `output_value_id` — REJECTED。** 弱化 parity。wasm lane 必须产生与 evaluator 一致的 id sequence;stripping 是 hide 差异,不是 fix。
8. **升 wire-format version(v3/v4)— REJECTED。** 无新 wire field;变化是 cardinality/semantic,不是 format。big-bang ethos:extend existing field,不 bump version。

### 12.15.13 Costs / LOC

| Component | LOC delta | Notes |
|-----------|-----------|-------|
| `core_wasm_codegen.cpp` | ~400 | per-node blocks + frame-base runner(~200)、opaque construct(~80)、rodata scope(~40)、GAP 3 descriptor fix(~80) |
| `core_frame_layout.hpp` | ~20 | node_blocks 语义注释 + 无新 field |
| `workflow_session.cpp` | ~120 | node output decode + fail-closed(~80)、GAP 2 xcode routing(~40) |
| `capability_import.cpp` | ~60 | ArgDecodeSubReason enum + diagnostic(~40)、null check(~20) |
| `wasm_runner.cpp`(parity harness) | ~-20 | 删除 output_value_id stripping(negative LOC) |
| `core_wasm_schema_module.cpp` | ~30 | node_blocks cardinality join(per-node) |
| Tests | ~500 | 5 个新 conformance case + golden + fail-closed mutation |
| Docs | ~120 | 本节 |
| **Total** | **~1230** | |

### 12.15.14 Sequencing + gate revision

**顺序:**
1. **WH-5c.1**(GAP 3 descriptor fix + diagnostic)— 最高优先,unblock #425/#426/#427(三个 LLM smoke)。
2. **WH-5c.2**(GAP 1 per-node blocks)— unblock #213/#214/#421/#422。
3. **WH-5c.3**(GAP 5 rodata)— unblock #424。
4. **WH-5c.4**(GAP 2 opaque construct)— unblock #423。
5. **WH-5c.5**(GAP 4 parity + fail-closed)— 最后,依赖前四个的 node output 可用。
6. **WH-6**(ahflc run cutover commit)— gated on 全部 9 test green + census 72/0 + ASan + WASM=OFF。

**Gate revision:** §12.14.13 的 gate set 是 "WH-6 gated on 5b.1 + 5b.3 + 5b.2"。本节扩展为 "WH-6 gated on 5b.1 + 5b.3 + 5b.2 + **5c.1-5c.5**"。WH-5c 的五个 sub-slice 全部 landed 且 9 test green 后,WH-6 cutover 才能 commit。

### 12.15.15 Prior-decision preservation statement

- **§12.5(embedded host facade):** 保留,不重写。WH-5c 不改变 wasm3 engine facade / import callback ABI / region authorization 模型。
- **§12.6(WH-4b suspend/resume):** 保留,不重写。GAP 2 的 opaque construct 不改变 memo/replay;snapshot schema v2 不变。
- **§12.7(WH-6 cutover,无 fallback):** 保留,不重写。WASM=OFF 策略不变。gate set 扩展(§12.15.14)。
- **§12.11 / §12.12(WH-5b / Option A'):** 保留。§12.12 的 opaque-final-no-bridge prohibition 保留;GAP 2 在 opaque lane 内 fix,不转 bridge。
- **§12.13(WH-5b.3 transcode):** 保留,不重写。GAP 2 复用 xcode 的 P4D_TO_JSON direction,不改变 transcode routing order / shadow / payload arena / fail-closed guards。
- **§12.14(WH-5b.2 bridge-pending parity):** 保留,不重写。GAP 3 的 descriptor fix 不改变 PENDING graceful arm / memo-replay / snapshot schema。

### 12.15.16 GAP 1 approach revision (2026-10-02, dedicated decision-revision agent)

本节修订 §12.15.3。§12.15.3 原文保留作历史档案(append-only)。修订动因为协调者转交的技术挑战:codebase 已存在 per-packaged-instance 的 whole-body relocation 机制(`P6FrameRelocation`),挑战方主张把该机制的 cardinality 从 per-instance 推广到 per-node(APPROACH B),而不是 §12.15.3 选择的 frame-base-reentrant runner(APPROACH A)。所有代码断言已对工作树(HEAD c960bff0 + uncommitted WH-6 cutover)逐条复核。

#### 12.15.16.1 挑战摘要

§12.15.3 选择 APPROACH A(runner 变为 frame-base-reentrant:新增 frame_base param、把每个 baked `i32.const I_k/C_k/scratch_k/O_k` immediate 改写为 `frame_base + offset` relative addressing、per-node current_state slot array)的核心理由是:"fixed 64KiB page 无法为 fan-out workflow 的每个 node 存一个 baked block;AHFL 的 wasm3 单 page 约束使 code duplication 不可行(一个 runner body 已占 code space)"。

挑战方主张:(1) wasm3 的 CODE section 不受 64KiB linear-memory page 约束——code duplication 占的是 code section(wasm3 heap),不是 data page;data page 在两种方案下都需要 N 个 block。(2) `P6FrameRelocation`(`core_wasm_codegen.cpp:847-862`)本就是为 per-context baked address 设计的;把它的 cardinality 从 per-instance 推广到 per-node 是机械变更,复用现有 relocation machinery,不需要 relative-addressing 重写。

#### 12.15.16.2 已验证的代码事实(file:line)

**Relocation 机制(挑战方的事实成立):**

- `P6FrameRelocation` 持有全部四个 base+capacity+三个 global index(`core_wasm_codegen.cpp:847-862`)。
- 每个 packaged P6 instance 做一次 relocated build,在 `build_agent_plan` 前安装(`core_wasm_codegen.cpp:13410-13465`):relocation 在 13411-13426 填充,`build_agent_plan` 在 13427-13449 以 `.frame_relocation = &relocation` 调用。
- `p6_block_by_runner` 把 packaged-instance runner index 映射到 P6-dense `node_blocks` 下标(`core_wasm_codegen.cpp:950,983,12309,13112`)。
- `node_blocks` 是 P6-dense(per packaged instance),在 D6 planner 中按 per-runner gathered scratch high-water 分配(`core_wasm_codegen.cpp:12345-12373`);frame section 发射在 13833-13848。
- D6 block planner 按 P6 runner 迭代,block 四区域在 block 内连续(`plan_p6_node_block_cursor` `core_wasm_codegen.cpp:17410-17441`:`bases[part] = cursor + within; within += parts[part]`)。
- runner 忽略其 `(ptr,len)` args 做 addressing,返回 baked `O_k`(`make_workflow_p6_runner_body` `core_wasm_codegen.cpp:15197-15309`:params 是 local 0/1 但从不用于 addressing;返回 `block.output_base`/`block.output_size` 在 15306-15308)。
- scheduler 验证 returned `O_k` 命名 dispatched node 的 block(`core_wasm_codegen.cpp:16659-16672`);C_k zero-fill 在 16548-16651;per-dispatch state global reset 在 15201-15205。
- state globals:`(capability?6:5) + runner_count` 个,每个 P6 runner 一个 private `current_state`(`core_wasm_codegen.cpp:17183-17198`)。
- D5 rejection gate:`core_wasm_codegen.cpp:13025-13044`(`instance_users` count > 1 -> `kResourceExhausted`)。

**Handler 架构(APPROACH A 的真实重写面):**

- handler 是 `() -> i32` 函数(type index 5,`core_wasm_codegen.cpp:17091,17161`),无 param。
- runner 在 dispatch ladder 中调用 handler:`core_wasm_codegen.cpp:15279-15280`(computed-goto)、`15288-15289`(computed-return)。
- relocation accessor(`input_base()` 等)在 handler builder 中被 ~15 处消费:`core_wasm_codegen.cpp:3031-3053`(定义)、`3776-3792`(ProjectionRoot)、`4801,5678`(scratch capacity)、`6836`(bridge arg)、`6931,7023`(construct)、`7747-7837`(computed final)。
- runner 已接收 `(I_k, input_size)` 作为 local 0/1(`core_wasm_codegen.cpp:16617-16619` scheduler 传入),但 handler 是独立函数,看不到 runner 的 local 0。APPROACH A 必须要么改 handler functype 为 `(i32)->i32`(param shift 影响 handler builder 的 local 分配),要么引入 global frame_base(每个 addressing site 多 2 条指令)。

**APPROACH A 的 host-side 身份不变量破坏(决定性):**

- host 的 `runner_to_schedule` 假设 runner -> node 是 1:1(`workflow_session.cpp:412-415`:`runner_to_schedule[descriptor.nodes[i].runner] = i`;消费在 121-134)。APPROACH A 让两个 node 共享一个 runner,第二个 node 覆盖第一个的映射。
- state-trace ring 记录 `rec.runner`(`workflow_session.cpp:106-134`),host 用 `runner_to_schedule` 把 runner 归因到 node。共享 runner 使两个 node 的 trace 记录无法区分。
- manifest v2 的 per-node `bridge_sites` table(`core_wasm_schema_module.cpp:672-684,864-903`)要求 `call_site_id` 在 workflow 内 unique(§12.14.4 host trust check)。APPROACH A 让两个 node 共享同一 runner 的 bridge sites,manifest 会出现 duplicate `call_site_id`。

**wasm3 code-section 限制(§12.15.3 前提证伪):**

- `third_party/wasm3/source/m3_config.h` 只定义 `d_m3MaxLinearMemoryPages = 65536`(linear memory / data page 上限)和 `d_m3MaxFunctionStackHeight = 8000`(per-function 栈高,非 code size 上限)。**没有 code-section size 或 function-count 上限。** code section 存储在 wasm3 自己的 heap(`d_malloc`),与 64KiB linear memory 完全独立。
- §12.15.3 的 "一个 runner body 已占 code space" 和 "code duplication 不可行" 前提**事实错误**:code duplication 占 code section(wasm3 heap,实际无界),不占 data page(64KiB)。data page 在两种方案下都需要 N 个 block。

**实测 code size(measured,非估计):**

- `bridge_pending_suspends.wasm`(2 个 P6 node,bridge 调用):code section 1869 bytes;runner 306+306 bytes;handler 89+128+89+128=434 bytes。per-node(runner + 2 handler)= **523 bytes**。
- `priority_high.wasm`(3 个 P6 agent,e2e_multi_agent):code section 3635 bytes;runner 318+506+318=1142 bytes;handler 1335 bytes。
- fan-out 度数:SmokeWorkflow(`tests/scripts/llm_provider_runtime_smoke.py:159-160`)是 2 个 node(`first`、`second`)复用 `EchoAgent`。APPROACH B 额外 code = 1 个 node 的 runner + handler ≈ **523 bytes**。
- 结论:fan-out 2-5 node 的 code duplication 成本是 0.5-2.6 KB,对 wasm3 code section 可忽略。

**Manifest v2 无 block 字段(再次确认):**

- `ManifestNode`(`core_wasm_schema_module.cpp:672-684`)字段:`workflow_node_id`、`schedule_pos`、`cap_call_count`、`capabilities[]`、`bridge_sites[]`。**无 block/base/ordinal 字段。** 两种方案都不改变 AHFLXM bytes。§12.15.8 的 no-version-bump verdict 不变。

#### 12.15.16.3 逐轴对比

| 轴 | APPROACH A(§12.15.3,frame-base reentrant) | APPROACH B(per-node relocated copies) |
|----|---------------------------------------------|----------------------------------------|
| **Change surface** | 深:handler functype `()->i32` => `(i32)->i32`(type/function section + param shift 影响 handler builder local 分配)、~15 个 accessor site + 每个 emit consumer 改 relative addressing、runner 2 个 handler call site 传 frame_base、runner return 改算 O_k。**外加 host 身份修复**(下条)。 | 广但机械:relocation loop 改 per-node 迭代、`relocated_handlers`/`runner_count`/state-globals/`p6_block_by_runner` cardinality 改 per-node、D6 planner per-node、bridge registry `wf_runner` tag 改 node ordinal、scheduler per-node runner lookup。**`P6ComputationHandlerBuilder` 和 `make_workflow_p6_runner_body` 完全不变。** |
| **Correctness hazards** | (1) 破坏 host `runner_to_schedule` 1:1(`workflow_session.cpp:412-415,121-134`);(2) state-trace ring `rec.runner` 归因歧义(106-134);(3) manifest v2 per-node `bridge_sites` duplicate `call_site_id`(§12.14.4 uniqueness);(4) handler builder hot-path relative-addressing 重写的 silent memory-corruption 风险。 | (1) code 增长 ~523 B/node(实测,可忽略);(2) bridge registry 必须用 unique per-node tag(否则 `compact_workflow_runner` 的 contiguous-tail 隔离破坏);(3) rodata 必须保持 ONE shared Data segment(`shared_rodata_pool` 不变,天然满足);(4) dense control-base/stride identity 保持(`capability_import.cpp:504-541` 验证 `block_ptr == control_base + site.block_offset`,per-node site 仍 dense)。 |
| **Page capacity(DATA)** | N 个 block(与 B 相同)。 | N 个 block(与 A 相同);D6 planner 已按 per-instance gathered scratch high-water 分配,推广到 per-node 是 cardinality 变更。 |
| **Reference Hierarchy** | Swift generics(runtime parameterization)——codebase 中**不存在**的新模型。 | Clang monomorphization(per-context code emission)——**现有 V2-D 模型**(`P6FrameRelocation` 本就是为此设计)。 |
| **WH-5b.2/§12.14/§12.6 交互** | 共享 bridge site => manifest duplicate `call_site_id`、trace-ring runner 歧义;PENDING replay 本身 OK(scheduler 每次 dispatch 传 block base,fresh instance replay)。 | per-node bridge site => manifest v2 per-node table 干净、trace ring 归因干净;PENDING replay 不变(fresh instance + per-node memo,`(node, ordinal)` identity 天然 per-node)。 |
| **Hybrid(dedup)** | 无安全 dedup:同 agent type 的不同 node 有不同 frame(不同 I/O block base),baked address 不同,无法共享 block。 | 不适用。 |

#### 12.15.16.4 决策:REVERSE 到 APPROACH B(per-node relocated copies)

**§12.15.3 的 frame-base-reentrant runner 设计被否决。选择 APPROACH B:把现有 per-packaged-instance relocation 的 cardinality 推广到 per-workflow-node。**

**理由:**

1. **§12.15.3 的核心前提事实错误。** "wasm3 单 page 约束使 code duplication 不可行" 混淆了 code section 与 linear-memory data page。wasm3 code section 无 size 上限(`m3_config.h` 只 bound `d_m3MaxLinearMemoryPages`);data page 在两种方案下都需要 N 个 block。实测 per-node code 成本 ~523 bytes,fan-out 2-5 node 可忽略。

2. **APPROACH B 复用现有 V2-D relocation machinery,不引入新寻址模型。** `P6FrameRelocation` 本就是为 per-context baked address 设计的(Clang monomorphization 模型)。把 cardinality 从 per-instance 推广到 per-node 是机械变更:`P6ComputationHandlerBuilder` 和 `make_workflow_p6_runner_body` 完全不变。APPROACH A 则需要在 handler builder 的 hot path 引入 relative-addressing 新模型,并改 handler functype + local 分配。

3. **APPROACH A 破坏三个 host-side 身份不变量,APPROACH B 全部保持。** (a) `runner_to_schedule` 1:1(`workflow_session.cpp:412-415`);(b) state-trace ring `rec.runner` 归因(106-134);(c) manifest v2 per-node `bridge_sites` 的 `call_site_id` uniqueness(§12.14.4)。APPROACH A 让两个 node 共享一个 runner,三者全部破坏;APPROACH B 保持 one-runner-per-node,三者全部不变。

4. **APPROACH B 与 WH-5b.2/§12.14/§12.6 的交互更干净。** per-node bridge site 使 manifest v2 per-node table、resume frontier `(node, ordinal)`、trace-ring 归因全部天然 per-node,无需 relax 任何 uniqueness check。

**AHFL-specific divergence reason(替代 §12.15.3 的错误理由):** 选择 monomorphization(B)而非 runtime parameterization(A)的 AHFL-specific 理由不是 "code duplication 不可行"(该前提已证伪),而是:V2-D relocation machinery 已实现 per-context address baking,推广其 cardinality 是机械变更且保持全部现有身份不变量(runner->node 1:1、bridge-site dense identity、trace-ring 归因);runtime frame-base parameterization 会 (a) 在 handler builder hot path 引入新 relative-addressing 模型,(b) 破坏 host 的 runner->node 1:1 身份,(c) 在 manifest v2 per-node table 产生 duplicate bridge-site identity,(d) 模糊 state-trace ring 的 runner 归因。monomorphization 保持 one-runner-per-node,与 Reference Hierarchy 的 Clang 模型一致。

#### 12.15.16.5 修订后的 builder 指令(amended)

**保留(与 §12.15.3 相同):**

1. **`node_blocks` per-node。** `CoreFrameLayoutSection::node_blocks` 从 P6-dense(per packaged instance)改为 per-node(per workflow node)。cardinality = P6 node 数。
2. **删除 D5。** `core_wasm_codegen.cpp:13025-13044` 的 `instance_users` reuse rejection 删除(big-bang)。`instance_users` map 本身删除。
3. **Stale comment 修正。** `core_frame_layout.hpp:165-166`、`core_wasm_codegen.cpp:15246-15250`(注释 "one packaged instance runs exactly once per run2 today" 改为 per-node 语义)。
4. **Manifest verdict 不变。** AHFLXM v2 不 bump(`ManifestNode` 无 block 字段,`core_wasm_schema_module.cpp:672-684`)。

**替换(§12.15.3 的 frame-base 设计被以下 per-node relocation 设计替换):**

5. **Relocation loop per-node。** `core_wasm_codegen.cpp:13345-13465` 的循环从 per-packaged-instance 改为 per-P6-node。每个 node 用自己的 `node_blocks[node]`(per-node block)填充 `P6FrameRelocation` 的四个 base+capacity;`current_state_global = (plan.imports.empty() ? 5u : 6u) + node_ordinal`。
6. **`relocated_handlers` per-node。** `core_wasm_codegen.cpp:1020` 的 `vector<vector<CompiledHandler>>` 按 node ordinal 索引(不是 runner)。所有消费点(13345,13465,17051,17157,17237,17289)改索引。
7. **`runner_count` = P6 node 数。** `core_wasm_codegen.cpp:17045-17046` 的 `runner_count` 从 `packaged_instances.size()` 改为 P6 node 数。`WorkflowFunctionTable`(17057)的 `runner(node)` 按 node ordinal 映射。
8. **State globals per-node。** `core_wasm_codegen.cpp:17183-17198` 的 `global_count = (capability?6:5) + (p6 ? p6_node_count : 0)`;每个 P6 node 一个 private `current_state` global,初值 `agent_plans[node.runner].initial.value`。
9. **`p6_block_by_runner` -> per-node ordinal。** 删除 `p6_block_by_runner` 间接层;`node_blocks` 直接按 node ordinal 索引。descriptor 的 `p6_block_ordinal`(`core_wasm_codegen.cpp:17839-17843`)设为 node ordinal。host 消费点(`workflow_session.cpp:1422-1438,1795-1807`)不变——它们已按 `p6_block_ordinal` 索引。
10. **D6 planner per-node。** `core_wasm_codegen.cpp:12289-12376` 的 block planning 从 per-P6-runner 改为 per-P6-node;`gathered[node.runner].scratch_high` 按 node 取。bridge/spill reservation(12418-12421)从 per-runner 改为 per-node。
11. **Bridge registry per-node tag。** `wf_runner`(`core_wasm_codegen.cpp:832,13444`)设为 node ordinal(每个 build unique)。`compact_workflow_runner`/`site_count_for_runner`/`runner_spill_extent`(677-760)按 node ordinal 隔离。`global_site_base`/`runner_spill_base` 按 node 累加。
12. **Scheduler dispatch per-node。** `core_wasm_codegen.cpp:16521-16691` 的 `runner = workflow_runner_index(...)` 改为 per-node runner lookup;`functions.runner(*runner)` 改为 `functions.runner(node_runner[node_id])`;block lookup 改为 `node_blocks[node_id]`(直接 per-node)。O_k verification(16659-16672)、C_k zero-fill(16548-16551)、PENDING arm(16630-16648)不变——它们已按 dispatched node 的 block 工作。
13. **`make_workflow_p6_runner_body` 不变。** 该函数(`core_wasm_codegen.cpp:15183-15310`)已接收 `block` + `state_global` 参数并 bake 它们;per-node 调用即可,函数体不变。
14. **`P6ComputationHandlerBuilder` 不变。** relocation machinery(`install_frame_relocation` + accessor)按原样复用;每个 per-node build 安装自己的 `P6FrameRelocation`。
15. **不做:** 不改 handler functype(保持 `() -> i32`)、不做 relative-addressing 重写、不引入 frame_base param/global、不做 per-node current_state slot array(每个 per-node build 有自己的 private global,runner 在每次 invocation reset 到 initial,15201-15205,天然安全)。

#### 12.15.16.6 对 §12.15.6-12.15.14 的 knock-on 影响

- **§12.15.6(GAP 3 descriptor fix):** 不受影响。descriptor fix 在 bridge arg path,与 runner cardinality 独立。
- **§12.15.7(GAP 4 output parity):** 不受影响。node output decode 用 `p6_block_ordinal` per-node 索引 `node_blocks`,APPROACH B 提供 per-node ordinal。
- **§12.15.8(manifest verdict):** 不变。两种方案都不 bump AHFLXM version。
- **§12.15.9(page capacity):** 不变。N 个 block 在两种方案下相同;D6 planner per-node 是 cardinality 变更,不改变 capacity 模型。
- **§12.15.10(tests):** GAP 1 conformance case(fan-out instance reuse)的验收不变——验证 per-node block 不 overlap、fan-out 正常运行。APPROACH B 满足。
- **§12.15.11(AC):** 不变。#213/#214/#421/#422 的验收是 exit 0 + fan-out 正常,不依赖内部机制(frame-base vs per-node copy)。
- **§12.15.13(LOC):** GAP 1 的 LOC 估计从 "per-node blocks + frame-base runner(~200)" 修订为 "per-node blocks + per-node relocated copies(~250)"。`core_wasm_codegen.cpp` 行从 ~400 修订为 ~450;总计从 ~1230 修订为 ~1280。
- **§12.15.14(sequencing):** 不变。WH-5c.2(GAP 1)的优先级和 gate 不变。

### 12.15.17 WH-5c residual gaps 6/7 (2026-10-02, dedicated decision agent)

本节记录 WH-5c.1(GAP 3 descriptor fix,uncommitted in tree)落地后,同两个 product smoke(#425 `ahflc.run.llm_failure_matrix.smoke`、#427 `ahflc.run.capability_bindings.smoke`)中剩余的两个 cutover-blocking discrepancy 的决策。所有代码断言已对工作树(HEAD `b6239120` + uncommitted WH-5c.1 + WH-6 cutover)逐条复核。两个 fix 分别以 slice WH-5c.6(diagnostic ranges)和 WH-5c.7(rich type matrix)落地,排在 WH-6 cutover commit 之前。§12.15.11 AC 表与 §12.15.14 sequencing 由本节修订。

#### 12.15.17.1 GAP 6 — runtime failure diagnostics lack SourceRange parity

**证据(已独立复核):**

- #425 的断言(`tests/scripts/llm_failure_matrix_smoke.py:537-544`):每个 failed-run 的 `capability_failed`/`node_failed`/`workflow_failed` 事件中,至少一个 materialized diagnostic 必须同时有 `message` + `code` + `range`(dict)。wasm lane 发射两次 `wasm.trap`/`wasm.host-abort`,`range` 为 null。
- wasm lane 的失败诊断构造:`src/runtime/wasm_host/workflow_session.cpp:1598-1676` 把失败归约为 `run_failure_code`/`run_failure_message` 两个 **string**;`src/runtime/wasm_host/wasm_lifecycle.cpp:31-39` 的 `add_error` 只调 `.code().message().emit()`,**不调 `.range()`**。
- 重复发射:`wasm_lifecycle.cpp:287-289`(NodeFailed)与 `330-331`(WorkflowFailed)对同一 `code`/`message` 各调一次 `add_error`,diagnostic bag 里出现两条相同 entry。
- evaluator 的对照:`src/runtime/engine/workflow_runtime.cpp:1499-1503` 把 node 级 diagnostic id 存入 `workflow_diagnostic`,`1594-1597` 的 WorkflowFailed **复用同一 id**——bag 里只有一条 entry,两个事件引用它。
- evaluator 的 range 来源(实测,非推断):`src/runtime/evaluator/evaluator.cpp:1800-1801` 的 `apply_default_range(source_range)` 把 capability **CallExpr** 的 `source_range` 应用到失败诊断。在 `build/dev/tests/runtime/llm-failure-matrix-smoke/run-3718777/auth_failure_events.jsonl` 中实测 evaluator 输出 `range={begin:455,end:491}`,对应 fixture 源码的 `Echo(Request { value: input.value })` 调用表达式(call-site range,不是 node/agent 声明 range)。
- wasm lane 在失败站点已知的身份:host-abort 路径的 import callback 知道 call site 的 `source_symbol`(capability SymbolId u64,`capability_import.cpp:286,402` 设 `config.context.source_capability_symbol_id`);trap 路径的 session 知道 failed node(`workflow_session.cpp:2078-2086` 按 schedule order 找第一个 non-completed node)。
- facade 持有 `ir::Program` 但只在 ctor:`src/runtime/wasm_runner/wasm_workflow_runtime.cpp:60-114`。ctor 已用 `ProgramIndex` 预算 `capability_effects_`(避免存储 ProgramIndex,`wasm_workflow_runtime.cpp:63-65` 注释)。session config 已有 `name_resolver` seam(`workflow_session.hpp:64-65`,§12.7.2 记录 CLI 留空)。
- IR 侧的 range 可得性:`WorkflowNode::source_range`(`include/ahfl/compiler/ir/decl.hpp:349`)、`CapabilityDecl::provenance.source_range`(`decl.hpp:67,210-217`,CapabilityDecl 无自身 `source_range` 字段,range 在 `DeclarationProvenance` 上)、`AgentDecl::provenance` 同理。

**Root cause:**

wasm session 把失败诊断归约为 code+message string 时,host 已有的 node 身份(`node_id`/`schedule_pos`)与 capability 身份(`source_symbol` u64)没有 resolver 映射回 `SourceRange`。facade 是唯一持有 `ir::Program` 的 wasm-lane 组件,但它只在 ctor 用 Program 预算了 `capability_effects_`,没有预算 range 表,也没有把 range resolver 传进 session config。

**决策:Option A — facade 构建 host-side range resolver 表,传入 session config。**

facade ctor 从 Program 预算两张表(与 `capability_effects_` 同生命周期,不存储 ProgramIndex):

1. **`node_ranges_`**:per workflow,按 dense node ordinal 索引的 `WorkflowNode::source_range`(decl.hpp:349)。session 跑单个 workflow,`run()` 按 workflow name 取出该 workflow 的 vector 传入。
2. **`capability_ranges_`**:`source_symbol`(SymbolId u64)→ `CapabilityDecl::provenance.source_range`(decl.hpp:67)。key 是 `capability.symbol_ref.id`(与 codegen 的 `record.source_symbol = *capability.symbol_ref.id`,`core_wasm_codegen.cpp:9970,13703` 一致)。

`WorkflowSessionConfig` 增加两个 optional 字段(与 `name_resolver` 同 seam):`node_range_resolver`(按 dense node ordinal 查 `SourceRangeOpt`)与 `capability_range_resolver`(按 `source_symbol` u64 查)。facade `run()` 把它们填入 session config(`wasm_workflow_runtime.cpp:145-154` 附近,与 `name_resolver` 同处)。

**失败发射站点(必须获得 range):**

| 站点 | 文件:行 | range 来源 |
|------|---------|-----------|
| Run2HostAborted(capability import failure) | `workflow_session.cpp:1619-1641` | `CapabilityImportState` 新增 `last_source_symbol`(callback 在 `capability_import.cpp:286,402` 设 `source_capability_symbol_id` 时同步记录),经 `capability_range_resolver` 查 capability 声明 range;fallback 到 failed node 的 `node_range_resolver` |
| Run2Trapped(capability executed + failed) | `workflow_session.cpp:1642-1655` | 同上(`last_capability_error` 已有,`304,428`;补 `last_source_symbol`) |
| Run2Trapped(generic,无 capability 上下文) | `workflow_session.cpp:1642-1655` | failed node 的 `node_range_resolver`(failed node 在 `2078-2086` 已识别) |
| invoke_run2 engine error | `workflow_session.cpp:1613-1618` | failed node range |
| run2 non-zero status / replay diverged / origination failure | `workflow_session.cpp:1666-1690,1598-1612` | failed node range(或 pending coordinate 的 node range) |
| output decode failed(kOutputDecodeFailed) | `workflow_session.cpp:1950-1991` | 被 decode 的 node 的 range |
| suspend 路径 | `workflow_session.cpp:1856-1864` | **不发诊断**(suspend 不是 failure;`1856-1864` 的 "suspended but no pending coordinate" 是 invariant violation,发 failed node range) |

**重复诊断发射的结构化:**

`emit_workflow_events`(`wasm_lifecycle.cpp:267-337`)改为与 evaluator 同构:failed node 的 NodeFailed 发射**一条** diagnostic(带 range),把返回的 `DiagnosticId` 存入 `WasmWorkflowRunFacts`;workflow 级 WorkflowFailed **复用该 id**(`330-331` 不再调 `add_error`,改为引用 facts 里存的 id)。若失败无 failed node(全 node completed 但 workflow 级失败,理论上不应发生),workflow 级发一条带 range 的诊断。`add_error` 增加 `SourceRangeOpt` 参数,调 `.range(...)`。

**诚实的 parity 注记:**

evaluator 附的是 capability **call-site** range(CallExpr `source_range`,实测 `{455,491}`);wasm lane 附的是 capability **声明** range(`provenance.source_range`)。这是诚实的最小 parity:manifest v2 只携带 capability 的 `source_symbol`(声明 SymbolId),不携带 call-site range;call-site parity 需要 codegen 在 descriptor 的 `CoreWasmBridgeCallSite` 上携带 per-call-site `SourceRange`(descriptor 是 host-side sidecar,不是 wire format,但这是更大的 codegen 改动),推迟到后续 slice。#425 的断言只要求 `range` 是 dict(非 null),声明级 range 满足。

**WASM=OFF 影响:** 无。wasm host(`workflow_session.cpp`/`wasm_lifecycle.cpp`)与 facade 在 WASM=OFF 下编译 out;`ahflc run` 以可行动诊断拒绝(§12.7.1),不跑 workflow。

**Reference Hierarchy:** Clang diagnostic infrastructure——diagnostic 在 source location 可解析的地方构造。facade 是唯一持有 `ir::Program` 的 wasm-lane 组件,在 ctor 预算 range 表(与 `capability_effects_` 同模式),session 在失败站点用 resolver 解析。AHFL-specific divergence:wasm3 host 跨 wasm boundary 执行,失败站点在 host import callback / trap handler,不在 IR 解释器内,因此 range 必须经 host-side resolver 表传递,不能像 evaluator 那样直接从 IR 表达式取。

**被否决的替代方案:**

- **(B) 在 custom wasm section 编码 range — REJECTED。** range 是 source-level 诊断概念,不是 wire identity(Principle 2)。module bytes 保持 identity-only;range 表是 host-side sidecar,经 session config 传递,不进 module。
- **(C) host 用 symbol table handle 经 source_symbol 映射 — SUBSUMED by (A)。** facade 直接从 Program 预算 `source_symbol → SourceRange` 表;单独的 symbol table handle 会重复 facade 刻意避免存储的 `ProgramIndex`(`wasm_workflow_runtime.cpp:63-65`)。

#### 12.15.17.2 GAP 7 — P6 body/computed-final codegen rejects the full rich wire-type matrix

**证据(已独立复核):**

- #427 scenario `run_rich_input_matrix` tag `baseline_all_shapes`(`tests/scripts/runtime_capability_bindings_smoke.py:635-650`):fixture `rich_input_pkg/main.ahfl` 是 capability-free agent,computed final 从 `RichInput` 逐 field 构造 `RichOutput`,覆盖 9 类类型:`Decimal(2)`、`Duration`、`collections::Set<Int>(8)`、`collections::Map<String,Int>(8)`、`option::Option<Int>`、`Unit`、`Float`(f64)、`Int`、`String`;workflow 级 computed return 从 node `first` 的 field 构造 `RichOutput`。evaluator 编译并运行;wasm emission 失败:`wasm.UNSUPPORTED_WORKFLOW_FRAME: ... cannot lower body 'Done': let value has a non-scalar or f64 type`(`core_wasm_codegen.cpp:6046`)。
- 实测 fixture 路径:`build/dev/tests/runtime/capability-bindings-smoke/run-*/rich_input_pkg/main.ahfl`(多份历史 run 留存)。

**P4-D 布局系统已支持的类型矩阵(全部 9 类已有 P4-D 布局,证明 "they already have P4-D layouts"):**

| 类型 | P4-D layout | 文件:行 |
|------|------------|---------|
| `Decimal(2)` | `CoreLayoutScalar(I64)`(ride IntI64) | `core_wasm_codegen.cpp:2071-2075` |
| `Duration` | `CoreLayoutScalar(I64)`(同上) | `core_wasm_codegen.cpp:2071-2075` |
| `Set<Int>(8)` | `CoreLayoutContainer(capacity=8, stride, element=I32)` | `core_layout.hpp:101-109` |
| `Map<String,Int>(8)` | `CoreLayoutContainer(element=key, value=map-value)` | `core_layout.hpp:102-103` |
| `Option<Int>` | `CoreLayoutEnum`(payload-bearing) | `core_layout.hpp:93-99` |
| `Unit` | zero-sized(无 bytes) | — |
| `Float` | `CoreLayoutScalar(F64)` | `core_layout.hpp:35-43` |
| `Int` | `CoreLayoutScalar(I32/I64)` | `core_layout.hpp:35-43` |
| `String` | `CoreLayoutPtrLen`(8-byte pair) | `core_layout.hpp:51-55` |

**已工作的端到端路径(不需重写):**

- **WH-2 host packer/unpacker**:`frame_packer.cpp:395-436` 处理 Set/List(bounded collection + placement backing);`260-293` 处理 Option(tag + payload inline)。`frame_reader.cpp:238-258` 对称。**reject** `Float/Decimal/Duration/Map`(`frame_packer.cpp:470`、`frame_reader.cpp:447` 的 fallback)。
- **WH-5b.3 transcode / wire codec**:`core_wire_codec.cpp` 对 wire JSON ↔ native Value 处理**全部 9 类**(Float:173、Decimal:217、Duration:233、Option:286、Set:320、Map)。exact-decode 路径是 full-matrix。
- **body builder `p6_scalar_kind`**(`core_wasm_codegen.cpp:1988-2077`):支持 Bool/IntI32/IntI64/Decimal/Duration(ride IntI64)/String(PtrLen)/Closure/Index(tag-only enum)/Ptr(struct/payload-enum)/Collection(bounded collection)。**reject f64**(`2018`:`scalar->repr == F64 → nullopt`)与 **Unit**(zero-sized,不匹配任何分支 → `2040` nullopt)。
- **golden 证据**:`tests/golden/wasm/wh5b_hybrid_rich_fidelity.ahfl`(tag-only enum/String/Int/Bool 双向 transcode)与 `wh5b_hybrid_decimal_fail_closed.ahfl`(pin Decimal 在 entry pack fail-closed)。前者 NOTE 明确记录 "payload-bearing enums and nested structs in the workflow return are rejected by the scheduler materializer"。

**Root cause(四层独立 gate,按触发顺序):**

1. **let gate**(`core_wasm_codegen.cpp:6033-6047`):`Done` body 的 construct field 值被 lower 成 let 绑定;`input.nothing`(Unit)与 `input.flag`(Float)的 `scalar_kind` 返回 nullopt → `6046` reject。sibling gate:`3884`(plan_expr entry)、`4412`(plan_literal)、`5743`(match arm binding)、`6284`(plan_expr second site)、`7859`(plan_unary)。
2. **construct operand gate**(`plan_construct_operand`,`4816-4867`):
   - `4821`:nullopt-kind operand(f64/Unit)→ reject "constructor operand has a non-aggregate, non-scalar type"。
   - `4834`:Collection-kind operand 进 aggregate_leaf slot(container slot 是 aggregate_leaf,`3686-3699`)但 kind != Ptr → reject "constructor operand is not an aggregate for its aggregate slot"。影响 `nums: input.nums`(Set)与 `tags: input.tags`(Map)。
   - `4844`:input-sourced aggregate operand(`opt: input.opt`,Option payload-enum)→ reject "a computed final stores an aggregate/enum field sourced from the host-packed INPUT frame"(inline-input-frame expansion 未落地)。
3. **scheduler materializer**(`13062-13107`):`workflow_inline_path_needs_child_dereference`(`12140-12162`)拒绝 scheduler-materialized region 从 inline source frame(host-packed entry 或 upstream node O_k)project nested struct/enum。workflow return 的 `opt: first.opt`(payload-enum)与 `nums: first.nums`(collection)命中此 gate。
4. **host packer/reader**:`frame_packer.cpp:470` / `frame_reader.cpp:447` reject Float/Decimal/Duration/Map。即使 body builder 放宽,entry pack(RichInput)与 output read(RichOutput)仍会 fail。

**决策:widen body builder + host packer/reader + materializer 到 full matrix,复用现有 frame/arena machinery(不引入并行 type system)。**

**(a) f64(Float)— copy-only 路径 ride IntI64:**

rich fixture 的 f64 只做 projection → construct copy(无算术)。`p6_scalar_kind` 对 `CoreScalarRepr::F64` 返回 `IntI64`(8-byte word,bit-identical copy);`place_is_scalar_leaf`(`3674-3678`)接受 f64 slot(8-byte scalar leaf)。emit 路径用 `i64.load`/`i64.store` 拷贝 8 字节(不引入 `f64.const`/f64 arithmetic ladder——rich fixture 不需要)。host packer/reader 增加 f64 分支(field offset 处 8-byte load/store,native `FloatValue` 重建)。**f64.const 与 f64 arithmetic 推迟到后续 slice**(P6 scalar ladder 的独立扩展)。

**(b) Unit(zero-sized)— no-op kind:**

`p6_scalar_kind` 对 zero-sized 类型返回一个 no-op kind(或 special-case):let 绑定不分配 local;construct operand 是 no-op(不 store);emit 路径跳过。host packer/reader 对 Unit 分支是 no-op(零字节,`CoreWireSchemaUnit` 已在 wire schema)。`plan_construct_operand` 的 `4821` gate 放宽以接受 zero-sized operand。

**(c) Collection(Set/Map)construct operand — header copy,不是 address store:**

`plan_construct_operand` 的 `4834` gate 放宽:Collection-kind operand 进 container slot 时接受。emit 路径拷贝 collection **header**(`base: u32, len: u32`,8 bytes inline)从 source 到 destination——**不是** Ptr address store。collection elements 留在 shared backing region(`kP6CollectionBackingBase=16384`,`core_wasm_abi_constants.hpp:71`),immutable borrow(output 的 header 指向 input 的 backing region,computed final 的 output 在 agent 完成后只读一次,安全)。host packer 增加 Map 分支(key-value pairs 进 container layout 的 `element`+`value` edge,`core_layout.hpp:102-103`);Set 已工作(`frame_packer.cpp:395-436`)。

**(d) Payload-enum from input(inline-input-frame expansion):**

`plan_construct_operand` 的 `4844` gate 在 computed-final lane 放宽:input-sourced aggregate operand 不再 reject。emit 路径拷贝 inline enum bytes(tag + payload)从 input frame 到 output frame——construct 命名 inline input 地址(`input_base + field_offset`),不是 module child address。这是 `13058-13061` 注释的 "inline->pointer-tree normalization" 在 construct 路径的落地。Option<Int> 的 inline 表示是 tag(1 byte + padding)+ payload(Int at `payload_offset`),`frame_packer.cpp:277-293` 已证实此 layout。

**(e) Scheduler materializer 扩展:**

`workflow_inline_path_needs_child_dereference` / materializer 的 copy 路径扩展以处理 rich projection:payload-enum(`first.opt`)、collection(`first.nums`/`first.tags`)、f64(`first.flag`)、Unit(`first.nothing`)从 inline node O_k block 拷贝。materializer 的 `latch_path_slot` + `copy_aggregate` 增加 inline-byte-copy 路径(与 (d) 同 machinery)。`13088-13096` 与 `13098-13106` 的 child-dereference reject 对 rich leaf 放宽(leaf 本身是 scalar/collection/enum,不是 nested struct 的 child dereference)。

**(f) Host packer/reader widening:**

`frame_packer.cpp:470` 与 `frame_reader.cpp:447` 的 fallback 前增加 Float/Decimal/Duration/Map 分支。Decimal/Duration ride i64(packer 写 i64 word;reader 读 i64 并按 wire schema 的 scale/unit 重建 `DecimalValue`/`DurationValue`)。Map 按 container layout 的 key+value edge pack/read。

**Bridge-crossability per type(honest matrix):**

| 类型 | bridge(capability arg) | in-agent construct | entry pack | output read |
|------|------------------------|-------------------|------------|-------------|
| Int / Bool / String / Unit | Spill ✓(`capability_import.cpp:77-81`) | ✓ | ✓ | ✓ |
| Option / Struct / Tuple | Root ✓(`82-85`) | ✓(d) | ✓(`260-293`) | ✓(`238-258`) |
| Float(f64) | **REJECT**(`94-95` else → Reject) | ✓(a) | ✓(f) | ✓(f) |
| Decimal / Duration | **REJECT**(同上) | ✓(已 ride IntI64) | ✓(f) | ✓(f) |
| Set(sequence)/ Map | **REJECT**(同上) | ✓(c) | Set ✓ / Map ✓(f) | Set ✓ / Map ✓(f) |

**f64 跨 bridge 保持 fail-closed 的诚实理由:** bridge ABI 的 spill/root subset 是 Bool/Int/Unit/String/tag-only-enum(Spill)+ Struct/Option/Tuple/payload-enum(Root)。f64 跨 bridge 需要:(1) `bridge_param_kind` 把 `CoreWireSchemaFloat` 加入 Spill set(`capability_import.cpp:72-96`),(2) JS oracle `bridgeParamKind` 同步,(3) packer/reader 的 f64 支持。wire schema **已有** `CoreWireSchemaFloat`(`core_wire_schema.hpp:39-41`),所以 **不需要 wire-schema 变更**;但 bridge-param-classification + JS oracle 是独立 slice,大于 WH-5c.7。rich echo agent **无 capability**,bridge 限制不适用。§12.15.10 的 string-leaf bridge case 是把 String(PtrLen,已 bridge-crossable)thread 过 constructed aggregate bridge arg;与 f64/Decimal/Duration/Set/Map 的 bridge reject 正交——前者跨 bridge,后者不跨。

**Page/backing-region capacity + SourceRange:**

- collection backing region(`kP6CollectionBackingBase=16384`,capacity 49152)hold Set/Map elements。rich fixture 的 Set(8)+Map(8)需 `8*stride + 8*entry_stride`。capacity planner(`plan_workflow_p6_capacity_family`,`12181+`)把 rich types 的 backing + construct scratch 计入。超限 → `kResourceExhausted` + SourceRange diagnostic(Principle 5,与 §12.15.9 同 style)。
- construct scratch arena(`kP6AggregateScratchBase`)hold intermediate construct。rich construct 的 scratch 用量按 `aggregate_size` 计入(已有 high-water check)。
- 所有新增 reject gate(f64 arithmetic、f64.const、超限 collection backing)携带 `statement.source_range` / `expr.source_range`(Principle 5)。

**与 WH-5c.2(per-node blocks)的交互:**

rich fixture 单 node,per-node blocks 不改变 capacity 模型。construct scratch 在 per-node block 的 scratch region 内(WH-5c.2 的 per-node relocation 提供 per-node scratch base);collection backing 是 shared page region。WH-5c.2 的 per-node relocation 与 rich type matrix 独立——前者改 cardinality,后者改 type coverage。

**被否决的替代方案:**

- **弱化 G4b matrix — REJECTED。** 9 类 shape 都是 evaluator shipped 的合法行为;弱化 fixture 是 falsify conformance。
- **evaluator fallback — REJECTED。** Principle 1(no old-and-new coexistence);evaluator 在 WH-6 cutover 后不可达。
- **把类型编码为 opaque JSON blob 进 P6 frame — REJECTED。** 全部 9 类已有 P4-D 布局(上表证明);opaque JSON 是并行 type system,违反 Principle 1/3。
- **f64 引入完整 f64 opcode ladder(const/arithmetic)— DEFERRED。** rich fixture 只需 copy(load/store);f64.const/arithmetic 是独立 slice,不在 WH-5c.7。
- **f64 跨 bridge 在本 slice 开放 — REJECTED(本 slice)。** 需要 bridge-param-classification + JS oracle + packer/reader 三处协同,大于 WH-5c.7;保持 fail-closed with explicit diagnostic。

#### 12.15.17.3 AC(mapped to #425 / #427)

| Test | 机制 | Acceptance |
|------|------|-----------|
| #425 `ahflc.run.llm_failure_matrix.smoke` | GAP 6 | exit 0;每个 failed-run 的 materialized diagnostic 有 code+message+range(wasm lane 的 trap/host-abort 诊断携带 node/capability 声明 SourceRange);diagnostic bag 无重复 entry(node_failed 与 workflow_failed 引用同一 id) |
| #427 `ahflc.run.capability_bindings.smoke` | GAP 7 | exit 0;`run_rich_input_matrix` tag `baseline_all_shapes` 在 wasm lane 运行,9 类类型 round-trip(RichInput → agent → RichOutput → workflow return);evaluator 与 wasm 的 output_json 一致 |

**Additional gates:**
- Conformance census:`kExpectedAgreed` 见 §12.15.17.4;`kExpectedSkipped` 见同节。
- ASan build & test clean。
- WASM=OFF build clean。
- Fresh build `-Werror` clean。
- f64 跨 bridge 保持 fail-closed:有 explicit diagnostic(不是 silent),pin 在 golden/conformance case。

#### 12.15.17.4 Census

**Mandatory additions:**

1. **rich-shapes computed-final workflow case(全部 9 类 round-trip):** capability-free agent,computed final 从 RichInput 构造 RichOutput(9 类 field),workflow computed return 从 node output 构造。`engines.wasm.eligible = "orchestration"`。这是 agreed case(evaluator 与 wasm 都跑)。
2. **fail-closed pin:f64 跨 bridge 保持 reject:** 一个 capability 声明带 f64 arg 的 workflow,wasm lane 以 explicit diagnostic(`wasm.UNSUPPORTED_*` 或 bridge reject)拒绝,evaluator 可跑。这是 wasm-ineligible case(`engines.wasm.eligible = "none"` + reason),pin 拒绝是 explicit diagnostic 而非 silent。

**Census pin movements(predicted,builder sequence):**

- 当前(after WH-5c.1):`kExpectedAgreed = 68`(`conformance_wasm_node_runner.cpp:147`)。
- WH-5c.2/5c.3/5c.4/5c.5:+4(§12.15.10 的 5 个 case 中 5c.1 已 +1,剩余 4 个)→ **72**。
- WH-5c.6(diagnostic ranges):+0 agreed(diagnostic parity 由 #425 smoke 测试,不是 conformance census case)。
- WH-5c.7(rich type matrix):+1 agreed(rich-shapes case)→ **73**。fail-closed pin(f64 跨 bridge)是 wasm-ineligible case,若作为 conformance case 加入则 `kExpectedSkipped` 从 0 → 1;若作为 golden test(类似 `wh5b_hybrid_decimal_fail_closed.ahfl`)则 census 不变。

**最终预测:`kExpectedAgreed = 73`;`kExpectedSkipped = 0 或 1`(取决于 f64-bridge pin 是 conformance case 还是 golden test)。**

#### 12.15.17.5 Sequencing(amends §12.15.14)

§12.15.14 的顺序修订为:

1. WH-5c.1(GAP 3 descriptor fix)— landed(uncommitted)。
2. WH-5c.2(GAP 1 per-node blocks,APPROACH B per §12.15.16)。
3. WH-5c.3(GAP 5 rodata)。
4. WH-5c.4(GAP 2 opaque construct)。
5. WH-5c.5(GAP 4 parity + fail-closed)。
6. **WH-5c.6(GAP 6 diagnostic SourceRange parity)— 新增,排在 5c.5 后。**
7. **WH-5c.7(GAP 7 rich type matrix)— 新增,排在 5c.6 后。**
8. WH-6(ahflc run cutover commit)— gated on 全部 9 test green + census 73 + ASan + WASM=OFF。

**Gate revision:** §12.15.14 的 gate set 从 "5b.1 + 5b.3 + 5b.2 + 5c.1-5c.5" 扩展为 "5b.1 + 5b.3 + 5b.2 + **5c.1-5c.7**"。

#### 12.15.17.6 LOC estimate

| Component | LOC delta | Notes |
|-----------|-----------|-------|
| `wasm_workflow_runtime.cpp/hpp` | ~80 | node_ranges_ + capability_ranges_ 表构建 + session config 传递 |
| `workflow_session.cpp/hpp` | ~60 | range resolver 字段 + 失败站点 range 解析 + `last_source_symbol` |
| `wasm_lifecycle.cpp` | ~40 | `add_error` 带 range + 重复发射消除(单 diagnostic + id 复用) |
| `capability_import.hpp` | ~10 | `CapabilityImportState::last_source_symbol` |
| `core_wasm_codegen.cpp` | ~350 | f64/Unit/Collection/inline-enum construct operand + materializer 扩展 + 各 gate 放宽 |
| `frame_packer.cpp` | ~80 | Float/Decimal/Duration/Map pack 分支 |
| `frame_reader.cpp` | ~80 | Float/Decimal/Duration/Map read 分支 |
| Tests | ~400 | rich-shapes conformance case + golden + f64-bridge fail-closed pin |
| Docs | ~80 | 本节 |
| **Total** | **~1180** | |

#### 12.15.17.7 Prior-decision preservation

- **§12.15.1-12.15.15:** 保留,不重写。GAP 1-5 的决策不变;本节只新增 GAP 6/7。
- **§12.15.16(APPROACH B per-node blocks):** 保留。WH-5c.2 按 APPROACH B 落地;WH-5c.7 的 rich type matrix 与 per-node cardinality 独立(§12.15.17.2 交互节)。
- **§12.7(WH-6 cutover,无 fallback):** 保留。WASM=OFF 策略不变。gate set 扩展(§12.15.17.5)。
- **§12.15.10(string-leaf bridge case):** 保留。String 跨 bridge(PtrLen spill)与 f64/Decimal/Duration/Set/Map 的 bridge reject 正交(§12.15.17.2 bridge-crossability 节)。
- **§12.15.8(no version bump):** 不变。GAP 6/7 不增加 wire field(range 是 host-side sidecar;rich types 已有 P4-D 布局)。AHFLXM v2 不 bump。

#### 12.15.17.8 落地记录(2026-10-02):WH-5c.6 GAP 6 实现 + 对抗 review fix-forward

Option A 按本节决策落地(builder → 独立对抗 review → coordinator fix-forward),机制与决策一致,另有两处 review 发现的加固:

1. **facade 预算两张表(ctor,与 `capability_effects_` 同生命周期,不存储 ProgramIndex):** `node_ranges_by_workflow_` 按 workflow name 保存 source-order 的 `WorkflowNode::source_range` vector;`capability_ranges_` 按 `capability.symbol_ref.id`(u64,absent id 跳过)保存 `CapabilityDecl::provenance.source_range`。`run()` 用 `descriptor.nodes[].node_id`(dense source-order id)把 source-order vector 重排成 schedule-order vector(node_id OOB 与 schedule_pos OOB 双重 guard),在 session config 上装 `node_range_resolver(schedule_pos)` 与 `capability_range_resolver(source_symbol)`——与 `name_resolver` 同 seam。
2. **`CapabilityImportState.last_source_symbol`:** 仅在两个 call-site 解析成功后的站点(`capability_import.cpp` opaque/bridge,即原 `context.source_capability_symbol_id = call_site.source_symbol()` 两处)记录。
3. **session 失败 range:** per-node output-decode 站点立即解析"被 decode 的 node"的 range(唯一有 node 在 scope 的 kOutputDecodeFailed 站点);其余失败在 section 13 兜底——capability 声明 range 仅当 `(last_error || last_capability_error) && last_source_symbol` 成立,否则/fallback 用 failed node 的 `WorkflowNode::source_range`。workflow-output / stash-region decode 站点无 node 在 scope(此时所有 node 已 completed、failed_node_index 为空),range 为 nullopt——这是诚实的失败形态,不是漏挂。
4. **单一 DiagnosticId:** `add_error` 增加 `SourceRangeOpt` 参数(engaged 才调 `.range()`);NodeFailed 发射一条带 range 的诊断并把 id 存入 facts,WorkflowFailed **复用该 id**(删除重复 bag entry,与 evaluator `workflow_runtime.cpp:1594-1597` 同构);只有"全 node completed 但 workflow 级失败"的 fallback 才由 workflow 级发一条带 range 诊断。agent lane 继续传 nullopt(agent lane 不持有 ir::Program,超出本 slice)。
5. **诚实 parity 注记(代码注释中声明):** evaluator 附 capability **call-site** range;wasm lane 附 capability **声明** range(manifest v2 只携带声明 symbol)。call-site parity 仍需 descriptor 携带 per-call-site range,保持推迟。

**Review fix-forward(P1 ×2 + P2 ×1,coordinator 应用):**

- **P1 stale-symbol 错挂:** review 发现 `last_source_symbol` 跨 call 不清空——node 0 capability 成功后,node 1 的 call 在**解析前**站点(name resolve / arg decode)失败时,`last_error` 与上一次成功的 symbol 同时成立,section 13 会把成功 capability 的声明 range 错挂到 node 1 的失败上。修复:import callback 入口(`resolve_import_call_site` 之前)无条件 `last_source_symbol = nullopt`,每次 attempt 从干净状态开始。
- **P1 新 golden 未入 pinned corpus:** `wh5c6_cap_then_trap.ahfl`(成功 cap + 除零 trap 的 precedence fixture)被 `ahfl.ir.core_json_round_trip` 的 corpus-pin 发现但未登记,该测试不在 wasm label 内(builder 的 88/88 无法暴露)。已登记;同时新增第二个 fixture `wh5c6_reorder_trap.ahfl` 一并登记。
- **P2 reordered-DAG range pin(补强):** 新增 fixture 中 trap node `b` **source id = 0 但 schedule pos = 1**(`b after [a]`,Kahn 调度 `[a,b]`),断言失败 range 等于 b 的 source range 且不等于 echo node 的 range。该 pin 对"schedule pos 当 source index 用"的 remap 变异非空转(identity remap 会挂到 echo node range 而失败),并先非空地 pin 住 `nodes[0].node_id==1 / nodes[1].node_id==0` 的前置条件。

**测试证据:** workflow_session 单测 615 checks(新增 7 个 WH-5c.6 case:capability error / host-abort / generic trap / 无 resolver 安全 / P6 O_k decode fail-closed / cap-then-trap precedence / reordered-DAG remap);wasm_runner 单测扩展 single-id + range 断言;`-L wasm` 88/88;`ahfl.ir.core_json_round_trip` 绿。

**Gate #425 状态(诚实):** range/dup 断言(line 523-545)已全部通过——实测 failed-run 的 node_failed 与 workflow_failed 共享 `diagnostic_id: 0`,诊断携带 `range: {begin:145,end:191}`,经独立复核正是 fixture 中 `capability Echo ... -> Response` 的**声明** range(非 call-site)。#425 仍红,但**唯一**剩余失败是 line 579-580 要求 `capability_failed` 事件:wasm lane 当前对失败 call 发射 `capability_completed`(output_value_id=null),这是已记录的 **GAP 8(§12.15.18.1)**,随 WH-5c.8 的共享 projection helper 落地。GAP 6 本身关闭;#425 的完整转绿依赖 5c.8,与 §12.15.18.1 builder 指令 #6(失败路径用 cap_call 的 diagnostic_code + 5c.6 的 capability resolver 提供 range)的排序一致。

**不变量:** module bytes / descriptor / wire schema 零变更(无 `src/compiler/`、`include/ahfl/compiler/` diff),golden SHA freeze 不动,conformance 文件不碰,census 维持 native 72/0、Node 69/3。

#### 12.15.17.9 落地记录(2026-10-03):WH-5c.7 GAP 7 九类型矩阵 + 对抗 review fix-forward

按 §12.15.17.2 四层机制落地(builder → 独立对抗 review → fix-forward builder → coordinator 独立复核)。九类型(Int/String 既有;Float(f64)、Decimal、Duration、Set、Map、Option、Unit 新增)在 capability-free P6/wasm lane 全量 round-trip:

1. **let/scalar gate:** f64 复用 IntI64 kind 做 bit-copy word——独立复核确认全 codegen 不发射 f64.const/f64 算术(f64 literal/unary/binary 三处 reject 均带 SourceRange 并有 pin);Unit 为 zero-sized no-op,不发射零长 load/store,容量族不产生碰撞区间。
2. **plan_construct_operand:** Collection 只内联拷贝 8-byte header(base,len),elements 留在 kP6CollectionBackingBase=16384 immutable borrow(无任何 store 可 alias borrowed backing);Option 经 is_inline_copyable_enum_layout 在 computed-final lane 内联拷贝 1B tag + payload(Scalar/PtrLen)。
3. **scheduler materializer:** rich-leaf 从内联 node O_k block 投影;rich matrix fixture 的 workflow return 全部经 `first.<field>` 字段投影,非 whole-node passthrough。
4. **host packer/reader:** Float 8-byte word;Decimal/Duration ride i64(mantissa/millis);Map 新增 key+value 边,checked_mul_u32 + checked_add_u32 容量数学(溢出 kResourceExhausted)。

**Review fix-forward(P0 ×2 + P1 ×2 + P2 ×3):**

- **P0 gate #265(本 slice 新红):** 工具侧 `tests/conformance/wasm_eligibility.cpp` else-if 链漏 `NodeHostAwaitsRichWireTypes` 分支(单元目录已更新、工具二进制未覆盖——unlabeled full ctest 再次成为唯一暴露途径)。
- **P0 gate #427:** facade not-found 文案收敛到 evaluator canonical `workflow '<n>' not found in program`(`wasm_workflow_runtime.cpp:161` == `workflow_runtime.cpp:616`);另四处 facade 错误删除 "wasm workflow runtime: " 前缀(:102 core lowering failed、:111 layout computation failed、:126 wasm emission failed for workflow、:283 session 错误直透 host 文案)。packer 错误新增 `frame_pack_error_name()` 把 FramePackError 码名透进 session 错误。
- **P1 spelling fail-closed(真实语义分歧):** evaluator `value_json.cpp:79-82` 原样输出 source spelling,裸 i64 frame 丢失 spelling family,reader 的 format_decimal/format_duration_spelling 会重规范化("1.2" 入 Decimal(2)→"1.20";"60s"→"1m")。两个 formatter 上移为共享 `frame_spelling.hpp`(packer/reader 单一实现),packer 入口用同一 formatter 重建并与 source spelling 精确比对,不一致即 ValueNotWireEncodable 在模块运行前拒绝。接受 pin "1.20"/"01.20 拒"/"1m"/"5s"/"250ms",拒绝 pin "1.2"(Decimal(2))/"1.200"/"60s"/"5000ms"。codegen literal 常量不经 packer,不受影响;不携带原始 spelling、不改 evaluator。
- **P1 bridge pins 补全:** 旧 wh5b decimal golden 的 capability 路径在本 slice 删除后,逐形状实测真实拒绝层并补 exact-code pin——Decimal/Duration:运行时 `bridge_param_kind` Reject(模块编译成功、import arg decode abort、NodeFailed);Map/Set:P6 scalar codegen 编译期 kUnsupportedCapabilityFrame;Float:return-position cap call 不被 region_contains_capability 识别(扫的是 CapabilityCallStmt),落 WireJson lane 无法服务,NodeFailed。`bridge_param_kind`(capability_import.cpp:72-99)与 verify_frame_bridge_sites 均未削弱。
- **P2:** (a) 名实不符 golden big-bang 更名 wh5b_hybrid_decimal_fail_closed → wh5b_hybrid_decimal_round_trip(文件/module/workflow/全部引用,注释保留 WH-5b.3 来历);(b) f64 pin 断言真实拒绝层/lane/状态,golden 注释改正;(c) workflow 级 kInvalidLayout 改用 **workflow 声明** SourceRange——CoreWorkflowDecl 新增 source_range(core_lower 从 provenance 落子,core_json 序列化/反序列化对称,2 个 core golden 仅新增 source_range 字段,workflow_session 新增 keyword-range 单测)。

**诚实性复核要点:** 生产 CLI 输入路径(type-aware `wire_codec::decode_json`)经 #427 run_rich_input_matrix 独立证明九类型真实 round-trip;tests/conformance/compile_source.hpp 的 typed decoder 只是 harness 并行实现(evaluator schema-free value_from_json 无法表示 Map/Set),不掩盖生产缺陷;blessed observation 经 evaluator bless+verify byte-match;core_frame_layout.cpp 解码器放宽复用既有 per-placement 校验,无校验削弱。

**证据:** full unlabeled dev ctest 581/588,红恰为 #79/#81/#84/#85(product gates,WH-6 翻)+ #421(5c.8)/#424(5c.9)/#425(5c.8),wasmtime skips #5/#7/#57/#60,#265/#427 绿无新增红;census native 73 agreed/0 skipped、Node 69 agreed/4 skipped(新 skip `node_host_awaits_rich_wire_types`,orchestration-only,exact-stem pin {wh5c7_rich_input_matrix});pure-P6 byte freeze #575 1513 bytes / SHA ca80b9d1.. 不变;WASM=OFF 独立构建 exit 0;ASan 下 9 个相关二进制(core_json/workflow_session/wasm_runner/conformance_case/eligibility/双 census/capability_bindings/binary_gate)全绿无 sanitizer 报告。新增 wasm goldens 均入 pinned_core_corpus();@repo-std marker 支持 p6 fixture 的 import std::collections,4 个既有 p6_* fixture 随 IR JSON 变化纳入 corpus pin。

### 12.15.18 WH-5c residual gaps 8/9 (2026-10-02, dedicated decision agent)

本节记录 WH-5c cutover-blocking 普查暴露的最后两个 wasm-lane gap 的机制决策。GAP 8(#421 provider-runtime capability lifecycle event parity)与 GAP 9(#424 non-final String PtrLen carry + cross-node String edges)都不阻塞 WH-5c.2/5c.3 的实现(2026-10-02 实现完成、独立 review 中),但阻塞 WH-6 cutover 的 test-green gate。所有代码断言已对工作树(HEAD 70419ba1 + uncommitted WH-5c.2/5c.3)逐条复核。本节是 append-only;§12.15.1-12.15.17 原文保留。

#### 12.15.18.1 GAP 8 — provider-runtime capability lifecycle event parity

**Empirical signature(file:line):**

fan-out unblocked 后,`tests/scripts/llm_provider_runtime_smoke.py` 的 fallback-stream case(:230-277)在 wasm lane 运行到 completed 但失败:wasm lane 发射 ZERO `provider_degraded` 事件。leftover wasm output(`build/dev/tests/runtime/llm-provider-runtime-smoke/fallback-stream.jsonl`)证实:0 provider_degraded、0 retry_scheduled、0 capability_failed;2 capability_started(attempt=1)、2 capability_completed。

根因是 wasm lane 的 per-call emission 把 aggregate `CapabilityCallResult` 折叠成单一事件序列:

- **evaluator(reference):** `workflow_runtime.cpp:1040` 调用 invoker **一次**;`:1066-1115` 的 synthesis loop 从 aggregate `attempts=N` 合成 per-attempt 事件——每个 attempt 一个 `CapabilityStarted`(:1080-1086)、attempt 之间一个 `CapabilityRetryScheduled`(:1074-1078)、terminal attempt 一个 `CapabilityUsageRecorded`(:1088-1098)、非 terminal-success 一个 `CapabilityFailed`(:1102-1113)。`:1117-1131` 发射 `ProviderDegraded`(用 `add_provider(degraded_name)` + `add_provider(selected_name)` 分配 provider id)。`:1134-1164` 仅在 success 时发射 `CapabilityCompleted`(用 last-attempt invocation id)。`:1057-1064` Pending 路径在 synthesis loop 之前 return,不发射 `CapabilityStarted`(parity 正确)。
- **wasm lane(collapse site):** `wasm_lifecycle.cpp:225-265`(workflow lane)与 `:409-444`(agent lane)对每个 collected call 发射**一个** `CapabilityStarted(attempt=1)`(:237-243)、可选 `CapabilityUsageRecorded`(:244-254)、**一个** `CapabilityCompleted`(:259-264)——**即使 call 失败也发射 Completed**(bug)。`:276` 注释正确声明 Pending 不发射 `CapabilityStarted`(parity 正确)。
- **WasmCapabilityCall 丢字段:** `wasm_lifecycle.hpp:64-79` 的 `WasmCapabilityCall` 只有 node_id、capability_name、success、output、attempts、cache_hit、usage——**缺** status、failure_kind、diagnostic_code、error_message、provider_degraded、degraded_provider_name、selected_provider_name。`workflow_session.cpp:575-588`(workflow)与 `wasm_agent_runner.cpp:265-275`(agent)的 wrapped_invoker 在 collect 时丢弃这些字段。
- **budget-warn gap:** `llm_capability_provider.cpp:534-553` 的 warn-policy reject 把 `CapabilityPolicyNotice` ride 在 `usage.notices` 上;evaluator 的 `capability_eval.cpp:74-81` 把 notices 转成 WARNING diagnostic(进 DiagnosticBag → stderr);wasm lane 不做此转换,notice 只进 report JSON 的 `CapabilityUsageRecorded`,不进 stderr。
- **budget-fail gap:** `llm_capability_provider.cpp:596-601`(usage budget)与 `:629-634`(cost budget)的 fail-policy reject 返回 `CapabilityCallResult{Error, BudgetRejected, diagnostic_code}`;wasm lane 的 `workflow_session.cpp:2084-2128` 把 failed node 归因为 `wasm.trap`/`wasm.host-abort`(不用 cap_call 的 diagnostic_code),且 wasm_lifecycle 发射 `CapabilityCompleted`(应发射 `CapabilityFailed`)。

**关键事实:evaluator 的 per-attempt 事件本身就是 synthetic-from-aggregate。** invoker 被调用一次(`workflow_runtime.cpp:1040`),返回 aggregate `CapabilityCallResult`(`capability_bridge.hpp:76-93`,无 per-attempt trail);synthesis loop 从 `attempts=N` 合成 per-attempt 事件。per-attempt timing/usage/failure detail 在 **两个 lane 都不可得**。因此 parity **不**要求丰富 `CapabilityCallResult` 或把 retry iteration 移到 host loop——它要求 wasm lane 复制**同一个 synthesis**。

**决策:host-side post-run projection helper(单实现,双 lane 调用)。**

新增 `src/runtime/engine/capability_event_projection.{hpp,cpp}`(与 `capability_bridge.hpp` 同目录,因 wasm_host/ 已依赖 engine/),封装 evaluator 的 synthesis 逻辑为一个共享 helper。evaluator 的 `workflow_runtime.cpp:1066-1164` synthesis loop 重构为调用该 helper(big-bang 去重,Principle 1);wasm lane 的 `wasm_lifecycle.cpp:225-265` 与 `:409-444` 替换为调用同一 helper。helper 在 WH-9 evaluator 删除后存活(与 `capability_bridge.hpp` 一起 relocate,见 WH-9 交互节)。

helper 的职责(给定 metadata store + emit sink + node/cap/provider id + `CapabilityCallResult` + optional output_value_id):

1. Pending → 不发射任何事件(与 evaluator `:1057-1064` 一致)。
2. `attempts = max(call.attempts, 1)`;for attempt 1..attempts:分配 invocation id(首个由 evaluator 预分配传入;wasm lane 由 helper 分配);attempt 之间发射 `CapabilityRetryScheduled`;发射 `CapabilityStarted`;terminal attempt + usage → 发射 `CapabilityUsageRecorded`;非 terminal-success → 发射 `CapabilityFailed`。
3. `provider_degraded` → `add_provider(degraded_name)` + `add_provider(selected_name)`,发射 `ProviderDegraded`。
4. success → 发射 `CapabilityCompleted`(last-attempt invocation id、output_value_id、attempts、cache_hit);failure → 不发射 `CapabilityCompleted`。
5. `usage.notices` → 每个 notice 转成 WARNING diagnostic 进 `result.diagnostics`(与 `capability_eval.cpp:74-81` 同 code/message)。

wasm lane 的 invocation 时机是 **post-run**(events 在 `invoke_run2` 期间 buffer,在 finalize 前 projection)——这是 AHFL-specific divergence(见下)。

**被否决的替代方案:**

- **event-synthesis-at-renderer(在 report/JSON renderer 里合成 per-attempt 事件)— REJECTED。** renderer 没有 `CapabilityCallResult`(它只看到已发射的 event stream);在 renderer 里合成会让 audit trail 不诚实(event stream 本身缺事件,report 是 event stream 的 projection 这一不变量被破坏)。parity 必须在 event stream 层面达成,不是在 report 层面。
- **丰富 `CapabilityCallResult` 加 per-attempt trail — REJECTED。** evaluator 本身没有 per-attempt detail(invoker 调用一次,返回 aggregate);加 trail 是发明 evaluator 不拥有的信息,违反 "parity = 复制同一 synthesis" 的事实基础。
- **把 retry iteration 移到 host loop(host 调用 invoker N 次)— REJECTED。** evaluator 也只调用 invoker 一次(`:1040`);retry 是 provider-internal 行为,host loop 重试会改变 provider 的 retry/budget/cache 语义(第二次 host 调用会命中 cache 而不是 provider 内部 retry)。
- **在 wasm_lifecycle 里 inline 复制 synthesis loop(不抽 helper)— REJECTED。** Principle 1(no old-and-new coexistence):evaluator 的 synthesis loop 与 wasm lane 的复制是同一逻辑的两份实现,WH-9 删 evaluator 后留下 wasm lane 的副本——但在此之前是两份必须手动保持同步的实现。抽 helper 让两个 lane 调用同一函数,WH-9 只删 evaluator 的 call site。

**AHFL-specific divergence note:** evaluator 在 call time projection(invoker 返回后立即合成事件);wasm lane 在 post-run projection(`invoke_run2` 是 synchronous/non-reentrant,事件在 invoke 期间 buffer,在 finalize 前统一 projection)。这不是语义差异——projection 的输入(`CapabilityCallResult`)和输出(event stream)在两个 lane 完全相同——而是 wasm3 同步执行模型的后果:host 无法在 wasm invoke 期间安全地 re-enter metadata/event 系统。post-run projection 保持 event stream 的诚实性(事件在 run 完成后、report 生成前发射,audit trail 完整)。

**Builder 指令:**

1. **`WasmCapabilityCall` 改为 `{node_id, capability_name, CapabilityCallResult result}`。** `wasm_lifecycle.hpp:64-79` 的现有字段(success/output/attempts/cache_hit/usage)全部由 `result` 派生或冗余,big-bang 删除。`success` 派生自 `result.status == Success`;`output` 即 `result.value`;`attempts`/`cache_hit`/`usage` 是 `result` 的同名字段。
2. **新增 `src/runtime/engine/capability_event_projection.{hpp,cpp}`。** hpp 声明 projection 函数(签名见上);cpp 实现 synthesis loop + ProviderDegraded + Completed-on-success-only + notices→WARNING。`capability_failure_kind`(`workflow_runtime.cpp:295-312`,anonymous namespace)relocate 到 `capability_bridge.hpp` 作为 `[[nodiscard]] inline` 函数(WH-9 存活的前提)。
3. **evaluator 重构为调用 helper。** `workflow_runtime.cpp:1066-1164` 的 synthesis loop + ProviderDegraded + Completed 替换为 helper 调用;`:295-312` 的 anonymous-namespace `capability_failure_kind` 删除(call site 改用 `capability_bridge.hpp` 的 shared 版本)。
4. **wasm_lifecycle 双 lane 调用 helper。** `wasm_lifecycle.cpp:225-265`(workflow)与 `:409-444`(agent)的 per-call emission loop 替换为 helper 调用;caller 在调用前注册 output value(传 output_value_id)。
5. **wrapped_invoker 记录完整 result。** `workflow_session.cpp:575-588` 与 `wasm_agent_runner.cpp:265-275`:`cap_call.result = std::move(result)`(或 copy);agent lane 对齐 workflow lane 的 Pending skip(`workflow_session.cpp:571`)。
6. **session 失败路径用 cap_call 的 diagnostic_code。** `workflow_session.cpp:2084-2128`:failed node 若有 failed cap_call 且 diagnostic_code 非空,用它做 `failure_code`(替代 `wasm.trap`/`wasm.host-abort`),用 cap_call 的 `error_message` 做 `failure_message`;range 由 WH-5c.6 的 `capability_range_resolver` 提供(GAP 6 决策)。
7. **Pending parity 保持。** helper 跳过 Pending(不发射事件),与 evaluator `:1057-1064` 一致;`wasm_lifecycle.cpp:276` 注释保留。
8. **不做:** 不丰富 `CapabilityCallResult`、不把 retry 移到 host loop、不在 renderer 合成事件、不引入 per-attempt trail。

**Affected files + LOC:**

| 文件 | LOC delta | Notes |
|------|-----------|-------|
| `src/runtime/engine/capability_event_projection.hpp` | +30 | 新文件,projection 函数声明 |
| `src/runtime/engine/capability_event_projection.cpp` | +120 | synthesis loop + ProviderDegraded + notices→WARNING |
| `src/runtime/engine/capability_bridge.hpp` | +15 | `capability_failure_kind` inline relocate |
| `src/runtime/engine/workflow_runtime.cpp` | -80 | 删 anonymous-namespace `capability_failure_kind` + synthesis loop 重构为 helper 调用 |
| `src/runtime/wasm_host/wasm_lifecycle.hpp` | -10 | `WasmCapabilityCall` 精简为 `{node_id, capability_name, result}` |
| `src/runtime/wasm_host/wasm_lifecycle.cpp` | -35 | 双 lane emission loop 替换为 helper 调用 |
| `src/runtime/wasm_host/workflow_session.cpp` | +45 | wrapped_invoker 记录完整 result + 失败路径用 cap_call diagnostic_code |
| `src/runtime/wasm_host/wasm_agent_runner.cpp` | +10 | wrapped_invoker 记录完整 result + Pending skip |
| Tests | +250 | projection helper 单元测试(attempts=1/2+degraded/failed/budget-fail/notices)+ #421 smoke 验证 |
| Docs | +120 | 本节 |
| **Total** | **~465** | |

**AC(mapped to #421,全部 5 case):**

| Test case | Acceptance |
|-----------|-----------|
| fallback-stream(:230-277) | exit 0;`provider_degraded` count==1;`cache_hit` 序列 [False, True];2 `capability_completed`;wasm 与 evaluator 的 event stream 逐事件 byte-parity(ordinal 除外) |
| persistent-cache(:280-325) | exit 0;`cache_hit` [False,True] 然后 [True,True];server request_count==1;event parity |
| budget-warn(:328-388) | exit 0;completed;2 `capability_completed`;`LLM_TOKEN_BUDGET_EXCEEDED` 在 stderr(WARNING diagnostic,不只是 report JSON);event parity |
| budget-fail(:328-388) | exit 非 0;failed;`capability_failed` 事件(不是 `capability_completed`);ranged diagnostic 带 `LLM_TOKEN_BUDGET_EXCEEDED` code;event parity |
| cost-budget-fail(:391-433) | exit 非 0;failed;`LLM_COST_BUDGET_EXCEEDED` 在 stderr;`capability_failed`;event parity |

**Additional gates:** ASan build & test clean;WASM=OFF build clean;fresh build `-Werror` clean;secret leak 检查(`provider-runtime-secret` 不在 event stream/text 中,smoke 已有此断言)。

**Memo/replay(§12.6/§12.14)交互:** `CapabilityMemoEntry`(`workflow_recovery.hpp:81-94`)只存 ordinal/cap_id/arg_hash/result——不存 attempts/cache_hit/usage/degraded。replay 服务 bare Success。两个 lane 在 replay 时发射相同事件(bare Success → 一个 `CapabilityStarted` + 一个 `CapabilityCompleted`,无 retry/degraded/usage)。**PARITY,无交互。** memo replay 不经过 projection helper(replay 路径在 `workflow_session.cpp` 的 memo check 处直接返回,不 collect cap_call);若 replay 也需要 parity(未来 slice),helper 可复用——但本 slice 不改 replay 路径。

**WH-9 交互:** §12.7.3 删除 evaluator target + `WorkflowRuntime`/`agent_runtime`/`capability_eval`。`capability_bridge.hpp` 与 `capability_event_projection.{hpp,cpp}` 被 wasm lane 使用,**不能删**——WH-9 big-bang 必须把它们 relocate 到 shared location(或 `wasm_host/`)。这是 WH-9 的 prerequisite,记入 WH-9 的 deletion manifest。evaluator 的 synthesis loop call site 在 WH-9 删除后,helper 成为唯一实现。

#### 12.15.18.2 GAP 9 — non-final String PtrLen carry + cross-node String edges

**Empirical signature(file:line):**

`examples/execution-demo` 的 IntakeWorkflow 在编译 intake.ahfl 的 Normalize state 时失败:`ctx.normalized_service = input.service;`——String I_k→C_k copy 在 non-final goto handler 中。错误是 `wasm.UNSUPPORTED_WORKFLOW_FRAME`(read gate)。

两个 gate 共同阻止此模式:

- **READ GATE:** `core_wasm_codegen.cpp:4612-4620`——`if (edge_is_ptr_len(leaf->edge)) { if (!final_return_mode_ && bridge_registry_ == nullptr) return reject(...) }`。String PtrLen read 只在 computed-final handler(`final_return_mode_=true`)或 bridge handler(`bridge_registry_ != nullptr`)中允许。Normalize 是 non-final goto handler 且无 bridge → reject。
- **STORE GATE:** `core_wasm_codegen.cpp:7234-7269`——String ctx store 要求 rodata literal(`:7244-7251`,WH-5c.3 后允许)或 bridge-provenance(`:7258`,`value_derives_from_bridge_result`);input-borrowed String 在 `:7259-7265` 被 REJECT。

**Full chain map(execution-demo 全部文件的 String gate 状态):**

| 文件 / state | 操作 | gate 状态 |
|--------------|------|-----------|
| intake.ahfl / Normalize | `ctx.normalized_service = input.service;`(I_k→C_k String copy,non-final) | **READ + STORE GATE 阻止(本 gap)** |
| intake.ahfl / Done | computed-final 构造 IntakeResult(String leaves 从 ctx/input) | 已允许(`final_return_mode_=true`,read gate 放行) |
| decision.ahfl / Evaluate | `ctx.reason = "literal";`(rodata store) | 已允许(`:7244-7251`,WH-5c.3 后) |
| decision.ahfl / Done | computed-final 读 ctx.reason | 已允许 |
| main.ahfl / `node decide: DecisionAgent(intake)` | bare forward(P6→P6 whole-struct,String fields) | 已工作(materializer `store_operand:16126` + `copy_inline_leaf:16156-16158` 拷贝 PtrLen header;child-dereference gate `:12100-12143` 不 reject PtrLen) |
| main.ahfl / `node respond: ResponderAgent(ResponseInput{...String leaves...})` | constructed input(String fields 从 intake/decision 的 output) | 已工作(同上;`copy_inline_leaf` 处理 PtrLen) |
| response.ahfl / Compose | bridge lane(5c.1)+ `ctx.summary = draft.summary`(bridge-provenance store) | 已工作(5c.1 + `:7258` bridge-provenance) |
| response.ahfl / Done | computed-final | 已允许 |

**结论:只有 intake/Normalize 需要修复。** 其余全部已工作(WH-5c.1/5c.3 + 现有 materializer PtrLen 拷贝)。

**决策:borrow model + 两个 gate lift。**

机制:non-final handler 中,String PtrLen 的 8-byte header(payload_ptr: u32 + byte_len: u32)从 I_k slot 拷贝到 C_k slot(两个 i32 store,复用现有 `ptrlen_ctx_store_needed_`/`ctx_store_addr_local_` machinery,`:2857-2863`);payload bytes **borrow immutable** 自己授权的 page region——不拷贝、不分配新 region。

授权的 payload region(全部 fixed-page、immutable-after-init、whole-run-persistent):

1. **rodata** `[256, 1024)`(`core_wasm_abi_constants.hpp`)——String literal,WH-5c.3 后已支持。
2. **entry payload arena**(input frame `[1024, 4096)` 的 String payload)——workflow entry 的 String payload 在 entry pack 时写入,whole-run 只读。
3. **upstream O_k block**(`[12288, 16384)` per-node)——上游 node 的 output String payload,在 node 完成后只读;downstream node 的 I_k 在 dispatch 时从 upstream O_k materialize,borrow 在 downstream node 执行期间有效(upstream O_k 不被 zero-fill——C_k 才被 zero-fill,`:16628-16634`)。
4. **bridge result placement**——capability 返回的 String payload 由 host 写入 scratch/bridge region,在 agent 执行期间只读。

C_k zero-fill 是 once-per-node(`:16628-16634`,在 runner 启动前),不是 per-handler。Normalize 写入的 String PtrLen 在同一 node 的 Done handler 中持久存在(C_k slot 不被 re-zero);payload(borrowed,不在 C_k)不受 zero-fill 影响。

**与 §12.15.17.2(c) GAP 7 collection header-copy 的对比:** GAP 7 的 collection construct operand 拷贝 collection **header**(`base: u32, len: u32`,8 bytes inline),elements 留在 shared backing region(`kP6CollectionBackingBase=16384`)immutable borrow。GAP 9 的 String PtrLen carry 是**同一 borrow model 的应用**:8-byte header 拷贝,payload borrow immutable。区别只在 payload region:collection 用 shared backing region;String 用 rodata/entry arena/upstream O_k/bridge placement(都是已授权的 immutable region)。两个决策一致:AHFL 的 P6 frame 对 variable-length payload(String bytes、collection elements)统一采用 header-copy + payload-borrow,不拷贝 payload bytes。

**被否决的替代方案:**

- **new region/copy model(为 String payload 分配新 page region 并拷贝 bytes)— REJECTED。** (1) 浪费 page capacity:String payload 可能很大,拷贝到新 region 会快速耗尽 64KiB page;(2) 发明第三种机制(rodata literal / bridge-provenance / new-region-copy),违反 Principle 1(一个干净的大重构 > 一百个小 workaround);(3) GAP 7 的 collection header-copy 已确立 borrow model 先例,String 应遵循同一模型;(4) payload 的 immutability 已由 region 语义保证(rodata/entry arena/upstream O_k/bridge placement 都是 immutable-after-init),拷贝不增加安全性。
- **把 String 编码为 opaque JSON blob 进 P6 frame — REJECTED。** 与 GAP 7 的同类否决一致(§12.15.17.2):opaque JSON 是并行 type system,违反 Principle 1/3;String 已有 P4-D PtrLen 布局。
- **把 GAP 9 fold 进 WH-5c.7(GAP 7 rich type matrix)— REJECTED(保持独立)。** 理由:(1) gate 不同——GAP 9 是 read gate(`:4612-4620`)+ store gate(`:7258-7265`)的 lift;GAP 7 是 construct/let/materializer 的 4-layer widening(`plan_construct_operand`、`emit_construct_store`、`plan_let_binding`、scheduler materializer)。(2) test driver 不同——GAP 9 由 #424(execution-demo full run)驱动;GAP 7 由 #427(capability_bindings smoke + rich-shapes conformance case)驱动。(3) scope 不同——GAP 9 是 2 个 gate lift(~70 LOC codegen);GAP 7 是 9 类类型的 4-layer widening(~350 LOC codegen)。(4) risk 不同——GAP 9 的 borrow model 是 GAP 7 先例的直接应用,风险低;GAP 7 引入新类型(f64/Unit/Collection/inline-enum)的 construct/materialize 路径,风险高。合并会让 GAP 7 的大 blast radius 阻塞 GAP 9 的窄修复。
- **evaluator fallback — REJECTED。** Principle 1;evaluator 在 WH-6 cutover 后不可达。

**AHFL-specific divergence note:** Rust/Swift 的 String 是 owned(heap-allocated,move semantics);AHFL 的 P6 frame 是 fixed 64KiB page,无法为每个 String carry 分配 heap。AHFL 的选择是 PtrLen(header)+ immutable borrow(payload):String 的物理表示是 8-byte inline header,指向 page 内一个 immutable-after-init region。这与 Rust 的 `&str`(borrowed string slice)语义接近,但 borrow 的 lifetime 是 whole-run(不是 lexical scope)——由 page region 的 immutability 保证,不是 borrow checker。这是 AHFL 在 wasm3 单 page 约束下的自然选择:variable-length payload 不进 frame,frame 只持有 header。

**Builder 指令:**

1. **Lift read gate。** `core_wasm_codegen.cpp:4612-4620`:删除 `if (!final_return_mode_ && bridge_registry_ == nullptr) return reject(...)` 对 PtrLen read 的限制。non-final handler 无 bridge 现在可以 read String PtrLen from I_k/C_k。设 `ptrlen_read_needed_ = true`(现有 flag,`:2857-2863`)以分配 read temp。emit 路径(`:6889-6902`)已工作,不另 gate。
2. **Lift store gate + input-frame provenance。** `core_wasm_codegen.cpp:7258-7265`:新增第三个 provenance——input-frame projection。新增 helper `value_derives_from_input_frame`(类比 `value_derives_from_bridge_result` `:3164-3207`,但 root 是 `CorePathRoot::Input`):value 的 defining expr 是 `CorePathExpr` with root=Input(直接,或经 let-alias DAG walk)。这证明 payload 在 entry payload arena / upstream O_k(都是 whole-run-stable immutable region)。
3. **Fail-closed 保持。** String store 的 value 若不是 rodata literal、不是 bridge-derived、不是 input-frame-projected,保持 reject(`:7259-7265` 的现有 diagnostic,message 更新为命名三个授权 provenance)。每个新增/更新的 reject diagnostic 携带 SourceRange(Principle 5)。
4. **无 new region、无 payload copy。** payload bytes borrow immutable from 授权 region(rodata/entry arena/upstream O_k/bridge placement)。8-byte PtrLen header 拷贝(两个 i32 store via 现有 `ptrlen_ctx_store` machinery)。
5. **C_k lifetime 确认。** C_k zero-fill 是 once-per-node(`:16628-16634`),Normalize 写入的 String PtrLen 持久到 Done。无代码变更,但 builder 应加 test 验证(non-final 写 → final 读)。
6. **Cross-node edges 无变更。** scheduler materializer 的 `store_operand`(`:16090-16142`)+ `copy_inline_leaf`(`:16147-16159`)已处理 PtrLen;child-dereference gate(`:12100-12143`)不 reject PtrLen。无代码变更。
7. **不做:** 不分配新 page region、不拷贝 payload bytes、不引入 opaque JSON、不 fold 进 WH-5c.7、不 bump wire/manifest/snapshot version(§12.15.8 不变)。

**Affected files + LOC:**

| 文件 | LOC delta | Notes |
|------|-----------|-------|
| `src/compiler/backends/wasm/core_wasm_codegen.cpp` | +70 | read gate lift(~10)+ store gate lift + `value_derives_from_input_frame`(~50)+ diagnostic message 更新(~10) |
| Tests | +150 | #424 execution-demo full run + golden(String carry byte-parity) |
| Docs | +60 | 本节 |
| **Total** | **~280** | |

**AC(mapped to #424):**

| Test | Acceptance |
|------|-----------|
| #424 `ahflc.run.execution_demo.smoke` | exit 0;execution-demo 4 个 workflow(Intake/Decision/Response/main)全部在 wasm lane 编译+运行成功;output JSON 与 evaluator byte-parity;event stream 与 evaluator byte-parity(ordinal 除外) |
| golden `wh5c_string_carry.ahfl`(新增) | non-final goto handler `ctx.s = input.s;` → final computed return 读 `ctx.s`;wasm 编译成功(无 `wasm.UNSUPPORTED_WORKFLOW_FRAME`);output String 与 input byte-identical |
| fail-closed pin | String store from non-authorized provenance(如 scratch-computed String)仍被 reject,带 explicit diagnostic + SourceRange |

**Additional gates:** ASan build & test clean;WASM=OFF build clean;fresh build `-Werror` clean。

**Memo/replay(§12.6/§12.14)交互:** 无。String PtrLen carry 是 codegen 层的 frame 内操作,不经过 capability/memo 路径。memo replay 的 `(node, ordinal)` identity 与 String carry 正交。

**WH-9 交互:** 无。GAP 9 是 codegen gate lift,不引入 evaluator 依赖的 runtime 组件。WH-9 删 evaluator 后,codegen 的 String carry 路径不变。

#### 12.15.18.3 Sequencing(amends §12.15.17.5)

§12.15.17.5 的顺序修订为:

1. WH-5c.1(GAP 3 descriptor fix)— LANDED `70419ba1`(2026-10-02)。
2. WH-5c.2(GAP 1 per-node blocks,APPROACH B per §12.15.16)— 实现完成,review 中(2026-10-02)。
3. WH-5c.3(GAP 5 rodata)— 与 5c.2 同切片,review 中(2026-10-02)。
4. WH-5c.4(GAP 2 opaque construct)。
5. WH-5c.5(GAP 4 parity + fail-closed)。
6. WH-5c.6(GAP 6 diagnostic SourceRange parity)。
7. WH-5c.7(GAP 7 rich type matrix)。
8. **WH-5c.8(GAP 8 provider-runtime capability lifecycle event parity)— 新增,排在 5c.7 后。**
9. **WH-5c.9(GAP 9 non-final String PtrLen carry)— 新增,与 5c.8 并行(无依赖),可同 commit 或独立 commit。**
10. WH-6(ahflc run cutover commit)— gated on §12.15.11 的全部 9 个 CLI smoke green(#213/#214/#421/#422/#423/#424/#425/#426/#427,#79 为 #421 的 cascade 自动解除)+ census 73/0 + ASan + WASM=OFF。

**Gate revision:** §12.15.17.5 的 gate set 从 "5b.1 + 5b.3 + 5b.2 + 5c.1-5c.7" 扩展为 "5b.1 + 5b.3 + 5b.2 + **5c.1-5c.9**"。

**5c.8 与 5c.9 的并行性:** 两者无代码依赖(5c.8 改 runtime event projection;5c.9 改 codegen gate)。可并行实现;但 WH-6 cutover gated on 两者都 green。

#### 12.15.18.4 Census pin impact

**GAP 8(#421):** 无 census impact。#421 是 CLI smoke test(`SingleFileCliTests.cmake:401`),不是 conformance census case。`kExpectedAgreed`/`kExpectedSkipped` 不变。

**GAP 9(#424):** 无 census impact。#424 是 CLI smoke test(`SingleFileCliTests.cmake:434`),不是 conformance census case。execution-demo 的 4 个 workflow 若加入 conformance census 则 +4 agreed(但它们是 CLI smoke,不是 census case)。`kExpectedAgreed`/`kExpectedSkipped` 不变。

**Census verdict:** §12.15.17.4 的预测(`kExpectedAgreed = 73`)不变。GAP 8/9 不增加 conformance census case。

#### 12.15.18.5 LOC estimate

| Component | LOC delta | Notes |
|-----------|-----------|-------|
| GAP 8(§12.15.18.1) | ~465 | projection helper + WasmCapabilityCall 精简 + 双 lane 调用 + session 失败路径 + tests |
| GAP 9(§12.15.18.2) | ~280 | codegen gate lift + input-frame provenance + tests |
| **Total** | **~745** | |

#### 12.15.18.6 Prior-decision preservation

- **§12.15.1-12.15.15:** 保留,不重写。GAP 1-5 的决策不变。
- **§12.15.16(APPROACH B per-node blocks):** 保留。GAP 8/9 与 per-node cardinality 独立。
- **§12.15.17(GAP 6/7):** 保留。GAP 9 的 borrow model 是 §12.15.17.2(c) collection header-copy 先例的应用;GAP 8 与 GAP 6/7 独立。
- **§12.7(WH-6 cutover,无 fallback):** 保留。gate set 扩展(§12.15.18.3)。
- **§12.7.3(WH-9 evaluator deletion):** 保留,但增加 prerequisite:`capability_bridge.hpp` + `capability_event_projection.{hpp,cpp}` 必须 relocate(不删)。
- **§12.15.8(no version bump):** 不变。GAP 8/9 不增加 wire field(event projection 是 host-side;String carry 是 codegen 层 frame 操作)。AHFLXM v2 不 bump。
- **§12.6/§12.14(memo/replay):** 保留。GAP 8/9 与 memo replay 正交(§12.15.18.1/18.2 交互节)。

#### 12.15.18.7 落地记录(2026-10-03):WH-5c.8 GAP 8 共享 event projection + 对抗 review

按 §12.15.18.1 builder 指令 1..8 落地(builder → 独立对抗 review → coordinator 复核)。新增 `src/runtime/engine/capability_event_projection.{hpp,cpp}` 作为 evaluator 与双 wasm lane 的唯一 per-attempt 事件合成实现;evaluator `workflow_runtime.cpp` synthesis loop 重构为 helper 调用(event stream 保持,workflow_runtime_all 等 pin 全绿);`capability_failure_kind` relocate 到 `capability_bridge.hpp`(`[[nodiscard]] inline`,旧匿名命名空间副本删除);`WasmCapabilityCall` big-bang 塌缩为 `{node_id, capability_name, CapabilityCallResult result}`,旧字段全部删除;wrapped_invoker 经新增 `clone_capability_call_result`(deep-clone Value)在双 lane 记录完整 result;failure path 在 facts.capability_calls 中查失败 node 的非空 diagnostic_code 覆盖 failure_code/failure_message(range 用 5c.6 capability resolver,无 code 时保持 wasm.trap/host-abort 归因);agent lane 对齐 Pending skip。

**顺带修掉的既有隐患:** 删除 wasm lane 的 `cap_name_to_node` 名字→node 映射(5c.4 时代 symbol_to_schedule last-wins 同类问题),改用 `recorder.current_import()->node.index()`(import 时刻权威归因,shared-capability 多 node 不再错挂)。

**notices → WARNING 归属变化(诚实记录):** 转换点从 `capability_eval.cpp` 的 EvalResult bag 移到 helper 的 WorkflowResult bag。workflow 路径(唯一生产调用方)诊断落点不变——agent runtime 的 EvalResult bag 本来就 append 进同一个 WorkflowResult bag;coordinator grep 确认 `AgentRuntime` 在 src/ 内无 workflow_runtime 之外的生产 standalone 调用方(DAP/REPL 不直接驱动它),故无在树消费者受影响;evaluator AgentRuntime 本身将在 WH-9 删除。理论上的外部 standalone embedder 会失去 notice WARNING——记为接受的收窄,不留双份实现(双份会在 workflow 路径重复计数)。

**Review 结论 LAND(0 P0/P1,3 P2;coordinator 处置):** (1) 本条即 §12.15.18.1 Memo/replay 段的 append-only 勘误——原文称 replay 发射"一个 Started + 一个 Completed",实测双 lane replay **零事件**(`memo check 处直接返回,不 collect cap_call,helper 不可达),代码与 evaluator 行为一致,是决策文档措辞错误而非代码缺陷;(2) notices 收窄见上,接受;(3) 早期 coordinator 基线把 #79 归类为"pnpm 环境红"不准确:#79 的 evidence generator 内含 #422 provider smoke,GAP 8 修复后 #422 转绿、#79 级联转绿(同 5c.7 时 #79 因嵌套 #421 失败而红的日志证据);真正的环境红只有 #84(pnpm);#81 是经 #425 的 GAP 9 级联、#85 是 bundle artifact 缺失,均与本 slice 无关且失败原因不变。

**证据:** #422 llm_provider_runtime_smoke 全部 5 case(fallback-stream provider_degraded==1 + cache_hit [False,True] + 2 completed;persistent-cache server request_count==1;budget-warn completed + TOKEN_BUDGET_EXCEEDED 进 stderr;budget-fail/cost-budget-fail exit!=0 + capability_failed 非 completed + ranged code);#426 llm_failure_matrix line 579-580 capability_failed 满足、整二进制绿;#428 capability_bindings 绿;新增 #232 helper 单测 9 case(attempts=1/2 顺序与 invocation id、degraded、failure 无 Completed、budget code、多 notice、Pending 零事件)。full unlabeled ctest 589 个:红恰为 #81/#84/#85/#425(#425=旧 #424 profile_and_output_contract,5c.9 gate),wasmtime skips #5/#7/#57/#60,无新增红;census #270 native 73/0、#267 Node 69/4 不变;WASM=OFF 构建绿;ASan 下 workflow_runtime_all/capability_bridge_all/capability_event_projection_all/workflow_session/agent_runner 5 个二进制绿无 sanitizer 报告。

#### 12.15.18.8 落地记录(2026-10-03):WH-5c.9 GAP 9 non-final String PtrLen carry + 对抗 review

按 §12.15.18.2 builder 指令 1..7 落地(builder → 独立对抗 review → coordinator 复核)。两处 gate lift + 一个 provenance helper + 一个计划外 codegen 修复:

- **READ GATE lift**(`core_wasm_codegen.cpp` plan_path):删除 `!final_return_mode_ && bridge_registry_ == nullptr` 对 PtrLen read 的 fail-closed;non-final goto handler 现可从 I_k/C_k slot 读 String 两字 pair,`ptrlen_read_needed_` 照旧分配 read temp。无新 gate:读取只是两个 inline i32 load,payload 不被触碰。
- **STORE GATE lift + 第三 provenance:** 新增 `value_derives_from_input_frame`(与 `value_derives_from_bridge_result` 同构的 let-alias DAG walk),仅沿 `CoreValueRefExpr`(纯 alias)与 `CorePathExpr{Local}`(投影)边走到 `CorePathRoot::Input`;任何 transform 节点(Binary/Construct/Coerce/Collection/Call/CallClosure/Unsupported)或 Context/其他 root 都断链 → 保持 reject。reject diagnostic 更新为命名全部三个授权 provenance(rodata literal / capability bridge result / input-frame projection),SourceRange 保留。
- **borrow model 落地无新机制:** ctx String store 只经现有 `ptrlen_ctx_store` 路径拷贝 8-byte header(两个 i32.store);payload 留在 rodata [256,1024) / entry payload arena / upstream O_k [12288,16384) / bridge placement,全部 fixed-page、immutable-after-init、whole-run-persistent。C_k once-per-node zero-fill(`append_word_zero_fill` 在 runner dispatch 前,非 per-handler)保证 non-final 写入持久到 final;O_k 从不被 zero-fill,跨 node edge 的 borrow 在 downstream node 整个执行期有效。无新 region、无 payload copy、无 wire/manifest version bump、无 evaluator fallback。

**计划外修复(独立复核为真实既有 bug):** emit_store 的 PtrLen 路径把目的地址用 `local.tee` 存入 `ctx_store_addr_local_`——tee 留下一个无人消费的 i32 在栈上。直线 goto handler 靠末尾 `br` 展开 block 时丢弃该残余而"偶然合法";但 String ctx store 若出现在 statement-level void `if/else` arm 内(arm 以 `kEmptyBlock` 结尾,wasm3 `v_pop_ctrl` 要求操作数栈精确回到 frame height),wasm3 即报 `typeCountMismatch "incorrect value count on stack"`。execution-demo 的 decision.ahfl Evaluate 在嵌套 if/else arm 里有 rodata String store(`ctx.reason = "..."`),此前因 intake.ahfl GAP 9 先失败、整个 demo 编译到不了 decision 而被掩盖;gate lift 暴露该潜伏 bug。修复为 `local.set`(弹出地址),两个 `store_word` 各自 `local.get` 重取;READ 路径(值上下文,tee 后第一个 i32.load 消费栈顶、第二字 local.get 重取)的 tee 保持不变。emit_store 唯一调用方是 void 语句 visitor,无值上下文调用方,故 set 对全部既有 caller 语义中立。

**字节影响枚举:** 每个含 String(PtrLen)ctx store 的模块有 1 个 opcode 字节变化(0x22 tee → 0x21 set):wh5c_construct_bridge / v2c_* 等 behavioral case 与 examples/execution-demo(非 byte-pin,行为不变,census 双 runner 仍 agreed);纯 P6 freeze fixture wh5c_instance_reuse_fanout 为 Int-only(Frame.value: Int / Ctx.echoed: Int),不走 PtrLen 路径,**1513 bytes / SHA ca80b9d1… 不变**;e1/e2/e3 fixture 有 String 字段但无 String ctx store,不变;3 个新 golden 无历史字节。

**Review 结论 LAND(0 P0/P1,3 P2;coordinator 处置):** (1) tree 中 parked 的 WH-6 CLI cutover(workflow_run.cpp + CMakeLists.txt)不属于本 slice,不 stage、不评审,但其存在使 #425 经 `ahflc run` 跑在 wasm lane——5c.9 codegen 是必要解锁、parked cutover 是运行跑道,二者共同产生 #425 绿;cutover 本体留给 WH-6 review(#89)。(2) **#81 失败原因的 append-only 更正:** §12.15.18.7 与 5c.9 brief 预期 #81 是 stale evidence revision hash;coordinator 与 reviewer 实测 #81 现推进过 reference-workflow 阶段,最终失败于 `generate-beta-install-evidence.py` 的 platform VSIX packaging 步骤 `pnpm: command not found`(exit 127),与 #84/#85 同属 pnpm 环境缺失级联;revision-hash gate(check-beta-gate --require-ready)在本环境被更早的 pnpm 失败遮蔽。#81/#84/#85 均非 5c.9 回归,bundle generator 需要的代码输入(#425 等)已绿。(3) provenance walk 对 CoerceExpr 链与 Context-root String copy 保守拒绝(fail-closed,符合决策 AC 的 `ctx.s = input.s` 直接投影),记为未来 rung 的已知收窄。

**证据:** 新增 3 个 verifier-clean golden——`wh5c9_string_carry.ahfl`(non-final I_k→C_k → final computed 读回,empty + 2048-byte 长 String 输出 byte-parity)、`wh5c9_string_edge.ahfl`(两 node:upstream O_k borrow → downstream non-final carry → final 返回 byte-parity)、`wh5c9_string_construct_fail_closed.ahfl`(`let w = Wrap{inner: input.s}; ctx.carried = w.inner;` 经 Construct 断链被 kUnsupportedCapabilityFrame 拒绝,diagnostic 命名三 provenance + SourceRange),均 pin 进 pinned_core_corpus()。#425 profile_and_output_contract GREEN:execution-demo 4 个 workflow(Intake/Decision/Response/main)在 wasm lane 编译+运行,`service:"checkout"` 端到端穿过 intake Normalize→Done→decision→response,output/events byte-parity;examples/ 零改动。#576 byte freeze 1513/SHA ca80b9d1 不变;census #270 native 73/0、#267 Node 69/4 不变;#245 workflow_session 681 checks、#124 core_json_round_trip 15 cases/1264 assertions、core_wasm_codegen 单测 213/213(旧 fail-closed 用例翻转为 gate-lift 用例)绿。full unlabeled ctest 586/589 通过(1781s):红恰为 #81/#84/#85(全部 ahfl-beta-gate pnpm 缺失级联,非本 slice 回归),skip 恰为 #5/#7/#57/#60(wasmtime);WASM=OFF refusal(exit 1)绿;ASan 下 workflow_session/core_wasm_codegen/core_json_round_trip 绿无 sanitizer 报告;fresh dev -j2 -Werror 干净。

### 12.15.19 WH-5c GAP 4 机制修订:per-node output stash table(2026-10-02,dedicated decision-revision agent)

本节修订 §12.15.7 的 **per-node output host 可读性机制**(GAP 4)。§12.15.7 原文保留作历史档案(append-only),其 id 分配规则、fail-closed alignment、parity harness stripping 删除决策**不变**;仅"host 如何获得 per-node output"这一机制被替换。修订动因为协调者转交的经验证据(wh5c-builder-supplements.md §12.15.7):opaque WireJson lane 的 run2 只返回 final tuple,per-node result tuple 活在 scheduler local 里、host 不可读。所有代码断言已对工作树(HEAD `453d3888` + uncommitted WH-6 cutover)逐条复核。

#### 12.15.19.1 §12.15.7 的事实证伪

§12.15.7 的 Design 节声称:"identity node 的 output = 其 input tuple(host 已 packed);capability node 的 output = capability result(wire JSON)"。独立复核确认这**不是 under-specification,而是事实错误**(仅单节点 pure-identity workflow 成立):

1. **host 只 packed ENTRY tuple。** opaque lane 的 entry JSON 由 host 经 `alloc_then_write` 写入 bump heap(`wasm_agent_runner.cpp:451`);workflow lane 的 entry frame 由 host packed 进 schedule 首个 node 的 I_k(P6)或经 `alloc_then_write`(opaque)。host **从不** packed 非 entry node 的 input——非 entry node 的 input 是 upstream node 的 output,由 scheduler 在 guest 内 materialize / 透传。
2. **per-node result tuple 活在 wasm scheduler local 里。** scheduler 为每个 node 分配一对 local(`core_wasm_codegen.cpp:16499-16512`:`workflow_node_ptr_local` = `2 + node.value*2`、`workflow_node_len_local` = `+1`),runner 返回的 `(status, ptr, len)` 存入这对 local(`:16920-16924`)。wasm local **host 不可读**——host 只能读 linear memory 与 exported global。
3. **run2 只返回 final tuple。** `make_workflow_run2_body` 的 opaque 路径以 `append_workflow_source(body, plan.output)` 收尾(`:17124-17127`),只把 workflow output source(最终 node 的 ptr/len local)压栈返回。中间 node 的 tuple 不离开 guest。
4. **canonical / compute node 的 output ≠ input。** 无 capability 的 canonical agent(whole run in-guest,不做 host call)的 output 是在 guest 内计算的新 JSON bytes,host 无法从 input 重建。
5. **capability node 的 output ≠ cap result(一般情况)。** host 侧 `WasmCapabilityCall.output`(`wasm_lifecycle.hpp:64-79`)是 **capability result**,不是 node output。一个 node 可以 call cap 后再 compute(`let r = Cap(...); return Struct{x: r.x};`),其 node output 是计算产物,不是 cap result。§12.15.7 的等式只对 capability-FINAL node(`return Cap(...)`)成立,且需要 host 做 shape 分析才能识别 finality——host 不做这种分析。

结论:§12.15.7 的 "host 已 packed / cap result" 机制对多节点 chain、canonical compute、cap-then-compute 三类 shape 均不成立。GAP 4 的 conformance case(多 node identity + capability workflow)在该机制下无法实现。需要一个 guest 侧机制把 per-node output 暴露给 host。

#### 12.15.19.2 已验证的机器行为(file:line)

**scheduler 与 completion(`core_wasm_codegen.cpp`):**

- `append_workflow_schedule`(`:16731-17017`)按 `plan.schedule` 顺序逐 node 发射。P6 node 走 `:16754-16903`(materialize I_k → call runner → 校验 O_k → 写 tag-0 event record(仅 capability workflow)→ bump `workflow_completed_count`);opaque node 走 `:16905-17016`(xcode/source → call runner → 状态分派 → 写 event record(仅 capability workflow)→ bump `workflow_completed_count`)。
- `workflow_completed_count` global(`kWorkflowGlobalCompletedCount = 4`,`:1180`)在**每条** node 完成路径上 +1(P6 `:16899-16902`、identity opaque `:16936-16939`、capability opaque `:17013-17016`)。PENDING(suspend)路径在 bump 之前 return(`:16856` / `:16962`),不 bump。
- identity workflow(无 imports)不写 event record;completion 由 host 从 `workflow_completed_count` global 推导(`workflow_session.cpp:2049-2053`:`node_completed[i] = i < workflow_completed_count`)。
- workflow module 的 `fn_count = 0`(`:1129-1137`):packaged agent 的 outlined fn 不进 workflow module,故 workflow module 内无 fn-mode heap bump。

**heap 纪律(决定 stash 指向的 bytes 是否 stable):**

- opaque runner 无 computed handler(computed final → `has_computed_final` → P6;computed goto → P6),故**不经过** `P6ComputationHandlerBuilder`,**不发射** `heap_next = construct_heap_base` reset(`:2681-2684` 只在 handler-mode `reset_construct_heap_` 时发射)。opaque lane 的 bump heap 在整次 run 内**单调递增**,不回收。
- P6 relocated handler 的 construct-heap reset(若 `construct_heap_enabled`)把 `heap_next` 设为 relocated build 的 `construct_heap_base`;该 base 在 legacy lane 下 `backing_high ≥ kP6CollectionBackingBase = 16384`(`:10544-10573`),故 reset 总是把 `heap_next` **向上跳**到 ≥16384,**不会向下回收到** opaque JSON 所在的 `[heap_base, 16384)` 区间。opaque node 的 JSON bytes 在整次 run 内不被覆盖。
- bump allocator(`alloc` import,`:14213-14221`)单调推进 `heap_next`,超限 fail-closed。

**host 侧 join 现状(`workflow_session.cpp`):**

- node-output decode 路径(`:1760-1854`):遍历 node-event record,仅对 `node_desc->is_p6` 的 node 从 O_k block decode(`:1816-1838`,`read_value_at`);opaque node 得 silent `NoneValue`(`:1808-1815` 注释明言 "not host-observable")。decode 失败 silent(`:1834-1836`:`if (output.has_value())` 否则留 `NoneValue`)。
- identity workflow 无 event record,故事实上 `node_outputs` 全空、`node_completed_hook` 不触发(evaluator 对每个 completed node 触发 hook,`workflow_runtime.cpp:1429-1431`——这是既有 parity gap)。
- workflow output decode 已 fail-closed:P6 `:1952-1961`、WireJson `:1974-2001`(range check + `value_from_json` + `kOutputDecodeFailed`)。
- `workflow_completed_count` 校验(`:2020-2036`):成功 run 必须 `== workflow_node_count`;失败 run `<=`。
- evaluator id 分配(`workflow_runtime.cpp`):cap result id(`:1138`)在 node output id(`:1432`)之前,逐 node 按 schedule 顺序;workflow output id 在所有 node 之后(`:1525`)。wasm lifecycle helper 的 `add_value`(`wasm_lifecycle.cpp:24-25`)按相同顺序分配(cap_call output `:255-261` → node output `:268-272` → workflow output `:300-304`)。**id 排序机制已正确,唯一缺口是 opaque node output 未 decode → `node_facts.output` 为空 → 无 id。**

**wire schema / manifest:**

- `CoreWireFrameRoots.node_outputs`(`core_wire_schema.hpp:152-163`)是 **P6-dense**(parallel to `node_blocks`),all-opaque workflow 为空。opaque node output **无** wire-schema root——host 用 schemaless `value_from_json` decode(与 workflow output path `:1988` 相同)。无需 manifest / wire-schema 变更。
- `CoreWasmExecutionDescriptor`(`core_wasm_codegen.hpp:217-264`)已携带 `nodes`(schedule order,含 `is_p6`/`schedule_pos`/`p6_block_ordinal`)、`imports`、`workflow_node_count`、`heap_base`、`event_records_base`、`event_record_bytes`——host 计算 stash base 所需的一切,**无需新 descriptor field**。

**parity harness stripping(`wasm_runner.cpp`):**

- `strip_output_value_id`(`:1223-1238`)把 eval 与 wasm 两侧的 `output_value_id` 统一替换为 `null`;调用点 `:1335-1338`(仅 `!is_p6_frame`)。注释 `:1218-1222` 明言原因:"the wasm lane cannot observe individual node outputs"。

#### 12.15.19.3 决策:guest scheduler stash table(APPROACH A-revised)

**选择:guest scheduler 在每个 opaque node 完成后,把其 output tuple 的 `(ptr, len)` 存入一张 host-known 的固定页表(per-schedule_pos 索引,8 字节/slot);host 在 run 后读表,对 opaque node 用 `value_from_json` decode、对 P6 node 沿用既有 O_k `read_value_at` 路径,按 schedule 顺序 join 全部 node output 并分配 sequential `RuntimeValueId`。**

这是协调者候选空间中的 (A),但做两处收窄:(a) stash **只覆盖 opaque node**(P6 node 沿用 O_k,不 stash——见 §12.15.19.9 拒绝项 3);(b) 表按 `schedule_pos` 直接索引(8×N slot,P6 slot 留零),不引入 opaque-dense ordinal 映射。

**Reference Hierarchy:** Rust/Clang 的 flat store + index-based identity(Principle 2/3):一张 `vector<(u32 ptr, u32 len)>` 风格的固定表,index 是 `schedule_pos`(编译期已知的 dense ordinal),不是 string key。host 侧 join 是单一 decode loop(P6 → `read_value_at`,opaque → `value_from_json`),与 evaluator 的 node loop 同构。AHFL-specific divergence 见 §12.15.19.12。

#### 12.15.19.4 页区域放置(addresses / sizing / max-nodes / overflow)

stash 表是 node-event region **同族**的固定低页区域,位于 event region 之后、bump heap / P6 cursor 之前:

- **slot 布局:** 8 字节,`[0..4) = ptr (u32-LE)`、`[4..8) = len (u32-LE)`。按 `schedule_pos` 直接索引。
- **stash_base 推导(纯算术,ABI 常量 + N + imports flag,host 与 codegen 同一公式):**
  - `event_end = imports.empty() ? kNodeEventLogBase (1024) : event_records_base + N * event_record_bytes`(后者 = `1032 + 40*N`,天然 8 对齐)。
  - `stash_base = event_end`。
  - `stash_extent = 8 * N`(仅当 workflow 至少有一个 opaque node;pure-P6 workflow 为 0,不 reserved)。
- **各 lane 的 heap_base / cursor:**
  - pure-opaque identity(无 imports、无 P6):`heap_base = align8(1024 + 8*N)`(原 1024)。
  - pure-opaque capability(有 imports、无 P6):`heap_base = align8(1032 + 40*N + 8*N) = align8(1032 + 48*N)`(`compute_event_layout` 扩展 stash 项)。
  - hybrid / P6(有 P6 node):cursor planner 的 cursor 起点从 `event_end` 上移到 `event_end + 8*N`(`:12324-12339`),后续 bridge control / node blocks / scratch / entry payload / result placements / wf_output / state-trace / normalize / transcode 全部顺移;`wf_heap_base` 是顺移后的 cursor 末值。
  - pure-P6(无 opaque node):`stash_extent = 0`,cursor 起点不变,**module bytes 与现状逐字节相同**。
- **slot 0 与 event header 复用(identity lane)的安全条件(协调者已复核):** identity workflow(imports 为空)的 stash slot 0 占据 `[1024,1032)`,即 8 字节 node-event header 的同一地址。安全因为:(a) event header 的 reset 发射以 `capability_workflow` 为条件(`core_wasm_codegen.cpp:16711-16716`),identity workflow **不写** header;(b) host 的 node-event record decode 只对 capability workflow 进行,identity workflow 的 completion 只从 `workflow_completed_count` global 推导(`workflow_session.cpp:2049-2053`),**不读** `[1024,1032)`。builder 必须加一条 pin:identity workflow 跑完后 `[1024,1032)` 解析为 stash slot 0(ptr,len),且 event-decode 路径不被触发。
- **max-nodes bound(协调者按 `heap_base = 1024 + 8 + per*N ≤ 65536` 重算,decider 初稿数字有误,以本处为准):** capability workflow 低页预算从每 node 40 B 增至 48 B(40 B event record + 8 B stash slot),`heap_base = 1032 + 48*N`:**N=1343 可容(heap_base 65496),N=1344 被拒(65544)**。既有 40 B 布局的实测边界是 **N=1612 可容(65512)、1613 被拒(65552)**(`core_wasm_codegen.cpp:14937-14938` 注释同值;decider 初稿所写 1365 / 1637 是漏掉 `kNodeEventLogBase=1024` 基数的算术错误,作废)。identity workflow 为 `1024 + 8*N ≤ 65536 → N ≤ 8064`(初稿 8191 同样作废),但 scheduler local(`2*N + status + materializer locals`)与 wasm3 function local 上限先收紧——既有 local bound 不变,stash 只增加页字节预算;builder 不得硬编码 1343,一律以 exact checked arithmetic 推导(与 `compute_event_layout` 两阶段同风格)。
- **overflow:** 既有 capacity check 自然覆盖——P6 cursor 的 `cursor >= kP6CollectionBackingBase`(`:12795`)与 `cursor > kPage`(`:12801`);`compute_event_layout` 的 `heap_base > 65536`(`:14969`)。超限 fail-closed `kResourceExhausted` + SourceRange diagnostic(Principle 5,与 §12.15.9 同 style),命名 stash region 与 65536 page capacity。
- **host 侧校验:** `stash_base + 8*N <= descriptor.heap_base`(corrupt module → fail-closed);每个 opaque slot 的 `(ptr, len)` 校验 `ptr >= heap_base && ptr + len <= 65536 && ptr != 0 && len != 0`(与 workflow output range check `:1971-1982` 同 style)。

**不增加任何 wire / manifest / core-layout section / snapshot 字段**(§12.15.8 no-version-bump verdict 不变):stash_base 是 ABI 常量 + N + imports flag 的纯算术,host 从 descriptor 既有字段推导。AHFLXM v2 manifest bytes 不变;core-layout section `format_version=3` 不变;recovery snapshot v2 不变。

#### 12.15.19.5 id 排序规则(interleaved completion / suspend / retry)

- **成功 run:** `workflow_completed_count == N`(`:2021` 校验)。host 按 `schedule_pos = 0..N-1` 顺序逐 node decode(P6 → O_k,opaque → stash),`node_facts.output` 赋值;lifecycle helper 的 `add_value`(`wasm_lifecycle.cpp:268-272`)按此顺序分配 node output id 0..N-1;workflow output id = N(`:300-304`)。与 evaluator 的 `0,1,...,N-1(node),N(workflow)` 一致。
- **cap-result id 交错:** evaluator 在每个 node 的 cap success 时分配 cap-result id(`workflow_runtime.cpp:1138`)、node output id(`:1432`)在后;wasm lifecycle helper 同序(cap_call output `:255-261` → node output `:268-272`)。stash 不改变这一交错——cap-result id 由 GAP 8(§12.15.18.1)的 event projection 保证,wasm lane 单次 invoke、evaluator 的 per-attempt event 由 attempts=N 合成。stash 只填 node output 这一格。
- **失败 run:** 仅 `i < workflow_completed_count` 的 node completed(`:2029` 校验 `<= N`)。host 只 decode completed node;trapping node 无 stash entry(scheduler 在 OK 校验之后才 stash,trapping node 未到 stash 点)、无 id——与 evaluator 一致(failed node 无 output id)。
- **suspend(PENDING):** scheduler 在 bump `workflow_completed_count` 之前 return(`:16856` / `:16962`),suspended node 无 stash entry。host 只 decode `i < workflow_completed_count` 的 node。resume 是 **fresh-instance whole-module replay**(§12.6):module 重新实例化、整个 schedule 重跑、memo replay 逐字喂回 `authoritative_json`(`workflow_session.cpp:1044-1053`),scheduler 确定性重跑 → stash 表逐字节重建(bump heap 分配顺序确定、cap bytes 逐字相同 → JSON bytes 落在相同地址)。host 在 replay run 的 run 后读重建后的 stash,与 origination run 得到相同的 per-node output 与 id 序列。**replay 不需要特殊 stash 处理——stash 每次 run 重建。**
- **retry:** wasm lane 不 retry(单次 invoke);cap 失败 → node 失败 → 无 stash entry、无 id。evaluator 的 retry 是 GAP 8 event-parity 范畴,与 stash 正交。

#### 12.15.19.6 guest codegen 发射(per opaque node)

在 `append_workflow_schedule` 的 opaque node 路径,runner 返回 OK 且全部 OK 校验通过之后、bump `workflow_completed_count` 之前,发射:

```
i32.const <stash_base + schedule_pos*8>   ; 编译期常量地址
local.get ptr_local                        ; runner 返回的 output ptr
i32.store align=2 offset=0
i32.const <stash_base + schedule_pos*8>
local.get len_local                        ; runner 返回的 output len
i32.store align=2 offset=4
```

约 16 bytes / opaque node(2 × `i32.const` + 2 × `local.get` + 2 × `i32.store`)。P6 node **不发射** stash store(沿用 O_k)。identity opaque 路径(`:16926-16940`)与 capability opaque 路径(`:16967-17016`)各加一处;PENDING / ERROR 路径不发射(未完成)。`stash_base` 作为编译期常量 bake 进 scheduler(与 `kNodeEventLogBase` 同等待遇)。

#### 12.15.19.7 host post-run join + replay

`workflow_session.cpp` 的 post-run 路径(§10/§11/§12)重构为统一 node loop:

1. 读 `workflow_completed_count` global(既有,`:2011-2014`)并校验(既有,`:2020-2036`)。
2. 若 workflow 至少有一个 opaque node:计算 `stash_base`(§12.15.19.4 公式),校验 `stash_base + 8*N <= heap_base`。
3. 按 `schedule_pos = 0..N-1` 逐 node:
   - `i >= workflow_completed_count` → 未完成,`node_outputs[i] = nullopt`,不触发 hook。
   - `is_p6` → 既有 O_k `read_value_at` 路径(`:1816-1838`)。
   - opaque → 读 stash slot `(ptr, len)`,range 校验(§12.15.19.4),`value_from_json(mem[ptr..ptr+len])`。
   - decode 失败(P6 `read_value_at` 或 opaque `value_from_json` 或 range 校验)→ **fail-closed** `run_ok = false; run_failure_code = kOutputDecodeFailed`(与 workflow output path 同一 fail-closed path,§12.15.7 决策不变)。
   - `node_outputs[i] = decoded`;触发 `node_completed_hook`(带真实 output,不再 `NoneValue`——顺带修复 identity workflow 不触发 hook 的既有 parity gap)。
4. lifecycle helper 按 schedule 顺序 `add_value` 分配 id 0..N-1,workflow output id N(既有机制,§12.15.19.5)。
5. **不依赖 event record:** node completion 与 output join 只依赖 `workflow_completed_count` + stash/O_k;event record 仍 decode 供 capability lifecycle(GAP 8),但不是 completion authority。identity workflow(无 event record)由此获得完整 per-node output。
6. **replay:** 同 §12.15.19.5——fresh-instance replay 重建 stash,host 在 replay run 后走同一 join。`decode_opaque_import_args` 的 arg-hash identity(`:1016-1035`)不变。
7. **无 host heap 新分配模式:** raw JSON bytes 直接从 `read_whole_memory()` 的 span 读(与 workflow output path `:1984-1987` 相同的 `std::string` 构造,既有模式);decoded `Value` 经 `add_value` 入 value store(既有)。无 bridge ABI 使用(stash 是 raw memory 表,不是 bridge call)。

#### 12.15.19.8 失败模式与 diagnostic ranges

| 失败 | 站点 | 行为 |
|------|------|------|
| stash slot `ptr == 0` 或 `len == 0`(completed opaque node) | host join | `kOutputDecodeFailed`(scheduler 的 OK-empty=ERROR 契约 `:16977-16985` 保证 completed node 必有非空 output;零 entry = corrupt) |
| stash slot `ptr + len > 65536` 或 `ptr < heap_base` | host join | `kOutputDecodeFailed`(forged/corrupt entry,与 workflow output range check 同) |
| `value_from_json` 失败(opaque) | host join | `kOutputDecodeFailed`(与 workflow output `:1991-1999` 同) |
| `read_value_at` 失败(P6) | host join | `kOutputDecodeFailed`(原 silent `NoneValue`,§12.15.7 fail-closed alignment) |
| `stash_base + 8*N > heap_base` | host join | `kOutputDecodeFailed`(corrupt module layout) |
| stash + event + 固定区域超 65536 | codegen | `kResourceExhausted` + SourceRange(Principle 5,§12.15.9 同 style) |

所有 diagnostic 附 node 的 SourceRange(经 §12.15.17 GAP 6 的 facade resolver 表),**不 echo guest 地址**(协调者 choreography 注记)。

#### 12.15.19.9 拒绝的替代方案

1. **(B) host-side join only(identity/canonical 从 entry tuple + input edges 重建;cap node 从 host cap result 重建)— REJECTED。** §12.15.19.1 已证伪:非 entry node 的 input 是 upstream output(host 无副本)、canonical compute node 的 output ≠ input(无法重建)、cap node 的 output 一般 ≠ cap result(`WasmCapabilityCall.output` 是 cap result 不是 node output)。仅单节点 pure-identity 成立。不可 sound。
2. **(C) hybrid(identity/canonical/P6 用 guest stash,capability node 用 host cap result)— REJECTED。** 同 (B) 的 cap-node unsoundness(cap-then-compute node 的 output 不是 cap result);且把 decode path 劈成 stash / cap-result 两条,违反 uniform decode path。stash 统一覆盖全部 opaque node。
3. **(A-variant) stash 全部 node(含 P6),host 从 stash decode 一切 — REJECTED。** P6 output 是 P4-D bytes(不是 JSON),host 仍需 `read_value_at`(P6)与 `value_from_json`(opaque)两个 decode 函数——stash P6 不消除分支。却给每个 P6 node 加 8 字节 slot + ~16 bytes code,并把每个 P6 workflow(含 pure-P6)的固定区域上移 8*N、破坏 byte-parity,零收益。收窄为 opaque-only。
4. **(D) per-node 固定 output region(把 JSON bytes 拷进 O_k 式固定块)— REJECTED。** JSON output 变长(unbounded String/collection 无静态上界),固定 region 无法 sizing。`(ptr,len)` 表 + bump-heap bytes 是变长 output 的正确模型(与 workflow output path 一致)。
5. **(E) 扩展 run2 multi-value 返回全部 node tuple — REJECTED。** 2*N 值的 multi-value return 不切实际(wasm3 multi-value 支持、ABI churn、scheduler local 生命周期)。固定表是 flat-store 正解(Principle 3)。
6. **(F) 把 per-node output 编进 node-event record(40→48 字节)— REJECTED。** identity workflow 不写 event record(这正是问题本身),扩展 record 救不了 identity workflow;且改 event record ABI 是 wire-format 变更。stash 与 event system 解耦。
7. **(G) 新增 core-layout section field 携带 stash_base — REJECTED。** stash_base 是 ABI 常量 + N + imports flag 的纯算术,host 从 descriptor 既有字段可推导;section field 冗余且改 section bytes。§12.15.8 no-version-bump 保持。

#### 12.15.19.10 成本 / LOC

| Component | LOC delta | Notes |
|-----------|-----------|-------|
| `core_wasm_codegen.cpp` | ~80 | stash store 发射(identity + capability 两条 opaque 路径)+ stash_base 计算 + `compute_event_layout` 扩展 + cursor 起点调整 |
| `core_wasm_abi_constants.hpp` | ~15 | `kNodeStashRecordBytes = 8` + 布局推导注释 |
| `workflow_session.cpp` | ~120 | 统一 node loop + stash base 计算 + range 校验 + opaque decode + fail-closed alignment + hook 修复 |
| `wasm_runner.cpp`(parity harness) | ~-25 | 删除 `strip_output_value_id` + 调用点(big-bang) |
| Tests | ~250 | GAP 4 conformance case(多 node identity + capability)+ fail-closed mutation pins(stash 越界 / 零 entry / JSON 损坏 / P6 decode 失败)+ replay stash 重建 |
| Docs | ~150 | 本节 |
| **Total** | **~590** | |

#### 12.15.19.11 验收标准

1. **GAP 4 conformance case(§12.15.10 case 5):** 多 node workflow(identity + capability node),wasm lane 的 `output_value_id` 序列与 evaluator 逐字节一致(0,1,...,N),`ahfl.run-report` byte-parity(不 strip)。
2. **parity harness stripping 删除(big-bang):** `wasm_runner.cpp:1223-1238` 的 `strip_output_value_id` 与 `:1335-1338` 调用点删除;`e3_identity_workflow`、`e3_capability_workflow`、`p2_14_diamond_dag` 三个 WireJson fixture 在**不 strip** 下 byte-parity。
3. **fail-closed pins:** (a) forged stash entry(ptr 越界)→ `kOutputDecodeFailed`;(b) completed opaque node 的 stash slot 为零 → `kOutputDecodeFailed`;(c) node-output JSON 损坏 → `kOutputDecodeFailed`(非 silent `NoneValue`);(d) P6 node-output decode 失败 → `kOutputDecodeFailed`(原 silent)。
4. **page capacity:** stash + event + 固定区域超 65536 的 workflow → `kResourceExhausted` + SourceRange。
5. **byte parity:** pure-P6 workflow(无 opaque node)module bytes 与现状逐字节相同(无 stash region、无 stash store)。**落地修订(2026-10-02):** conformance golden 只比较 observation 不比较 bytes,故 pure-P6 byte-identity 不能由"既有 P6 conformance golden"保证;`wasm_workflow_cap_binary_gate.py` 新增 pure-P6 freeze(`wh5c_instance_reuse_fanout.ahfl`,1513 bytes + SHA-256 + 无 import section),stash guard 一旦回退就 fail。
6. **replay:** 带 opaque node 的 suspend/resume workflow,resume run 的 stash 重建、`output_value_id` 序列与 evaluator 一致。
7. **census(落地校正 2026-10-02):** WH-5c.5 落地后 native `kExpectedAgreed = 72`(71→72,新增 `wh5c5_gap4_stash_parity` agreed case)、Node `kExpectedAgreed = 69` / `kExpectedSkipped = 3`。73 的最终 census 仍是 §12.15.17.4 的 WH-5c.7 后预测(5c.5 的 +1 在 68→72 批次内;5c.7 的 +1 → 73 待落地)。机制修订**新增** GAP 4 conformance case 到 corpus(§12.15.17.4 的 +4 批次已预测,但此前缺席),不是"不新增 case"。
8. **gates:** ASan build & test clean;WASM=OFF build clean;fresh build `-Werror` clean(CLAUDE.md memory:develop 可积累 -Werror breakage,commit 前 fresh build)。

##### 12.15.19.11.9 落地记录(2026-10-02):Node skip 理由纠偏 + review P2 处置

独立对抗评审判定机制正确(stash 算术/发射位置/host join/fail-closed/replay 全部核实),一处 P1 诚实性问题:

- **P1(已修):** `wh5c5_gap4_stash_parity` 是全 opaque(identity→capability→identity)**无 ahfl_xcode transcode site** 的 workflow,原 manifest 声明 `host_transcode_awaits_node_port` 是假标签(C++ Node runner 仅凭声明短路,真实原因是 Node oracle host 早于 WH-5c.5 stash-table join:不读 per-node stash slot,且 ok-null 归一化自检假设 capability 在 schedule position 0)。新增枚举 `WasmNodeObservationSkip::NodeHostAwaitsMultiNodeStashJoin`(manifest 字符串 `node_host_awaits_multinode_stash_join`),parser/eligibility/Node runner exact-set pin(`kExpectedMultiNodeStashStems`,与 `kExpectedHostTranscodeStems` 不相交)同步。两个 skip 理由永久分离:带 transcode site 的缺口不得挂 stash-join 标签,反之亦然。
- **P2-1(已修):** 见验收标准 5 的 pure-P6 byte-freeze gate。
- **P2-2(挂账):** host hook 顺序仍是"全部 state_entered → 全部 node_completed",不是 §12.9.4 修订的 per-node 顺序;属既有行为,GAP 4 未回退,不在本 slice 范围。
- **P2-3(挂账):** `workflow_session.cpp` cap-name 解析失败时静默归因 node 0(应 fail-closed);既有问题,parity harness 用 canonical `module::Echo` 名称绕开。
- **P2-4(已修):** descriptor identity 路径的 `1024 + n*8` 改为先 widen 64-bit 再乘,与 `encode_workflow_module` 一致。
- **P2-5(已修):** identity 路径"Identity P6 workflows have no opaque nodes anyway"过时注释更正(import-free 才是跳过 event decode 的真实原因)。

#### 12.15.19.12 AHFL-specific divergence + prior-decision preservation

**AHFL-specific divergence:** `(ptr,len)` stash 表本身是主流 flat-store 模式(Rust/Clang:index-based 固定表,无 string key)。divergence 在于 fixed 64 KiB single-page 约束(wasm3 embedded host):主流 wasm runtime 有 growable memory,per-node output 表会走 heap allocation;AHFL 的 fixed page 迫使编译期 region 规划 + fail-closed capacity gate。这与 §12.15.9(page capacity accounting)记录的 divergence 同源,不是新 divergence。

**Prior-decision preservation:**

- **§12.15.1-12.15.15:** 保留,不重写。§12.15.7 的 id 分配 / fail-closed / stripping 删除决策不变;仅 per-node output host 可读性机制被本节替换(§12.15.7 顶部已加 superseding pointer)。
- **§12.15.16(APPROACH B per-node blocks):** 保留。stash 与 per-node relocation 正交;P6 node 的 O_k 路径不变。
- **§12.15.17(GAP 6/7):** 保留。GAP 6 的 SourceRange resolver 被 stash 的 fail-closed diagnostic 复用;GAP 7 的 rich type matrix 与 stash 正交。
- **§12.15.18(GAP 8/9):** 保留。GAP 8 的 capability event projection 保证 cap-result id 交错(§12.15.19.5);GAP 9 的 String PtrLen carry 与 stash 正交。
- **§12.15.8(no version bump):** 不变。stash 不增加 wire / manifest / core-layout section / snapshot 字段。
- **§12.15.9(page capacity):** 保留,扩展。stash region 纳入同一 capacity check 家族(§12.15.19.4)。
- **§12.6/§12.14(memo/replay):** 保留。stash 在 fresh-instance replay 中确定性重建(§12.15.19.5),与 memo arg-hash identity 正交。
- **§12.7(WH-6 cutover,无 fallback):** 保留。WH-5c.5 仍按 §12.15.18.3 sequencing 排在 5c.4 后;gate set 不变(5c.1-5c.9)。

## 12.16 WH-6 fix-forward decision: post-lowering compile-diagnostic source rendering (2026-10-03, dedicated decision agent, no human gate)

### 12.16.1 背景与 P1

WH-6 把 `ahflc run` 从 tree-walking evaluator big-bang 切换到 vendored-wasm3 `WasmWorkflowRuntime` facade。独立对抗评审(582 pass / 3 environmental pnpm red #81/#84/#85 / 4 wasmtime skip #5/#7/#57/#60;census 73-0 + 69-4;byte-exact WASM=OFF refusal;ASan clean)返回 FIX-FORWARD + 单个 P1(review dimension 10):

facade ctor 编译 IR->Core->wasm,失败时把结构化诊断存入 `DiagnosticBag`(`src/runtime/wasm_runner/wasm_workflow_runtime.cpp:29-56` `record_compile_failure`;`compile_errors_` 是 `DiagnosticBag`,`wasm_workflow_runtime.hpp:129`)。每条 first-class 诊断携带 code + message + 有效 `SourceRange`(`.range(d.source_range)`,`:54`;codegen `add_diag` 设 code+message+source_range,`core_wasm_codegen.cpp:1209-1215`)。umbrella `wasm.compile-failed` 伴随 >=1 first-class ranged 诊断;`ahflc run` exit 1。数据模型 Principle-5 完整。

但 CLI 在 `src/tooling/cli/workflow_run.cpp:1903` 以 `result.diagnostics.render(err, std::nullopt, true)` 渲染——不传 SourceFile,诊断未预 stamp source_name/position。`DiagnosticBag::render`(`include/ahfl/base/support/diagnostics.hpp:1112-1114`)仅在 `diagnostic.source_name` 已设(未设)或传入单个 SourceFile(未传)时输出 `(file:line:col)`(`:1122-1133`);source snippet + caret(`:1138-1172`)仅在传入 SourceFile 时渲染。结果:只有 code+message,无 `(file:line:column)`、无 caret。§12.7.10 AC#5 要求 "lowering/codegen 错误以 SourceRange 诊断渲染 ... (非拼接字符串)"。

### 12.16.2 决策摘要

**选择 Option A:host-supplied diagnostic source locator seam(`WasmWorkflowRuntimeConfig::diagnostic_source_locator`)+ source identity 在生产点 stamp。**

facade 是可嵌入运行时(RFC 0020),不触碰 filesystem/frontend,无 source content。故 offset->line:col 的解析权交给 host:facade 把每条编译失败诊断绑定到 owning declaration 的 module_name,调 host locator 取 `{source_name, position}`,stamp 到 DiagnosticBuilder。render 保持 source-less;`(file:line:col)` 经已 stamp 字段渲染。single-file 传 primary SourceFile 出 caret;SourceGraph 不出 caret(line:col 仍渲染)。

### 12.16.3 已验证的代码事实(file:line 已对 HEAD 952f0c4d 复核)

1. **render API:** `DiagnosticBag::render(out, MaybeCRef<SourceFile> source = nullopt, include_code = false)`(`diagnostics.hpp:1112`)。`source_name`(+`position`)已设时输出 `(source_name:line:col)`(`:1122-1127`);否则若传入单个 SourceFile 且诊断有 range,输出 `(display_name:line:col)`(`:1128-1133`)。caret 仅在传入 SourceFile 时渲染(`:1138-1172`)。Builder 有 `.source_name(name, pos)` / `.position(pos)`(`:981-992`)。

2. **pipeline 诊断结构只携带 bare offset,无 source-unit id:** `CoreWasmDiagnostic { code, message, ir::SourceRangeOpt source_range }`(`core_wasm_codegen.hpp:58-62`);`CoreLowerDiagnostic { severity, code, message, SourceRangeOpt source_range }`(`core_ir.hpp:2017-2022`)。`SourceRange { begin_offset, end_offset }`(`source.hpp:36`)仅 offset;`SourceId` 存在(`source.hpp:13`)但不被这些结构携带。

3. **source identity 在上一层可用:** `DeclarationProvenance { module_name, source_path, source_range, id }`(`decl.hpp:64-69`)。每个 `*Decl`(含 `WorkflowDecl :354`)有 `.provenance`。`WorkflowNode`(`:344`)有 bare `source_range`,其 owning file 是 enclosing `WorkflowDecl` 的 provenance 标识的文件(workflow 声明在一个文件里)。

4. **Core lowering 保留 surface range:** `CoreWorkflowDecl::source_range = decl.provenance.source_range`(`core_lower.cpp:5038`);node input region 用 `decl.nodes[i].source_range`(`:5100-5101`);return region 用 `decl.provenance.source_range`(`:5104`)。codegen 诊断的 `statement.source_range`(`core_wasm_codegen.cpp:6429` 等)trace 到这些 surface range——**是 enclosing workflow 声明文件的 surface range,两种 invocation 形状均成立**。

5. **source content 只在 CLI/frontend 层:** `SourceUnit { ... SourceFile source; ... }`(`frontend.hpp:33-46`);`SourceGraph { entry_sources, sources, module_to_source, ... }`(`:54-59`)。run dispatch 在 `cli_driver.cpp:3986` 的 template `run_analysis<InputT>`(`:3827`)内。single-file(`InputT=ast::Program`)有 `source_file`(MaybeSourceFile,`:1760`);package/manifest(`InputT=SourceGraph`)`source_file` 是 nullopt,但 `input`(SourceGraph&)持有每个文件的 SourceFile。**主产品门 #213/#214/#425 走 manifest = SourceGraph 形状。**

6. **module_name 是 IR->frontend 的 sound join key:** HIR lowerer 的 `current_module_name_ = source.module_name`(`typed_hir_lower.cpp:513`),`provenance.module_name = current_module_name_`(`:1441`)。SourceGraph 的 `module_to_source` 以 module_name 为 key(`project.cpp:745`),且 frontend 强制 module_name 唯一(duplicate module owner 报错,`:725-731`)。故 `provenance.module_name` 唯一标识 SourceGraph 中的 owning SourceUnit。

7. **既有 multi-file 诊断先例(stamp at production):** typecheck(`typecheck.cpp:1195-1198`)与 resolver(`resolver.cpp:3852-3855`)在生产点 stamp `source_name = src_unit.source.display_name` + `position = src_unit.source.locate(range.begin_offset)`,CLI 以 source-less render 得到 `(file:line:col)`。这是 AHFL 既有模式。

8. **run seam:** `run_workflow_with_llm(const ir::Program&, const CommandLineOptions&, ostream&, ostream&)`(`workflow_run.hpp:10-13`),WASM=ON 定义于 `workflow_run.cpp:1590`,WASM=OFF refusal stub 于 `:1919-1927`。§12.7.1 明文冻结 "函数签名 ... 不变"(`kr68 doc:1147`)。本决策修订该条款。

9. **WH-5c.6 runtime 诊断同病:** session config 的 `node_range_resolver` / `capability_range_resolver`(`workflow_session.hpp:77-88`)产出 bare SourceRange,经同一 `render(err, nullopt, true)` 渲染——同样无 line:col。

10. **emit-wasm 更差:** `ahflc emit wasm` 把诊断 flatten 成 `diag.code + ": " + diag.message`,丢弃 range(`driver.cpp:210-244`)。

### 12.16.4 数据流(谁 stamp、谁 resolve、caret)

**source identity 在生产点 stamp(知道文件的一方):**

- **codegen 阶段(per-workflow):** facade ctor 已建 `ProgramIndex` 并遍历 `prog_index.workflows()`(`wasm_workflow_runtime.cpp:79-88`)。新增 `workflow_module_by_name_`(workflow name -> provenance.module_name)。codegen 失败点(`:117-130`)有 `wf.name`,查 map 得 module_name。**无需 Core 层改动。**
- **lowering/layout 阶段(whole-program):** `CoreLowerDiagnostic` 新增 `std::string source_module`(空 = unknown)。core_lower / core_layout / core_frame_layout 在生产点 stamp enclosing decl 的 `provenance.module_name`。facade 的 lowering/layout `record_compile_failure` 逐条读 `d.source_module`。

**offset->line:col 由 host resolve(facade 无 source content):**

- `WasmWorkflowRuntimeConfig` 新增 `diagnostic_source_locator`(见 §12.16.5)。facade 在 `record_compile_failure` 时对每条诊断调 `locator(module_name, range)`,把返回的 `{source_name, position}` stamp 到 DiagnosticBuilder(`.source_name(label.source_name, label.position)`)。
- locator 未设(embedder 无 source content):stamp `source_name = module_name`(best-effort 标签,无 line:col)。module_name 为空则不 stamp。诚实降级。

**render 保持 source-less:**

- `workflow_run.cpp:1903` 改为 `result.diagnostics.render(err, source_context.primary_source, true)`。
- single-file:`primary_source` 是该 SourceFile -> `(file:line:col)` 经 stamp 字段 + caret 经 primary_source。
- SourceGraph:`primary_source` 是 nullopt -> `(file:line:col)` 经 stamp 字段,无 caret(与 frontend multi-file 诊断行为一致)。

### 12.16.5 seam 形状 + §12.7.1 签名修订

**(a) `src/runtime/wasm_runner/wasm_workflow_runtime.hpp`:**

```cpp
// Host-resolved source location for a compile diagnostic. The embeddable
// facade has no source content (RFC 0020); the host resolves the bare offset
// to line:col and supplies the display label.
struct LocatedDiagnosticSource {
    std::string source_name;                  // host display label
    std::optional<SourcePosition> position;   // line:col; nullopt if unresolvable
};

// In WasmWorkflowRuntimeConfig:
// WH-6 fix-forward (kr68 §12.16): host-supplied locator for compile-failure
// diagnostics. Given the owning declaration's module name + the diagnostic's
// bare SourceRange, resolves the display label + line:col. The facade stamps
// the result onto each compile diagnostic. When unset, the facade stamps
// source_name = module_name (best-effort, no line:col).
std::function<std::optional<LocatedDiagnosticSource>(
    std::string_view module_name, SourceRange range)>
    diagnostic_source_locator;
```

**(b) `include/ahfl/compiler/ir/core_ir.hpp`:** `CoreLowerDiagnostic` 新增 `std::string source_module;`(空 = unknown)。

**(c) `src/tooling/cli/workflow_run.hpp`:** 新增 `WorkflowRunSourceContext` + 修订签名:

```cpp
#include "runtime/wasm_runner/wasm_workflow_runtime.hpp"  // LocatedDiagnosticSource

struct WorkflowRunSourceContext {
    // Installed on WasmWorkflowRuntimeConfig::diagnostic_source_locator.
    std::function<std::optional<ahfl::runtime::wasm_runner::LocatedDiagnosticSource>(
        std::string_view module_name, ahfl::SourceRange range)>
        locate_compile_diagnostic;
    // Single-file runs: the one SourceFile for caret rendering.
    // SourceGraph runs: nullopt (line:col still renders; no caret).
    std::optional<std::reference_wrapper<const ahfl::SourceFile>> primary_source;
};

[[nodiscard]] int run_workflow_with_llm(const ahfl::ir::Program &program,
                                        const CommandLineOptions &options,
                                        std::ostream &out,
                                        std::ostream &err,
                                        const WorkflowRunSourceContext &source_context = {});
```

**(d) `src/tooling/cli/workflow_run.cpp`:** 把 `source_context.locate_compile_diagnostic` 装到 `runtime_config.diagnostic_source_locator`;`:1903` render 改传 `source_context.primary_source`。

**(e) `src/tooling/cli/cli_driver.cpp:3986`:** 在 template 内构造 `WorkflowRunSourceContext` 并传入:
- `InputT=ast::Program`(single-file):locator 捕获 `source_file->get()`,返回 `{src.display_name, src.locate(range.begin_offset)}`;`primary_source = source_file`。
- `InputT=SourceGraph`(manifest):locator 捕获 `input`(SourceGraph&),按 module_name 查 `module_to_source` -> `SourceUnit.source`(linear scan by id 或直接按 module_name 匹配),返回 `{unit.source.display_name, unit.source.locate(...)}`;`primary_source = nullopt`。

**(f) WASM=OFF stub(`workflow_run.cpp:1919-1927`):** 签名加 `const WorkflowRunSourceContext&` 参数(unnamed),stub 忽略它(在编译前拒绝)。stub 仍编译干净(AHFL_ENABLE_BACKEND_WASM=OFF)。

**§12.7.1 签名修订(2026-10-03,本决策):** §12.7.1(`kr68 doc:1147`)原冻结 "函数签名、`cli_driver.cpp:3986` 的 dispatch、usage 校验(在 gate 内)不变。" 现修订为:`run_workflow_with_llm` 签名新增 `const WorkflowRunSourceContext& source_context` 参数(默认 `{}`);`cli_driver.cpp:3986` dispatch 改为传入构造的 context;usage 校验仍不变。gate 边界、WASM=OFF 拒绝策略、零业务逻辑 #ifdef 等其余 §12.7.1 条款**不变**。修订理由:AC#5 要求 line:col 渲染,而 source content 只在 CLI 层;不经签名传入 source context,facade 无法 resolve offset->line:col(可嵌入约束,RFC 0020)。

### 12.16.6 范围边界(不并入)

**WH-5c.6 runtime 诊断(node/capability 执行失败):不并入本 slice。** 理由:① AC#5 针对 "lowering/codegen 错误"(编译期),非运行时失败;② runtime 诊断由 session(`wasm_host`)生产,经 `node_range_resolver`/`capability_range_resolver` 得 bare range,其 owning file 是 workflow 文件(node)或 **capability 文件**(capability,可能与 workflow 文件不同)——facade 无法正确 stamp capability 诊断的文件;③ evaluator 车道有同样的 runtime 渲染(无 line:col),非 WH-6 回归。locator seam 形状可复用:未来 slice 可给 `WorkflowSessionConfig` 加同类 locator(或让 resolver 返回 module 身份),无需重开本决策。

**`ahflc emit wasm` flattening:不并入。** emit 路径是独立 CLI 动词,其 flatten(`driver.cpp:210-244`)是既有行为,非 WH-6 回归。facade 的 structured bag 已是更优模式;emit-wasm 采用它是独立 slice。

### 12.16.7 拒绝的替代方案

- **Option B(thread a source registry into the run seam + multi-file render):** 改共享 `DiagnosticBag::render` API(每个 stage 都用)接受 registry——blast radius 大;stamp-at-production(Option A)匹配 typecheck/resolver 既有先例;A 已给两种形状 line:col,B 额外给的 multi-file caret 是既有 graph 路径也没有的能力。over-engineered,REJECT。
- **Option C(facade owns source content):** 违反 RFC 0020(可嵌入运行时不触碰 filesystem/frontend);其他 embedder 无 CLI/source file 也驱动 `WasmWorkflowRuntime`;把 content 传进 facade 会 (a) 撑大 config,(b) 让 facade 重复 `SourceFile::locate`,(c) 耦合运行时层与 source content 所有权。REJECT。
- **Option D(keep current + rescope AC#5):** 把 "SourceRange 诊断" 重定义为 "数据模型有 range",把 line:col 推到未来 slice。这是为实现 downgrade AC:wasm ctor 可能失败而 evaluator ctor 从不失败(新用户可见失败模式);用户看到 `error [wasm.UNSUPPORTED_CAPABILITY_FRAME]: <message>` 却不知源码位置,违反 Principle 5(诊断带 SourceRange 且用户可见)与 DSL 编译器的核心诊断契约。frontend 各 stage 都渲染 line:col,post-lowering stage 不渲染是不一致,非设计。REJECT。

### 12.16.8 AHFL-specific divergence

1. **join key 是 module_name(string),非 FileID(index)。** Clang 的 backend 诊断携带 FileID + offset,host SourceManager 解析。AHFL 的 IR 层刻意丢弃 frontend 的 SourceId(`DeclarationProvenance` 携带 module_name,不携带 SourceId;`typed_hir_lower.cpp:1431-1445` 有 `current_source_id_` 但不存进 provenance),且 frontend 的 `module_to_source` 已以 module_name 为 key。故 join key 是 module_name——source-level 名(Principle 2 允许 string 用于 source-level 名/诊断标签),facade 不建 string-keyed 内部 store,只把 module_name 传给 host callback;host 经其既有 index(SourceId)resolve。内部身份在 host 侧仍是 index-based。把 SourceId 加进 `DeclarationProvenance` 是更大的 IR 变更,本 slice 无消费者 beyond 此诊断路径,不做。

2. **stamp-at-production + host-resolves-position 混合。** AHFL 既有模式(typecheck/resolver)是生产点 stamp source_name+position(有 SourceUnit);Clang 是 render 时经 SourceManager resolve。wasm facade 是可嵌入运行时(RFC 0020),无 source content,不能在生产点 stamp position——故委托 host locator resolve position。这是对 "stamp at production" 模式的 AHFL-specific 适配:source identity 在生产点 stamp,position 经 host callback resolve。

### 12.16.9 成本 / LOC

| Component | LOC delta | Notes |
|-----------|-----------|-------|
| `wasm_workflow_runtime.hpp` | ~25 | `LocatedDiagnosticSource` + `diagnostic_source_locator` config 字段 |
| `wasm_workflow_runtime.cpp` | ~50 | `workflow_module_by_name_` 构建 + `record_compile_failure` locator stamping(codegen per-workflow;lowering/layout per-diag source_module) |
| `core_ir.hpp` | ~3 | `CoreLowerDiagnostic::source_module` 字段 |
| `core_lower.cpp` + `core_layout.cpp` + `core_frame_layout.cpp` | ~40 | stamp `source_module`(ExprLowerer 加 module_name_ 成员 + 3 构造点 + error() helper;直接 push_back 站点 stamp enclosing decl module_name) |
| `workflow_run.hpp` | ~20 | `WorkflowRunSourceContext` + 签名修订 |
| `workflow_run.cpp` | ~10 | 装 locator + render 传 primary_source + stub 参数 |
| `cli_driver.cpp` | ~30 | template 内构造 `WorkflowRunSourceContext`(两分支) |
| Tests | ~120 | single-file + manifest 两个 CLI codegen-failure line:col 测试(见 §12.16.10) |
| Docs | ~150 | 本节 |
| **Total** | **~450** | |

### 12.16.10 验收 / 验证标准

1. **single-file codegen-failure line:col:** 用 `tests/golden/wasm/wh5c9_string_construct_fail_closed.ahfl`(`kUnsupportedCapabilityFrame`,codegen 阶段)跑 `ahflc run <file> --input <valid Frame JSON> [--llm-config ...]`,断言 stderr 含 `wasm.compile-failed` umbrella **且** >=1 first-class `wasm.UNSUPPORTED_CAPABILITY_FRAME` 诊断带 `(<display_name>:<line>:<col>)`(regex `\([^)]+:[0-9]+:[0-9]+\)`),exit 1。
2. **manifest/package codegen-failure line:col:** 新建 package fixture(ahfl.toml + 含 codegen-failing workflow 的 .ahfl,module_name 与 manifest 一致)跑 `ahflc run --manifest <toml> --sysroot ... --input ...`,断言 stderr 同样含 umbrella + first-class 诊断带 `(<package file display_name>:<line>:<col>)`,exit 1。**这是主产品门形状(SourceGraph),必须覆盖。**
3. **umbrella 不孤立:** 两个测试都断言 `wasm.compile-failed` 不单独出现(伴随 >=1 first-class ranged 诊断)。
4. **caret(single-file):** single-file 测试断言 stderr 含 source snippet + caret 行(`  N | ...` / `  ~~~`)。
5. **WASM=OFF:** `ahflc run` 仍打印 §12.7.1 byte-exact 拒绝诊断 + exit 1;stub 编译干净(AHFL_ENABLE_BACKEND_WASM=OFF,零 warning)。
6. **embedder 无 locator:** 单元测试:不设 `diagnostic_source_locator` 构造 facade,编译失败诊断仍带 code+message+range,`source_name` 退化为 module_name(或空),无 position——诚实降级,不崩溃。
7. **全量 ctest:** 仅 #81/#84/#85(pnpm environmental)red + #5/#7/#57/#60(wasmtime)skip;无新增 red/skip。
8. **ASan + `-Werror`:** build & test clean;fresh dev build 零 warning(CLAUDE.md memory:develop 可积累 -Werror breakage,commit 前 fresh build)。

### 12.16.11 prior-decision preservation

- **§12.7.1(WASM=OFF 产品策略):** 保留,仅修订 "函数签名不变" 条款(见 §12.16.5)。WASM=OFF 拒绝策略、gate 边界、零业务逻辑 #ifdef 不变。
- **§12.7.2(facade config 诚实子集):** 保留,扩展。`diagnostic_source_locator` 是 host-supplied seam(与 hooks/invoker/name_resolver 同类),非 evaluator-only 概念。
- **§12.7.4(退出码/报告/编译错误):** 保留。ctor 编译失败仍存 DiagnosticBag + run() 返回失败 WorkflowResult + exit 1;本决策只补 source rendering。
- **§12.7.8(facade ctor 编译失败语义):** 保留,扩展。DiagnosticBag 升级不变;本决策补 source_name+position stamping。
- **§12.7.10 AC#5:** 满足(非 rescope)——line:col 对 single-file + SourceGraph 均渲染。
- **§12.15.17.1(WH-5c.6 range resolvers):** 保留。runtime 诊断的 line:col 是独立 slice(§12.16.6)。
- **无 parallel path / 无 flag:** locator 是 config 字段(embedder 可选),非 engine-select flag;无新旧渲染路径共存。

### 12.16.12 Coordinator 修订(2026-10-03):codegen 阶段按 owning decl(agent/workflow)精确归属 module,弃用 workflow-name 启发式

§12.16.4/§12.16.5 原定 codegen 阶段由 facade 建 `workflow_module_by_name_`(workflow name -> workflow provenance.module_name)给整条模块的诊断 stamp module,并声称 "无需 Core 层改动"。Coordinator 在 builder 前独立复核发现该归属对 **跨 module 编排** 不正确,且 owning decl 必须按 reject 的真实词法宿主区分:

- codegen reject(如 `kUnsupportedCapabilityFrame` 的语句 reject)发生在 **handler body 语句**上,而 body 的词法宿主是 **`flow for X { ... }` 块 = `ir::FlowDecl`**,不是 agent 声明本身。仓库内实证跨文件形状:`tests/integration/workflow_value_flow` 中 `pub agent AliasAgent` 声明在 `lib/agents.ahfl`(module `lib::agents`),而 `flow for agents::AliasAgent { ... }` 写在 `app/main.ahfl`(module `app::main`),workflow 也在 app 侧。即 agent 声明文件、flow body 文件、workflow 文件三者可两两不同。用 entry workflow 的 module(原启发式)去 locate flow 体内语句的 offset,会渲染出**错误文件的错误行号**;用 agent decl 的 module(本修订初稿)同样错误。违反 Principle 5;`SourceFile::locate` 对越界 offset 静默 clamp(source.hpp:60),属静默误诊。
- 正确归属在 codegen 层可拿到:`build_agent_plan` 在 `core_wasm_codegen.cpp:1140` 已解析 `const CoreFlowDecl *flow = unique_target_flow(program, target)`,所有 handler/body 规划都在该 flow 之下;scheduler 侧 reject 属 `CoreWorkflowDecl`。但 **`CoreFlowDecl` 当前不携带任何 provenance**(core_ir.hpp:1292-1298 只有 target/agent_name/target_ref/storage/states),其 IR 源 `ir::FlowDecl` 有完整 `DeclarationProvenance`(decl.hpp:321),lowering 构造点在 core_lower.cpp:5888/5912/5939。
- codegen 持有 owning decl 的其他位点:planner 全程可及 `const CoreAgentDecl &agent`(agent 骨架级 reject 属 agent 文件,如 entry/flow 解析失败);frame-bridge 规划遍历的 `const CoreCapabilityDecl &capability`(`:10467`,capability ABI/schema 级 reject 属 capability 文件);`build_workflow_plan`(`:13534`)持有 `const CoreWorkflowDecl &workflow`。

**修订(同属 Option A,仅改 source-identity 的生产粒度;seam/签名/数据流其余不变):**

1. `source_module` 加在四类 Core 声明 + 两类诊断载体上,均为末位字段:
   - `CoreFlowDecl`(新增;handler-body reject 的真实宿主)——`std::string source_module`;
   - `CoreAgentDecl`(agent 骨架级 reject)、`CoreWorkflowDecl`(scheduler/workflow region reject)、`CoreCapabilityDecl`(capability ABI/import 级 reject);
   - 诊断载体 `CoreLowerDiagnostic`(§12.16.5(b),verifier 复用它,见 core_verify.hpp:251)与 `CoreWasmDiagnostic`。
2. `core_lower.cpp` 在各生产点 stamp `decl.provenance.module_name`:`CoreFlowDecl` 构造点 :5888/:5912/:5939(`ir::FlowDecl::provenance` 在作用域);agent `lower_agent` :1896 旁;workflow :5038/:5100-5104 旁;capability `:1974` 同构。**`CoreLowerDiagnostic` 的全部生产站点(core_lower/core_layout/core_frame_layout/core_wire_migration/core_wire_schema/core_verify 共 6 文件)必须逐站点 stamp enclosing decl 的 module_name。** 警告:`source_module` 给默认空值后,`CoreLowerDiagnostic{sev, code, msg, range}` 旧聚合初始化仍可编译(末位缺省 = 空),会**静默丢归属**——builder 必须 grep 每个构造点逐一填值,无 enclosing decl 的 whole-program 级诊断才允许留空。
3. codegen 侧采用 **scoped owning-module** 机制(不逐站点加参数):`CoreWasmCodegenResult` 增 `std::string active_source_module`;`add_diag`(`:1209`)push 时把它拷入 `CoreWasmDiagnostic::source_module`。RAII scope 在 `build_workflow_plan` 取 workflow.module、在 `build_agent_plan` 于 :1140 解析 flow 后取 **flow.source_module**(workflow 内嵌套规划打包 agent 时 RAII 自然恢复外层 workflow module);emit 入口 verifier/layout 拒绝在任何 scope 外,module 取 `first.source_module`(此时 `CoreLowerDiagnostic` 已带 module);确无归属才空。capability 内在 reject(如 :10467-10483)允许经 `add_diag` 的可选末位 override 参数传 `capability.source_module`,但不强制——它们多无 range,留空 fail-closed 可接受。
4. facade 对 lower/layout/codegen **统一**逐条读 `d.source_module` 调 host locator——**删除** §12.16.4/§12.16.9 中 `workflow_module_by_name_` 的 workflow-name 启发式(无该成员、无该 ~LOC)。
5. **host locator 必须 fail-closed 校验 offset:** CLI locator 按 module_name 解析到 SourceUnit 后,仅当 `range.begin_offset <= unit.source.content.size()`(建议同时校验 end)才返回 position;越界返回 `nullopt`(退化为只显示 source_name/module 标签,不编造行号)。locator 缺省或返回 nullopt 但 module_name 非空时,facade 仍 stamp `source_name = module_name`(无 position)。这同时兜底任何残余归属缺口。
6. **序列化/相等性 big-bang 同步:** 四个声明新字段须同步 `src/compiler/ir/core_json.cpp` writer+reader+key-set(flow: print_flows :1247/read_flows :2321;agent :1223/:3232 区;capability :1169/:3206 区;workflow :1409/:3641 区;key 集合如 :2555/:3168/:3228/:3499),否则 #124 core_json_round_trip(golden 全带非空 module)相等性崩;out-of-line `operator==`(CoreFlowDecl core_lower.cpp:214、CoreWorkflowDecl :236)加字段比较;`CoreAgentDecl`/`CoreCapabilityDecl` 为 defaulted == 自动覆盖。
7. **验收增补第 9 条(跨文件 flow 归属):** package fixture——module B(`lib`)声明 agent 且 **`flow for` body 写在 module B 文件**并在 body 内触发 codegen reject;module A(`app`)声明 workflow 经 `import B` 以 `B::Agent(input)` 引用(仿 workflow_value_flow 的 lib/app + ahfl.toml 形状)。断言诊断 `(file:line:col)` 锚在 **module B 的 flow 文件**正确行(不是 module A 的 workflow 文件)。同时 builder 以单元测试或第二 fixture 覆盖 "agent 在 B、flow 在 A、workflow 在 A" 形状,锚在 A(flow 文件),锁死三类文件区分。

理由:本修订不改变所选 option、locator seam、§12.7.1 签名修订或 render 路径,仅把 module 键的来源从 "入口 workflow" 精确到 "被拒绝语句的真实词法宿主 decl(flow/agent/workflow/capability)",以杜绝跨文件静默误诊;RAII scope 比 80+ 个 add_diag 站点逐参数改动更简单且不可遗漏。属对已记录数据流的事实性修正(coordinator 对 HEAD 952f0c4d 独立核实,workflow_value_flow 为仓库内实证),非新设计分叉。

### 12.16.13 Coordinator 复核记录(2026-10-03):对抗 re-review 裁决 + 两项接受的连带修正与一项已知边界

独立对抗 re-review(builder 后,HEAD 952f0c4d 未提交工作树)裁决 **FIX-FORWARD**,0 P0 / 1 P1 / 3 P2。Coordinator 裁决:

**P1(必修,fix-forward):** §12.16.12 第 7 条要求的两种跨文件形状只落地了形状 (i)(agent + flow 同在 lib 文件),形状 (ii)("agent 在 B、flow 在 A、workflow 在 A",仓库内 workflow_value_flow 的真实布局)缺失。形状 (i) 的锚点无法区分正确的 flow 归属与回归成 agent 归属(文件相同,regex 行号为 `[0-9]+`)。codegen 实现本身正确(RAII 以 `flow->source_module` 为键),但无测试锁死。处置:新增第二 package fixture(agent 在 lib、`flow for` body 在 app/main、workflow 在 app),断言 codegen reject 锚在 **app 的 flow 文件**,以测试区分三类文件。

**接受的连带修正 1(P2,记录在案):single-file/裸文件路径的 provenance.module_name 补全。** builder 在 `src/compiler/ir/typed_hir_lower.cpp` `current_provenance()` 解除了 "必须同时有 current_source_id_ 才产出 module_name" 的耦合:detached bare-file(query engine 单文件 `ahflc run`)路径下 typed decl 无 SourceId,旧逻辑使全部 `provenance.module_name` 为空,进而 CoreFlowDecl.source_module 为空,§12.16.10 AC#6 的 module-label 降级与本 slice 全部单文件归属都不成立。修正后:module_name 只要 current_module_name_ 非空即产出;source_path 仍仅在有 SourceId 时产出(保持空)。blast radius 经实测中性:仅影响 source_id 缺失的裸文件 decl;20/20 emit golden(ir/ir_json/summary/native_json)零漂移;SMV 只消费 source_path(仍空);裸文件 `emit ir` 现打印 `source=` 空的 provenance 标签,纯外观。此修正在此记录为 §12.16 的必要组成部分(非新分叉):source module 是 source-level 诊断标签,在单文件路径同样必须存在。

**接受的连带修正 2(P2,记录在案):单文件车道 WH-5c.6 运行时诊断被动获得 line:col+caret。** compile/runtime 共用 run 尾部 `render(err, primary_source, true)`;单文件车道 primary_source 非空,render 的 SourceFile fallback 分支使 node/capability 运行时诊断也渲染 `(file:line:col)` + caret。经核实正确且安全:单文件下 node range 与 capability range 均偏移进同一主文件(裸文件不能 import 其他用户文件;stdlib capability 无用户文件 range);SourceGraph 车道 primary_source 为 nullopt,行为完全不变——§12.16.6 担心的跨文件 capability 误诊在 manifest 车道不发生。这是朝 Principle 5 的被动改善,予以接受;**manifest 车道运行时诊断的 line:col(需经 session locator 解析 node=workflow 文件 / capability=各自文件)仍是独立未来 slice,§12.16.6 边界继续有效。**

**已知边界(P2,后续):CoreFnDecl 无 source_module。** core_verify 的 SourceModuleScope 覆盖 agent/capability/flow/workflow;`verify_fns()` 的 fn 级诊断有 enclosing decl 但 CoreFnDecl 不在本 slice 四类声明内,故其 module 为空(whole-program 标签降级)。fn 级 verifier 诊断的跨文件归属留待未来需要时按同一模式给 CoreFnDecl 加 source_module。

### 12.16.14 Coordinator P1 关闭记录(2026-10-03):形状 (ii) fixture 落地与独立验证

§12.16.13 的唯一 P1 已修复并经 coordinator 独立验证(HEAD 952f0c4d 未提交工作树):

- 新增第二 package fixture `tests/integration/wh6_codegen_diag_flow_app/`(package `wh6-diag2-lib` / `wh6-diag2-app`):lib 只声明 `pub agent CrossAgent`(无 `flow for`);app/main.ahfl(module `app::main`)内含 `flow for agents::CrossAgent` body(第 8 行第 9 列 `ctx.carried = w.inner;` 触发 wh5c9 fail-closed codegen reject)与 `pub workflow CrossWorkflow`。这是 workflow_value_flow 三方布局(agent 在 B、flow 在 A、workflow 在 A)叠加 fail-closed 触发。
- ctest `ahflc.run.manifest.wh6_codegen_diag.flow_app_source`(ProjectTests.cmake)EXPECTED_REGEX = `wasm\.UNSUPPORTED_CAPABILITY_FRAME.*app/main\.ahfl:[0-9]+:[0-9]+`。若归属回归为 agent 键,锚点会变成 lib/agents.ahfl,regex 失败——三类文件区分被测试锁死。
- Coordinator 独立复跑:`ahflc check --target workflow` 退出 0(失败确实发生在 codegen 而非前端);`ahflc run` 退出 1,诊断实测锚 `app/main.ahfl:8:9`(逐字符核对:第 8 行 8 空格缩进,`ctx.carried` 起于第 9 列),无 caret(SourceGraph 车道,primary_source=nullopt,正确);形状 (i) `flow_lib_source` 与三个单文件 ctest、`ahfl.runtime.wasm_runner` 共 6/6 通过。
- 新 fixture 不进 pinned_core_corpus(包级 codegen-failure 用例,非 core-IR golden 语料);无任何测试/脚本 GLOB 枚举 tests/integration 包(仅 AhflTesting 的 *with_package* golden glob 与 release-evidence 硬编码路径,均不触及新目录)。

至此 §12.16.12 第 7 条两种形状齐备,P1 关闭;0 P0 / 0 P1,P2-1/P2-2 已在 §12.16.13 接受,P2-3(CoreFnDecl)记入 backlog。WH-6 fix-forward 具备落地条件。

## 12.17 WH-6 落地记录(2026-10-03):`ahflc run` 全切到 wasm3 facade + 编译诊断 source-render seam

KR6.8 WH-6 在 HEAD 952f0c4d 之上 LANDED(builder -> 独立对抗 review -> fix-forward -> 独立对抗 re-review -> coordinator 独立验证;无人类 owner 门;§12.7 cutover + §12.16 诊断 seam 同一次提交 big-bang)。

### 12.17.1 实际落地形状

1. **单边界 cutover(§12.7.1/§12.7.3):** `src/tooling/cli/workflow_run.cpp` 的 `run_workflow_with_llm` 整个函数体由单一 `#ifdef AHFL_ENABLE_BACKEND_WASM` 门控;WASM=ON 走 `WasmWorkflowRuntime` facade(诚实子集 config,无逐字段镜像),WASM=OFF 为 byte-exact 拒绝 stub:`error: ahflc run requires the embedded wasm engine; this build was configured with -DAHFL_ENABLE_BACKEND_WASM=OFF. Rebuild with the default (ON) to run workflows.`(exit 1),其余 ahflc 动词(check/emit/...)在 WASM=OFF 完全可用。`src/tooling/cli/CMakeLists.txt` 仅在 WASM=ON PRIVATE 链接 `ahfl_runtime_wasm_runner` 并传播同名 compile definition;无 engine-select flag、无 evaluator fallback、无并行路径。`run_workflow_with_llm` 全仓唯一调用点 cli_driver.cpp 显式传 SourceContext,无默认参数降级。
2. **编译诊断 source-render seam(§12.16/§12.16.12):** 后 lowering 阶段(lower/layout/codegen)诊断在**生产点**按词法宿主 decl 打 `source_module`:CoreAgentDecl/CoreCapabilityDecl/CoreFlowDecl/CoreWorkflowDecl 及 CoreLowerDiagnostic/CoreWasmDiagnostic 新尾字段(IR JSON writer/reader/required-key/out-of-line operator== big-bang 同步,3 个 core golden 重新生成);codegen 用 RAII `ActiveSourceModuleScope`(flow 作用域装在 build_agent_plan 找到唯一 target flow 之后,workflow 作用域覆盖 build_workflow_plan 与 emit 编码,嵌套正确恢复),capability intrinsic 站点以 capability.source_module override,whole-program/byte-level wire 站点故意空 `{}` 并注释。host 经 `WasmWorkflowRuntimeConfig::diagnostic_source_locator(module, range) -> {source_name, position?}` 解析 offset->line:col,**fail-closed 边界校验**(越界返回 nullopt,退化为 module 标签,不编造行号)。CLI 单文件车道传 primary SourceFile(render 出 `(file:line:col)` + caret),SourceGraph 车道按 provenance.module_name 经 module_to_source 解析(出 line:col,无 caret)。
3. **flow for 归属(§12.16.12 核心):** handler-body codegen reject 归属**词法宿主 `flow for X {}` 声明**而非 agent 声明——二者可在不同文件(实证 `tests/integration/workflow_value_flow`:agent 在 lib,flow 在 app)。两类跨文件形状均有 ctest 锁死:形状 (i) `wh6_codegen_diag_flow_lib`(agent+flow 同在 lib,锚 lib/agents.ahfl),形状 (ii) `wh6_codegen_diag_flow_app`(agent 在 lib、flow+workflow 在 app,锚 app/main.ahfl,agent 键回归必挂)。
4. **连带修正(§12.16.13 已接受):** detached bare-file query 路径解除 source_id 与 module_name 的耦合(单文件诊断归属的前提,emit golden 零漂移);单文件车道 WH-5c.6 运行时诊断被动获得正确 line:col+caret(manifest 车道 primary_source=nullopt,行为不变)。
5. **删除/零冗余:** facade 侧 record 辅助函数从自由函数改为成员函数(需读 config_);原 `auto &&builder = bag.error()...` 悬垂临时引用(double-free)在本 slice 修复为具名局部变量;无新增死代码。

### 12.17.2 验证证据(coordinator 独立复跑)

- 强制全量重编译(touch 4 个变更 public 头)零 warning(`-Wall -Wextra -Werror`)。
- 定向 ctest 全绿:两个 manifest wh6_codegen_diag(#218/#219)、三个单文件 wh6_codegen_diag(#432-434)、`ahfl.runtime.wasm_runner`(273 checks)、core_json_round_trip、emit golden 20/20。
- 实测锚点:单文件 `wh5c9_string_construct_fail_closed.ahfl:37:9` 带 caret;manifest 形状 (i) `lib/agents.ahfl:18:9` 无 caret;形状 (ii) `app/main.ahfl:8:9` 无 caret(逐字符核对行:列)。
- WASM=OFF:184 target 干净构建,refusal byte-exact,exit 1。
- 完整无标签 `ctest --preset test-dev` 与 ASan preset 的结果记录于本提交后运行(见提交报告);#283/#284 证据文件于 post-commit 树重跑刷新(gitignored,compute_source_revision 含 untracked 内容,故必须在提交后同一棵树运行)。

### 12.17.3 已知边界(不在本 slice)

- manifest 车道**运行时**(WH-5c.6 node/capability)诊断的 line:col 仍需 session locator 按 node=workflow 文件 / capability=各自文件解析,§12.16.6 边界继续有效,留独立未来 slice。
- CoreFnDecl 无 source_module,verify_fns 的 fn 级诊断不归属模块(§12.16.13 P2-3,backlog)。
- 评估器 WorkflowRuntime 保留至 WH-9 原子删除;WH-7(REPL)/WH-8(DAP)按 §12.7.3 顺序各自 cutover,§12.7.1 WASM=OFF 产品策略两 slice 继承。

### 12.17.4 落地后全套测试修复(2026-10-03):workflow 作用域越界解引用 P0

WH-6 提交(91ddd558)后的完整无标签 ctest(594 项)暴露一个定向评审集未覆盖的 P0:`ahfl.backends.wasm_all`(#371,`ahfl_core_wasm_codegen_tests`)确定性 `std::bad_alloc` 中止。

- 根因:`emit_core_wasm` workflow 分支在安装 `ActiveSourceModuleScope` 时直接索引 `program.workflows[target.value].source_module`,该索引发生在**越界入口检查之前**。测试以 `CoreWorkflowId{9}`(程序仅 1 个 workflow)断言 fail-closed `kEntryNotFound`,越界读到的 `source_module` 是垃圾字节,由 string_view 构造 std::string 时按 7.5e18 字节容量抛 bad_alloc。
- 修复(one-big-bang,无 shim):在作用域安装前先做 `workflow->value >= program.workflows.size()` 越界检查,越界即发 whole-program `kEntryNotFound` 并返回(与 §12.16/§12.16.12 "entry-resolution 诊断故意 module 为空" 一致;`build_workflow_plan` 内部原有同名检查保留,供其其他调用者)。审计另两个作用域站点均安全:build_workflow_plan :13582 在自身检查之后;build_agent_plan :11178 有 agent 越界检查 + flow null 检查。
- 验证:复现(gdb 栈定位到 :19490/:1218)→ 修复后 #371 单独通过(3.31s)→ wasm 标签 88/88(4 个 wasmtime skip 为预期)。教训记入流程:codegen 改动的定向集必须包含 ahfl_core_wasm_codegen_tests(#371),不能只跑 CLI/runner 侧。

## 12.18 WH-7 落地记录(2026-10-03):REPL 求值全切到 wasm3 agent runner

### 12.18.1 实际落地形状

单 commit 落在 WH-6 HEAD b9c75736 之上。`src/tooling/repl/repl.cpp` 删除 `runtime/evaluator/evaluator.hpp`;默认 eval 处理器按 §12.8.10 重写为两阶段:

1. **stage-1 类型推断(共享 helper)**:`infer_repl_type` 把表达式包进 `fn repl_probe() -> Unit { let repl_val = <expr>\n; return {}; }`(表达式后换行分隔,防尾随行/doc 注释吞掉包装尾部;字符串内 `//` 不被触碰),经真实 parse/resolve/typecheck 后从 `FnTypeInfo.body_block_index` -> `blocks[].statement_indexes[0]` -> TypedStmt(Let / FromInitializerType)取 `let_type`;任何内部不变量破坏 fail-closed 为 "(internal) repl type extraction failed"。同时修复自 62b38665 起事实坏掉的 `:type`(旧 const 包装违反 constDecl 显式类型 + IDENT 首字符规则)。
2. **声明 fallback 存活**:stage-1 失败且原始输入本身是声明(const/struct/fn/...)时,直接 run_pipeline + print_program_ir。
3. **失败相位优先级(fix-forward P2-2)**:`PipelineFailurePhase`/`InferFailurePhase` 区分 Parse/Resolve/Typecheck/Internal;stage-1 与 fallback 同时失败时,Typecheck/Internal 浮出真实类型错误(如 `1 + true` -> `operator '+' is not defined for Int and Bool`,不再被 fallback token dump 遮蔽),Parse/Resolve 保持 fallback 的顶层 parse 错误(§12.8.10.12 1.4 nominal 失败面:`Option::Some(3)` -> `mismatched input 'Option'`)。
4. **Unit 短路**:`{}` -> `{}`,不经 wasm(codegen 拒绝 Unit 输出字段);WASM=ON/OFF 均可用。
5. **stage-2 合成 agent**(无 module、非 pub、零能力、Init->Done 两态):`struct ReplIn {}` + `struct ReplOut { value: <describe(T)> }`,Done 行在表达式后换行分隔;lower 后 `run_wasm_agent`,要求 Completed + 非空 StructValue 输出,取 `value` 字段 print_value。非 Completed 渲染 `WorkflowStatus` 枚举名 + 诊断袋首条(如 `4 / 0` -> `Error: agent did not complete (status: NodeFailed): [wasm.trap]: run_wasm_agent: runv trapped`,不再泄露裸整数 `(status: 1)`)。
6. **单一 #ifdef 组合边缘**:eval wasm 体一个 `#ifdef AHFL_ENABLE_BACKEND_WASM` triplet;WASM=OFF 字节固定拒绝 `Error: evaluation requires the embedded wasm engine; this build was configured with -DAHFL_ENABLE_BACKEND_WASM=OFF. Rebuild with the default (ON) to evaluate expressions.`(stage-1 推断 / `:type` / Unit 短路 / 声明 fallback 仍工作)。
7. **EnumVariantT->parent 转换分支按 §12.8.10.12 §2 在本 slice 删除**(detached REPL 管线不可达;post-WH-9 "REPL session accumulation + prelude mode" 后援切片在声明可见通道建成时重新引入)。

CMake:`ahfl_tooling_repl` 删除直接 `ahfl_runtime_evaluator` 边,显式 `ahfl_compiler`/`ahfl_verification_formal`/`ahfl_runtime_value`,gated `ahfl_runtime_wasm_runner` + `AHFL_ENABLE_BACKEND_WASM=1` generator expression。evaluator 仅经 wasm_runner->engine 传递依赖存活(engine->evaluator 边在 WH-8 翻 PRIVATE,WH-9 原子删除)。

测试:unit Tests 10-21(类型矩阵/Unit 短路/声明 fallback/注释分隔/字符串内 `//`/nominal 失败面/类型错误优先级/wasm 算术/字符串/codegen 拒绝/除零状态名,WASM=OFF 分支同 TU 编译;64/64 ON、47/47 OFF);`tests/scripts/repl_smoke.py` baseline/wasm/wasm-off 三模式 + `tests/cmake/ProjectTests.cmake` 互斥注册。

### 12.18.2 验证证据(coordinator 独立复跑)

- 对抗评审 verdict FIX-FORWARD(2 P1 + 2 P2,coordinator 全部独立复现),fix-forward 后四项逐字复核:P1-1 `1 + 2 // c` eval -> `3`、`:type` -> `Int`(`"http://x"`/`"a // b"` 字符串不被变换,doc 注释同样修复);P1-2 EnumVariantT field/branch/doc grep-zero;P2-1 `4 / 0` -> `(status: NodeFailed): [wasm.trap]: run_wasm_agent: runv trapped`;P2-2 `1 + true` eval -> 真实类型错误、不含 `expecting {<EOF>`,nominal 三面(`mismatched input 'Option'`/`unknown callable 'Option::Some'`/`unknown type 'Color'`)钉住。
- dev 强制重编译 REPL TU 零 warning(`-Wall -Wextra -Wpedantic -Werror`);ASan preset 全量重建零 warning。
- `ctest --preset test-dev -R repl` 3/3;unit binary 64/64 ON。
- WASM=OFF scratch(<job tmp>/build-wasm-off-wh7 冷重建,exit 0、零 warning):repl ctests 3/3 + unit 47/47,拒绝字节逐字一致,`:type`/Unit/声明 fallback 正常。
- grep-zero:`runtime/evaluator` 于 src/tooling/repl/;#ifdef triplet 1。
- 受保护文件(src/tooling/cli/CMakeLists.txt、tests/conformance/、tests/observability/、goldens)零改动;HEAD 全程 b9c75736 未被 agent 移动。
- 完整无标签 dev ctest(595 项,1722.69s):99% 通过,4 failed——#81/#84/#85(ahfl-beta-gate,pnpm 未安装的既有环境噪声,历次全量一致;#85 是 #84 缺 install-smoke.json 的下游);#64 `native_grpc_gate` 为并发 scratch configure 污染:仓库根 `compile_commands.json` 符号链接由 `cmake/modules/AhflCompileCommands.cmake` 在每次 configure 重指向 `${CMAKE_BINARY_DIR}`,scratch WASM=OFF configure 期间瞬态指向 job-tmp 构建树,scanner `root.rglob()` 经符号链接 resolve 出树根触发 `relative_to` ValueError;scratch 空闲后 coordinator 两次独立复跑 #64 均通过。4 skip = #5/#7/#57/#60 wasmtime(缺第三方二进制,既有)。
- ASan(单独 configure + -j2 全量重建 + 单独全量跑,3767.06s):595 项 99%,**全日志零 AddressSanitizer/LeakSanitizer/UBSan 报告**;4 skip 同上 wasmtime;4 failed 全部为非本 slice 的既有噪声——#81/#84/#85 与 dev 同因(pnpm);#78 `long_soak_smoke` 为 RSS 统计门在 ASan 插桩下的假阳性,证据链:① soak worker 二进制时间戳 15:27、本次 ASan 构建 ninja 只重链 6 个 REPL 目标([6/6]),worker 未重建,其链接闭包(package_graph/compiler_ir/provider_llm)零 WH-7 代码;② 默认 ASAN_OPTIONS 下隔离复跑稳定失败(peak_rss first-quartile 均值 ~345MB、增长 ~36MB > 5% 容差 ~17MB),但 worker 自报的 allocator_in_use 趋势门通过(应用层存活分配平稳,仅 OS RSS 爬升);③ `ASAN_OPTIONS="quarantine_size_mb=0:thread_local_quarantine_size_kb=0"` 隔离复跑立即通过——quarantine 保留已释放块不还 OS 即全部增长来源;④ dev(无插桩)全量 #78 通过。列入 backlog(soak RSS 门与 ASan quarantine 不兼容,需独立 slice 决议测试 ENVIRONMENT/门豁免,不在 WH-7 范围)。

### 12.18.3 已知边界(不在本 slice)

- enum/struct/option 求值面收窄(§12.8.10.12 verdict A):detached REPL 不经 Project/discovery,无 prelude、零跨行状态;后援切片 "REPL session accumulation + prelude mode" 排在 post-WH-9,进入条件(std 嵌入或显式 sysroot 契约 + 会话累积 + module 前缀决议 + 错误恢复语义)见 §12.8.10.12 §4。
- Float 算术、String 拼接等仍由 P6 codegen fail-closed 拒绝(后续 P6 opcode ladder slice,与 WH-7 无关)。
- `long_soak_smoke` 的 peak_rss 趋势门在 ASan quarantine 下假阳性(12.18.2),backlog 独立处理。
- evaluator 本体保留;WH-8(DAP cutover,§12.9 已决策)、WH-9(原子退役,§12.10)随后。

### 12.19 WH-8 落地记录(2026-10-04,base `1b8f9cfb`)

单 commit 落在 WH-7 HEAD `1b8f9cfb` 之上。`src/tooling/dap/` 删除 evaluator include,DebugSession 唯一执行后端改为 `WasmWorkflowRuntime` facade;evaluator 仅剩 wasm_runner->engine 传递边,WH-9 原子删除。

#### 12.19.1 实际落地形状

1. **双 hook 解耦(§12.9.14 verdict a)**:`WasmRuntimeHooks`/`WorkflowSessionConfig` 新增 `state_entered_live_hook`(签名与 state_entered_hook 逐字相同)。live hook 仅在 workflow P6 lane 的 capability import boundary、trace-ring prefix decode 处、capability 分派前触发;WireJson(无 trace ring)/post-run/agent runner 永不触发。post-run full decode 保持传单一 `StateEnteredHook{}`(只收集);`rebuild_and_fire_states` 更名 `rebuild_states`(只收集),`reconstruct_wirejson_states`/`collect_opaque_node_states` 同样去触发化。
2. **per-node 交错(§12.9.4 item 2)**:post-run section 10 合并为单一 schedule 序循环,每个 index i 先 `node_completed(i)` 再触发该 node 的 state hooks(`state_hooks_visited` 精确一次);decode 失败 break 后后缀 index 由 FUNCTION 作用域后缀循环补触发 state hooks(后缀循环在 `if (mem)` 之外,no-memory 也触发)。修复多 node 共享一个 identity-final AgentShell 时 `agent_outputs_` last-wins 的错值(T3 钉 x==1/x==2)。
3. **DAP dedup**:`live_count_`/`post_count_` 两张 map,key `node\x1fstate`;post-run hook 当 `post <= live` 跳过。trace ring 线性 append-only,live 流是全序列严格前缀,occurrence 对齐。
4. **八行活性分类器**:`classify_record_liveness_locked`(单记录,7 行 pinned 消息逐字)+ `classify_line_liveness_locked`(收集该行全部记录;全同归并,分歧走第 8 行 AMBIGUOUS SHARED LINE,§12.9.14.4)。descriptor 经 facade `descriptor_for(name)` 只读获取;codegen 新增 `CoreWasmStateWalk.last_cap_walk_index`(`runner_last_cap_walk_index`,与 `runner_walk_names` 同一 initial/visited/GotoAction 回放,bridge_calls ∪ construct_capability_terminals 的并集;两个 descriptor.agents 填充点都填)。
5. **late binding(F5)**:`handle_set_breakpoints` 在 session 存在时收集新增 (id,line),响应体合成后、handle_request 返回前经 `report_breakpoint_liveness` 发 unsolicited breakpoint 事件(classifier 在 mutex_ 下短暂取锁;worker pause 在 cv 等待期间不持锁,无死锁)。
6. **cancellation/interruption 四点检查**:session 入口(pre-run,构造全 Skipped facts)、wrapped_callback TOP、live hook 阻塞返回后二次检查(F1(a),lane 分派前)、wrapped_invoker 在 capability_invoked_hook 阻塞返回后、invoker 副作用前二次检查(F1(b):返回 Error+Cancelled/Interrupted,不调 invoker/observer/args/calls;collected_capabilities 的 name-only 项加注释,消费者死代码/只投影 cap_calls)。两个 import executor 站点 map Error->AHFL_CAP_ERROR,bridge lane kOpUnreachable trap、opaque lane (ERROR,0,0),workflow 不可能成功完成;post-run 覆盖链最前段把结果分类为 Cancelled/Interrupted,无 Failed node。lifecycle 在终态前补 RunCancellationRequested/RunInterrupted,与 evaluator 字节对齐。
7. **wasm lane 无 agent_input hook**:Node frame 在该 node 首个 state entry 合成;Input/Output 作用域与 evaluate 从 workflow IR node 参数 `PathExpr.path`(root input->workflow_input_,identifier->node_results_,members 经 resolve_member 链,miss->nullptr)推断。
8. **facade run() 可重复**:5 个 hooks + post_run2_memory_mutator + predicates + invoker 全部 copy(非 move)。
9. **WASM=OFF**:单 `#ifdef`;launch 字节固定拒绝 `debug adapter requires WASM backend support`(stderr + terminated + `{}`),非执行请求存活;CMake genex 传 `AHFL_ENABLE_BACKEND_WASM=1`。
10. **CMake big-bang**:engine evaluator 边 PUBLIC->PRIVATE;DAP 去 engine 改显式 value + gated wasm_runner;6 个测试目标显式补 evaluator 链接。

#### 12.19.2 对抗评审与 fix-forward

- 首轮对抗评审:2 P1(P1-1 live hook 阻塞返回后无二次取消检查;P1-2 post-run 批量触发违反 per-node 交错)。coordinator 追加 P1-1 孪生(wrapped_invoker 同样窗口)与 gate blocker(WASM=OFF 测试二进制 77/144 失败,builder 只验证编译未跑二进制)。
- fix-forward F1-F5 + T1-T5 全部修复;独立 fix-forward 复审 P0=0 P1=0,P2 两条:seq_counter_ 竞态**证伪**(dap_server.hpp:70 HEAD 即 `std::atomic<int>`,++ 为原子 RMW);同一物理行多 handler first-wins **证真**。
- F6(第二轮 fix-forward):分类器全记录裁决 + 第 8 行 pinned 消息 + T6a 混合(pre-F6 必败:声明序首记录为 AgentA.Init LIVE,断言 !saw_live)/T6b 一致归并;cancel 路径 name-only 注释;F6 聚焦复审 P0=0 P1=0。
- P3 advisory:unsolicited event(seq=N+1)可能先于 response(seq=N)上线,DAP 按 request_seq 匹配,不改。

#### 12.19.3 验证证据(coordinator 独立复跑)

- dev 强制 fresh build 零 warning(-Wall -Wextra -Werror);F6 后增量再建零 warning。
- `ctest -L dap` 2/2;直跑 ahfl_tooling_dap_tests **194/194 ON**;WASM=OFF scratch 增量重建零产品 warning + **25/25** exit 0(同 TU constexpr kBackendWasm,OFF 额外 T5 钉拒绝字节)。
- `ctest -L wasm` 88/88(4 wasmtime skip 为既有 #5/#7/#57/#60)。
- conformance census:native **73 agreed/0 skipped**;node **69 agreed/4 skipped**(既有 Node-port 缺能力)。
- grep 门:`StateEnteredHook{}` workflow_session.cpp 恰好 1 处(post-run);`runtime/engine` 于 src/tooling/dap/ 零;`rebuild_and_fire_states` 于 src/ 零。
- 受保护文件(src/tooling/cli/**、src/tooling/repl/**、tests/conformance/**、tests/observability/**、goldens)零改动。
- 完整无标签 dev ctest(595 项):99%,591 pass / 4 fail,全部既有非产品噪声——#81/#84/#85 pnpm/vsce 未安装(VSIX packaging exit 127)beta-gate 级联;#64 native_grpc_gate 为本 slice WASM=OFF scratch configure 经 AhflCompileBooks 把仓库根 compile_commands.json 符号链接重指向 job-tmp 树、scanner rglob 解引用出树根的 ValueError(与 §12.18.2 同因);coordinator 恢复链接指向 build/dev/compile_commands.json 后独立复跑 #64 通过。
- ASan(单独 fresh configure + -j2 全量重建 + 单独全量跑):595 项 99%,**全日志零 AddressSanitizer/LeakSanitizer/UBSan 报告**,591 pass / 4 fail,全部既有噪声——#81/#84/#85 与 dev 同因(pnpm/vsce);#78 `long_soak_smoke` 为 §12.18.2 记录的 RSS 门 ASan quarantine 假阳性,`ASAN_OPTIONS="quarantine_size_mb=0:thread_local_quarantine_size_kb=0"` 隔离复跑通过(3.44s)。#64 在 ASan 全量中未复现。4 skip = 既有 wasmtime。

#### 12.19.4 已知边界(不在本 slice)

- DAP pause()/disconnect 的 stopped-vs-terminated 事件竞态(既有,任务 #125;WH-8 增大窗口但未改变语义,pause() 合约注释已标注)。
- 共线 handler 第 8 行仅影响活性**消息**;断点命中仍按 (agent,state) 对每个 handler 各自暂停(正确 DAP 语义)。
- evaluator 本体保留至 WH-9 单一原子 BREAKING CHANGE 删除(§12.10)。
