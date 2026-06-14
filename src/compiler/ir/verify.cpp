#include "ahfl/compiler/ir/verify.hpp"

#include "ahfl/compiler/ir/analysis.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
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

class ProgramVerifier {
  public:
    explicit ProgramVerifier(const Program &program) : program_(program) {}

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

    void collect_symbol_identity(const SymbolRef &symbol, const std::string &path) {
        if (!symbol.id.has_value()) {
            return;
        }
        auto [iter, inserted] = symbol_identities_.emplace(*symbol.id, symbol_ref_name(symbol));
        if (!inserted && iter->second != symbol_ref_name(symbol)) {
            add_error(path,
                      "symbol id " + std::to_string(*symbol.id) + " is also bound to " +
                          iter->second);
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
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref");
    }

    void verify_decl(const TypeAliasDecl &decl, const std::string &path) {
        verify_type_ref(decl.aliased_type_ref, path + ".aliased_type_ref");
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref");
    }

    void verify_decl(const StructDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref");
        for (std::uint32_t index = 0; index < decl.fields.size(); ++index) {
            const auto field_path = path + ".fields[" + std::to_string(index) + "]";
            verify_optional_expr_ref(decl.fields[index].default_value, field_path + ".default");
            verify_type_ref(decl.fields[index].type_ref, field_path + ".type_ref");
            verify_source_range(decl.fields[index].source_range, field_path, "source range");
        }
    }

    void verify_decl(const EnumDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref");
    }

    void verify_decl(const CapabilityDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref");
        verify_params(decl.params, path + ".params");
        verify_type_ref(decl.return_type_ref, path + ".return_type_ref");
        verify_source_range(decl.effect.source_range, path + ".effect", "source range");
    }

    void verify_decl(const PredicateDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref");
        verify_params(decl.params, path + ".params");
    }

    void verify_decl(const AgentDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref");
        verify_type_ref(decl.input_type_ref, path + ".input_type_ref");
        verify_type_ref(decl.context_type_ref, path + ".context_type_ref");
        verify_type_ref(decl.output_type_ref, path + ".output_type_ref");
        for (std::uint32_t index = 0; index < decl.capability_refs.size(); ++index) {
            verify_symbol_ref(decl.capability_refs[index],
                              path + ".capability_refs[" + std::to_string(index) + "]");
        }
    }

    void verify_decl(const ContractDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.target_ref, path + ".target_ref");
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
        verify_symbol_ref(decl.target_ref, path + ".target_ref");
        for (std::uint32_t index = 0; index < decl.state_handlers.size(); ++index) {
            const auto handler_path = path + ".state_handlers[" + std::to_string(index) + "]";
            verify_source_range(
                decl.state_handlers[index].source_range, handler_path, "source range");
            verify_block(decl.state_handlers[index].body, handler_path + ".body");
        }
    }

    void verify_decl(const WorkflowDecl &decl, const std::string &path) {
        verify_symbol_ref(decl.symbol_ref, path + ".symbol_ref");
        verify_type_ref(decl.input_type_ref, path + ".input_type_ref");
        verify_type_ref(decl.output_type_ref, path + ".output_type_ref");
        for (std::uint32_t index = 0; index < decl.nodes.size(); ++index) {
            const auto node_path = path + ".nodes[" + std::to_string(index) + "]";
            verify_required_expr_ref(decl.nodes[index].input, node_path + ".input");
            verify_symbol_ref(decl.nodes[index].target_ref, node_path + ".target_ref");
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

    void verify_statement_node(const GotoStatement & /*stmt*/, const std::string & /*path*/) {}

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
        for (std::uint32_t index = 0; index < expr.arguments.size(); ++index) {
            verify_required_expr_ref(expr.arguments[index],
                                     path + ".arguments[" + std::to_string(index) + "]");
        }
    }

    void verify_expr_node(const StructLiteralExpr &expr, const std::string &path) {
        for (std::uint32_t index = 0; index < expr.fields.size(); ++index) {
            verify_required_expr_ref(expr.fields[index].value,
                                     path + ".fields[" + std::to_string(index) + "].value");
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

    void verify_temporal_node(const CalledTemporalExpr & /*expr*/, const std::string & /*path*/) {}
    void verify_temporal_node(const InStateTemporalExpr & /*expr*/, const std::string & /*path*/) {}
    void verify_temporal_node(const RunningTemporalExpr & /*expr*/, const std::string & /*path*/) {}
    void verify_temporal_node(const CompletedTemporalExpr & /*expr*/,
                              const std::string & /*path*/) {}

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

    void verify_symbol_ref(const SymbolRef &symbol, const std::string &path) {
        if (!symbol.id.has_value()) {
            return;
        }
        const auto found = symbol_identities_.find(*symbol.id);
        if (found == symbol_identities_.end()) {
            return;
        }
        const auto name = symbol_ref_name(symbol);
        if (name != "<unknown>" && found->second != name) {
            add_error(path,
                      "symbol id " + std::to_string(*symbol.id) + " resolves to " + found->second +
                          ", not " + name);
        }
    }

    void verify_type_ref(const TypeRef &type, const std::string &path) {
        verify_source_range(type.source_range, path, "source range");
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

    const Program &program_;
    VerificationResult result_;
    std::unordered_map<std::size_t, std::string> symbol_identities_;
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
    return ProgramVerifier(program).run();
}

} // namespace ahfl::ir
