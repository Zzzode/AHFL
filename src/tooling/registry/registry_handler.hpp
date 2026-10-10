#pragma once

#include "tooling/registry/request_handler.hpp"
#include "tooling/registry/store.hpp"

namespace ahfl::registry {

/// Implements the seven-route registry protocol the RFC 0010 client speaks
/// (mirrored by the LocalRegistry fixture in
/// scripts/generate-release-evidence-archive.py):
///
///   GET  /v1/packages/{name}/registry-index
///   GET  /v1/packages/{name}/versions/{version}/registry-index
///   GET  /v1/packages/{name}/versions/{version}/source-archive
///   GET  /v1/packages/{name}/versions/{version}/source-archive.payload
///   GET  /v1/packages/{name}/versions/{version}/public-api
///   PUT  /v1/packages/{name}/versions/{version}/publish
///   POST /v1/packages/{name}/versions/{version}/yank
///
/// The `/v1` (or `/api/v1`) prefix is optional for routing. All responses
/// are JSON except the source archive payload, which is served as raw bytes.
class RegistryRequestHandler final : public RequestHandler {
  public:
    explicit RegistryRequestHandler(RegistryStore store) : store_(std::move(store)) {}

    [[nodiscard]] HttpResponse handle(const HttpRequest &request) override;

  private:
    RegistryStore store_;
};

} // namespace ahfl::registry
