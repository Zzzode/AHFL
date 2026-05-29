#pragma once

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
    [[nodiscard]] bool contains(const std::string &uri) const;
    [[nodiscard]] std::vector<std::string> all_uris() const;

private:
    std::unordered_map<std::string, TextDocumentItem> documents_;
};

} // namespace ahfl::lsp
