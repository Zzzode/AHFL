#pragma once

#include <ostream>

#include "ahfl/runtime_session/session.hpp"

namespace ahfl {

void print_runtime_session_json(const runtime_session::RuntimeSession &session, std::ostream &out);

} // namespace ahfl
