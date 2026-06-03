#include "runtime/engine/grpc_transport.hpp"

#include "runtime/engine/curl_transport_adapter.hpp"
#include "runtime/engine/wire_value.hpp"

#include <sstream>

namespace ahfl::runtime {

// ============================================================================
// GrpcStatusCode helpers
// ============================================================================

const char *grpc_status_name(GrpcStatusCode code) {
    switch (code) {
    case GrpcStatusCode::Ok:
        return "OK";
    case GrpcStatusCode::Cancelled:
        return "CANCELLED";
    case GrpcStatusCode::Unknown:
        return "UNKNOWN";
    case GrpcStatusCode::InvalidArgument:
        return "INVALID_ARGUMENT";
    case GrpcStatusCode::DeadlineExceeded:
        return "DEADLINE_EXCEEDED";
    case GrpcStatusCode::NotFound:
        return "NOT_FOUND";
    case GrpcStatusCode::AlreadyExists:
        return "ALREADY_EXISTS";
    case GrpcStatusCode::PermissionDenied:
        return "PERMISSION_DENIED";
    case GrpcStatusCode::ResourceExhausted:
        return "RESOURCE_EXHAUSTED";
    case GrpcStatusCode::FailedPrecondition:
        return "FAILED_PRECONDITION";
    case GrpcStatusCode::Aborted:
        return "ABORTED";
    case GrpcStatusCode::OutOfRange:
        return "OUT_OF_RANGE";
    case GrpcStatusCode::Unimplemented:
        return "UNIMPLEMENTED";
    case GrpcStatusCode::Internal:
        return "INTERNAL";
    case GrpcStatusCode::Unavailable:
        return "UNAVAILABLE";
    case GrpcStatusCode::DataLoss:
        return "DATA_LOSS";
    case GrpcStatusCode::Unauthenticated:
        return "UNAUTHENTICATED";
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

namespace {

/// Map HTTP status codes to gRPC status codes for transcoding.
[[nodiscard]] GrpcStatusCode http_to_grpc_status(int http_code) {
    if (http_code >= 200 && http_code < 300) {
        return GrpcStatusCode::Ok;
    }
    switch (http_code) {
    case 400:
        return GrpcStatusCode::InvalidArgument;
    case 401:
        return GrpcStatusCode::Unauthenticated;
    case 403:
        return GrpcStatusCode::PermissionDenied;
    case 404:
        return GrpcStatusCode::NotFound;
    case 409:
        return GrpcStatusCode::AlreadyExists;
    case 429:
        return GrpcStatusCode::ResourceExhausted;
    case 499:
        return GrpcStatusCode::Cancelled;
    case 500:
        return GrpcStatusCode::Internal;
    case 501:
        return GrpcStatusCode::Unimplemented;
    case 503:
        return GrpcStatusCode::Unavailable;
    case 504:
        return GrpcStatusCode::DeadlineExceeded;
    default:
        break;
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
    return serialize_value_for_wire_json(value);
}

std::string serialize_args_for_grpc(const std::vector<evaluator::Value> &args) {
    return serialize_args_for_wire_json(args);
}

// ============================================================================
// build_grpc_curl_command
// ============================================================================

std::string build_grpc_curl_command(const GrpcRequest &request) {
    return describe_curl_transport(request);
}

// ============================================================================
// execute_grpc
// ============================================================================

GrpcResponse execute_grpc(const GrpcRequest &request) {
    const auto response = execute_curl_transport(request);
    if (response.timed_out) {
        return GrpcResponse{
            .status_code = GrpcStatusCode::DeadlineExceeded,
            .body = {},
            .error_message = "gRPC deadline exceeded",
        };
    }
    if (response.status_code == 0 && !response.error.empty()) {
        return GrpcResponse{
            .status_code = GrpcStatusCode::Unavailable,
            .body = response.body,
            .error_message = response.error,
        };
    }

    // Map HTTP status to gRPC status
    const auto grpc_status = http_to_grpc_status(response.status_code);

    if (grpc_status != GrpcStatusCode::Ok) {
        return GrpcResponse{
            .status_code = grpc_status,
            .body = response.body,
            .error_message = std::string("gRPC ") + grpc_status_name(grpc_status) + " (HTTP " +
                             std::to_string(response.status_code) + ")",
        };
    }

    return GrpcResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = response.body,
        .error_message = {},
    };
}

} // namespace ahfl::runtime
