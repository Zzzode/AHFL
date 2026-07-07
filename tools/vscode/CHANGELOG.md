# Changelog

All notable changes to the AHFL VS Code extension will be documented in this file.

## Unreleased

### Changed

- Resolve resource-scoped `ahfl.toolchain.sysroot` through `workspace/configuration`, including `${workspaceFolder}` expansion and relative path normalization before sending sysroot profiles to the LSP server.
- Make `.github/workflows/vscode-extension.yml` the only release workflow for Marketplace platform VSIX artifacts; the release path now requires a bundled release `ahfl-lsp`, bundled `std`, package inventory validation, and install smoke evidence.

## [0.2.0] - 2026-06-28

### Added

- **TextMate grammar overhaul** — expanded keyword coverage to 40+ AHFL tokens including `fn`, `enum`, `predicate`, `effect`, `assert`, `unwrap`, `unreachable`, `self`, `Self`, `Pure`, `Nondet`, `Some`, `none`, `const`, `trait`, `impl`, `builtin`, `extern`, and `doc`.
- **Language configuration** — added `<>` bracket pairs, quote auto-close for single quotes, and a precise `wordPattern` for symbol selection and word-based navigation.
- **Editor defaults** — new `configurationDefaults` for AHFL files (4-space indent, semantic highlighting enabled, 100-column ruler).
- **Extension identity** — published under `ahfl` name with `ahfl-team` publisher, display name `AHFL`, description `AHFL Agent Flow Language support`.
- **GitHub Actions release automation** — initial VSIX packaging and Marketplace/Open VSX publishing automation for the extension.
- **Marketplace metadata** — categories and keywords updated to reflect LSP-backed features.
- **CI artifact upload** — release workflow uploads generated `.vsix` as a GitHub Actions artifact for manual verification before publish.

### Changed

- Version bumped from `0.1.0` to `0.2.0`.
- `.vscodeignore` now explicitly excludes `.git/`, `docs/`, and lockfiles for a cleaner VSIX payload.

---

## [0.1.0]

- Add AHFL syntax highlighting, snippets, and language configuration.
- Add LSP client wiring for `ahfl-lsp`.
- Add configurable `ahfl.serverPath` and `ahfl.serverArgs` settings.
