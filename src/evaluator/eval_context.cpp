#include "ahfl/evaluator/eval_context.hpp"

namespace ahfl::evaluator {

// ============================================================================
// Input scope
// ============================================================================

void EvalContext::set_input(const std::string &name, Value value) {
    input_scope_.insert_or_assign(name, std::move(value));
}

std::optional<Value> EvalContext::get_input(const std::string &name) const {
    auto it = input_scope_.find(name);
    if (it == input_scope_.end())
        return std::nullopt;
    return clone_value(it->second);
}

// ============================================================================
// Context scope
// ============================================================================

void EvalContext::set_ctx(const std::string &name, Value value) {
    ctx_scope_.insert_or_assign(name, std::move(value));
}

std::optional<Value> EvalContext::get_ctx(const std::string &name) const {
    auto it = ctx_scope_.find(name);
    if (it == ctx_scope_.end())
        return std::nullopt;
    return clone_value(it->second);
}

// ============================================================================
// Node output scope
// ============================================================================

void EvalContext::set_node_output(const std::string &node, const std::string &field, Value value) {
    node_output_scope_[node].insert_or_assign(field, std::move(value));
}

std::optional<Value> EvalContext::get_node_output(const std::string &node,
                                                  const std::string &field) const {
    auto node_it = node_output_scope_.find(node);
    if (node_it == node_output_scope_.end())
        return std::nullopt;
    auto field_it = node_it->second.find(field);
    if (field_it == node_it->second.end())
        return std::nullopt;
    return clone_value(field_it->second);
}

// ============================================================================
// Local scope
// ============================================================================

void EvalContext::bind_local(const std::string &name, Value value) {
    local_scope_.insert_or_assign(name, std::move(value));
}

std::optional<Value> EvalContext::get_local(const std::string &name) const {
    auto it = local_scope_.find(name);
    if (it == local_scope_.end())
        return std::nullopt;
    return clone_value(it->second);
}

// ============================================================================
// Path lookup: input.field -> input_scope, ctx.field -> ctx_scope,
//              node_name.field -> node_output_scope
// ============================================================================

std::optional<Value> EvalContext::lookup_path(const std::string &root,
                                              const std::string &member) const {
    if (root == "input") {
        return get_input(member);
    }
    if (root == "ctx") {
        return get_ctx(member);
    }
    // Otherwise treat root as node name
    return get_node_output(root, member);
}

} // namespace ahfl::evaluator
