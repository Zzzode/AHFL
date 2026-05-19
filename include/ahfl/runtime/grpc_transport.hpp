#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ahfl/evaluator/value.hpp"

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

/// gRPC endpoint definition.
struct GrpcEndpoint {
    std::string host;
    uint16_t port{50051};
    std::string service_name;
    std::string method_name;
    bool use_tls{false};

    /// Build the full URL for HTTP/2-based gRPC JSON transcoding.
    [[nodiscard]] std::string to_url() const;

    /// Build the gRPC path (/<service>/<method>).
    [[nodiscard]] std::string to_path() const;
};

/// gRPC request structure.
struct GrpcRequest {
    GrpcEndpoint endpoint;
    std::string serialized_body; // JSON encoding of Value
    std::vector<std::pair<std::string, std::string>> metadata;
    std::chrono::seconds timeout{30};
};

/// gRPC response structure.
struct GrpcResponse {
    GrpcStatusCode status_code{GrpcStatusCode::Unknown};
    std::string body;
    std::string error_message;

    [[nodiscard]] bool is_ok() const { return status_code == GrpcStatusCode::Ok; }
};

/// Simulates gRPC call via HTTP/2 using curl (h2c for plaintext, h2 for TLS).
/// In production, this would use a real gRPC library.
[[nodiscard]] GrpcResponse execute_grpc(const GrpcRequest &request);

/// Serialize a Value to JSON wire format for gRPC.
[[nodiscard]] std::string serialize_value_for_grpc(const evaluator::Value &value);

/// Serialize a vector of Values to JSON wire format for gRPC.
[[nodiscard]] std::string serialize_args_for_grpc(const std::vector<evaluator::Value> &args);

/// Build the curl command string for a gRPC request (exposed for testing).
[[nodiscard]] std::string build_grpc_curl_command(const GrpcRequest &request);

} // namespace ahfl::runtime
