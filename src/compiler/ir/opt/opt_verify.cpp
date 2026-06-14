#include "compiler/ir/opt/opt_verify.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ahfl::ir::opt {
namespace {

[[nodiscard]] bool is_resolved(const TypeRef &type) noexcept {
    return type.kind != TypeRefKind::Unresolved;
}

[[nodiscard]] bool type_refs_compatible(const TypeRef &lhs, const TypeRef &rhs) {
    if (!is_resolved(lhs) || !is_resolved(rhs)) {
        return true;
    }
    if (lhs.kind != rhs.kind) {
        return false;
    }
    if (!lhs.canonical_name.empty() && !rhs.canonical_name.empty() &&
        lhs.canonical_name != rhs.canonical_name) {
        return false;
    }
    if (!lhs.variant_name.empty() && !rhs.variant_name.empty() &&
        lhs.variant_name != rhs.variant_name) {
        return false;
    }
    if (lhs.string_bounds.has_value() && rhs.string_bounds.has_value() &&
        lhs.string_bounds != rhs.string_bounds) {
        return false;
    }
    if (lhs.decimal_scale.has_value() && rhs.decimal_scale.has_value() &&
        lhs.decimal_scale != rhs.decimal_scale) {
        return false;
    }
    if (lhs.first && rhs.first && !type_refs_compatible(*lhs.first, *rhs.first)) {
        return false;
    }
    if (lhs.second && rhs.second && !type_refs_compatible(*lhs.second, *rhs.second)) {
        return false;
    }
    return true;
}

[[nodiscard]] bool source_range_has_valid_order(const SourceRange &range) noexcept {
    return range.end_offset >= range.begin_offset;
}

class FunctionVerifier {
  public:
    explicit FunctionVerifier(const OptFunction &function) : function_(function) {}

    [[nodiscard]] VerificationResult run() {
        verify_function_shape();
        verify_locals();
        verify_blocks();
        verify_unreachable_blocks();
        return std::move(result_);
    }

  private:
    void add(VerificationSeverity severity,
             std::string message,
             std::optional<std::uint32_t> block_id = std::nullopt) {
        result_.diagnostics.push_back(VerificationDiagnostic{
            .severity = severity,
            .function_name = function_.name,
            .block_id = block_id,
            .message = std::move(message),
        });
    }

    void add_error(std::string message, std::optional<std::uint32_t> block_id = std::nullopt) {
        add(VerificationSeverity::Error, std::move(message), block_id);
    }

    void add_warning(std::string message, std::optional<std::uint32_t> block_id = std::nullopt) {
        add(VerificationSeverity::Warning, std::move(message), block_id);
    }

    [[nodiscard]] bool valid_block_ref(std::uint32_t block_id) const noexcept {
        return block_id < function_.blocks.size();
    }

    [[nodiscard]] bool valid_local_ref(LocalRef local) const noexcept {
        return local != kNoLocal && local < function_.locals.size();
    }

    [[nodiscard]] bool valid_optional_local_ref(LocalRef local) const noexcept {
        return local == kNoLocal || local < function_.locals.size();
    }

    [[nodiscard]] const TypeRef *local_type(LocalRef local) const noexcept {
        if (!valid_local_ref(local)) {
            return nullptr;
        }
        return &function_.locals[local].type;
    }

    void mark_local_definition(LocalRef local) {
        if (local != kNoLocal && local < definitions_.size()) {
            definitions_[local] = true;
        }
    }

    void mark_local_use(LocalRef local) {
        if (local != kNoLocal && local < uses_.size()) {
            uses_[local] = true;
        }
    }

    void verify_function_shape() {
        if (function_.name.empty()) {
            add_error("function name is empty");
        }
        if (function_.arg_count > function_.locals.size()) {
            add_error("arg_count exceeds local count");
        }
        if (function_.blocks.empty()) {
            add_error("function has no entry block");
        }
        if (function_.source_range.has_value()) {
            verify_source_range(*function_.source_range, "function source range", std::nullopt);
        }
        definitions_.assign(function_.locals.size(), false);
        uses_.assign(function_.locals.size(), false);
        for (std::uint32_t index = 0;
             index < function_.arg_count && index < function_.locals.size();
             ++index) {
            definitions_[index] = true;
        }
    }

    void verify_locals() {
        std::unordered_set<std::uint32_t> ids;
        ids.reserve(function_.locals.size());
        for (std::uint32_t index = 0; index < function_.locals.size(); ++index) {
            const auto &local = function_.locals[index];
            if (local.id != index) {
                add_error("local id does not match its vector index");
            }
            if (!ids.insert(local.id).second) {
                add_error("duplicate local id " + std::to_string(local.id));
            }
            if (local.source_range.has_value()) {
                verify_source_range(*local.source_range, "local source range", std::nullopt);
            }
        }
    }

