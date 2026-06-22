#include "compiler/ir/opt/opt_print.hpp"

#include <cstddef>
#include <optional>
#include <ostream>
#include <string_view>
#include <type_traits>
#include <variant>

namespace ahfl::ir::opt {
namespace {

[[nodiscard]] std::string_view type_kind_name(TypeRefKind kind) {
    switch (kind) {
    case TypeRefKind::Unresolved:
        return "unresolved";
    case TypeRefKind::Any:
        return "any";
    case TypeRefKind::Never:
        return "never";
    case TypeRefKind::Unit:
        return "unit";
    case TypeRefKind::Bool:
        return "bool";
    case TypeRefKind::Int:
        return "int";
    case TypeRefKind::Float:
        return "float";
    case TypeRefKind::String:
        return "string";
    case TypeRefKind::BoundedString:
        return "bounded_string";
    case TypeRefKind::UUID:
        return "uuid";
    case TypeRefKind::Timestamp:
        return "timestamp";
    case TypeRefKind::Duration:
        return "duration";
    case TypeRefKind::Decimal:
        return "decimal";
    case TypeRefKind::Struct:
        return "struct";
    case TypeRefKind::Enum:
        return "enum";
    case TypeRefKind::Optional:
        return "optional";
    case TypeRefKind::List:
        return "list";
    case TypeRefKind::Set:
        return "set";
    case TypeRefKind::Map:
        return "map";
    case TypeRefKind::Fn:
        return "fn";
    }
    return "unknown";
}

[[nodiscard]] std::string_view operand_kind_name(Operand::Kind kind) {
    switch (kind) {
    case Operand::Kind::Local:
        return "local";
    case Operand::Kind::Constant:
        return "constant";
    case Operand::Kind::Arg:
        return "arg";
    }
    return "unknown";
}

[[nodiscard]] std::string_view rvalue_kind_name(Rvalue::Kind kind) {
    switch (kind) {
    case Rvalue::Kind::Use:
        return "use";
    case Rvalue::Kind::BinaryOp:
        return "binary";
    case Rvalue::Kind::UnaryOp:
        return "unary";
    case Rvalue::Kind::Call:
        return "call";
    case Rvalue::Kind::Aggregate:
        return "aggregate";
    case Rvalue::Kind::Field:
        return "field";
    case Rvalue::Kind::Index:
        return "index";
    }
    return "unknown";
}

[[nodiscard]] std::string_view statement_kind_name(Statement::Kind kind) {
    switch (kind) {
    case Statement::Kind::Assign:
        return "assign";
    case Statement::Kind::StorageLive:
        return "storage_live";
    case Statement::Kind::StorageDead:
        return "storage_dead";
    case Statement::Kind::Nop:
        return "nop";
    }
    return "unknown";
}

[[nodiscard]] std::string_view terminator_kind_name(Terminator::Kind kind) {
    switch (kind) {
    case Terminator::Kind::Goto:
        return "goto";
    case Terminator::Kind::SwitchBool:
        return "switch_bool";
    case Terminator::Kind::Return:
        return "return";
    case Terminator::Kind::Call:
        return "call";
    case Terminator::Kind::Assert:
        return "assert";
    case Terminator::Kind::Unreachable:
        return "unreachable";
    }
    return "unknown";
}

void print_escaped(std::ostream &out, std::string_view value) {
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
}

void print_type_ref(const TypeRef &type, std::ostream &out) {
    out << type_kind_name(type.kind);
    if (!type.display_name.empty()) {
        out << '(' << type.display_name << ')';
    } else if (!type.canonical_name.empty()) {
        out << '(' << type.canonical_name;
        if (!type.variant_name.empty()) {
            out << "::" << type.variant_name;
        }
        out << ')';
    }
}

void print_source_range(const std::optional<SourceRange> &source_range, std::ostream &out) {
    if (!source_range.has_value()) {
        return;
    }
    out << " source=[" << source_range->begin_offset << ',' << source_range->end_offset << ')';
}

void print_constant(const Constant &constant, std::ostream &out) {
    std::visit(
        [&out](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                out << "unit";
            } else if constexpr (std::is_same_v<T, bool>) {
                out << (value ? "true" : "false");
            } else if constexpr (std::is_same_v<T, std::string>) {
                out << '"';
                print_escaped(out, value);
                out << '"';
            } else {
                out << value;
            }
        },
        constant);
}

void print_operand(const Operand &operand, std::ostream &out) {
    out << operand_kind_name(operand.kind) << '(';
    switch (operand.kind) {
    case Operand::Kind::Local:
        out << '%' << operand.local;
        break;
    case Operand::Kind::Constant:
        print_constant(operand.constant, out);
        break;
    case Operand::Kind::Arg:
        out << operand.arg_index;
        break;
    }
    out << ')';
}

void print_operand_list(const std::vector<Operand> &operands, std::ostream &out) {
    out << '[';
    for (std::size_t index = 0; index < operands.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        print_operand(operands[index], out);
    }
    out << ']';
}

void print_rvalue(const Rvalue &rvalue, std::ostream &out) {
    out << rvalue_kind_name(rvalue.kind);
    if (!rvalue.op.empty()) {
        out << " op=" << rvalue.op;
    }
    if (!rvalue.callee.empty()) {
        out << " callee=" << rvalue.callee;
    }
    if (!rvalue.field_name.empty()) {
        out << " field=" << rvalue.field_name;
    }
    out << " operands=";
    print_operand_list(rvalue.operands, out);
    out << " type=";
    print_type_ref(rvalue.result_type, out);
}

void print_statement(const Statement &statement, std::ostream &out) {
    out << "    stmt " << statement_kind_name(statement.kind);
    if (statement.dest != kNoLocal) {
        out << " %" << statement.dest << " = ";
        print_rvalue(statement.rvalue, out);
    }
    print_source_range(statement.source_range, out);
    out << '\n';
}

void print_terminator(const Terminator &terminator, std::ostream &out) {
    out << "    term " << terminator_kind_name(terminator.kind);
    switch (terminator.kind) {
    case Terminator::Kind::Goto:
        out << " target=bb" << terminator.target;
        break;
    case Terminator::Kind::SwitchBool:
        out << " cond=%" << terminator.condition << " then=bb" << terminator.then_block
            << " else=bb" << terminator.else_block;
        break;
    case Terminator::Kind::Return:
        if (terminator.return_value != kNoLocal) {
            out << " value=%" << terminator.return_value;
        }
        break;
    case Terminator::Kind::Call:
        out << " callee=" << terminator.callee << " dest=%" << terminator.call_dest << " resume=bb"
            << terminator.call_resume << " args=";
        print_operand_list(terminator.args, out);
        break;
    case Terminator::Kind::Assert:
        out << " cond=%" << terminator.condition << " resume=bb" << terminator.target;
        break;
    case Terminator::Kind::Unreachable:
        break;
    }
    print_source_range(terminator.source_range, out);
    out << '\n';
}

void print_function(const OptFunction &function, std::ostream &out) {
    out << "function " << function.name << " args=" << function.arg_count << " return=";
    print_type_ref(function.return_type, out);
    print_source_range(function.source_range, out);
    out << " {\n";

    out << "  locals:\n";
    for (const auto &local : function.locals) {
        out << "    %" << local.id << " " << local.name;
        if (local.is_temp) {
            out << " temp";
        }
        out << " type=";
        print_type_ref(local.type, out);
        print_source_range(local.source_range, out);
        out << '\n';
    }

    out << "  blocks:\n";
    for (const auto &block : function.blocks) {
        out << "  bb" << block.id << " " << block.label << ":\n";
        for (const auto &statement : block.statements) {
            print_statement(statement, out);
        }
        print_terminator(block.terminator, out);
    }

    out << "}\n";
}

void print_skipped_temporal_fragment(const SkippedTemporalFragment &fragment, std::ostream &out) {
    out << "skipped_temporal scope=" << fragment.scope << " kind=" << fragment.kind << " reason=\"";
    print_escaped(out, fragment.reason);
    out << '"';
    print_source_range(fragment.source_range, out);
    out << '\n';
}

} // namespace

void print_opt_program(const OptProgram &program, std::ostream &out) {
    out << "opt_program source_version=" << program.source_program_version << '\n';
    for (const auto &fragment : program.skipped_temporal_fragments) {
        print_skipped_temporal_fragment(fragment, out);
    }
    for (const auto &function : program.functions) {
        print_function(function, out);
    }
}

} // namespace ahfl::ir::opt
