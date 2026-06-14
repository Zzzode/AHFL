#pragma once

#include "ahfl/base/support/source.hpp"
#include "ahfl/compiler/ir/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace ahfl::ir::opt {

// A local variable in SSA form. Each definition creates a new Local.
struct Local {
    std::uint32_t id{0};
    std::string name;    // debug name (e.g., "x", "_tmp0")
    TypeRef type;        // resolved type
    bool is_temp{false}; // compiler-generated temporary
    std::optional<SourceRange> source_range;
};

// An SSA value reference (index into OptFunction::locals)
using LocalRef = std::uint32_t;
constexpr LocalRef kNoLocal = UINT32_MAX;

// Constant values
using Constant = std::variant<std::monostate, // unit/none
                              bool,           // bool
                              std::int64_t,   // int
                              double,         // float
                              std::string     // string/decimal/duration spelling
                              >;

// An operand (right-hand side of an assignment)
struct Operand {
    enum class Kind {
        Local,    // use of another local
        Constant, // literal constant
        Arg,      // function argument by index
    };
    Kind kind{Kind::Local};
    LocalRef local{kNoLocal};
    Constant constant;
    std::uint32_t arg_index{0};
};

// Rvalue: the right-hand side of a statement
struct Rvalue {
    enum class Kind {
        Use,       // simple copy: dest = operand
        BinaryOp,  // dest = lhs op rhs
        UnaryOp,   // dest = op operand
        Call,      // dest = callee(args...)
        Aggregate, // dest = StructLiteral{fields...}
        Field,     // dest = base.field
        Index,     // dest = base[index]
    };
    Kind kind{Kind::Use};
    std::string op;                // binary/unary op name
    std::string callee;            // for Call
    std::string field_name;        // for Field
    std::vector<Operand> operands; // inputs
    TypeRef result_type;           // type of the produced value
};

// A statement within a basic block
struct Statement {
    enum class Kind {
        Assign,      // local = rvalue
        StorageLive, // mark local as live
        StorageDead, // mark local as dead
        Nop,         // no-op (placeholder)
    };
    Kind kind{Kind::Assign};
    LocalRef dest{kNoLocal};
    Rvalue rvalue;
    std::optional<SourceRange> source_range;
};

// Block terminator (how control leaves a basic block)
struct Terminator {
    enum class Kind {
        Goto,        // unconditional jump
        SwitchBool,  // if (cond) goto then else goto else
        Return,      // return value
        Call,        // call + continuation block
        Assert,      // assert condition, fail -> unwind
        Unreachable, // should never be reached
    };
    Kind kind{Kind::Goto};
    LocalRef condition{kNoLocal};    // for SwitchBool/Assert
    LocalRef return_value{kNoLocal}; // for Return
    std::uint32_t target{0};         // for Goto: target block
    std::uint32_t then_block{0};     // for SwitchBool
    std::uint32_t else_block{0};     // for SwitchBool
    std::string callee;              // for Call terminator
    std::vector<Operand> args;       // for Call terminator
    LocalRef call_dest{kNoLocal};    // for Call: destination local
    std::uint32_t call_resume{0};    // for Call: resume block after return
    std::optional<SourceRange> source_range;
};

// A basic block
struct BasicBlock {
    std::uint32_t id{0};
    std::string label; // debug label
    std::vector<Statement> statements;
    Terminator terminator;
};

// A function in the optimization IR (one per agent state handler or workflow node)
struct OptFunction {
    std::string name;               // e.g., "agent::MyAgent::Init"
    std::vector<Local> locals;      // all locals (params first, then temporaries)
    std::uint32_t arg_count{0};     // how many locals are parameters
    std::vector<BasicBlock> blocks; // blocks[0] is the entry block
    TypeRef return_type;
    std::optional<SourceRange> source_range;
};

// Temporal fragments that are intentionally not lowered into expression
// functions because they represent event/state atoms rather than pure values.
struct SkippedTemporalFragment {
    std::string scope; // e.g., "workflow::W::safety::0"
    std::string kind;  // called / running / completed / in_state
    std::string reason;
    std::optional<SourceRange> source_range;
};

// The complete optimization IR program
struct OptProgram {
    std::vector<OptFunction> functions;
    std::vector<SkippedTemporalFragment> skipped_temporal_fragments;
    std::string source_program_version; // traces back to ir::Program::format_version
};

} // namespace ahfl::ir::opt
