#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace ahfl::repl {

enum class ReplCommandKind {
    Eval,
    Type,
    Verify,
    Simulate,
    Clear,
    Load,
    Help,
    Quit,
    Unknown
};

struct ReplResult {
    bool success = false;
    std::string output;
    std::string error;
    ReplCommandKind command = ReplCommandKind::Eval;
};

struct ReplConfig {
    std::string prompt = "ahfl> ";
    bool show_types = true;
    bool verbose = false;
    // File loaded at startup as the initial session (empty = none). The file
    // must be self-contained: no module/import/pub-use. A load failure prints
    // a warning and starts with an empty session (kr68 §12.8.11.2).
    std::string prelude_path;
};

[[nodiscard]] ReplCommandKind parse_command(const std::string &input);

[[nodiscard]] ReplResult execute_command(const std::string &input);

[[nodiscard]] std::string get_help_text();

class Repl {
  public:
    explicit Repl(ReplConfig config = {});

    // Non-copyable, non-movable: default-handler closures capture `this`.
    Repl(const Repl &) = delete;
    Repl &operator=(const Repl &) = delete;
    Repl(Repl &&) = delete;
    Repl &operator=(Repl &&) = delete;

    [[nodiscard]] ReplResult process_input(const std::string &input);

    [[nodiscard]] const ReplConfig &config() const;
    [[nodiscard]] size_t history_size() const;
    [[nodiscard]] const std::vector<std::string> &history() const;

    // Session accumulation (kr68 §12.8.11.1).
    [[nodiscard]] size_t session_declarations() const;
    void clear_session();

    void set_eval_handler(std::function<std::string(const std::string &)> handler);
    void set_type_handler(std::function<std::string(const std::string &)> handler);
    void set_verify_handler(std::function<std::string(const std::string &)> handler);
    void set_simulate_handler(std::function<std::string(const std::string &)> handler);

  private:
    // Default handlers (member functions so they can see session_source_).
    [[nodiscard]] std::string default_eval(const std::string &input);
    [[nodiscard]] std::string default_type(const std::string &input);
    [[nodiscard]] std::string default_verify(const std::string &input);
    [[nodiscard]] std::string default_simulate(const std::string &input);
    [[nodiscard]] std::string default_load(const std::string &path);

    // Session helpers.
    [[nodiscard]] std::string with_session(const std::string &input) const;
    void append_to_session(const std::string &input);
    [[nodiscard]] bool has_module_or_import(const std::string &source) const;

    ReplConfig config_;
    std::vector<std::string> history_;
    // Accumulated accepted declarations (newline-separated). Prepended as a
    // prologue to every pipeline input so declarations on earlier lines are
    // visible to later expressions (kr68 §12.8.11.1).
    std::string session_source_;
    std::function<std::string(const std::string &)> eval_handler_;
    std::function<std::string(const std::string &)> type_handler_;
    std::function<std::string(const std::string &)> verify_handler_;
    std::function<std::string(const std::string &)> simulate_handler_;
};

} // namespace ahfl::repl
