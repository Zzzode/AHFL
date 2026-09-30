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
