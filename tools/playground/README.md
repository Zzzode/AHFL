# AHFL Playground

Browser-based playground for the AHFL compiler. Write AHFL code, compile, verify, and format — all in the browser.

## Architecture

- **Frontend**: Vite + TypeScript + Monaco Editor (AHFL syntax highlighting)
- **Backend**: Dev-server middleware that spawns the local `ahflc` binary
- **Compiler**: Real `ahflc` (not a stub) — compile, verify, and format use the actual compiler

## Quick Start

```bash
# From the repository root:
cmake --build --preset build-dev -j$(nproc)

# Start the playground (auto-detects ahflc at build/dev/src/tooling/cli/ahflc):
cd tools/playground
npm install
npm run dev
```

Open http://localhost:5173.

### Custom ahflc path

```bash
AHFLC_PATH=/path/to/ahflc npm run dev
```

## Features

- **Compile**: `ahflc check` — type-check and report diagnostics
- **Verify**: `ahflc check --verify=on` — formal verification with NuSMV/Z3
- **Build**: `ahflc build` — compile to a WASM binary with download link
- **Format**: `ahflc fmt` — format source in place
- **Monaco Editor**: AHFL syntax highlighting, dark theme
- **Structured Output**: diagnostics, stats, and verification results rendered as readable cards (not raw CLI text)

## Current Status

This is the server-side wrapper slice: the playground spawns a local `ahflc`
process. The long-term goal is a WASM-compiled compiler for fully client-side
operation (no server needed).
