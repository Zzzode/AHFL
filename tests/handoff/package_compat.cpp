#include "ahfl/backends/native_json.hpp"
#include "ahfl/handoff/package.hpp"
#include "ahfl/ir/ir.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::size_t count_occurrences(const std::string &haystack, const std::string &needle) {
    std::size_t count = 0;
    std::size_t position = 0;

    while ((position = haystack.find(needle, position)) != std::string::npos) {
        ++count;
        position += needle.size();
    }

    return count;
}

[[nodiscard]] bool contains_text(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

int run_normalize_identity_format_version() {
    ahfl::handoff::PackageMetadata metadata;
    metadata.identity = ahfl::handoff::PackageIdentity{
        .format_version = "ahfl.native-package.v999",
        .name = "compat-check",
        .version = "1.2.3",
    };

    const auto package = ahfl::handoff::lower_package(ahfl::ir::Program{}, std::move(metadata));
    if (!package.metadata.identity.has_value() ||
        package.metadata.identity->format_version != ahfl::handoff::kFormatVersion ||
        package.metadata.identity->name != "compat-check" ||
        package.metadata.identity->version != "1.2.3") {
        std::cerr << "unexpected normalized identity\n";
        return 1;
    }

    std::ostringstream output;
    ahfl::print_package_native_json(package, output);

    const auto rendered = output.str();
    const std::string expected_format_version = "\"format_version\": \"ahfl.native-package.v1\"";
    if (count_occurrences(rendered, expected_format_version) != 2 ||
        rendered.find("ahfl.native-package.v999") != std::string::npos) {
        std::cerr << "unexpected native json format version rendering\n";
        return 1;
    }

    return 0;
}

int run_omit_empty_provenance() {
    ahfl::handoff::Package package;
    package.executable_targets.push_back(ahfl::handoff::AgentExecutable{
        .canonical_name = "compat::Agent",
        .input_type = "compat::Request",
        .context_type = "compat::Context",
        .output_type = "compat::Response",
        .states = {"Init"},
        .initial_state = "Init",
        .final_states = {"Init"},
    });

    std::ostringstream output;
    ahfl::print_package_native_json(package, output);

    if (contains_text(output.str(), "\"provenance\"")) {
        std::cerr << "unexpected empty provenance rendering\n";
        return 1;
    }

    return 0;
}

int run_escape_control_characters() {
    std::string raw = "compat";
    raw.push_back('\b');
    raw.push_back('\f');
    raw.push_back('\x01');
    raw.push_back('\n');
    raw += "\\\"";

    ahfl::handoff::PackageMetadata metadata;
    metadata.identity = ahfl::handoff::PackageIdentity{
        .name = raw,
        .version = "1.0.0",
    };

    std::ostringstream native_output;
    ahfl::print_package_native_json(ahfl::handoff::lower_package(ahfl::ir::Program{}, std::move(metadata)),
                                    native_output);

    const std::string expected_escape = "compat\\b\\f\\u0001\\n\\\\\\\"";
    if (!contains_text(native_output.str(), "\"name\": \"" + expected_escape + "\"")) {
        std::cerr << "native json did not escape control characters\n";
        return 1;
    }

    ahfl::ir::Program program;
    program.declarations.push_back(ahfl::ir::ModuleDecl{
        .name = raw,
    });

    std::ostringstream ir_output;
    ahfl::print_program_ir_json(program, ir_output);
    if (!contains_text(ir_output.str(), "\"name\": \"" + expected_escape + "\"")) {
        std::cerr << "ir json did not escape control characters\n";
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: package_compat <case>\n";
        return 2;
    }

    const std::string test_case = argv[1];
    if (test_case == "normalize-identity-format-version") {
        return run_normalize_identity_format_version();
    }

    if (test_case == "omit-empty-provenance") {
        return run_omit_empty_provenance();
    }

    if (test_case == "escape-control-characters") {
        return run_escape_control_characters();
    }

    std::cerr << "unknown test case: " << test_case << '\n';
    return 2;
}
