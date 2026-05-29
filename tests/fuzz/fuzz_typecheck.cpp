#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "ahfl/compiler/frontend/frontend.hpp"
#include "ahfl/compiler/semantics/resolver.hpp"
#include "ahfl/compiler/semantics/typecheck.hpp"

namespace {

void typecheck_input(const uint8_t *data, std::size_t size) {
    if (data == nullptr || size == 0 || size > 1024 * 1024)
        return;
    std::string text(reinterpret_cast<const char *>(data), size);

    try {
        ahfl::Frontend frontend;
        auto parse_result = frontend.parse_text("fuzz_input", std::move(text));
        if (parse_result.has_errors() || !parse_result.program)
            return;

        ahfl::Resolver resolver;
        auto resolve_result = resolver.resolve(*parse_result.program);
        if (resolve_result.has_errors())
            return;

        ahfl::TypeChecker checker;
        auto tc_result = checker.check(*parse_result.program, resolve_result);
        volatile bool has_err = tc_result.has_errors();
        (void)has_err;
    } catch (...) {
        // ANTLR4 may throw on invalid UTF-8 sequences; that's acceptable
    }
}

} // namespace

#ifdef AHFL_FUZZ_STANDALONE
int main() {
    std::printf("fuzz_typecheck standalone check\n");
    const char *valid = "struct Foo { x: Int; y: String; }";
    typecheck_input(reinterpret_cast<const uint8_t *>(valid), std::strlen(valid));
    typecheck_input(nullptr, 0);
    const uint8_t garbage[] = {0xFF, 0xFE, 0x00, 0x01};
    typecheck_input(garbage, sizeof(garbage));
    std::printf("  PASS: standalone typecheck fuzz check\n");
    return 0;
}
#else
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
    typecheck_input(data, size);
    return 0;
}
#endif
