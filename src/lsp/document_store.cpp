#include "ahfl/lsp/document_store.hpp"

namespace ahfl::lsp {

void DocumentStore::open(const TextDocumentItem &item) {
    documents_[item.uri] = item;
}

void DocumentStore::change(const std::string &uri, int version, const std::string &new_text) {
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        it->second.version = version;
        it->second.text = new_text;
    }
}

void DocumentStore::close(const std::string &uri) {
    documents_.erase(uri);
}

const TextDocumentItem *DocumentStore::get(const std::string &uri) const {
    auto it = documents_.find(uri);
    if (it != documents_.end()) {
        return &it->second;
    }
    return nullptr;
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
