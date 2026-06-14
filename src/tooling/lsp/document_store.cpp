#include "tooling/lsp/document_store.hpp"

namespace ahfl::lsp {

std::uint64_t DocumentStore::hash_content(const std::string &text) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char ch : text) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void DocumentStore::open(const TextDocumentItem &item) {
    documents_[item.uri] = StoredDocument{
        .item = item,
        .revision = next_revision_++,
        .content_hash = hash_content(item.text),
    };
    ++workspace_revision_;
}

void DocumentStore::change(const std::string &uri, int version, const std::string &new_text) {
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        it->second.item.version = version;
        it->second.item.text = new_text;
        it->second.revision = next_revision_++;
        it->second.content_hash = hash_content(new_text);
        ++workspace_revision_;
    }
}

void DocumentStore::close(const std::string &uri) {
    if (documents_.erase(uri) > 0) {
        ++workspace_revision_;
    }
}

const TextDocumentItem *DocumentStore::get(const std::string &uri) const {
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        return &it->second.item;
    }
    return nullptr;
}

std::optional<std::uint64_t> DocumentStore::revision(const std::string &uri) const {
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        return std::nullopt;
    }
    return it->second.revision;
}

std::optional<std::uint64_t> DocumentStore::content_hash(const std::string &uri) const {
    auto it = documents_.find(uri);
    if (it == documents_.end()) {
        return std::nullopt;
    }
    return it->second.content_hash;
}

std::uint64_t DocumentStore::workspace_revision() const noexcept {
    return workspace_revision_;
}

bool DocumentStore::contains(const std::string &uri) const {
    return documents_.contains(uri);
}

std::vector<std::string> DocumentStore::all_uris() const {
    std::vector<std::string> uris;
    uris.reserve(documents_.size());
    for (const auto &[uri, _] : documents_) {
        uris.push_back(uri);
    }
    return uris;
}

} // namespace ahfl::lsp
