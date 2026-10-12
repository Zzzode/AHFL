# AHFL

VS Code language support for **AHFL (Agent Handoff Flow Language)**. Edit
workflow source with syntax highlighting, snippets, diagnostics, navigation,
and debugging. Language Server Protocol (LSP) connects the extension to the
`ahfl-lsp` server that supplies semantic editor features.

## Install and open a project

1. Obtain a platform VSIX extension package for your operating system and CPU.
   You can build one from the repository root:

   ```bash
   scripts/package-vscode-vsix-release.sh
   ```

2. Install the generated package:

   ```bash
   code --install-extension tools/vscode/dist/ahfl-language-<version>-<target>.vsix
   ```

   Replace `<version>` and `<target>` with the generated filename. You can also
   choose **Extensions: Install from VSIX...** in VS Code.

3. Open your project's root folder and an `.ahfl` file. Platform packages
   include `ahfl-lsp` and the standard library, so language features do not
   require a separate server installation.
4. If you use a client-only extension package, install `ahfl-lsp` on `PATH` or
   set `ahfl.serverPath` to its absolute executable path in VS Code settings.

## Features

### Syntax Highlighting

Syntax highlighting covers the core AHFL keyword set:

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
- **Document Formatting** -- format the active file with `AHFL: Format Document`.

### Commands

| Command                          | Action                                           |
|----------------------------------|--------------------------------------------------|
| `AHFL: Restart Language Server`  | Stop and restart the `ahfl-lsp` process          |
| `AHFL: Format Document`          | Format the active AHFL file                      |
| `AHFL: Show Language Server Output` | Open the extension output channel for LSP logs  |

### Debug a workflow (Requires `ahfl-dap`)

Install or build `ahfl-dap`, the Debug Adapter Protocol (DAP) server, and set
`ahfl.debugAdapterPath` to its absolute executable path if it is not on `PATH`.
Press `F5` on an open `.ahfl` file (or add a launch configuration) to start a
workflow debug session. You can set breakpoints on state transitions and
capability invocations, step through execution, and inspect agent state.
See the [debugging guide](../../docs/tutorials/debugging.zh.md) for provider
configuration and workflow selection.

A minimal launch configuration:

```jsonc
{
  "type": "ahfl",
  "request": "launch",
  "name": "AHFL: Debug workflow",
  // Path to the .ahfl file whose workflow should be debugged.
  "program": "${file}",
  // Optional: the specific workflow to debug.
  "workflow": "",
  "stopOnEntry": false
}
```

The adapter binary is resolved like the language server: `ahfl.debugAdapterPath`
takes precedence, then the bundled release adapter under `server/`, then
`ahfl-dap` on `PATH`.

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

## Resolve editor problems

- **No diagnostics or completion**: run `AHFL: Show Language Server Output`,
  check that `ahfl.serverPath` points to `ahfl-lsp`, and run
  `AHFL: Restart Language Server` after correcting the setting.
- **Standard library imports fail**: use a platform VSIX with the bundled
  standard library or configure the project's sysroot as described in the
  [VS Code guide](../../docs/reference/lsp-vscode-extension.zh.md).
- **Debug session cannot start**: check `ahfl.debugAdapterPath` and confirm
  that `ahfl-dap` exists and is executable.

To build a local language server from the repository root:

```bash
cmake --preset dev
cmake --build --preset build-dev --target ahfl-lsp -j"$(getconf _NPROCESSORS_ONLN)"
```

The executable is `build/dev/src/tooling/lsp/ahfl-lsp`. The language server is
launched directly as `ahfl-lsp`; configure that executable in `ahfl.serverPath`.

## Develop the extension

Extension contributors can find dependency setup, tests, packaging, and release
instructions in the [VS Code reference](../../docs/reference/lsp-vscode-extension.zh.md).
