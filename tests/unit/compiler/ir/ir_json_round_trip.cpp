#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "ahfl/compiler/ir/lowering.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path golden_ir_dir() {
#ifdef AHFL_SOURCE_DIR
    return std::filesystem::path{AHFL_SOURCE_DIR} / "tests" / "golden" / "ir";
#else
    return std::filesystem::path{"tests"} / "golden" / "ir";
#endif
}

[[nodiscard]] std::string read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// Every committed IR JSON golden that is `print_program_ir_json` output
// (format_version "ahfl.ir.v2"). KR5.9 requires parse -> print to reproduce
// each of these byte-for-byte.
//
// The `*.opt-ir.json` / `*.opt-ir.optimized.json` goldens are intentionally
// EXCLUDED: they are a different serialization (`"format":
// "AHFL_OPT_IR_V1"`) produced by the opt-IR pipeline's own emitter, not by
// IrJsonPrinter / print_program_ir_json, so parse_program_ir_json does not
// (and should not) accept them.
constexpr std::string_view kGoldenJsonFiles[] = {
    "ok_expr_temporal.json",
    "ok_workflow_value_flow.json",
};

} // namespace

TEST_CASE("IR JSON round-trips byte-identically for every golden") {
    const auto dir = golden_ir_dir();
    for (const auto &name : kGoldenJsonFiles) {
        CAPTURE(name);
        const auto path = dir / std::string(name);
        REQUIRE(std::filesystem::exists(path));

        const std::string original = read_file(path);
        REQUIRE_FALSE(original.empty());

        const auto program = ahfl::parse_program_ir_json(original);
        REQUIRE(program.has_value());

        std::ostringstream out;
        ahfl::print_program_ir_json(*program, out);
        const std::string reemitted = out.str();

        // Byte-identical round-trip: parse(original) -> print == original.
        CHECK(reemitted == original);
    }
}

TEST_CASE("IR JSON deserializer rejects malformed input") {
    CHECK_FALSE(ahfl::parse_program_ir_json("not json").has_value());
    CHECK_FALSE(ahfl::parse_program_ir_json("{}").has_value()); // missing declarations
    CHECK_FALSE(ahfl::parse_program_ir_json("[]").has_value()); // not an object
}
