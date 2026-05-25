#pragma once

#include "formal/counterexample.hpp"

#include <ostream>
#include <string>

namespace ahfl::formal {

/// Write a structured counterexample trace and explanation as JSON to the output stream.
void write_counterexample_json(const CounterexampleTrace &trace,
                               const ViolationExplanation &explanation,
                               std::ostream &out);

/// Serialize a counterexample trace and explanation to a JSON string.
[[nodiscard]] std::string counterexample_to_json(const CounterexampleTrace &trace,
                                                  const ViolationExplanation &explanation);

} // namespace ahfl::formal
