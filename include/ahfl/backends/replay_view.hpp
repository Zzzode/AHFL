#pragma once

#include <ostream>

#include "ahfl/replay_view/replay.hpp"

namespace ahfl {

void print_replay_view_json(const replay_view::ReplayView &replay, std::ostream &out);

} // namespace ahfl
