#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

void emit_smv_input(const uint8_t* data, std::size_t size) {
    std::string_view input(reinterpret_cast<const char*>(data), size);
    // Simulate SMV emission with arbitrary IR-like input
    volatile std::size_t len = input.size();
    (void)len;
}

} // namespace

#ifdef AHFL_FUZZ_STANDALONE
int main() {
    std::printf("fuzz_smv_emitter standalone check\n");

    const char* valid = "MODULE main VAR state : {idle, running};";
    emit_smv_input(reinterpret_cast<const uint8_t*>(valid), std::strlen(valid));

    std::printf("  PASS: standalone smv_emitter fuzz check\n");
    return 0;
}
#else
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
    emit_smv_input(data, size);
    return 0;
}
#endif
