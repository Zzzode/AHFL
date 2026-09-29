// RFC 0026 KR6.8 (WH-0): vendored wasm3 interpreter smoke test.
//
// WH-0 vendors wasm3 v0.9.0 (MIT) as `third_party/wasm3` and wires it into the
// build; the production C++ host is WH-1+. This test is the only consumer, and
// its job is narrow and load-bearing: prove that the vendored static archive
// actually links into an AHFL C++ translation unit, that the installed/system
// include surface (`wasm3.h`) is self-sufficient under `extern "C"`, and that
// the interpreter really executes a module end-to-end inside this build.
//
// The module is hand-assembled below rather than checked in as a binary blob so
// the expected bytes and the expected result stay auditable side by side, and so
// the test has no fixture path or toolchain (wat2wasm) dependency: it is
// deterministic and fully offline.
//
// Module source (WAT) and its 34-byte encoding:
//   (module (func (export "f") (result i32) i32.const 42))
//   00 61 73 6d 01 00 00 00   magic + version
//   01 05 01 60 00 01 7f      type section:      () -> (i32)
//   03 02 01 00               function section:  one func, type 0
//   07 05 01 01 66 00 00      export section:    "f" -> func 0
//   0a 06 01 04 00 41 2a 0b   code section:      locals 0; i32.const 42; end
//
// The interpreter is created with a minimal stack and no WASI/libc adapters --
// exactly the vendored subset, which is why this module may not import anything.

#include <wasm3.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int test_count = 0;
int pass_count = 0;
void check(bool condition, const std::string &name) {
    ++test_count;
    if (condition) {
        ++pass_count;
    } else {
        std::cerr << "FAIL: " << name << "\n";
    }
}

[[nodiscard]] const char *describe(const M3Result &result) {
    if (result == nullptr) {
        return "<none>";
    }
    return result;
}

// The vendored module bytes, built once and reused; the interpreter requires
// them to outlive the parsed module (see m3_ParseModule's contract).
[[nodiscard]] const std::vector<uint8_t> &probe_module() {
    static const std::vector<uint8_t> bytes = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // magic + version 1
        0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7f,       // type:    () -> (i32)
        0x03, 0x02, 0x01, 0x00,                         // func:    type 0
        0x07, 0x05, 0x01, 0x01, 0x66, 0x00, 0x00,       // export:  "f" -> func 0
        0x0a, 0x06, 0x01, 0x04, 0x00, 0x41, 0x2a, 0x0b, // code:    i32.const 42
    };
    return bytes;
}

// Version pin: the vendored source is v0.9.0 (third_party/wasm3/VERSION). The
// macros come from the vendored header, so a silent upstream bump is caught
// here rather than discovered as a behaviour change in the conformance census.
void test_version_pin() {
    check(M3_VERSION_MAJOR == 0 && M3_VERSION_MINOR == 9 && M3_VERSION_REV == 0,
          "M3_VERSION_{MAJOR,MINOR,REV} == 0.9.0");
}