    void verify_blocks() {
        std::unordered_set<std::uint32_t> ids;
        ids.reserve(function_.blocks.size());
        for (std::uint32_t index = 0; index < function_.blocks.size(); ++index) {
            const auto &block = function_.blocks[index];
            if (block.id != index) {
                add_error("basic block id does not match its vector index", block.id);
            }
            if (!ids.insert(block.id).second) {
                add_error("duplicate basic block id " + std::to_string(block.id), block.id);
            }
            verify_block(block);
        }

        for (std::uint32_t index = function_.arg_count; index < uses_.size(); ++index) {
            if (uses_[index] && !definitions_[index]) {
                add_error("local %" + std::to_string(index) + " is used without a definition");
            }
        }
    }

    void verify_block(const BasicBlock &block) {
        for (const auto &stmt : block.statements) {
            verify_statement(stmt, block.id);
        }
        verify_terminator(block.terminator, block.id);
    }

    void verify_statement(const Statement &stmt, std::uint32_t block_id) {
        if (stmt.source_range.has_value()) {
            verify_source_range(*stmt.source_range, "statement source range", block_id);
        }

        switch (stmt.kind) {
        case Statement::Kind::Assign:
            if (!valid_local_ref(stmt.dest)) {
                add_error("assignment destination local is invalid", block_id);
            } else {
                mark_local_definition(stmt.dest);
                if (!type_refs_compatible(function_.locals[stmt.dest].type,
                                          stmt.rvalue.result_type)) {
                    add_error("assignment result type does not match destination local type",
                              block_id);
                }
            }
            verify_rvalue(stmt.rvalue, block_id);
            break;
        case Statement::Kind::StorageLive:
        case Statement::Kind::StorageDead:
            if (!valid_local_ref(stmt.dest)) {
                add_error("storage statement local is invalid", block_id);
            }
            break;
        case Statement::Kind::Nop:
            break;
        }
    }

    void verify_rvalue(const Rvalue &rvalue, std::uint32_t block_id) {
        switch (rvalue.kind) {
        case Rvalue::Kind::Use:
            expect_operand_count(rvalue, 1, "use rvalue", block_id);
            break;
        case Rvalue::Kind::BinaryOp:
            expect_operand_count(rvalue, 2, "binary rvalue", block_id);
            if (rvalue.op.empty()) {
                add_error("binary rvalue has no operator", block_id);
            }
            break;
        case Rvalue::Kind::UnaryOp:
            expect_operand_count(rvalue, 1, "unary rvalue", block_id);
            if (rvalue.op.empty()) {
                add_error("unary rvalue has no operator", block_id);
            }
            break;
        case Rvalue::Kind::Call:
            if (rvalue.callee.empty()) {
                add_error("call rvalue has no callee", block_id);
            }
            break;
        case Rvalue::Kind::Aggregate:
            break;
        case Rvalue::Kind::Field:
            expect_operand_count(rvalue, 1, "field rvalue", block_id);
            if (rvalue.field_name.empty()) {
                add_error("field rvalue has no field name", block_id);
            }
            break;
        case Rvalue::Kind::Index:
            expect_operand_count(rvalue, 2, "index rvalue", block_id);
            break;
        }

        for (const auto &operand : rvalue.operands) {
            verify_operand(operand, block_id);
        }
    }

    void expect_operand_count(const Rvalue &rvalue,
                              std::size_t expected,
                              const char *label,
                              std::uint32_t block_id) {
        if (rvalue.operands.size() != expected) {
            add_error(std::string(label) + " has " + std::to_string(rvalue.operands.size()) +
                          " operands; expected " + std::to_string(expected),
                      block_id);
        }
    }

    void verify_operand(const Operand &operand, std::uint32_t block_id) {
        switch (operand.kind) {
        case Operand::Kind::Local:
            if (!valid_local_ref(operand.local)) {
                add_error("operand local is invalid", block_id);
            } else {
                mark_local_use(operand.local);
            }
            break;
        case Operand::Kind::Arg:
            if (operand.arg_index >= function_.arg_count) {
                add_error("argument operand index is out of range", block_id);
            }
            break;
        case Operand::Kind::Constant:
            break;
        }
    }

    void verify_terminator(const Terminator &terminator, std::uint32_t block_id) {
        if (terminator.source_range.has_value()) {
            verify_source_range(*terminator.source_range, "terminator source range", block_id);
        }

        switch (terminator.kind) {
        case Terminator::Kind::Goto:
            verify_block_target(terminator.target, "goto target", block_id);
            break;
        case Terminator::Kind::SwitchBool:
            verify_required_bool_local(terminator.condition, "switch condition", block_id);
            verify_block_target(terminator.then_block, "switch then target", block_id);
            verify_block_target(terminator.else_block, "switch else target", block_id);
            break;
        case Terminator::Kind::Return:
            if (!valid_optional_local_ref(terminator.return_value)) {
                add_error("return value local is invalid", block_id);
            } else if (terminator.return_value != kNoLocal) {
                mark_local_use(terminator.return_value);
                const auto *return_type = local_type(terminator.return_value);
                if (return_type != nullptr &&
                    !type_refs_compatible(function_.return_type, *return_type)) {
                    add_error("return value type does not match function return type", block_id);
                }
            }
            break;
        case Terminator::Kind::Call:
            if (terminator.callee.empty()) {
                add_error("call terminator has no callee", block_id);
            }
            for (const auto &arg : terminator.args) {
                verify_operand(arg, block_id);
            }
            if (!valid_optional_local_ref(terminator.call_dest)) {
                add_error("call destination local is invalid", block_id);
            } else if (terminator.call_dest != kNoLocal) {
                mark_local_definition(terminator.call_dest);
            }
            verify_block_target(terminator.call_resume, "call resume target", block_id);
            break;
        case Terminator::Kind::Assert:
            verify_required_bool_local(terminator.condition, "assert condition", block_id);
            verify_block_target(terminator.target, "assert resume target", block_id);
            break;
        case Terminator::Kind::Unreachable:
            break;
        }
    }

