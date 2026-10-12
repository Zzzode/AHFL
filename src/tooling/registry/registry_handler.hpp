#pragma once

#include "tooling/registry/request_handler.hpp"
#include "tooling/registry/store.hpp"

namespace ahfl::registry {

/// Implements the seven-route registry protocol the RFC 0010 client speaks
/// (mirrored by the LocalRegistry fixture in
/// scripts/generate-release-evidence-archive.py):
///
///   GET  /packages/{name}/registry-index
///   GET  /packages/{name}/versions/{version}/registry-index
///   GET  /packages/{name}/versions/{version}/source-archive
///   GET  /packages/{name}/versions/{version}/source-archive.payload
///   GET  /packages/{name}/versions/{version}/public-api
///   PUT  /packages/{name}/versions/{version}/publish
///   POST /packages/{name}/versions/{version}/yank
///
/// Only the documented route spelling is accepted. All responses
/// are JSON except the source archive payload, which is served as raw bytes.
class RegistryRequestHandler final : public RequestHandler {
  public:
    explicit RegistryRequestHandler(RegistryStore store) : store_(std::move(store)) {}

    [[nodiscard]] HttpResponse handle(const HttpRequest &request) override;

  private:
    RegistryStore store_;
};

} // namespace ahfl::registry
