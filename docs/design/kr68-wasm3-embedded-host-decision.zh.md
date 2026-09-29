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