// The unmodified end-to-end path: parse, load, find, call, read result.
void test_executes_exported_function() {
    const std::vector<uint8_t> &bytes = probe_module();

    IM3Environment env = m3_NewEnvironment();
    check(env != nullptr, "m3_NewEnvironment");
    if (env == nullptr) {
        return;
    }

    // 4 KiB stack: this module has no locals and no calls. Small on purpose, so
    // a regression that silently reserves more per-frame memory is visible.
    IM3Runtime rt = m3_NewRuntime(env, 4096, nullptr);
    check(rt != nullptr, "m3_NewRuntime");
    if (rt == nullptr) {
        m3_FreeEnvironment(env);
        return;
    }

    IM3Module mod = nullptr;
    M3Result result = m3_ParseModule(env, &mod, bytes.data(),
                                     static_cast<uint32_t>(bytes.size()));
    check(result == nullptr, std::string("m3_ParseModule: ") + describe(result));
    if (result != nullptr) {
        m3_FreeRuntime(rt);
        m3_FreeEnvironment(env);
        return;
    }
    check(mod != nullptr, "m3_ParseModule yields a module");

    result = m3_LoadModule(rt, mod);
    check(result == nullptr, std::string("m3_LoadModule: ") + describe(result));
    if (result != nullptr) {
        // m3_LoadModule returned non-none, so it did NOT take ownership.
        m3_FreeModule(mod);
        m3_FreeRuntime(rt);
        m3_FreeEnvironment(env);
        return;
    }
    // From here the runtime owns the module; free only the runtime.

    IM3Function fn = nullptr;
    result = m3_FindFunction(&fn, rt, "f");
    check(result == nullptr, std::string("m3_FindFunction: ") + describe(result));
    if (result == nullptr) {
        check(fn != nullptr, "m3_FindFunction yields a function");
        check(m3_GetArgCount(fn) == 0, "f takes no arguments");
        check(m3_GetRetCount(fn) == 1, "f returns one value");
        check(m3_GetRetType(fn, 0) == c_m3Type_i32, "f returns i32");
    }

    if (fn != nullptr) {
        result = m3_CallV(fn);
        check(result == nullptr, std::string("m3_CallV: ") + describe(result));
        if (result == nullptr) {
            // Zero-initialized: a failure to write the result slot must not be
            // able to alias a correct 42 from a previous run.
            uint32_t value = 0;
            result = m3_GetResultsV(fn, &value);
            check(result == nullptr,
                  std::string("m3_GetResultsV: ") + describe(result));
            check(value == 42, "f() == 42 (got " + std::to_string(value) + ")");
        }
    }

    m3_FreeRuntime(rt);
    m3_FreeEnvironment(env);
}

// A malformed module must be rejected rather than accepted silently. Truncating
// the type section keeps the magic/version valid, so this exercises the parser
// and the validation path rather than the header check.
void test_rejects_truncated_module() {
    std::vector<uint8_t> bytes(probe_module().begin(), probe_module().begin() + 12);

    IM3Environment env = m3_NewEnvironment();
    check(env != nullptr, "m3_NewEnvironment (negative case)");
    if (env == nullptr) {
        return;
    }

    IM3Module mod = nullptr;
    const M3Result result = m3_ParseModule(
        env, &mod, bytes.data(), static_cast<uint32_t>(bytes.size()));
    check(result != nullptr, "truncated module is rejected");
    if (result == nullptr) {
        // Never expected: releasing it keeps a future behaviour change from
        // leaking in ASan instead of failing the assertion above.
        m3_FreeModule(mod);
    }

    m3_FreeEnvironment(env);
}

// The load-bearing WH-0 path: a THREE-result host import. Every capability
// AHFL emits imports ahfl_cap.cap_<id> with functype (i32,i32)->(i32,i32,i32)
// (the decision doc's decisive fact), and a single-arg variant has the same
// three-result shape. This proves the vendored interpreter subset actually
// pulls in and runs m3_bind.c (m3_LinkRawFunction), not just parse/compile.
//
// Module (multi-value):
//   (type $cap (func (param i32) (result i32 i32 i32)))
//   (import "ahfl_cap" "cap_0" (func $cap (type $cap)))
//   (func (export "call") (result i32 i32 i32) i32.const 42 call $cap)
[[nodiscard]] const std::vector<uint8_t> &host_import_module() {
    static const std::vector<uint8_t> bytes = {
        0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // wasm 1, multi-value
        // type section (14 bytes): (i32)->(i32,i32,i32) and ()->(i32,i32,i32)
        0x01, 0x0e, 0x02,
            0x60, 0x01, 0x7f, 0x03, 0x7f, 0x7f, 0x7f,
            0x60, 0x00,       0x03, 0x7f, 0x7f, 0x7f,
        // import section (18 bytes): ahfl_cap.cap_0 : func type 0
        0x02, 0x12, 0x01,
            0x08, 'a', 'h', 'f', 'l', '_', 'c', 'a', 'p',
            0x05, 'c', 'a', 'p', '_', '0',
            0x00, 0x00,
        // function section: func 1 : type 1
        0x03, 0x02, 0x01, 0x01,
        // export section: "call" -> func 1
        0x07, 0x08, 0x01, 0x04, 'c', 'a', 'l', 'l', 0x00, 0x01,
        // code section: body [locals 0; i32.const 42; call 0; end] (6 bytes)
        0x0a, 0x08, 0x01, 0x06, 0x00, 0x41, 0x2a, 0x10, 0x00, 0x0b,
    };
    return bytes;
}

