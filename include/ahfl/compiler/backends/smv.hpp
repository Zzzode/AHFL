#pragma once

#include <ostream>
#include <cstddef>
#include <string>
#include <vector>

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/lowering.hpp"

namespace ahfl {

// RFC 0026 (KR6.3): the SMV verification backend consumes the AHFL-IR
// (verification / orchestration) layer of the IR tower. `ir::AhflIr` is that
// layer; today it aliases `ir::Program`, so this is a zero-behavior-change
// boundary annotation.
enum class SmvSpecificationOrigin { GeneratedControl, UserTemporal };
enum class SmvSpecificationCoverage { Complete, Conditional, Unsupported };
enum class SmvSpecificationAssumption { None, ControlEnvironment, DataObservation };

struct SmvSpecification {
    std::size_t id{0};
    SmvSpecificationOrigin origin{SmvSpecificationOrigin::GeneratedControl};
    SmvSpecificationCoverage coverage{SmvSpecificationCoverage::Complete};
    bool emitted{true};
    std::string description;
    SmvSpecificationAssumption assumption{SmvSpecificationAssumption::None};
};

struct SmvVerificationModel {
    std::string text;
    std::vector<SmvSpecification> specifications;
    std::size_t environment_assumption_count{0};
};

[[nodiscard]] SmvVerificationModel generate_program_smv(const ir::AhflIr &program);

void print_program_smv(const ir::AhflIr &program, std::ostream &out);

void emit_program_smv(const ast::Program &program,
                      const ResolveResult &resolve_result,
                      const TypeCheckResult &type_check_result,
                      std::ostream &out);

} // namespace ahfl
