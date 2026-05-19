#include "ahfl/runtime/grpc_transport.hpp"

#include "ahfl/evaluator/value_json.hpp"

#include <array>
#include <cstdio>
#include <sstream>

namespace ahfl::runtime {

// ============================================================================
// GrpcStatusCode helpers
// ============================================================================

const char *grpc_status_name(GrpcStatusCode code) {
    switch (code) {
    case GrpcStatusCode::Ok: return "OK";
    case GrpcStatusCode::Cancelled: return "CANCELLED";
    case GrpcStatusCode::Unknown: return "UNKNOWN";
    case GrpcStatusCode::InvalidArgument: return "INVALID_ARGUMENT";
    case GrpcStatusCode::DeadlineExceeded: return "DEADLINE_EXCEEDED";
    case GrpcStatusCode::NotFound: return "NOT_FOUND";
    case GrpcStatusCode::AlreadyExists: return "ALREADY_EXISTS";
    case GrpcStatusCode::PermissionDenied: return "PERMISSION_DENIED";
    case GrpcStatusCode::ResourceExhausted: return "RESOURCE_EXHAUSTED";
    case GrpcStatusCode::FailedPrecondition: return "FAILED_PRECONDITION";
    case GrpcStatusCode::Aborted: return "ABORTED";
    case GrpcStatusCode::OutOfRange: return "OUT_OF_RANGE";
    case GrpcStatusCode::Unimplemented: return "UNIMPLEMENTED";
    case GrpcStatusCode::Internal: return "INTERNAL";
    case GrpcStatusCode::Unavailable: return "UNAVAILABLE";
    case GrpcStatusCode::DataLoss: return "DATA_LOSS";
    case GrpcStatusCode::Unauthenticated: return "UNAUTHENTICATED";
    }
    return "UNKNOWN";
}

// ============================================================================
// GrpcEndpoint
// ============================================================================

std::string GrpcEndpoint::to_url() const {
    std::ostringstream oss;
    oss << (use_tls ? "https" : "http") << "://" << host << ":" << port;
    oss << to_path();
    return oss.str();
}

std::string GrpcEndpoint::to_path() const {
    return "/" + service_name + "/" + method_name;
}

// ============================================================================
// Shell quoting (same approach as HttpTransport)
// ============================================================================

namespace {

[[nodiscard]] std::string shell_quote(std::string_view value) {
    std::string quoted = "'";
    for (const auto character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted += "'";
    return quoted;
}

/// Map HTTP status codes to gRPC status codes for transcoding.
[[nodiscard]] GrpcStatusCode http_to_grpc_status(int http_code) {
    if (http_code >= 200 && http_code < 300) {
        return GrpcStatusCode::Ok;
    }
    switch (http_code) {
    case 400: return GrpcStatusCode::InvalidArgument;
    case 401: return GrpcStatusCode::Unauthenticated;
    case 403: return GrpcStatusCode::PermissionDenied;
    case 404: return GrpcStatusCode::NotFound;
    case 409: return GrpcStatusCode::AlreadyExists;
    case 429: return GrpcStatusCode::ResourceExhausted;
    case 499: return GrpcStatusCode::Cancelled;
    case 500: return GrpcStatusCode::Internal;
    case 501: return GrpcStatusCode::Unimplemented;
    case 503: return GrpcStatusCode::Unavailable;
    case 504: return GrpcStatusCode::DeadlineExceeded;
    default: break;
    }
    if (http_code >= 400 && http_code < 500) {
        return GrpcStatusCode::FailedPrecondition;
    }
    return GrpcStatusCode::Internal;
}

} // namespace

// ============================================================================
// serialize helpers
// ============================================================================

std::string serialize_value_for_grpc(const evaluator::Value &value) {
    return evaluator::value_to_json(value);
}

std::string serialize_args_for_grpc(const std::vector<evaluator::Value> &args) {
    if (args.empty()) {
        return "{}";
    }
    if (args.size() == 1) {
        if (std::holds_alternative<evaluator::StructValue>(args[0].node)) {
            return evaluator::value_to_json(args[0]);
        }
        return "{\"value\":" + evaluator::value_to_json(args[0]) + "}";
    }
    std::ostringstream oss;
    oss << "{\"args\":[";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            oss << ',';
        }
        oss << evaluator::value_to_json(args[i]);
    }
    oss << "]}";
    return oss.str();
}

