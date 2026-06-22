#include "compiler/ir/opt/opt_json.hpp"

#include <ostream>
#include <string_view>
#include <type_traits>

namespace ahfl::ir::opt {

namespace {

void indent(std::ostream &out, int level) {
    for (int i = 0; i < level; ++i) {
        out << "  ";
    }
}

void write_string(std::ostream &out, std::string_view value) {
    out << '"';
    for (char ch : value) {
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
    out << '"';
}

[[nodiscard]] std::string_view type_ref_kind_name(TypeRefKind kind) {
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
    return "unresolved";
}

void print_source_range(const std::optional<SourceRange> &range, std::ostream &out, int level) {
    if (!range.has_value()) {
        out << "null";
        return;
    }
    out << "{\n";
    indent(out, level + 1);
    out << "\"begin\": " << range->begin_offset << ",\n";
    indent(out, level + 1);
    out << "\"end\": " << range->end_offset << '\n';
    indent(out, level);
    out << '}';
}

void print_type_ref(const TypeRef &type, std::ostream &out, int level) {
    out << "{\n";
    indent(out, level + 1);
    out << "\"kind\": ";
    write_string(out, type_ref_kind_name(type.kind));
    out << ",\n";
    indent(out, level + 1);
    out << "\"display_name\": ";
    write_string(out, type.display_name);
    out << ",\n";
    indent(out, level + 1);
    out << "\"canonical_name\": ";
    write_string(out, type.canonical_name);
    if (!type.variant_name.empty()) {
        out << ",\n";
        indent(out, level + 1);
        out << "\"variant_name\": ";
        write_string(out, type.variant_name);
    }
    if (type.decimal_scale.has_value()) {
        out << ",\n";
        indent(out, level + 1);
        out << "\"decimal_scale\": " << *type.decimal_scale;
    }
    if (type.string_bounds.has_value()) {
        out << ",\n";
        indent(out, level + 1);
        out << "\"string_bounds\": {\"min\": " << type.string_bounds->first
            << ", \"max\": " << type.string_bounds->second << '}';
    }
    if (type.first) {
        out << ",\n";
        indent(out, level + 1);
        out << "\"first\": ";
        print_type_ref(*type.first, out, level + 1);
    }
    if (type.second) {
        out << ",\n";
        indent(out, level + 1);
        out << "\"second\": ";
        print_type_ref(*type.second, out, level + 1);
    }
    if (type.source_range.has_value()) {
        out << ",\n";
        indent(out, level + 1);
        out << "\"source_range\": ";
        print_source_range(type.source_range, out, level + 1);
    }
    out << '\n';
    indent(out, level);
    out << '}';
}

void print_constant(const Constant &constant, std::ostream &out) {
    std::visit(
        [&](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                out << "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                out << (value ? "true" : "false");
            } else if constexpr (std::is_same_v<T, std::string>) {
                write_string(out, value);
            } else {
                out << value;
            }
        },
        constant);
}

void print_nullable_local(LocalRef local, std::ostream &out) {
    if (local == kNoLocal) {
        out << "null";
        return;
    }
    out << local;
}

void print_nullable_block(std::optional<std::uint32_t> block, std::ostream &out) {
    if (!block.has_value()) {
        out << "null";
        return;
    }
    out << *block;
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
    return "local";
}

void print_operand(const Operand &operand, std::ostream &out, int level) {
    out << "{\n";
    indent(out, level + 1);
    out << "\"kind\": ";
    write_string(out, operand_kind_name(operand.kind));
    out << ",\n";
    indent(out, level + 1);
    out << "\"local\": ";
    print_nullable_local(operand.kind == Operand::Kind::Local ? operand.local : kNoLocal, out);
    out << ",\n";
    indent(out, level + 1);
    out << "\"arg_index\": ";
    if (operand.kind == Operand::Kind::Arg) {
        out << operand.arg_index;
    } else {
        out << "null";
    }
    out << ",\n";
    indent(out, level + 1);
    out << "\"constant\": ";
    if (operand.kind == Operand::Kind::Constant) {
        print_constant(operand.constant, out);
    } else {
        out << "null";
    }
    out << '\n';
    indent(out, level);
    out << '}';
}

template <typename T, typename Printer>
void print_array(const std::vector<T> &items, std::ostream &out, int level, Printer printer) {
    out << "[";
    if (!items.empty()) {
        out << '\n';
        for (std::size_t index = 0; index < items.size(); ++index) {
            indent(out, level + 1);
            printer(items[index], out, level + 1);
            if (index + 1 < items.size()) {
                out << ',';
            }
            out << '\n';
        }
        indent(out, level);
    }
    out << ']';
}

[[nodiscard]] std::string_view rvalue_kind_name(Rvalue::Kind kind) {
    switch (kind) {
    case Rvalue::Kind::Use:
        return "use";
    case Rvalue::Kind::BinaryOp:
        return "binary_op";
    case Rvalue::Kind::UnaryOp:
        return "unary_op";
    case Rvalue::Kind::Call:
        return "call";
    case Rvalue::Kind::Aggregate:
        return "aggregate";
    case Rvalue::Kind::Field:
        return "field";
    case Rvalue::Kind::Index:
        return "index";
    }
    return "use";
}

void print_rvalue(const Rvalue &rvalue, std::ostream &out, int level) {
    out << "{\n";
    indent(out, level + 1);
    out << "\"kind\": ";
    write_string(out, rvalue_kind_name(rvalue.kind));
    out << ",\n";
    indent(out, level + 1);
    out << "\"op\": ";
    write_string(out, rvalue.op);
    out << ",\n";
    indent(out, level + 1);
    out << "\"callee\": ";
    write_string(out, rvalue.callee);
    out << ",\n";
    indent(out, level + 1);
    out << "\"field_name\": ";
    write_string(out, rvalue.field_name);
    out << ",\n";
    indent(out, level + 1);
    out << "\"operands\": ";
    print_array(rvalue.operands, out, level + 1, print_operand);
    out << ",\n";
    indent(out, level + 1);
    out << "\"result_type\": ";
    print_type_ref(rvalue.result_type, out, level + 1);
    out << '\n';
    indent(out, level);
    out << '}';
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
    return "assign";
}

void print_statement(const Statement &statement, std::ostream &out, int level) {
    out << "{\n";
    indent(out, level + 1);
    out << "\"kind\": ";
    write_string(out, statement_kind_name(statement.kind));
    out << ",\n";
    indent(out, level + 1);
    out << "\"dest\": ";
    print_nullable_local(statement.dest, out);
    out << ",\n";
    indent(out, level + 1);
    out << "\"rvalue\": ";
    print_rvalue(statement.rvalue, out, level + 1);
    out << ",\n";
    indent(out, level + 1);
    out << "\"source_range\": ";
    print_source_range(statement.source_range, out, level + 1);
    out << '\n';
    indent(out, level);
    out << '}';
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
    return "goto";
}

void print_terminator(const Terminator &terminator, std::ostream &out, int level) {
    out << "{\n";
    indent(out, level + 1);
    out << "\"kind\": ";
    write_string(out, terminator_kind_name(terminator.kind));
    out << ",\n";
    indent(out, level + 1);
    out << "\"condition\": ";
    print_nullable_local(terminator.kind == Terminator::Kind::SwitchBool ||
                                 terminator.kind == Terminator::Kind::Assert
                             ? terminator.condition
                             : kNoLocal,
                         out);
    out << ",\n";
    indent(out, level + 1);
    out << "\"return_value\": ";
    print_nullable_local(
        terminator.kind == Terminator::Kind::Return ? terminator.return_value : kNoLocal, out);
    out << ",\n";
    indent(out, level + 1);
    out << "\"target\": ";
    print_nullable_block(terminator.kind == Terminator::Kind::Goto
                             ? std::optional<std::uint32_t>{terminator.target}
                             : std::nullopt,
                         out);
    out << ",\n";
    indent(out, level + 1);
    out << "\"then_block\": ";
    print_nullable_block(terminator.kind == Terminator::Kind::SwitchBool
                             ? std::optional<std::uint32_t>{terminator.then_block}
                             : std::nullopt,
                         out);
    out << ",\n";
    indent(out, level + 1);
    out << "\"else_block\": ";
    print_nullable_block(terminator.kind == Terminator::Kind::SwitchBool
                             ? std::optional<std::uint32_t>{terminator.else_block}
                             : std::nullopt,
                         out);
    out << ",\n";
    indent(out, level + 1);
    out << "\"callee\": ";
    write_string(out, terminator.callee);
    out << ",\n";
    indent(out, level + 1);
    out << "\"args\": ";
    print_array(terminator.args, out, level + 1, print_operand);
    out << ",\n";
    indent(out, level + 1);
    out << "\"call_dest\": ";
    print_nullable_local(
        terminator.kind == Terminator::Kind::Call ? terminator.call_dest : kNoLocal, out);
    out << ",\n";
    indent(out, level + 1);
    out << "\"call_resume\": ";
    print_nullable_block(terminator.kind == Terminator::Kind::Call
                             ? std::optional<std::uint32_t>{terminator.call_resume}
                             : std::nullopt,
                         out);
    out << ",\n";
    indent(out, level + 1);
    out << "\"source_range\": ";
    print_source_range(terminator.source_range, out, level + 1);
    out << '\n';
    indent(out, level);
    out << '}';
}

void print_local(const Local &local, std::ostream &out, int level) {
    out << "{\n";
    indent(out, level + 1);
    out << "\"id\": " << local.id << ",\n";
    indent(out, level + 1);
    out << "\"name\": ";
    write_string(out, local.name);
    out << ",\n";
    indent(out, level + 1);
    out << "\"type\": ";
    print_type_ref(local.type, out, level + 1);
    out << ",\n";
    indent(out, level + 1);
    out << "\"is_temp\": " << (local.is_temp ? "true" : "false") << ",\n";
    indent(out, level + 1);
    out << "\"source_range\": ";
    print_source_range(local.source_range, out, level + 1);
    out << '\n';
    indent(out, level);
    out << '}';
}

void print_block(const BasicBlock &block, std::ostream &out, int level) {
    out << "{\n";
    indent(out, level + 1);
    out << "\"id\": " << block.id << ",\n";
    indent(out, level + 1);
    out << "\"label\": ";
    write_string(out, block.label);
    out << ",\n";
    indent(out, level + 1);
    out << "\"statements\": ";
    print_array(block.statements, out, level + 1, print_statement);
    out << ",\n";
    indent(out, level + 1);
    out << "\"terminator\": ";
    print_terminator(block.terminator, out, level + 1);
    out << '\n';
    indent(out, level);
    out << '}';
}

void print_function(const OptFunction &function, std::ostream &out, int level) {
    out << "{\n";
    indent(out, level + 1);
    out << "\"name\": ";
    write_string(out, function.name);
    out << ",\n";
    indent(out, level + 1);
    out << "\"arg_count\": " << function.arg_count << ",\n";
    indent(out, level + 1);
    out << "\"return_type\": ";
    print_type_ref(function.return_type, out, level + 1);
    out << ",\n";
    indent(out, level + 1);
    out << "\"source_range\": ";
    print_source_range(function.source_range, out, level + 1);
    out << ",\n";
    indent(out, level + 1);
    out << "\"locals\": ";
    print_array(function.locals, out, level + 1, print_local);
    out << ",\n";
    indent(out, level + 1);
    out << "\"blocks\": ";
    print_array(function.blocks, out, level + 1, print_block);
    out << '\n';
    indent(out, level);
    out << '}';
}

void print_skipped_temporal(const SkippedTemporalFragment &fragment, std::ostream &out, int level) {
    out << "{\n";
    indent(out, level + 1);
    out << "\"scope\": ";
    write_string(out, fragment.scope);
    out << ",\n";
    indent(out, level + 1);
    out << "\"kind\": ";
    write_string(out, fragment.kind);
    out << ",\n";
    indent(out, level + 1);
    out << "\"reason\": ";
    write_string(out, fragment.reason);
    out << ",\n";
    indent(out, level + 1);
    out << "\"source_range\": ";
    print_source_range(fragment.source_range, out, level + 1);
    out << '\n';
    indent(out, level);
    out << '}';
}

} // namespace

void print_opt_program_json(const OptProgram &program, std::ostream &out) {
    out << "{\n";
    indent(out, 1);
    out << "\"format\": \"AHFL_OPT_IR_V1\",\n";
    indent(out, 1);
    out << "\"source_program_version\": ";
    write_string(out, program.source_program_version);
    out << ",\n";
    indent(out, 1);
    out << "\"skipped_temporal\": ";
    print_array(program.skipped_temporal_fragments, out, 1, print_skipped_temporal);
    out << ",\n";
    indent(out, 1);
    out << "\"functions\": ";
    print_array(program.functions, out, 1, print_function);
    out << '\n';
    out << "}\n";
}

} // namespace ahfl::ir::opt
