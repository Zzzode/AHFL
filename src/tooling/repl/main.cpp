#include "tooling/repl/repl.hpp"

#include <cstdio>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

bool is_interactive_stdin() {
#if defined(_WIN32)
    return _isatty(_fileno(stdin)) != 0;
#else
    return isatty(STDIN_FILENO) != 0;
#endif
}

bool has_trailing_newline(const std::string &text) {
    return !text.empty() && text.back() == '\n';
}

void print_usage(std::ostream &out) {
    out << "Usage: ahfl-repl [--prelude <path>] [--help]\n\n"
        << "Starts the AHFL interactive REPL. Commands can also be piped on stdin.\n\n"
        << ahfl::repl::get_help_text();
}

} // namespace

int main(int argc, char **argv) {
    std::string prelude_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return 0;
        }
        if (arg == "--prelude" && i + 1 < argc) {
            prelude_path = argv[++i];
            continue;
        }
        std::cerr << "unknown option: " << arg << '\n';
        print_usage(std::cerr);
        return 2;
    }

    ahfl::repl::ReplConfig config;
    config.prelude_path = prelude_path;
    ahfl::repl::Repl repl(std::move(config));
    const bool interactive = is_interactive_stdin();
    std::string line;

    while (true) {
        if (interactive) {
            std::cout << repl.config().prompt << std::flush;
        }
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line.empty()) {
            continue;
        }

        auto result = repl.process_input(line);
        if (!result.output.empty()) {
            std::cout << result.output;
            if (!has_trailing_newline(result.output)) {
                std::cout << '\n';
            }
        }
        if (!result.success && !result.error.empty() && result.error != result.output) {
            std::cerr << result.error;
            if (!has_trailing_newline(result.error)) {
                std::cerr << '\n';
            }
        }
        if (result.command == ahfl::repl::ReplCommandKind::Quit) {
            return result.success ? 0 : 1;
        }
    }

    return 0;
}
