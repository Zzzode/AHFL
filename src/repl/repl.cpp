#include "ahfl/repl/repl.hpp"

namespace ahfl::repl {

ReplCommandKind parse_command(const std::string& input) {
    if (input.empty()) return ReplCommandKind::Unknown;

    if (input == ":quit" || input == ":q") return ReplCommandKind::Quit;
    if (input == ":help" || input == ":h") return ReplCommandKind::Help;
    if (input.starts_with(":type ") || input == ":type") return ReplCommandKind::Type;
    if (input.starts_with(":verify ") || input == ":verify") return ReplCommandKind::Verify;
    if (input.starts_with(":simulate ") || input == ":simulate") return ReplCommandKind::Simulate;

    return ReplCommandKind::Eval;
}

ReplResult execute_command(const std::string& input) {
    ReplResult result;
    result.command = parse_command(input);

    switch (result.command) {
        case ReplCommandKind::Help:
            result.success = true;
            result.output = get_help_text();
            break;
        case ReplCommandKind::Quit:
            result.success = true;
            result.output = "Goodbye.";
            break;
        case ReplCommandKind::Type:
            result.success = true;
            result.output = "(type inference not yet connected)";
            break;
        case ReplCommandKind::Verify:
            result.success = true;
            result.output = "(verification not yet connected)";
            break;
        case ReplCommandKind::Simulate:
            result.success = true;
            result.output = "(simulation not yet connected)";
            break;
        case ReplCommandKind::Eval:
            result.success = true;
            result.output = "(evaluation not yet connected)";
            break;
        case ReplCommandKind::Unknown:
            result.success = false;
            result.error = "unknown command: " + input;
            break;
    }

    return result;
}

std::string get_help_text() {
    return "AHFL REPL Commands:\n"
           "  :type <expr>     Show type of expression\n"
           "  :verify <decl>   Run formal verification\n"
           "  :simulate <agent> Step through agent states\n"
           "  :help, :h        Show this help\n"
           "  :quit, :q        Exit REPL\n"
           "  <expr>           Evaluate expression\n";
}

Repl::Repl(ReplConfig config) : config_(std::move(config)) {}

ReplResult Repl::process_input(const std::string& input) {
    history_.push_back(input);

    auto cmd = parse_command(input);
    ReplResult result;
    result.command = cmd;

    switch (cmd) {
        case ReplCommandKind::Help:
            result.success = true;
            result.output = get_help_text();
            break;
        case ReplCommandKind::Quit:
            result.success = true;
            result.output = "Goodbye.";
            break;
        case ReplCommandKind::Type:
            if (type_handler_) {
                auto arg = input.substr(6); // skip ":type "
                result.output = type_handler_(arg);
                result.success = true;
            } else {
                result.output = "(type handler not set)";
                result.success = true;
            }
            break;
        case ReplCommandKind::Verify:
            if (verify_handler_) {
                auto arg = input.substr(8); // skip ":verify "
                result.output = verify_handler_(arg);
                result.success = true;
            } else {
                result.output = "(verify handler not set)";
                result.success = true;
            }
            break;
        case ReplCommandKind::Simulate:
            result.output = "(simulation not yet connected)";
            result.success = true;
            break;
        case ReplCommandKind::Eval:
            if (eval_handler_) {
                result.output = eval_handler_(input);
                result.success = true;
            } else {
                result.output = "(eval handler not set)";
                result.success = true;
            }
            break;
        case ReplCommandKind::Unknown:
            result.success = false;
            result.error = "unknown command";
            break;
    }

    return result;
}

const ReplConfig& Repl::config() const { return config_; }
size_t Repl::history_size() const { return history_.size(); }
const std::vector<std::string>& Repl::history() const { return history_; }

void Repl::set_eval_handler(std::function<std::string(const std::string&)> handler) {
    eval_handler_ = std::move(handler);
}

void Repl::set_type_handler(std::function<std::string(const std::string&)> handler) {
    type_handler_ = std::move(handler);
}

void Repl::set_verify_handler(std::function<std::string(const std::string&)> handler) {
    verify_handler_ = std::move(handler);
}

} // namespace ahfl::repl
