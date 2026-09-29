# wasm3 — vendored third-party notice

wasm3 — Copyright (c) 2019 Steven Massey, Volodymyr Shymanskyy — MIT License —
see `third_party/wasm3/LICENSE`.

AHFL vendors the **interpreter-only subset** of wasm3 v0.9.0 (upstream commit
`0cd38327f0c721e75172f4f1eeb55854dc0517af`, see `VERSION`): the eleven
interpreter translation units listed in upstream `source/CMakeLists.txt` minus
the API glue. The WASI, libc, uvwasi, meta-wasi and tracer adapters, the
`platforms/` CLI and the `extensions/` directory are deliberately NOT vendored.
AHFL embeds this engine inside its own single-binary host and registers its own
host imports (`ahfl_cap`), so none of the upstream platform glue is reachable.

This notice satisfies the MIT attribution condition; the AHFL project itself
remains Apache-2.0 (see the root `LICENSE`).
