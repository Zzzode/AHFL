#pragma once
#include <functional>
#include <string>
#include <vector>

namespace ahfl::repl {

enum class ReplCommandKind {
    Eval,
    Type,
    Verify,
    Simulate,
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
};

[[nodiscard]] ReplCommandKind parse_command(const std::string &input);

[[nodiscard]] ReplResult execute_command(const std::string &input);

[[nodiscard]] std::string get_help_text();

class Repl {
  public:
    explicit Repl(ReplConfig config = {});

    [[nodiscard]] ReplResult process_input(const std::string &input);

    [[nodiscard]] const ReplConfig &config() const;
    [[nodiscard]] size_t history_size() const;
    [[nodiscard]] const std::vector<std::string> &history() const;

    void set_eval_handler(std::function<std::string(const std::string &)> handler);
    void set_type_handler(std::function<std::string(const std::string &)> handler);
    void set_verify_handler(std::function<std::string(const std::string &)> handler);
    void set_simulate_handler(std::function<std::string(const std::string &)> handler);

  private:
    ReplConfig config_;
    std::vector<std::string> history_;
    std::function<std::string(const std::string &)> eval_handler_;
    std::function<std::string(const std::string &)> type_handler_;
    std::function<std::string(const std::string &)> verify_handler_;
    std::function<std::string(const std::string &)> simulate_handler_;
};

} // namespace ahfl::repl
