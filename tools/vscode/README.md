# AHFL Language Support

VS Code language support for AHFL (Agent Handoff Flow Language).

## Features

- Syntax highlighting, snippets, and language configuration for `.ahfl` files.
- LSP-backed diagnostics, hover, completion, definition, references, rename, document symbols, workspace symbols, and signature help.
- Project-aware analysis for `ahfl.project.json` and `ahfl.workspace.json`.

## Requirements

Platform release VSIX packages include the matching release `ahfl-lsp` server
binary under `server/`.

Extension dependency installation uses pnpm through Corepack:

```bash
corepack enable
corepack prepare pnpm@10.10.0 --activate
```

The default configuration uses the bundled server when present. To override it,
set:

```json
{
  "ahfl.serverPath": "/absolute/path/to/ahfl-lsp"
}
```

For local extension development, a debug LSP build is fine:

```bash
cmake --build --preset build-dev --target ahfl-lsp
```

The development binary is usually at:

```text
build/dev/src/tooling/lsp/ahfl-lsp
```

## Hover Configuration

AHFL hover defaults to concise standard Markdown. Extra diagnostic metadata is
available through debug hover:

```json
{
  "ahfl.hover.detailLevel": "standard",
  "ahfl.hover.showSource": false,
  "ahfl.hover.maxFacts": 3,
  "ahfl.hover.markupKind": "auto"
}
```

Use `compact` for signature-only hover, or `debug` when inspecting canonical
names, module names, and source paths.

## Extension Regression

Run the VS Code extension-host regression after building the LSP server:

```bash
cmake --build --preset build-dev --target ahfl-lsp
cd tools/vscode
pnpm run test:extension
```

The test opens `tests/integration/check_ok` in a VS Code Extension Development
Host and validates schema labels, state labels, type aliases, struct fields, and
workflow nodes through `vscode.executeHoverProvider`. It verifies member
completion through `vscode.executeCompletionItemProvider`, applies a rename
workspace edit through `vscode.executeDocumentRenameProvider`, and confirms the
edited document updates both declaration and reference sites. It also edits an
unopened imported `.ahfl` file under the test workspace and verifies watched-file
invalidation refreshes diagnostics for the open dependent document. Finally, it
opens a temporary broken `.ahfl` file, waits for AHFL diagnostics to reach VS
Code's diagnostics collection, edits the file into a valid state, and verifies
the diagnostics clear.

Generate and validate a Problems diagnostics transcript:

```bash
cd tools/vscode
pnpm run test:problems-transcript
```

The transcript is written to `tools/vscode/dist/problems-transcript.json` and is
based on `vscode.languages.getDiagnostics`, the same diagnostics collection that
feeds VS Code's Problems view.

## Local VSIX Packaging

Build the platform VSIX users should install:

```bash
scripts/package-vscode-vsix-release.sh
```

The platform VSIX is written to:

```text
tools/vscode/dist/ahfl-language-<version>-<target>.vsix
```

It contains the VS Code client and the matching release `ahfl-lsp` server.
Verify the Marketplace package inventory and smoke test installation into a
temporary VS Code profile:

```bash
cd tools/vscode
pnpm run test:package-inventory
pnpm run test:vsix-install
```

For client-only extension development, package from `tools/vscode`:

```bash
cd tools/vscode
pnpm install --frozen-lockfile --ignore-scripts
pnpm run package
```

The VSIX is written to:

```text
tools/vscode/dist/ahfl-language.vsix
```

The client-only VSIX is useful for local testing, but it expects `ahfl-lsp` to
exist on `PATH` or be configured through `ahfl.serverPath`.

Install it locally:

```bash
code --install-extension tools/vscode/dist/ahfl-language-<version>-<target>.vsix
```

## Marketplace Publishing

Create a Visual Studio Marketplace publisher and a personal access token with Marketplace Manage permissions, then package the platform VSIX from the repository root:

```bash
scripts/package-vscode-vsix-release.sh
```

Publish the generated platform artifact from `tools/vscode`:

```bash
cd tools/vscode
pnpm run publish:vsix -- dist/ahfl-language-<version>-<target>.vsix --pat "$VSCE_PAT"
```