// ============================================================================
// build_grpc_curl_command
// ============================================================================

std::string build_grpc_curl_command(const GrpcRequest &request) {
    std::ostringstream cmd;

    // Use --http2 for TLS (h2) or --http2-prior-knowledge for plaintext (h2c)
    cmd << "curl -s -w '\\n%{http_code}' ";
    if (request.endpoint.use_tls) {
        cmd << "--http2 ";
    } else {
        cmd << "--http2-prior-knowledge ";
    }

    cmd << "-X POST ";
    cmd << shell_quote(request.endpoint.to_url()) << " ";

    // Always set gRPC-relevant headers
    cmd << "-H " << shell_quote("Content-Type: application/json") << " ";
    cmd << "-H " << shell_quote("TE: trailers") << " ";

    // User-specified metadata as headers
    for (const auto &[key, value] : request.metadata) {
        cmd << "-H " << shell_quote(key + ": " + value) << " ";
    }

    // Request body
    if (!request.serialized_body.empty()) {
        cmd << "-d " << shell_quote(request.serialized_body) << " ";
    } else {
        cmd << "-d " << shell_quote("{}") << " ";
    }

    cmd << "--max-time " << request.timeout.count();
    return cmd.str();
}

// ============================================================================
// execute_grpc
// ============================================================================

GrpcResponse execute_grpc(const GrpcRequest &request) {
    const auto command = build_grpc_curl_command(request) + " 2>&1";

    FILE *pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return GrpcResponse{
            .status_code = GrpcStatusCode::Internal,
            .body = {},
            .error_message = "failed to start curl process for gRPC call",
        };
    }

    std::string output;
    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int exit_code = pclose(pipe);

    // curl exit code 28 = timeout
    if (exit_code != 0 && output.empty()) {
        if (exit_code == 28 || (WIFEXITED(exit_code) && WEXITSTATUS(exit_code) == 28)) {
            return GrpcResponse{
                .status_code = GrpcStatusCode::DeadlineExceeded,
                .body = {},
                .error_message = "gRPC deadline exceeded",
            };
        }
        return GrpcResponse{
            .status_code = GrpcStatusCode::Unavailable,
            .body = {},
            .error_message = "curl command failed with exit code " + std::to_string(exit_code),
        };
    }

    // Extract HTTP status code from last line (written by curl -w)
    int http_status = 0;
    std::string body = output;

    const auto last_newline = output.rfind('\n');
    if (last_newline != std::string::npos && last_newline > 0) {
        const auto status_str = output.substr(last_newline + 1);
        body = output.substr(0, last_newline);

        if (!body.empty() && body.back() == '\n') {
            body.pop_back();
        }

        try {
            http_status = std::stoi(status_str);
        } catch (...) {
            http_status = 0;
            body = output;
        }
    } else if (!output.empty()) {
        try {
            http_status = std::stoi(output);
            body.clear();
        } catch (...) {
            http_status = 0;
            body = output;
        }
    }

    // Handle transport-level failures
    if (http_status == 0 && exit_code != 0) {
        const int real_exit = WIFEXITED(exit_code) ? WEXITSTATUS(exit_code) : exit_code;
        if (real_exit == 28) {
            return GrpcResponse{
                .status_code = GrpcStatusCode::DeadlineExceeded,
                .body = body,
                .error_message = "gRPC deadline exceeded",
            };
        }
        return GrpcResponse{
            .status_code = GrpcStatusCode::Unavailable,
            .body = body,
            .error_message = "gRPC transport error (curl exit " + std::to_string(real_exit) + ")",
        };
    }

    // Map HTTP status to gRPC status
    const auto grpc_status = http_to_grpc_status(http_status);

    if (grpc_status != GrpcStatusCode::Ok) {
        return GrpcResponse{
            .status_code = grpc_status,
            .body = body,
            .error_message = std::string("gRPC ") + grpc_status_name(grpc_status) +
                             " (HTTP " + std::to_string(http_status) + ")",
        };
    }

    return GrpcResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = body,
        .error_message = {},
    };
}

} // namespace ahfl::runtime
