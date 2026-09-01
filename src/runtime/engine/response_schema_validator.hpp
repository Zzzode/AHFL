#pragma once

#include "ahfl/compiler/ir/ir.hpp"
#include "ahfl/compiler/ir/types.hpp"
#include "runtime/engine/core_wire_codec.hpp"
#include "runtime/evaluator/value.hpp"

#include <string>

namespace ahfl::runtime {

// SchemaValidationResult is owned by the shared wire codec (core_wire_codec.hpp)
// so there is one {valid,error} validation-result type across the runtime. This
// legacy TypeRef-based validator reuses it; it will be demoted to a thin shim
// over the codec in a later C2b stage.

[[nodiscard]] SchemaValidationResult validate_value_against_schema(const evaluator::Value &value,
                                                                   const ir::TypeRef &expected);

[[nodiscard]] SchemaValidationResult validate_value_against_schema(const evaluator::Value &value,
                                                                   const ir::TypeRef &expected,
                                                                   const ir::Program &program);

} // namespace ahfl::runtime
