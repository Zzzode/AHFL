#include "compiler/ir/opt/opt_passes.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace ahfl::ir::opt {

namespace {

/// Check if an operand refers to a specific local.
bool operand_uses_local(const Operand &op, LocalRef ref) {
    return op.kind == Operand::Kind::Local && op.local == ref;
}

/// Replace uses of `from` with `replacement` in an operand.
/// Returns true if a replacement was made.
bool replace_operand(Operand &op, LocalRef from, const Operand &replacement) {
    if (operand_uses_local(op, from)) {
        op = replacement;
        return true;
    }
    return false;
}

/// Replace all uses of `from` with `replacement` across a function.
/// Does not replace definitions (dest fields).
bool replace_all_uses(OptFunction &function, LocalRef from, const Operand &replacement) {
    bool changed = false;

    for (auto &block : function.blocks) {
        for (auto &stmt : block.statements) {
            for (auto &op : stmt.rvalue.operands) {
                if (replace_operand(op, from, replacement)) {
                    changed = true;
                }
            }
        }
        // Replace in terminator operands.
        auto &term = block.terminator;
        if (term.condition == from && replacement.kind == Operand::Kind::Local) {
            term.condition = replacement.local;
            changed = true;
        }
        if (term.return_value == from && replacement.kind == Operand::Kind::Local) {
            term.return_value = replacement.local;
            changed = true;
        }
        for (auto &arg : term.args) {
            if (replace_operand(arg, from, replacement)) {
                changed = true;
            }
        }
    }

    return changed;
}

/// Count how many times a local is used as an operand (read).
std::unordered_map<LocalRef, std::uint32_t> build_use_counts(const OptFunction &function) {
    std::unordered_map<LocalRef, std::uint32_t> counts;

    for (const auto &block : function.blocks) {
        for (const auto &stmt : block.statements) {
            for (const auto &op : stmt.rvalue.operands) {
                if (op.kind == Operand::Kind::Local) {
                    counts[op.local]++;
                }
            }
        }
        // Count uses in terminators.
        const auto &term = block.terminator;
        if (term.condition != kNoLocal) {
            counts[term.condition]++;
        }
        if (term.return_value != kNoLocal) {
            counts[term.return_value]++;
        }
        for (const auto &arg : term.args) {
            if (arg.kind == Operand::Kind::Local) {
                counts[arg.local]++;
            }
        }
    }

    return counts;
}

/// Check whether an Rvalue is a simple Use with a single constant operand.
bool is_constant_use(const Rvalue &rv, Constant &out_constant) {
    if (rv.kind != Rvalue::Kind::Use) {
        return false;
    }
    if (rv.operands.size() != 1) {
        return false;
    }
    if (rv.operands[0].kind != Operand::Kind::Constant) {
        return false;
    }
    out_constant = rv.operands[0].constant;
    return true;
}

/// Check whether an Rvalue is a simple Use of a single local.
bool is_local_copy(const Rvalue &rv, LocalRef &out_source) {
    if (rv.kind != Rvalue::Kind::Use) {
        return false;
    }
    if (rv.operands.size() != 1) {
        return false;
    }
    if (rv.operands[0].kind != Operand::Kind::Local) {
        return false;
    }
    out_source = rv.operands[0].local;
    return true;
}

} // anonymous namespace

bool constant_propagation(OptFunction &function) {
    bool changed = false;

    // Build a map of locals that hold known constants.
    // A local is a known constant if it has exactly one definition and that
    // definition is `local = constant`.
    std::unordered_map<LocalRef, Constant> known_constants;

    for (const auto &block : function.blocks) {
        for (const auto &stmt : block.statements) {
            if (stmt.kind != Statement::Kind::Assign) {
                continue;
            }
            Constant c;
            if (is_constant_use(stmt.rvalue, c)) {
                known_constants[stmt.dest] = std::move(c);
            }
        }
    }

    // Replace uses of known-constant locals with the constant value.
    for (auto &block : function.blocks) {
        for (auto &stmt : block.statements) {
            for (auto &op : stmt.rvalue.operands) {
                if (op.kind == Operand::Kind::Local) {
                    auto it = known_constants.find(op.local);
                    if (it != known_constants.end()) {
                        op.kind = Operand::Kind::Constant;
                        op.constant = it->second;
                        op.local = kNoLocal;
                        changed = true;
                    }
                }
            }
        }
        // Also propagate into terminator args.
        for (auto &arg : block.terminator.args) {
            if (arg.kind == Operand::Kind::Local) {
                auto it = known_constants.find(arg.local);
                if (it != known_constants.end()) {
                    arg.kind = Operand::Kind::Constant;
                    arg.constant = it->second;
                    arg.local = kNoLocal;
                    changed = true;
                }
            }
        }
    }

    return changed;
}

bool dead_store_elimination(OptFunction &function) {
    bool changed = false;

    auto use_counts = build_use_counts(function);

    for (auto &block : function.blocks) {
        auto it = block.statements.begin();
        while (it != block.statements.end()) {
            if (it->kind != Statement::Kind::Assign) {
                ++it;
                continue;
            }
            auto dest = it->dest;
            if (dest == kNoLocal) {
                ++it;
                continue;
            }
            // Don't remove if it has uses.
            if (use_counts.count(dest) != 0 && use_counts[dest] > 0) {
                ++it;
                continue;
            }
            // Don't remove calls (they may have side effects).
            if (it->rvalue.kind == Rvalue::Kind::Call) {
                ++it;
                continue;
            }
            // Safe to remove.
            it = block.statements.erase(it);
            changed = true;
        }
    }

    return changed;
}

bool copy_propagation(OptFunction &function) {
    bool changed = false;

    // Find all simple copies: x = y (Use rvalue with single local operand).
    std::vector<std::pair<LocalRef, LocalRef>> copies; // (dest, source)

    for (const auto &block : function.blocks) {
        for (const auto &stmt : block.statements) {
            if (stmt.kind != Statement::Kind::Assign) {
                continue;
            }
            LocalRef source = kNoLocal;
            if (is_local_copy(stmt.rvalue, source)) {
                copies.emplace_back(stmt.dest, source);
            }
        }
    }

    // Replace all uses of dest with source.
    for (const auto &[dest, source] : copies) {
        Operand replacement;
        replacement.kind = Operand::Kind::Local;
        replacement.local = source;
        if (replace_all_uses(function, dest, replacement)) {
            changed = true;
        }
    }

    return changed;
}

bool optimize(OptFunction &function) {
    bool any_changed = false;
    bool changed = true;

    // Iterate until fixpoint.
    while (changed) {
        changed = false;
        if (constant_propagation(function)) {
            changed = true;
        }
        if (copy_propagation(function)) {
            changed = true;
        }
        if (dead_store_elimination(function)) {
            changed = true;
        }
        if (changed) {
            any_changed = true;
        }
    }

    return any_changed;
}

} // namespace ahfl::ir::opt
