#pragma once

#include "ahfl/evaluator/value.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace ahfl::evaluator {

// ============================================================================
// EvalContext - Provides variable scopes for expression evaluation
// ============================================================================

class EvalContext {
  public:
    // --- Input scope ---
    void set_input(const std::string &name, Value value);
    [[nodiscard]] std::optional<Value> get_input(const std::string &name) const;

    // --- Context scope ---
    void set_ctx(const std::string &name, Value value);
    [[nodiscard]] std::optional<Value> get_ctx(const std::string &name) const;

    // --- Node output scope ---
    void set_node_output(const std::string &node, const std::string &field, Value value);
    [[nodiscard]] std::optional<Value> get_node_output(const std::string &node,
                                                       const std::string &field) const;

    // --- Local scope ---
    void bind_local(const std::string &name, Value value);
    [[nodiscard]] std::optional<Value> get_local(const std::string &name) const;

    // --- Path lookup ---
    // Handles patterns: input.field, ctx.field, node_name.field
    [[nodiscard]] std::optional<Value> lookup_path(const std::string &root,
                                                   const std::string &member) const;

  private:
    std::unordered_map<std::string, Value> input_scope_;
    std::unordered_map<std::string, Value> ctx_scope_;
    std::unordered_map<std::string, std::unordered_map<std::string, Value>> node_output_scope_;
    std::unordered_map<std::string, Value> local_scope_;
};

} // namespace ahfl::evaluator
