#include "runtime/engine/http_transport.hpp"

#include "runtime/engine/wire_transport_adapter.hpp"

namespace ahfl::runtime {

std::string HttpTransport::build_curl_command(const HttpRequest &request) {
    return describe_wire_transport(request);
}

HttpResponse HttpTransport::execute(const HttpRequest &request) {
    const auto response = execute_wire_transport(request);

    return HttpResponse{
        .status_code = response.status_code,
        .body = response.body,
        .error = response.error,
    };
}

} // namespace ahfl::runtime
