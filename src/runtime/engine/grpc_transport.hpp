#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "runtime/evaluator/value.hpp"

namespace ahfl::runtime {

/// gRPC status codes (subset matching google.rpc.Code).
enum class GrpcStatusCode : int {
    Ok = 0,
    Cancelled = 1,
    Unknown = 2,
    InvalidArgument = 3,
    DeadlineExceeded = 4,
    NotFound = 5,
    AlreadyExists = 6,
    PermissionDenied = 7,
    ResourceExhausted = 8,
    FailedPrecondition = 9,
    Aborted = 10,
    OutOfRange = 11,
    Unimplemented = 12,
    Internal = 13,
    Unavailable = 14,
    DataLoss = 15,
    Unauthenticated = 16,
};

/// Convert a gRPC status code to human-readable string.
[[nodiscard]] const char *grpc_status_name(GrpcStatusCode code);

/// HTTP/2 JSON-transcoding endpoint for a gRPC-shaped service path.
struct GrpcJsonTranscodingEndpoint {
    std::string host;
    uint16_t port{50051};
    std::string service_name;
    std::string method_name;
    bool use_tls{false};

    /// Build the full URL for HTTP/2-based gRPC JSON transcoding.
    [[nodiscard]] std::string to_url() const;

    /// Build the service path (/<service>/<method>).
    [[nodiscard]] std::string to_path() const;
};

/// HTTP/2 JSON-transcoding request structure.
struct GrpcJsonTranscodingRequest {
    GrpcJsonTranscodingEndpoint endpoint;
    std::string serialized_body; // JSON encoding of Value
    std::vector<std::pair<std::string, std::string>> metadata;
    std::chrono::seconds timeout{30};
};

/// HTTP/2 JSON-transcoding response mapped onto gRPC status codes.
struct GrpcJsonTranscodingResponse {
    GrpcStatusCode status_code{GrpcStatusCode::Unknown};
    std::string body;
    std::string error_message;

    [[nodiscard]] bool is_ok() const {
        return status_code == GrpcStatusCode::Ok;
    }
};

/// Execute an HTTP/2 JSON-transcoding call.
[[nodiscard]] GrpcJsonTranscodingResponse
execute_grpc_json_transcoding(const GrpcJsonTranscodingRequest &request);

/// Serialize a Value to the JSON-transcoding wire format.
[[nodiscard]] std::string serialize_value_for_grpc_json_transcoding(const evaluator::Value &value);

/// Serialize a vector of Values to the JSON-transcoding wire format.
[[nodiscard]] std::string
serialize_args_for_grpc_json_transcoding(const std::vector<evaluator::Value> &args);

/// Build the curl command string for a JSON-transcoding request (exposed for testing).
[[nodiscard]] std::string
build_grpc_json_transcoding_curl_command(const GrpcJsonTranscodingRequest &request);

} // namespace ahfl::runtime
