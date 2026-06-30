# AHFL

VS Code language support for the **AHFL Agent Flow Language** -- a typed DSL for
defining agent handoff flows, capability contracts, workflow orchestration, and
verified effect handling.

## Features

### Syntax Highlighting

Full TextMate grammar covering the core AHFL keyword set:

- **Modules**: `module`, `import`
- **Types**: `struct`, `enum`, `type`, `const`, `trait`, `impl`
- **Functions & predicates**: `fn`, `predicate`, `builtin`, `extern`
- **Agent domain**: `agent`, `workflow`, `capability`, `contract`, `flow`, `effect`
- **Control flow**: `if`, `else`, `match`, `return`, `let`, `unwrap`, `unreachable`
- **Contracts & verification**: `requires`, `assert`, `invariant`, `always`, `eventually`, `never`
- **Self references**: `self`, `Self`
- **Effect modifiers**: `Pure`, `Nondet`
- **Option constructors**: `Some`, `none`
- **Booleans**: `true`, `false`
- **Documentation**: `doc`, `///` doc-comments

The grammar also highlights numeric literals (decimal, hex, octal, binary),
string and character escapes, operators, attributes (`#[...]`), and declaration
names (types, functions, modules).

### Code Snippets

Predefined snippets for the most common AHFL constructs:

| Prefix      | Expands to                                        |
|-------------|---------------------------------------------------|
| `agent`     | Agent skeleton with states and transitions        |
| `workflow`  | Workflow block with nodes and return              |
| `capability`| Capability signature with request/response types  |
| `struct`    | Struct definition with fields                     |
| `contract`  | Contract block with requires/ensures/invariant    |
| `flow`      | Flow implementation with states                   |

### LSP-Backed Features (Requires `ahfl-lsp`)

The extension bundles a Language Server Protocol client. When the `ahfl-lsp`
binary is available (bundled in release VSIX packages, or on `PATH`, or
configured via `ahfl.serverPath`), the following features are active:

- **Diagnostics** -- parse errors, type-checking violations, effect safety
  checks, contract violations, and liveness warnings are surfaced in the
  Problems panel and as inline squiggles. Diagnostics update live as you type.
- **Hover** -- hovering over a symbol shows its canonical type, effect
  annotation, declaration origin, and a configurable number of inferred facts.
  Detail level is controllable via `ahfl.hover.detailLevel`
  (`compact` / `standard` / `debug`).
- **Semantic tokens** -- server-provided token classification augments the
  TextMate grammar for precise highlighting of types, parameters, bindings,
  and built-in symbols.
- **Completion** -- context-aware suggestions for struct fields, enum variants,
  capability members, and imported symbols.
- **Go to Definition / References** -- navigate to declarations and find all
  usages across the project.
- **Rename** -- workspace-aware renaming of types, agents, capabilities, and
  local bindings.
- **Document & Workspace Symbols** -- quick outline navigation and cross-file
  symbol search.
- **Code Actions** -- quick fixes for common diagnostics (e.g. missing fields,
  unused imports).
- **Code Lens** -- contract coverage counts and effect summaries above agent
  and capability declarations.
- **Document Highlight** -- highlight all references to the symbol under the
  cursor.
- **Selection Range** -- expand/shrink selection following AHFL parse tree
  boundaries.
- **Document Formatting** -- invoke the `ahflc fmt` backend via the language
  server (command: `AHFL: Format Document`).

### Commands

| Command                          | Action                                           |
|----------------------------------|--------------------------------------------------|
| `AHFL: Restart Language Server`  | Stop and restart the `ahfl-lsp` process          |
| `AHFL: Format Document`          | Format the active AHFL file                      |
| `AHFL: Show Language Server Output` | Open the extension output channel for LSP logs  |

### Configuration

```jsonc
{
  // Path to ahfl-lsp (empty = use bundled or find on PATH)
  "ahfl.serverPath": "",
  // Extra CLI arguments for ahfl-lsp
  "ahfl.serverArgs": [],
  // Hover verbosity: compact | standard | debug
  "ahfl.hover.detailLevel": "standard",
  // Show source file paths in debug hover
  "ahfl.hover.showSource": false,
  // Max primary facts per hover tooltip
  "ahfl.hover.maxFacts": 3,
  // Hover markup format: auto | markdown | plaintext
  "ahfl.hover.markupKind": "auto"
}
```

## Requirements

### LSP Server

Release VSIX packages include the matching release `ahfl-lsp` server binary
under `server/`. When installing a client-only build, you need a working
`ahfl-lsp` binary either on `PATH` or configured via `ahfl.serverPath`.

Build a local LSP server for development:

```bash
cmake --build --preset build-dev --target ahfl-lsp
```

The development binary is at:

```text
build/dev/src/tooling/lsp/ahfl-lsp
```

### Node Tooling

Extension dependency installation uses pnpm via Corepack:

```bash
corepack enable
corepack prepare pnpm@10.10.0 --activate
```

## Building & Testing the Extension

```bash
cd tools/vscode
pnpm install --frozen-lockfile --ignore-scripts
pnpm run compile                    # type-check + esbuild bundle
pnpm run test:extension             # full extension-host regression
pnpm run test:problems-transcript   # capture diagnostics transcript
pnpm run package                    # produce client-only .vsix
```

## Marketplace Publishing

Tag-based releases are automated via
`.github/workflows/release-vscode.yml`. To trigger a release:

```bash
git tag vscode-v0.2.0
git push origin vscode-v0.2.0
```

This builds the client VSIX, uploads it as an artifact, and publishes to both
the Visual Studio Marketplace (using `secrets.VSCE_PAT`) and the Open VSX
Registry (using `secrets.OPEN_VSX_TOKEN`).

Manual publishing is also available via the workflow dispatch with an explicit
`version` input.

## Known Limitations

- The LSP server is a **standalone `ahfl-lsp` binary**; there is no
  `ahflc --lsp` subcommand. Make sure you point `ahfl.serverPath` to the
  correct executable if not using the bundled release.
- Platform-specific VSIX packages with embedded `ahfl-lsp` are produced by the
  separate `vscode-extension.yml` workflow (one job per OS). The
  `release-vscode.yml` workflow described above publishes the client-side
  extension.
