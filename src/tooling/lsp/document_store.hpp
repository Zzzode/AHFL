#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include "tooling/lsp/protocol_types.hpp"

namespace ahfl::lsp {

/// In-memory store of open text documents.
class DocumentStore {
  public:
    void open(const TextDocumentItem &item);
    void change(const std::string &uri, int version, const std::string &new_text);
    void close(const std::string &uri);

    [[nodiscard]] const TextDocumentItem *get(const std::string &uri) const;
    [[nodiscard]] std::optional<std::uint64_t> revision(const std::string &uri) const;
    [[nodiscard]] std::optional<std::uint64_t> content_hash(const std::string &uri) const;
    [[nodiscard]] std::uint64_t workspace_revision() const noexcept;
    [[nodiscard]] bool contains(const std::string &uri) const;
    [[nodiscard]] std::vector<std::string> all_uris() const;

  private:
    struct StoredDocument {
        TextDocumentItem item;
        std::uint64_t revision{0};
        std::uint64_t content_hash{0};
    };

    [[nodiscard]] static std::uint64_t hash_content(const std::string &text) noexcept;

    std::unordered_map<std::string, StoredDocument> documents_;
    std::uint64_t next_revision_{1};
    std::uint64_t workspace_revision_{0};
};

} // namespace ahfl::lsp
