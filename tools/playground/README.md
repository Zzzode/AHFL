# AHFL Playground

Use this local playground to edit AHFL source, inspect diagnostics, verify
control rules, format code, and download a compiled WebAssembly (WASM) workflow.
You need Node.js, npm, and a local `ahflc` binary. Compilation runs on your
machine through the development server; keep that server running while using
the editor.

## Start the editor

From the repository root:

```bash
cmake --preset dev
cmake --build --preset build-dev -j"$(getconf _NPROCESSORS_ONLN)"
cd tools/playground
npm install
npm run dev
```

Open [the local editor](http://localhost:5173). If Vite selects another port,
use the URL printed in the terminal. Stop the server with `Ctrl+C` when done.

To use a different compiler, set its absolute path before starting the server:

```bash
AHFLC_PATH=/path/to/ahflc npm run dev
```

## Check, build, and run your code

1. Enter AHFL source in the editor or choose an included example.
2. Select **Compile** or **Verify** to check types and control rules and
   view diagnostics. Data-contract proofs
   require the CLI's `--verify-data-contracts` option; see the
   [assurance guide](../../docs/tutorials/assurance.zh.md).
3. Select **Format** to update the editor's source layout.
4. Select **Build** to download `output.wasm`. Your source must declare a
   `pub workflow` entry point. Building here does not perform verification;
   use **Verify** separately before relying on control-rule results.
5. Select **Run** to build and execute a workflow in the browser. Edit
   **Workflow Input (JSON)** and select **Execute** to try another input. If
   the workflow calls capabilities, supply their JSON mock responses and use
   **Re-run with mocks**. These mocks do not call your external services.

The result view shows diagnostics, output values, capability calls, and state
traces. For multi-file projects, dependencies, real provider calls, or host
integration, use the [CLI guides](../../docs/tutorials/README.md).

## Resolve common problems

- **Failed to start ahflc**: build the compiler or set `AHFLC_PATH` to an
  executable binary, then restart the development server.
- **No pub workflow found**: add a public workflow before using **Build** or
  **Run**.
- **Verification failure**: inspect the reported diagnostic and model result;
  a successful build does not establish a verification result.
- **Missing capability response**: fill in the mock JSON for each listed
  capability before executing again.
