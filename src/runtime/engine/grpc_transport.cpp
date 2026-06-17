#include "runtime/engine/grpc_transport.hpp"

#include "runtime/engine/wire_transport_adapter.hpp"
#include "runtime/engine/wire_value.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ahfl::runtime {

namespace {

[[nodiscard]] std::string percent_decode(const std::string &encoded) {
    std::string decoded;
    decoded.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.size() &&
            std::isxdigit(static_cast<unsigned char>(encoded[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(encoded[i + 2]))) {
            const auto hi = encoded[i + 1];
            const auto lo = encoded[i + 2];
            auto hex_digit = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return 0;
            };
            decoded += static_cast<char>((hex_digit(hi) << 4) | hex_digit(lo));
            i += 2;
        } else {
            decoded += encoded[i];
        }
    }
    return decoded;
}

[[nodiscard]] bool case_insensitive_equals(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](char lhs, char rhs) {
        return std::tolower(static_cast<unsigned char>(lhs)) ==
               std::tolower(static_cast<unsigned char>(rhs));
    });
}

} // namespace

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
// GrpcJsonTranscodingEndpoint
// ============================================================================

std::string GrpcJsonTranscodingEndpoint::to_url() const {
    std::ostringstream oss;
    oss << (use_tls ? "https" : "http") << "://" << host << ":" << port;
    oss << to_path();
    return oss.str();
}

std::string GrpcJsonTranscodingEndpoint::to_path() const {
    return "/" + service_name + "/" + method_name;
}

GrpcStatusCode grpc_status_from_http_status(int http_code) {
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

// ============================================================================
// gRPC header/trailer parsing
// ============================================================================

GrpcStatusCode
parse_grpc_status_from_headers(const std::vector<std::pair<std::string, std::string>> &headers) {
    for (const auto &[name, value] : headers) {
        if (case_insensitive_equals(name, "grpc-status")) {
            try {
                const int code = std::stoi(value);
                return static_cast<GrpcStatusCode>(code);
            } catch (...) {
                return GrpcStatusCode::Unknown;
            }
        }
    }
    return GrpcStatusCode::Ok;
}

std::string
parse_grpc_message_from_headers(const std::vector<std::pair<std::string, std::string>> &headers) {
    for (const auto &[name, value] : headers) {
        if (case_insensitive_equals(name, "grpc-message")) {
            return percent_decode(value);
        }
    }
    return {};
}

// ============================================================================
// serialize helpers
// ============================================================================

std::string serialize_value_for_grpc_json_transcoding(const evaluator::Value &value) {
    return serialize_value_for_wire_json(value);
}

std::string serialize_args_for_grpc_json_transcoding(const std::vector<evaluator::Value> &args) {
    return serialize_args_for_wire_json(args);
}

std::string build_grpc_json_transcoding_curl_command(const GrpcJsonTranscodingRequest &request) {
    return describe_wire_transport(request);
}

GrpcJsonTranscodingResponse
execute_grpc_json_transcoding(const GrpcJsonTranscodingRequest &request) {
    const auto response = execute_wire_transport(request);
    if (response.timed_out) {
        return GrpcJsonTranscodingResponse{
            .status_code = GrpcStatusCode::DeadlineExceeded,
            .body = {},
            .error_message = "gRPC JSON transcoding deadline exceeded",
            .response_metadata = response.response_headers,
            .trailers = response.trailers,
        };
    }
    if (response.status_code == 0 && !response.error.empty()) {
        return GrpcJsonTranscodingResponse{
            .status_code = GrpcStatusCode::Unavailable,
            .body = response.body,
            .error_message = response.error,
            .response_metadata = response.response_headers,
            .trailers = response.trailers,
        };
    }

    // Map HTTP status to gRPC status for the JSON-transcoding bridge.
    const auto grpc_status = grpc_status_from_http_status(response.status_code);

    if (grpc_status != GrpcStatusCode::Ok) {
        return GrpcJsonTranscodingResponse{
            .status_code = grpc_status,
            .body = response.body,
            .error_message = std::string("gRPC ") + grpc_status_name(grpc_status) + " (HTTP " +
                             std::to_string(response.status_code) + ")",
            .response_metadata = response.response_headers,
            .trailers = response.trailers,
        };
    }

    return GrpcJsonTranscodingResponse{
        .status_code = GrpcStatusCode::Ok,
        .body = response.body,
        .error_message = {},
        .response_metadata = response.response_headers,
        .trailers = response.trailers,
    };
}

} // namespace ahfl::runtime
