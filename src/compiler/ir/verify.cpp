#include "ahfl/compiler/ir/verify.hpp"

#include "ahfl/compiler/ir/analysis.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace ahfl::ir {
namespace {

[[nodiscard]] bool has_any_analysis(const AnalysisBundle &analyses) noexcept {
    return analyses.source_program_revision != 0 || !analyses.state_handler_summaries.empty() ||
           !analyses.workflow_node_input_summaries.empty() ||
           !analyses.workflow_return_summaries.empty() || !analyses.formal_observations.empty();
}

[[nodiscard]] std::string symbol_ref_name(const SymbolRef &symbol) {
    if (!symbol.canonical_name.empty()) {
        return symbol.canonical_name;
    }
    if (!symbol.local_name.empty()) {
        return symbol.local_name;
    }
    return "<unknown>";
}

[[nodiscard]] std::string_view symbol_ref_kind_name(SymbolRefKind kind) noexcept {
    switch (kind) {
    case SymbolRefKind::Unknown:
        return "unknown";
    case SymbolRefKind::Type:
        return "type";
    case SymbolRefKind::Const:
        return "const";
    case SymbolRefKind::Capability:
        return "capability";
    case SymbolRefKind::Predicate:
        return "predicate";
    case SymbolRefKind::Agent:
        return "agent";
    case SymbolRefKind::Workflow:
        return "workflow";
    case SymbolRefKind::Function:
        return "function";
    }
    return "unknown";
}

[[nodiscard]] bool is_backend_ready_mode(IrVerificationMode mode) noexcept {
    return mode == IrVerificationMode::BackendReady ||
           mode == IrVerificationMode::SerializedArtifact;
}

[[nodiscard]] bool contains_sentinel(std::string_view value) noexcept {
    return value.find("<missing-") != std::string_view::npos ||
           value.find("<invalid-") != std::string_view::npos;
}

[[nodiscard]] bool canonical_matches_decl_name(std::string_view canonical,
                                               std::string_view decl_name) noexcept {
    if (canonical == decl_name) {
        return true;
    }
    const auto pos = canonical.rfind("::");
    return pos != std::string_view::npos && canonical.substr(pos + 2) == decl_name;
}

[[nodiscard]] std::string
analysis_key(std::string_view owner, std::size_t index, std::string_view name = {}) {
    std::string key;
    key.reserve(owner.size() + name.size() + 32);
    key += owner;
    key += '\x1f';
    key += std::to_string(index);
    key += '\x1f';
    key += name;
    return key;
}

struct SymbolIdentity {
    std::string name;
    SymbolRefKind kind{SymbolRefKind::Unknown};
};

class ProgramVerifier {
  public:
    ProgramVerifier(const Program &program, IrVerificationMode mode)
        : program_(program), mode_(mode) {}

    [[nodiscard]] VerificationResult run() {
        verify_expression_arena();
        collect_decl_symbol_identities();
        verify_declarations();
        verify_analysis_phase();
        return std::move(result_);
    }

  private:
    void add(VerificationSeverity severity, std::string path, std::string message) {
        result_.diagnostics.push_back(VerificationDiagnostic{
            .severity = severity,
            .path = std::move(path),
            .message = std::move(message),
        });
    }

    void add_error(std::string path, std::string message) {
        add(VerificationSeverity::Error, std::move(path), std::move(message));
    }

    void add_warning(std::string path, std::string message) {
        add(VerificationSeverity::Warning, std::move(path), std::move(message));
    }

    void verify_expression_arena() {
        std::unordered_set<std::uint32_t> expr_ids;
        expr_ids.reserve(program_.expr_arena.size());
        const auto expressions = program_.all_exprs();
        for (std::uint32_t index = 0; index < expressions.size(); ++index) {
            const auto *expr = expressions[index];
            if (expr == nullptr) {
                add_error("expr_arena[" + std::to_string(index) + "]",
                          "arena expression pointer is null");
                continue;
            }
            if (!expr_ids.insert(expr->id).second) {
                add_error("expr_arena[" + std::to_string(index) + "]",
                          "duplicate expression id " + std::to_string(expr->id));
            }
            verify_source_range(expr->source_range,
                                "expr_arena[" + std::to_string(index) + "]",
                                "expression source range");
            verify_type_ref(expr->resolved_type,
                            "expr_arena[" + std::to_string(index) + "].resolved_type");
            verify_expr_children(*expr, "expr_arena[" + std::to_string(index) + "]");
        }
    }

    void collect_decl_symbol_identities() {
        for (const auto &decl : program_.declarations) {
            std::visit([this](const auto &value) { collect_decl_symbol_identity(value); }, decl);
        }
    }

