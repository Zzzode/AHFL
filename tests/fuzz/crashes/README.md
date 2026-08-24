# tests/fuzz/crashes — Crash regression corpus

This directory is the **durable save location** for fuzzer-discovered
crash-triggering inputs and their minimized repros. Unlike `../corpus/`,
which archives dated *batches* of raw findings for triage, this tree holds
the small, curated set of inputs that each fuzz target must **never crash on
again**. Every file here is wired into a deterministic CTest regression guard
(see `../CMakeLists.txt`, targets `ahfl.fuzz.<target>.crash_replay`).

The tree starts empty on purpose. Nothing here is a fabricated sample — the
convention below defines where a *real* crash goes the moment one is found.

---

## Layout

```
tests/fuzz/crashes/
├── README.md                     ← this file (the convention)
├── fuzz_parser/                  ← one dir per fuzz target
│   ├── .gitkeep
│   └── <sha256>                  ← the raw crash input (verbatim bytes)
│   └── <sha256>.repro.md         ← the human record for that crash
├── fuzz_typecheck/
│   └── .gitkeep
└── fuzz_smv_emitter/
    └── .gitkeep
```

Rules:

- **One subdirectory per fuzz target.** The directory name matches the
  libFuzzer executable name exactly: `fuzz_parser`, `fuzz_typecheck`,
  `fuzz_smv_emitter`. A new target gets a new subdirectory (+ `.gitkeep`).
- **Crash input filename = sha256 of the input bytes**, no extension. This is
  the same naming libFuzzer produces via `-artifact_prefix`
  (`crash-<sha1>`); we canonicalize to the full sha256 so the filename is
  self-verifying. Store the **minimized** input, not the raw first hit.
- **Companion `<sha256>.repro.md`** documents the crash (see template below).
- Any file under a target directory whose name is `README.md`, ends in
  `.repro.md`, or is `.gitkeep` is treated as **documentation** and is NOT
  fed to the fuzzer by the regression test. Every other file IS treated as a
  crash input.

---

## Adding a real crash (end-to-end)

Assume `fuzz_parser` crashed on some input while running libFuzzer.

```bash
cd <AHFL_ROOT>
cmake --preset fuzz && cmake --build --preset build-fuzz -j8

# 1. libFuzzer wrote the raw crash artifact, e.g. ./crash-<sha1>.
#    Minimize it (repeatedly shrinks while still reproducing the crash):
./build-fuzz/tests/fuzz/fuzz_parser -minimize_crash=1 -runs=100000 \
  -exact_artifact_path=/tmp/min_input  ./crash-<sha1>

# 2. Confirm the minimized input still crashes (single-run replay):
./build-fuzz/tests/fuzz/fuzz_parser /tmp/min_input    # expect non-zero exit

# 3. Canonicalize the filename to its sha256 and move it into place:
SHA=$(sha256sum /tmp/min_input | cut -d' ' -f1)
cp /tmp/min_input tests/fuzz/crashes/fuzz_parser/$SHA

# 4. Write the repro record next to it:
cp tests/fuzz/crashes/_TEMPLATE.repro.md \
   tests/fuzz/crashes/fuzz_parser/$SHA.repro.md
#    ...then fill in the fields.

# 5. Open a PR. The crash input is now a live regression guard: the
#    ahfl.fuzz.fuzz_parser.crash_replay CTest feeds it to the binary and
#    FAILS until the crash is fixed. Once the fix lands, the same test
#    turns green and stays green — a re-introduced bug re-fails it.
```

The regression test only exists in a fuzzer-enabled build
(`-DAHFL_ENABLE_FUZZING=ON`), because only the libFuzzer executables accept a
file argument and run a single input. In a normal `dev` build the standalone
`fuzz_*_check` smoke binaries are built instead and this replay test is not
registered.

See `../README.md` for the full fuzzing workflow and
`../corpus/` for the dated triage batches.
