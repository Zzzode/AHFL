#include "ahfl/frontend.hpp"
#include "ahfl/ir.hpp"
#include "ahfl/resolver.hpp"
#include "ahfl/typecheck.hpp"
#include "ahfl/validate.hpp"

#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CommandLineOptions {
    bool dump_ast{false};
    bool dump_types{false};
    bool emit_ir{false};
    std::vector<std::string_view> positional;
};

void print_usage(std::ostream &out) {
    out << "Usage:\n"
        << "  ahflc check [--dump-ast] <input.ahfl>\n"
        << "  ahflc dump-ast <input.ahfl>\n"
        << "  ahflc dump-types <input.ahfl>\n"
        << "  ahflc emit-ir <input.ahfl>\n"
        << "  ahflc [--dump-ast] <input.ahfl>\n";
}

[[nodiscard]] bool is_subcommand(std::string_view argument) {
    return argument == "check" || argument == "dump-ast" || argument == "dump-types" ||
           argument == "emit-ir";
}

[[nodiscard]] int parse_command_line(std::span<const std::string_view> arguments,
                                     CommandLineOptions &options) {
    bool explicit_subcommand = false;

    for (const auto argument : arguments) {
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            return 0;
        }

        if (argument == "--dump-ast") {
            options.dump_ast = true;
            continue;
        }

        if (argument == "--dump-types") {
            options.dump_types = true;
            continue;
        }

        if (is_subcommand(argument) && !explicit_subcommand && options.positional.empty()) {
            explicit_subcommand = true;
            if (argument == "dump-ast") {
                options.dump_ast = true;
            }
            if (argument == "dump-types") {
                options.dump_types = true;
            }
            if (argument == "emit-ir") {
                options.emit_ir = true;
            }
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "error: unknown option '" << argument << "'\n";
            print_usage(std::cerr);
            return 2;
        }

        options.positional.push_back(argument);
    }

    return -1;
}

int run_cli(std::span<const std::string_view> arguments) {
    CommandLineOptions options;
    if (const auto parse_status = parse_command_line(arguments, options); parse_status >= 0) {
        return parse_status;
    }

    const auto action_count = static_cast<int>(options.dump_ast) +
                              static_cast<int>(options.dump_types) +
                              static_cast<int>(options.emit_ir);
    if (action_count > 1) {
        std::cerr << "error: choose at most one of dump-ast, dump-types, or emit-ir\n";
        print_usage(std::cerr);
        return 2;
    }

    if (options.positional.size() != 1) {
        print_usage(std::cerr);
        return 2;
    }

    const ahfl::Frontend frontend;
    auto parse_result = frontend.parse_file(std::string(options.positional.front()));
    parse_result.diagnostics.render(std::cerr, std::cref(parse_result.source));

    if (parse_result.program && options.dump_ast) {
        ahfl::dump_program_outline(*parse_result.program, std::cout);
    }

    if (parse_result.has_errors() || !parse_result.program) {
        return parse_result.has_errors() ? 1 : 0;
    }

    const ahfl::Resolver resolver;
    auto resolve_result = resolver.resolve(*parse_result.program);
    resolve_result.diagnostics.render(std::cerr, std::cref(parse_result.source));

    if (resolve_result.has_errors()) {
        return 1;
    }

    const ahfl::TypeChecker type_checker;
    auto type_check_result = type_checker.check(*parse_result.program, resolve_result);
    type_check_result.diagnostics.render(std::cerr, std::cref(parse_result.source));

    if (options.dump_types) {
        ahfl::dump_type_environment(
            type_check_result.environment, resolve_result.symbol_table, std::cout);
    }

    if (type_check_result.has_errors()) {
        return 1;
    }

    const ahfl::Validator validator;
    auto validation_result =
        validator.validate(*parse_result.program, resolve_result, type_check_result);
    validation_result.diagnostics.render(std::cerr, std::cref(parse_result.source));

    if (!validation_result.has_errors() && options.emit_ir) {
        ahfl::emit_program_ir(*parse_result.program, resolve_result, type_check_result, std::cout);
    }

    if (!validation_result.has_errors() && !options.dump_ast && !options.dump_types &&
        !options.emit_ir) {
        std::cout << "ok: checked " << parse_result.program->declarations.size()
                  << " top-level declaration(s), " << resolve_result.symbol_table.symbols().size()
                  << " symbol(s), " << resolve_result.references.size() << " reference(s), "
                  << type_check_result.environment.structs().size() +
                         type_check_result.environment.enums().size()
                  << " named type(s)\n";
    }

    return validation_result.has_errors() ? 1 : 0;
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