    template <typename DeclT> void collect_decl_symbol_identity(const DeclT & /*decl*/) {}

    void collect_decl_symbol_identity(const ConstDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "const " + decl.name);
    }
    void collect_decl_symbol_identity(const TypeAliasDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "type " + decl.name);
    }
    void collect_decl_symbol_identity(const StructDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "struct " + decl.name);
    }
    void collect_decl_symbol_identity(const EnumDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "enum " + decl.name);
    }
    void collect_decl_symbol_identity(const CapabilityDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "capability " + decl.name);
    }
    void collect_decl_symbol_identity(const PredicateDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "predicate " + decl.name);
    }
    void collect_decl_symbol_identity(const AgentDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "agent " + decl.name);
    }
    void collect_decl_symbol_identity(const WorkflowDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "workflow " + decl.name);
    }
    // P2c (RFC §3.2.2): collect a fn declaration's self symbol reference.
    void collect_decl_symbol_identity(const FnDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "fn " + decl.name);
    }

    void collect_decl_symbol_identity(const InstanceDecl &decl) {
        collect_symbol_identity(decl.symbol_ref, "instance " + decl.name);
    }

    void collect_symbol_identity(const SymbolRef &symbol, const std::string &path) {
        if (!symbol.id.has_value()) {
            return;
        }
        auto [iter, inserted] = symbol_identities_.emplace(
            *symbol.id, SymbolIdentity{.name = symbol_ref_name(symbol), .kind = symbol.kind});
        if (!inserted && iter->second.name != symbol_ref_name(symbol)) {
            add_error(path,
                      "symbol id " + std::to_string(*symbol.id) + " is also bound to " +
                          iter->second.name);
        }
        if (!inserted && iter->second.kind != symbol.kind) {
            add_error(path,
                      "symbol id " + std::to_string(*symbol.id) + " has inconsistent kind " +
                          std::string(symbol_ref_kind_name(symbol.kind)) + " vs " +
                          std::string(symbol_ref_kind_name(iter->second.kind)));
        }
    }

    void verify_declarations() {
        std::unordered_set<std::uint32_t> provenance_ids;
        provenance_ids.reserve(program_.declarations.size());
        for (std::uint32_t index = 0; index < program_.declarations.size(); ++index) {
            std::visit(
                [this, &provenance_ids, index](const auto &value) {
                    const auto path = declaration_path(value, index);
                    verify_provenance(value.provenance, path, provenance_ids);
                    verify_decl(value, path);
                },
                program_.declarations[index]);
        }
    }

    template <typename DeclT>
    [[nodiscard]] std::string declaration_path(const DeclT &decl, std::uint32_t index) const {
        if constexpr (requires { decl.name; }) {
            if (!decl.name.empty()) {
                return "decl[" + std::to_string(index) + "](" + decl.name + ")";
            }
        }
        return "decl[" + std::to_string(index) + "]";
    }

    void verify_provenance(const DeclarationProvenance &provenance,
                           const std::string &path,
                           std::unordered_set<std::uint32_t> &ids) {
        if (!ids.insert(provenance.id).second) {
            add_error(path, "duplicate declaration provenance id " + std::to_string(provenance.id));
        }
        verify_source_range(provenance.source_range, path + ".provenance", "source range");
    }

    void verify_decl(const ModuleDecl & /*decl*/, const std::string & /*path*/) {}

    void verify_decl(const ImportDecl & /*decl*/, const std::string & /*path*/) {}

    void verify_decl(const ConstDecl &decl, const std::string &path) {
        verify_required_expr_ref(decl.value, path + ".value");
        verify_type_ref(decl.type_ref, path + ".type_ref");
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Const, decl.name);
    }

    void verify_decl(const TypeAliasDecl &decl, const std::string &path) {
        verify_type_ref(decl.aliased_type_ref, path + ".aliased_type_ref");
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Type, decl.name);
    }

    void verify_decl(const StructDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Type, decl.name);
        for (std::uint32_t index = 0; index < decl.fields.size(); ++index) {
            const auto field_path = path + ".fields[" + std::to_string(index) + "]";
            verify_optional_expr_ref(decl.fields[index].default_value, field_path + ".default");
            verify_type_ref(decl.fields[index].type_ref, field_path + ".type_ref");
            verify_source_range(decl.fields[index].source_range, field_path, "source range");
        }
    }

    void verify_decl(const EnumDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Type, decl.name);
    }

    void verify_decl(const CapabilityDecl &decl, const std::string &path) {
        verify_symbol_ref(
            decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Capability, decl.name);
        verify_params(decl.params, path + ".params");
        verify_type_ref(decl.return_type_ref, path + ".return_type_ref");
        verify_source_range(decl.effect.source_range, path + ".effect", "source range");
    }

    void verify_decl(const PredicateDecl &decl, const std::string &path) {
        verify_symbol_ref(
            decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Predicate, decl.name);
        verify_params(decl.params, path + ".params");
    }

    void verify_decl(const AgentDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Agent, decl.name);
        verify_type_ref(decl.input_type_ref, path + ".input_type_ref");
        verify_type_ref(decl.context_type_ref, path + ".context_type_ref");
        verify_type_ref(decl.output_type_ref, path + ".output_type_ref");
        for (std::uint32_t index = 0; index < decl.capability_refs.size(); ++index) {
            verify_symbol_ref(decl.capability_refs[index],
                              path + ".capability_refs[" + std::to_string(index) + "]",
                              SymbolRefKind::Capability);
        }
    }

    void verify_decl(const ContractDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.target_ref, path + ".target_ref", SymbolRefKind::Agent);
        for (std::uint32_t index = 0; index < decl.clauses.size(); ++index) {
            const auto clause_path = path + ".clauses[" + std::to_string(index) + "]";
            verify_source_range(decl.clauses[index].source_range, clause_path, "source range");
            std::visit(
                [this, &clause_path](const auto &value) {
                    verify_contract_clause_value(value, clause_path + ".value");
                },
                decl.clauses[index].value);
        }
    }

    void verify_decl(const FlowDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.target_ref, path + ".target_ref", SymbolRefKind::Agent);
        for (std::uint32_t index = 0; index < decl.state_handlers.size(); ++index) {
            const auto handler_path = path + ".state_handlers[" + std::to_string(index) + "]";
            verify_source_range(
                decl.state_handlers[index].source_range, handler_path, "source range");
            verify_block(decl.state_handlers[index].body, handler_path + ".body");
        }
    }

    void verify_decl(const WorkflowDecl &decl, const std::string &path) {
        verify_symbol_ref(
            decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Workflow, decl.name);
        verify_type_ref(decl.input_type_ref, path + ".input_type_ref");
        verify_type_ref(decl.output_type_ref, path + ".output_type_ref");
        for (std::uint32_t index = 0; index < decl.nodes.size(); ++index) {
            const auto node_path = path + ".nodes[" + std::to_string(index) + "]";
            verify_required_expr_ref(decl.nodes[index].input, node_path + ".input");
            verify_symbol_ref(
                decl.nodes[index].target_ref, node_path + ".target_ref", SymbolRefKind::Agent);
            verify_source_range(decl.nodes[index].source_range, node_path, "source range");
        }
        for (std::uint32_t index = 0; index < decl.safety.size(); ++index) {
            verify_temporal_ptr(decl.safety[index],
                                path + ".safety[" + std::to_string(index) + "]");
        }
        for (std::uint32_t index = 0; index < decl.liveness.size(); ++index) {
            verify_temporal_ptr(decl.liveness[index],
                                path + ".liveness[" + std::to_string(index) + "]");
        }
        verify_required_expr_ref(decl.return_value, path + ".return_value");
    }

    // P2c/P2d: verify a top-level fn declaration. Prototypes and @builtin
    // declarations have no body; source fn definitions carry a lowered block.
    void verify_decl(const FnDecl &decl, const std::string &path) {
        verify_symbol_ref(
            decl.symbol_ref, path + ".symbol_ref", SymbolRefKind::Function, decl.name);
        verify_params(decl.params, path + ".params");
        if (decl.has_return_type) {
            verify_type_ref(decl.return_type_ref, path + ".return_type_ref");
        }
        verify_source_range(decl.effect.source_range, path + ".effect", "source range");
        for (std::uint32_t index = 0; index < decl.effect.capabilities.size(); ++index) {
            const auto capability_path =
                path + ".effect.capabilities[" + std::to_string(index) + "]";
            verify_symbol_ref(
                decl.effect.capabilities[index], capability_path, SymbolRefKind::Capability);
        }
        if (decl.has_body) {
            if (decl.body) {
                verify_block(*decl.body, path + ".body");
            } else {
                add_error(path + ".body", "function has_body is true but body is null");
            }
        } else if (decl.body) {
            add_error(path + ".body", "function body is present but has_body is false");
        }
    }

    void verify_decl(const InstanceDecl &decl, const std::string &path) {
        // InstanceDecl is a concrete instance copy of a nominal declaration.
        // symbol_ref.kind must match InstanceKind; type_ref fields that are
        // irrelevant for the given kind may be Unresolved and are skipped.
        switch (decl.kind) {
        case InstanceKind::Capability:
            verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref",
                              SymbolRefKind::Capability);
            break;
        case InstanceKind::Predicate:
            verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref",
                              SymbolRefKind::Predicate);
            break;
        case InstanceKind::Agent:
            verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref",
                              SymbolRefKind::Agent);
            break;
        case InstanceKind::Workflow:
            verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref",
                              SymbolRefKind::Workflow);
            break;
        case InstanceKind::Unknown:
            if (decl.symbol_ref.id.has_value() || !decl.symbol_ref.canonical_name.empty()) {
                verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref",
                                  SymbolRefKind::Unknown);
            }
            break;
        }
        for (std::uint32_t index = 0; index < decl.type_args.size(); ++index) {
            verify_type_ref(decl.type_args[index],
                            path + ".type_args[" + std::to_string(index) + "]");
        }
        verify_params(decl.params, path + ".params");
        // Only validate kind-relevant type-ref fields.
        switch (decl.kind) {
        case InstanceKind::Capability:
            verify_type_ref(decl.return_type_ref, path + ".return_type_ref");
            break;
        case InstanceKind::Predicate:
            break;
        case InstanceKind::Agent:
            verify_type_ref(decl.agent_input_type_ref, path + ".agent_input_type_ref");
            verify_type_ref(decl.agent_context_type_ref, path + ".agent_context_type_ref");
            verify_type_ref(decl.agent_output_type_ref, path + ".agent_output_type_ref");
            break;
        case InstanceKind::Workflow:
            verify_type_ref(decl.workflow_input_type_ref, path + ".workflow_input_type_ref");
            verify_type_ref(decl.workflow_output_type_ref, path + ".workflow_output_type_ref");
            break;
        case InstanceKind::Unknown:
            break;
        }
    }

    void verify_params(const std::vector<ParamDecl> &params, const std::string &path) {
        for (std::uint32_t index = 0; index < params.size(); ++index) {
            const auto param_path = path + "[" + std::to_string(index) + "]";
            verify_type_ref(params[index].type_ref, param_path + ".type_ref");
            verify_source_range(params[index].source_range, param_path, "source range");
        }
    }

    void verify_contract_clause_value(const ExprRef &expr, const std::string &path) {
        verify_required_expr_ref(expr, path);
    }

    void verify_contract_clause_value(const TemporalExprPtr &expr, const std::string &path) {
        verify_temporal_ptr(expr, path);
    }

    void verify_block(const Block &block, const std::string &path) {
        verify_source_range(block.source_range, path, "source range");
        for (std::uint32_t index = 0; index < block.statements.size(); ++index) {
            const auto stmt_path = path + ".statements[" + std::to_string(index) + "]";
            if (!block.statements[index]) {
                add_error(stmt_path, "statement pointer is null");
                continue;
            }
            verify_statement(*block.statements[index], stmt_path);
        }
    }

    void verify_statement(const Statement &stmt, const std::string &path) {
        if (!statement_ids_.insert(stmt.id).second) {
            add_error(path, "duplicate statement id " + std::to_string(stmt.id));
        }
        verify_source_range(stmt.source_range, path, "source range");
        std::visit([this, &path](const auto &value) { verify_statement_node(value, path); },
                   stmt.node);
    }

    void verify_statement_node(const LetStatement &stmt, const std::string &path) {
        verify_type_ref(stmt.type_ref, path + ".type_ref");
        verify_required_expr_ref(stmt.initializer, path + ".initializer");
    }

    void verify_statement_node(const AssignStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.value, path + ".value");
    }

    void verify_statement_node(const IfStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.condition, path + ".condition");
        if (stmt.then_block) {
            verify_block(*stmt.then_block, path + ".then");
        } else {
            add_error(path + ".then", "then block is null");
        }
        if (stmt.else_block) {
            verify_block(*stmt.else_block, path + ".else");
        }
    }

    void verify_statement_node(const GotoStatement &stmt, const std::string &path) {
        if (!is_backend_ready_mode(mode_)) {
            return;
        }
        if (stmt.target_state.empty()) {
            add_error(path, "goto statement is missing target state");
        } else if (contains_sentinel(stmt.target_state)) {
            add_error(path, "goto statement contains sentinel target state");
        }
    }

    void verify_statement_node(const ReturnStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.value, path + ".value");
    }

    void verify_statement_node(const AssertStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.condition, path + ".condition");
    }

    void verify_statement_node(const ExprStatement &stmt, const std::string &path) {
        verify_required_expr_ref(stmt.expr, path + ".expr");
    }

    void verify_expr_children(const Expr &expr, const std::string &path) {
        std::visit([this, &path](const auto &value) { verify_expr_node(value, path); }, expr.node);
    }

    template <typename ExprT>
    void verify_expr_node(const ExprT & /*expr*/, const std::string & /*path*/) {}

    void verify_expr_node(const SomeExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.value, path + ".value");
    }

    void verify_expr_node(const CallExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.callee)) {
            add_error(path, "call expression contains sentinel callee");
        }
        for (std::uint32_t index = 0; index < expr.arguments.size(); ++index) {
            verify_required_expr_ref(expr.arguments[index],
                                     path + ".arguments[" + std::to_string(index) + "]");
        }
    }

    void verify_expr_node(const LambdaExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_)) {
            for (std::uint32_t index = 0; index < expr.params.size(); ++index) {
                if (contains_sentinel(expr.params[index])) {
                    add_error(path + ".params[" + std::to_string(index) + "]",
                              "lambda parameter contains sentinel name");
                }
            }
        }
        verify_required_expr_ref(expr.body, path + ".body");
    }

    void verify_expr_node(const StructLiteralExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.type_name)) {
            add_error(path, "struct literal contains sentinel type name");
        }
        for (std::uint32_t index = 0; index < expr.fields.size(); ++index) {
            verify_required_expr_ref(expr.fields[index].value,
                                     path + ".fields[" + std::to_string(index) + "].value");
        }
    }

    void verify_expr_node(const QualifiedValueExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.value)) {
            add_error(path, "qualified value expression contains sentinel payload");
        }
    }

    void verify_expr_node(const ListLiteralExpr &expr, const std::string &path) {
        verify_expr_ref_list(expr.items, path + ".items");
    }

    void verify_expr_node(const SetLiteralExpr &expr, const std::string &path) {
        verify_expr_ref_list(expr.items, path + ".items");
    }

    void verify_expr_node(const MapLiteralExpr &expr, const std::string &path) {
        for (std::uint32_t index = 0; index < expr.entries.size(); ++index) {
            const auto entry_path = path + ".entries[" + std::to_string(index) + "]";
            verify_required_expr_ref(expr.entries[index].key, entry_path + ".key");
            verify_required_expr_ref(expr.entries[index].value, entry_path + ".value");
        }
    }

    void verify_expr_node(const UnaryExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.operand, path + ".operand");
    }

    void verify_expr_node(const BinaryExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.lhs, path + ".lhs");
        verify_required_expr_ref(expr.rhs, path + ".rhs");
    }

    void verify_expr_node(const MemberAccessExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.base, path + ".base");
    }

    void verify_expr_node(const IndexAccessExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.base, path + ".base");
        verify_required_expr_ref(expr.index, path + ".index");
    }

    void verify_expr_node(const MatchExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.scrutinee, path + ".scrutinee");
        for (std::uint32_t index = 0; index < expr.arms.size(); ++index) {
            const auto arm_path = path + ".arms[" + std::to_string(index) + "]";
            verify_match_pattern(expr.arms[index].pattern, arm_path + ".pattern");
            verify_optional_expr_ref(expr.arms[index].guard, arm_path + ".guard");
            verify_required_expr_ref(expr.arms[index].body, arm_path + ".body");
        }
    }

    void verify_match_pattern(const MatchPattern &pattern, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(pattern.text)) {
            add_error(path, "match pattern contains sentinel text");
        }
        std::visit([this, &path](const auto &node) { verify_match_pattern_node(node, path); },
                   pattern.node);
    }

    template <typename PatternT>
    void verify_match_pattern_node(const PatternT & /*pattern*/, const std::string & /*path*/) {}

    void verify_match_pattern_node(const VariantPattern &pattern, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(pattern.path)) {
            add_error(path, "variant pattern contains sentinel path");
        }
        for (std::uint32_t index = 0; index < pattern.subpatterns.size(); ++index) {
            const auto subpattern_path = path + ".subpatterns[" + std::to_string(index) + "]";
            if (!pattern.subpatterns[index]) {
                add_error(subpattern_path, "match subpattern pointer is null");
                continue;
            }
            verify_match_pattern(*pattern.subpatterns[index], subpattern_path);
        }
    }

    void verify_match_pattern_node(const BindingPattern &pattern, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(pattern.name)) {
            add_error(path, "binding pattern contains sentinel name");
        }
        if (pattern.nested) {
            verify_match_pattern(*pattern.nested, path + ".nested");
        }
    }

    void verify_match_pattern_node(const TuplePattern &pattern, const std::string &path) {
        for (std::uint32_t index = 0; index < pattern.elements.size(); ++index) {
            const auto element_path = path + ".elements[" + std::to_string(index) + "]";
            if (!pattern.elements[index]) {
                add_error(element_path, "tuple pattern element pointer is null");
                continue;
            }
            verify_match_pattern(*pattern.elements[index], element_path);
        }
    }

    void verify_match_pattern_node(const OrPattern &pattern, const std::string &path) {
        for (std::uint32_t index = 0; index < pattern.branches.size(); ++index) {
            const auto branch_path = path + ".branches[" + std::to_string(index) + "]";
            if (!pattern.branches[index]) {
                add_error(branch_path, "or pattern branch pointer is null");
                continue;
            }
            verify_match_pattern(*pattern.branches[index], branch_path);
        }
    }

    void verify_expr_ref_list(const std::vector<ExprRef> &exprs, const std::string &path) {
        for (std::uint32_t index = 0; index < exprs.size(); ++index) {
            verify_required_expr_ref(exprs[index], path + "[" + std::to_string(index) + "]");
        }
    }

    void verify_temporal_ptr(const TemporalExprPtr &expr, const std::string &path) {
        if (!expr) {
            add_error(path, "temporal expression pointer is null");
            return;
        }
        verify_source_range(expr->source_range, path, "source range");
        std::visit([this, &path](const auto &value) { verify_temporal_node(value, path); },
                   expr->node);
    }

    void verify_temporal_node(const EmbeddedTemporalExpr &expr, const std::string &path) {
        verify_required_expr_ref(expr.expr, path + ".expr");
    }

    void verify_temporal_node(const CalledTemporalExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.capability)) {
            add_error(path, "called temporal expression contains sentinel capability");
        }
    }

    void verify_temporal_node(const InStateTemporalExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.state)) {
            add_error(path, "in_state temporal expression contains sentinel state");
        }
    }

    void verify_temporal_node(const RunningTemporalExpr &expr, const std::string &path) {
        if (is_backend_ready_mode(mode_) && contains_sentinel(expr.node)) {
            add_error(path, "running temporal expression contains sentinel node");
        }
    }

    void verify_temporal_node(const CompletedTemporalExpr &expr, const std::string &path) {
        if (!is_backend_ready_mode(mode_)) {
            return;
        }
        if (contains_sentinel(expr.node)) {
            add_error(path, "completed temporal expression contains sentinel node");
        }
        if (expr.state_name.has_value() && contains_sentinel(*expr.state_name)) {
            add_error(path, "completed temporal expression contains sentinel state");
        }
    }

    void verify_temporal_node(const TemporalUnaryExpr &expr, const std::string &path) {
        verify_temporal_ptr(expr.operand, path + ".operand");
    }

    void verify_temporal_node(const TemporalBinaryExpr &expr, const std::string &path) {
        verify_temporal_ptr(expr.lhs, path + ".lhs");
        verify_temporal_ptr(expr.rhs, path + ".rhs");
    }

    void verify_required_expr_ref(ExprRef ref, const std::string &path) {
        if (!ref) {
            add_error(path, "expression reference is null");
            return;
        }
        verify_expr_ref(ref, path);
    }

    void verify_optional_expr_ref(ExprRef ref, const std::string &path) {
        if (!ref) {
            return;
        }
        verify_expr_ref(ref, path);
    }

    void verify_expr_ref(ExprRef ref, const std::string &path) {
        if (ref.index == ExprRef::kInvalid || ref.index >= program_.expr_arena.size()) {
            add_error(path, "expression reference index is out of range");
            return;
        }
        if (ref.get() == nullptr) {
            add_error(path, "expression reference pointer is null");
            return;
        }
        if (&program_.expr_arena.get(ref.index) != ref.get()) {
            add_error(path, "expression reference pointer does not match arena index");
        }
    }

    void verify_symbol_ref(const SymbolRef &symbol,
                           const std::string &path,
                           std::optional<SymbolRefKind> expected_kind = std::nullopt,
                           std::string_view declaration_name = {}) {
        if (is_backend_ready_mode(mode_)) {
            if (!symbol.id.has_value()) {
                add_error(path, "required symbol reference is missing id");
            }
            if (symbol.kind == SymbolRefKind::Unknown) {
                add_error(path, "required symbol reference has unknown kind");
            }
            if (symbol.canonical_name.empty()) {
                add_error(path, "required symbol reference is missing canonical name");
            }
            if (contains_sentinel(symbol.canonical_name) || contains_sentinel(symbol.local_name) ||
                contains_sentinel(symbol.module_name)) {
                add_error(path, "symbol reference contains sentinel identity");
            }
            if (expected_kind.has_value() && symbol.kind != *expected_kind) {
                add_error(path,
                          "symbol reference has kind " +
                              std::string(symbol_ref_kind_name(symbol.kind)) + ", expected " +
                              std::string(symbol_ref_kind_name(*expected_kind)));
            }
            if (!declaration_name.empty()) {
                if (symbol.local_name.empty()) {
                    add_error(path, "declaration symbol reference is missing local name");
                } else if (symbol.local_name != declaration_name &&
                           (symbol.canonical_name.empty() ||
                            !canonical_matches_decl_name(symbol.canonical_name,
                                                         declaration_name))) {
                    add_error(path,
                              "declaration name '" + std::string(declaration_name) +
                                  "' drifts from symbol local name '" + symbol.local_name + "'");
                }
                if (!symbol.canonical_name.empty() &&
                    !canonical_matches_decl_name(symbol.canonical_name, declaration_name)) {
                    add_error(path,
                              "declaration name '" + std::string(declaration_name) +
                                  "' drifts from symbol canonical name '" + symbol.canonical_name +
                                  "'");
                }
            }
        }
        if (!symbol.id.has_value()) {
            return;
        }
        const auto found = symbol_identities_.find(*symbol.id);
        if (found == symbol_identities_.end()) {
            return;
        }
        const auto name = symbol_ref_name(symbol);
        if (name != "<unknown>" && found->second.name != name) {
            add_error(path,
                      "symbol id " + std::to_string(*symbol.id) + " resolves to " +
                          found->second.name + ", not " + name);
        }
        if (is_backend_ready_mode(mode_) && found->second.kind != SymbolRefKind::Unknown &&
            symbol.kind != SymbolRefKind::Unknown && found->second.kind != symbol.kind) {
            add_error(path,
                      "symbol id " + std::to_string(*symbol.id) + " resolves to kind " +
                          std::string(symbol_ref_kind_name(found->second.kind)) + ", not " +
                          std::string(symbol_ref_kind_name(symbol.kind)));
        }
    }

    void verify_type_ref(const TypeRef &type, const std::string &path) {
        verify_source_range(type.source_range, path, "source range");
        if (is_backend_ready_mode(mode_)) {
            if (type.kind == TypeRefKind::Unresolved) {
                add_error(path, "required type reference is unresolved");
            }
            if (contains_sentinel(type.display_name) || contains_sentinel(type.canonical_name) ||
                contains_sentinel(type.variant_name)) {
                add_error(path, "type reference contains sentinel identity");
            }
            if ((type.kind == TypeRefKind::Struct || type.kind == TypeRefKind::Enum) &&
                type.canonical_name.empty()) {
                add_error(path, "nominal type reference is missing canonical name");
            }
        }
        switch (type.kind) {
        case TypeRefKind::Optional:
        case TypeRefKind::List:
        case TypeRefKind::Set:
            if (!type.first) {
                add_error(path, "type is missing its element type");
            } else {
                verify_type_ref(*type.first, path + ".first");
            }
            break;
        case TypeRefKind::Map:
            if (!type.first) {
                add_error(path, "map type is missing its key type");
            } else {
                verify_type_ref(*type.first, path + ".key_type");
            }
            if (!type.second) {
                add_error(path, "map type is missing its value type");
            } else {
                verify_type_ref(*type.second, path + ".value_type");
            }
            break;
        default:
            if (type.first) {
                verify_type_ref(*type.first, path + ".first");
            }
            if (type.second) {
                verify_type_ref(*type.second, path + ".second");
            }
            break;
        }
        for (std::size_t index = 0; index < type.params.size(); ++index) {
            if (!type.params[index]) {
                add_error(path + ".params[" + std::to_string(index) + "]",
                          "type argument reference is null");
                continue;
            }
            verify_type_ref(*type.params[index], path + ".params[" + std::to_string(index) + "]");
        }
    }

    void
    verify_source_range(const SourceRangeOpt &range, const std::string &path, const char *label) {
        if (!range.has_value()) {
            return;
        }
        if (range->end_offset < range->begin_offset) {
            add_error(path, std::string(label) + " has end before begin");
        }
    }

    void verify_analysis_phase() {
        if (program_.phase == ProgramPhase::Lowered) {
            if (has_any_analysis(program_.analyses)) {
                add_warning("program.phase", "lowered program carries derived analyses");
            }
            return;
        }

        if (!has_fresh_derived_analyses(program_)) {
            add_error("program.analyses",
                      "derived analyses are stale for current program analysis revision");
        }

        verify_analysis_owners_and_indexes();

        const bool has_flow_handlers = std::any_of(
            program_.declarations.begin(), program_.declarations.end(), [](const Decl &decl) {
                const auto *flow = std::get_if<FlowDecl>(&decl);
                return flow != nullptr && !flow->state_handlers.empty();
            });
        if (has_flow_handlers && program_.analyses.state_handler_summaries.empty()) {
            add_error("program.analyses", "analyzed program is missing state handler summaries");
        }

        const bool has_workflow_nodes = std::any_of(
            program_.declarations.begin(), program_.declarations.end(), [](const Decl &decl) {
                const auto *workflow = std::get_if<WorkflowDecl>(&decl);
                return workflow != nullptr && !workflow->nodes.empty();
            });
        if (has_workflow_nodes && program_.analyses.workflow_node_input_summaries.empty()) {
            add_error("program.analyses", "analyzed program is missing workflow node summaries");
        }
    }

    void verify_analysis_owners_and_indexes() {
        std::unordered_set<std::string> expected_state_handlers;
        std::unordered_set<std::string> expected_workflow_nodes;
        std::unordered_set<std::string> expected_workflow_returns;
        for (const auto &decl : program_.declarations) {
            if (const auto *flow = std::get_if<FlowDecl>(&decl); flow != nullptr) {
                const auto owner = flow->target_ref.canonical_name;
                for (std::size_t index = 0; index < flow->state_handlers.size(); ++index) {
                    expected_state_handlers.insert(
                        analysis_key(owner, index, flow->state_handlers[index].state_name));
                }
                continue;
            }
            if (const auto *workflow = std::get_if<WorkflowDecl>(&decl); workflow != nullptr) {
                const auto owner = workflow->symbol_ref.canonical_name.empty()
                                       ? workflow->name
                                       : workflow->symbol_ref.canonical_name;
                for (std::size_t index = 0; index < workflow->nodes.size(); ++index) {
                    expected_workflow_nodes.insert(
                        analysis_key(owner, index, workflow->nodes[index].name));
                }
                expected_workflow_returns.insert(analysis_key(owner, 0));
            }
        }

        verify_analysis_entries(expected_state_handlers,
                                program_.analyses.state_handler_summaries,
                                "program.analyses.state_handler_summaries",
                                [](const StateHandlerSummaryAnalysis &entry) {
                                    return analysis_key(
                                        entry.flow_target, entry.handler_index, entry.state_name);
                                });
        verify_analysis_entries(expected_workflow_nodes,
                                program_.analyses.workflow_node_input_summaries,
                                "program.analyses.workflow_node_input_summaries",
                                [](const WorkflowNodeExprSummaryAnalysis &entry) {
                                    return analysis_key(
                                        entry.workflow_name, entry.node_index, entry.node_name);
                                });
        verify_analysis_entries(expected_workflow_returns,
                                program_.analyses.workflow_return_summaries,
                                "program.analyses.workflow_return_summaries",
                                [](const WorkflowReturnExprSummaryAnalysis &entry) {
                                    return analysis_key(entry.workflow_name, 0);
                                });

        std::unordered_set<std::string> formal_observation_symbols;
        for (std::size_t index = 0; index < program_.analyses.formal_observations.size(); ++index) {
            const auto &symbol = program_.analyses.formal_observations[index].symbol;
            if (symbol.empty()) {
                add_error("program.analyses.formal_observations[" + std::to_string(index) + "]",
                          "formal observation is missing symbol");
                continue;
            }
            if (!formal_observation_symbols.insert(symbol).second) {
                add_error("program.analyses.formal_observations[" + std::to_string(index) + "]",
                          "duplicate formal observation symbol " + symbol);
            }
        }
    }

    template <typename EntryT, typename KeyFn>
    void verify_analysis_entries(const std::unordered_set<std::string> &expected,
                                 const std::vector<EntryT> &actual,
                                 const std::string &path,
                                 KeyFn key_of) {
        if (actual.size() != expected.size()) {
            add_error(path,
                      "analysis entry count " + std::to_string(actual.size()) +
                          " does not match declaration count " + std::to_string(expected.size()));
        }
        std::unordered_set<std::string> seen;
        for (std::size_t index = 0; index < actual.size(); ++index) {
            const auto key = key_of(actual[index]);
            const auto entry_path = path + "[" + std::to_string(index) + "]";
            if (!expected.contains(key)) {
                add_error(entry_path, "analysis entry has no matching declaration owner/index");
            }
            if (!seen.insert(key).second) {
                add_error(entry_path, "duplicate analysis entry for declaration owner/index");
            }
        }
    }

    const Program &program_;
    IrVerificationMode mode_;
    VerificationResult result_;
    std::unordered_map<std::size_t, SymbolIdentity> symbol_identities_;
    std::unordered_set<std::uint32_t> statement_ids_;
};

} // namespace

bool VerificationResult::has_errors() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.severity == VerificationSeverity::Error;
    });
}

bool VerificationResult::has_warnings() const noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto &diagnostic) {
        return diagnostic.severity == VerificationSeverity::Warning;
    });
}

VerificationResult verify_ir_program(const Program &program) {
    return verify_ir_program(program, IrVerificationMode::Structural);
}

VerificationResult verify_ir_program(const Program &program, IrVerificationMode mode) {
    return ProgramVerifier(program, mode).run();
}

} // namespace ahfl::ir
