#pragma once

#include "tooling/registry/http.hpp"

namespace ahfl::registry {

/// Abstract endpoint logic. The HTTP transport (raw POSIX sockets today, any
/// HTTP library tomorrow) parses requests, dispatches them to a RequestHandler
/// and serializes its responses; endpoint logic never touches sockets.
class RequestHandler {
  public:
    virtual ~RequestHandler() = default;

    [[nodiscard]] virtual HttpResponse handle(const HttpRequest &request) = 0;
};

} // namespace ahfl::registry
