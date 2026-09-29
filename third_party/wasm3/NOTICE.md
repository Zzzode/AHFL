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
`m3_bind.c` is included on purpose: `m3_LinkRawFunction` is exactly the
host-import binding surface the embedded host uses, and the three-result
`(i32,i32)->(i32,i32,i32)` capability ABI is pinned by the
`ahfl.runtime.wasm3_embedded_smoke` test.

This notice satisfies the MIT attribution condition; the AHFL project itself
is licensed under the root `LICENSE` (Apache-2.0). The licence text above is
also installed under `${datadir}/licenses/wasm3` so binary SDK packages carry
the MIT notice.