    void verify_required_bool_local(LocalRef local, const char *label, std::uint32_t block_id) {
        if (!valid_local_ref(local)) {
            add_error(std::string(label) + " local is invalid", block_id);
            return;
        }
        mark_local_use(local);
        const auto &type = function_.locals[local].type;
        if (is_resolved(type) && type.kind != TypeRefKind::Bool) {
            add_error(std::string(label) + " must have Bool type", block_id);
        }
    }

    void verify_block_target(std::uint32_t target, const char *label, std::uint32_t block_id) {
        if (!valid_block_ref(target)) {
            add_error(std::string(label) + " is out of range", block_id);
        }
    }

    void verify_source_range(const SourceRange &range,
                             const char *label,
                             std::optional<std::uint32_t> block_id) {
        if (!source_range_has_valid_order(range)) {
            add_error(std::string(label) + " has end before begin", block_id);
        }
    }

    void verify_unreachable_blocks() {
        if (function_.blocks.empty()) {
            return;
        }

        std::vector<bool> reachable(function_.blocks.size(), false);
        std::vector<std::uint32_t> worklist{0};
        reachable[0] = true;
        while (!worklist.empty()) {
            const auto block_id = worklist.back();
            worklist.pop_back();
            const auto &terminator = function_.blocks[block_id].terminator;
            for (const auto target : terminator_targets(terminator)) {
                if (target < reachable.size() && !reachable[target]) {
                    reachable[target] = true;
                    worklist.push_back(target);
                }
            }
        }

        for (std::uint32_t index = 0; index < reachable.size(); ++index) {
            if (!reachable[index]) {
                add_warning("basic block is unreachable", index);
            }
        }
    }

    [[nodiscard]] std::vector<std::uint32_t>
    terminator_targets(const Terminator &terminator) const {
        switch (terminator.kind) {
        case Terminator::Kind::Goto:
            return {terminator.target};
        case Terminator::Kind::SwitchBool:
            return {terminator.then_block, terminator.else_block};
        case Terminator::Kind::Call:
            return {terminator.call_resume};
        case Terminator::Kind::Assert:
            return {terminator.target};
        case Terminator::Kind::Return:
        case Terminator::Kind::Unreachable:
            return {};
        }
        return {};
    }

    const OptFunction &function_;
    VerificationResult result_;
    std::vector<bool> definitions_;
    std::vector<bool> uses_;
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

VerificationResult verify_opt_function(const OptFunction &function) {
    return FunctionVerifier(function).run();
}

VerificationResult verify_opt_program(const OptProgram &program) {
    VerificationResult result;
    for (const auto &fragment : program.skipped_temporal_fragments) {
        if (fragment.scope.empty()) {
            result.diagnostics.push_back(VerificationDiagnostic{
                .severity = VerificationSeverity::Error,
                .function_name = "opt_program",
                .block_id = std::nullopt,
                .message = "skipped temporal fragment scope is empty",
            });
        }
        if (fragment.kind.empty()) {
            result.diagnostics.push_back(VerificationDiagnostic{
                .severity = VerificationSeverity::Error,
                .function_name = "opt_program",
                .block_id = std::nullopt,
                .message = "skipped temporal fragment kind is empty",
            });
        }
        if (fragment.reason.empty()) {
            result.diagnostics.push_back(VerificationDiagnostic{
                .severity = VerificationSeverity::Error,
                .function_name = "opt_program",
                .block_id = std::nullopt,
                .message = "skipped temporal fragment reason is empty",
            });
        }
        if (fragment.source_range.has_value() &&
            !source_range_has_valid_order(*fragment.source_range)) {
            result.diagnostics.push_back(VerificationDiagnostic{
                .severity = VerificationSeverity::Error,
                .function_name =
                    fragment.scope.empty() ? std::string{"opt_program"} : fragment.scope,
                .block_id = std::nullopt,
                .message = "skipped temporal fragment source range has end before begin",
            });
        }
    }
    for (const auto &function : program.functions) {
        auto function_result = verify_opt_function(function);
        result.diagnostics.insert(result.diagnostics.end(),
                                  std::make_move_iterator(function_result.diagnostics.begin()),
                                  std::make_move_iterator(function_result.diagnostics.end()));
    }
    return result;
}

} // namespace ahfl::ir::opt
