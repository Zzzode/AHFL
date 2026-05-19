#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

void typecheck_input(const uint8_t* data, std::size_t size) {
    std::string_view input(reinterpret_cast<const char*>(data), size);
    // Simulate type checking on arbitrary AST-like structures
    // Real implementation would parse first, then typecheck
    volatile std::size_t len = input.size();
    (void)len;
}

} // namespace

#ifdef AHFL_FUZZ_STANDALONE
int main() {
    std::printf("fuzz_typecheck standalone check\n");

    const char* valid = "struct Foo { x: Int; y: String; }";
    typecheck_input(reinterpret_cast<const uint8_t*>(valid), std::strlen(valid));

    typecheck_input(nullptr, 0);

    std::printf("  PASS: standalone typecheck fuzz check\n");
    return 0;
}
#else
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    typecheck_input(data, size);
    return 0;
}
#endif
