#pragma once

#include <iostream>
#include <ostream>

namespace ahfl::cli {

struct OutputContext {
    std::ostream &out = std::cout;
    std::ostream &err = std::cerr;
};

} // namespace ahfl::cli
