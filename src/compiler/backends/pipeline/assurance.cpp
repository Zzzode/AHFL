#include "compiler/backends/pipeline/assurance.hpp"

#include "compiler/assurance/assurance.hpp"

namespace ahfl {

void print_program_assurance_json(const ir::Program &program, std::ostream &out) {
    assurance::print_program_assurance_json(program, out);
}

} // namespace ahfl
