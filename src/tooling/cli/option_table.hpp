#pragma once

#include "tooling/cli/command_catalog.hpp"

#include <cstddef>
#include <optional>
#include <ostream>
#include <span>
#include <string_view>

namespace ahfl::cli {

enum class OptionArgKind {
    Flag,            // --dump-ast (no argument)
    RequiredValue,   // --project <path>
    RepeatableValue, // --search-root <dir> (can appear multiple times)
};

using OptionSetter = void (*)(CommandLineOptions &options, std::optional<std::string_view> value);

struct OptionSpec {
    std::string_view long_name;
    std::string_view short_name; // empty if no short form
    OptionArgKind arg_kind;
    OptionSetter setter;
    std::string_view help_text;
    std::string_view metavar; // e.g. "<path>" for help display; also used in missing-arg errors
};

// Parse command-line arguments using the option table.
// Returns: nullopt if parsing succeeded (continue), ParseResult if terminal (help/error).
// Populates options.positional for non-option arguments.
// Recognizes action groups plus artifact ids, e.g. `emit provider/write-attempt`.
struct ParseResult {
    bool should_exit;
    int exit_code; // 0 = help shown, 2 = usage error
};

[[nodiscard]] std::optional<ParseResult>
parse_options_from_table(std::span<const std::string_view> arguments, CommandLineOptions &options);

// Auto-generate help text from the option table + command catalog
void print_generated_usage(std::ostream &out);

} // namespace ahfl::cli
