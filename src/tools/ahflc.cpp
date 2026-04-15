#include "ahfl/frontend.hpp"

#include <functional>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

void print_usage(std::ostream &out) {
    out << "Usage: ahflc [--dump-ast] <input.ahfl>\n";
}

int run_cli(std::span<const std::string_view> arguments) {
    bool dump_ast = false;
    std::vector<std::string_view> positional;

    for (const auto argument : arguments) {
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            return 0;
        }

        if (argument == "--dump-ast") {
            dump_ast = true;
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "error: unknown option '" << argument << "'\n";
            print_usage(std::cerr);
            return 2;
        }

        positional.push_back(argument);
    }

    if (positional.size() != 1) {
        print_usage(std::cerr);
        return 2;
    }

    const ahfl::Frontend frontend;
    auto result = frontend.parse_file(std::string(positional.front()));

    result.diagnostics.render(std::cerr, std::cref(result.source));

    if (result.program && dump_ast) {
        ahfl::dump_program_outline(*result.program, std::cout);
    }

    if (result.program && !dump_ast && !result.has_errors()) {
        std::cout << "ok: scanned " << result.program->declarations.size()
                  << " top-level declaration(s)\n";
    }

    return result.has_errors() ? 1 : 0;
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));

    for (int index = 1; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }

    return run_cli(arguments);
}
