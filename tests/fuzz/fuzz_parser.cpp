#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "ahfl/compiler/frontend/frontend.hpp"

namespace {

void parse_input(const uint8_t *data, std::size_t size) {
    if (data == nullptr || size == 0 || size > 1024 * 1024)
        return;
    std::string text(reinterpret_cast<const char *>(data), size);
    try {
        ahfl::Frontend frontend;
        auto result = frontend.parse_text("fuzz_input", std::move(text));
        volatile bool has_err = result.has_errors();
        (void)has_err;
    } catch (...) {
        // ANTLR4 may throw on invalid UTF-8 sequences; that's acceptable
    }
}

} // namespace

#ifdef AHFL_FUZZ_STANDALONE
int main() {
    std::printf("fuzz_parser standalone check\n");
    parse_input(nullptr, 0);
    const char *valid = "agent Foo { states { idle, running } }";
    parse_input(reinterpret_cast<const uint8_t *>(valid), std::strlen(valid));
    const uint8_t garbage[] = {0xFF, 0xFE, 0x00, 0x01, 0x80};
    parse_input(garbage, sizeof(garbage));
    const char *empty = "";
    parse_input(reinterpret_cast<const uint8_t *>(empty), 0);
    std::printf("  PASS: standalone parser fuzz check\n");
    return 0;
}
#else
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, std::size_t size) {
    parse_input(data, size);
    return 0;
}
#endif