// wasm3 raw-function ABI: results occupy stack[0..retcount), arguments follow
// at stack[retcount..]. Mirrors the independent /tmp probe that selected wasm3
// over WAMR; kept deliberately signature-exact.
const void *three_result_callback(IM3Runtime /*runtime*/,
                                  IM3ImportContext /*context*/,
                                  std::uint64_t *stack, void * /*memory*/) {
    const auto argument = static_cast<uint32_t>(stack[3]);
    stack[0] = static_cast<std::uint64_t>(argument) + 100;
    stack[1] = 8;
    stack[2] = 7;
    return m3Err_none;
}

void test_three_result_host_import() {
    const std::vector<uint8_t> &bytes = host_import_module();

    IM3Environment env = m3_NewEnvironment();
    check(env != nullptr, "m3_NewEnvironment (host import)");
    if (env == nullptr) {
        return;
    }
    IM3Runtime rt = m3_NewRuntime(env, 4096, nullptr);
    check(rt != nullptr, "m3_NewRuntime (host import)");
    if (rt == nullptr) {
        m3_FreeEnvironment(env);
        return;
    }

    IM3Module mod = nullptr;
    M3Result result = m3_ParseModule(env, &mod, bytes.data(),
                                     static_cast<uint32_t>(bytes.size()));
    check(result == nullptr, std::string("host import m3_ParseModule: ") +
                                 describe(result));
    if (result != nullptr) {
        m3_FreeRuntime(rt);
        m3_FreeEnvironment(env);
        return;
    }
    result = m3_LoadModule(rt, mod);
    check(result == nullptr, std::string("host import m3_LoadModule: ") +
                                 describe(result));
    if (result != nullptr) {
        m3_FreeModule(mod);
        m3_FreeRuntime(rt);
        m3_FreeEnvironment(env);
        return;
    }

    result = m3_LinkRawFunction(mod, "ahfl_cap", "cap_0", "iii(i)",
                                &three_result_callback);
    check(result == nullptr,
          std::string("m3_LinkRawFunction iii(i): ") + describe(result));

    IM3Function fn = nullptr;
    if (result == nullptr) {
        result = m3_FindFunction(&fn, rt, "call");
        check(result == nullptr,
              std::string("host import m3_FindFunction: ") + describe(result));
    }
    if (result == nullptr && fn != nullptr) {
        check(m3_GetRetCount(fn) == 3, "call returns three values");
        result = m3_CallV(fn);
        check(result == nullptr, std::string("host import m3_CallV: ") +
                                     describe(result));
        if (result == nullptr) {
            uint32_t a = 0;
            uint32_t b = 0;
            uint32_t c = 0;
            result = m3_GetResultsV(fn, &a, &b, &c);
            check(result == nullptr, std::string("host import m3_GetResultsV: ") +
                                         describe(result));
            check(a == 142 && b == 8 && c == 7,
                  "three-result import yields 142,8,7 (got " +
                      std::to_string(a) + "," + std::to_string(b) + "," +
                      std::to_string(c) + ")");
        }
    }

    m3_FreeRuntime(rt);
    m3_FreeEnvironment(env);
}

} // namespace

int main() {
    test_version_pin();
    test_executes_exported_function();
    test_three_result_host_import();
    test_rejects_truncated_module();

    std::cout << pass_count << "/" << test_count << " tests passed\n";
    return (pass_count == test_count) ? EXIT_SUCCESS : EXIT_FAILURE;
}
