#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <vector>

#include "ahfl/compiler/ir/expr.hpp"

namespace ahfl::ir {

/// Arena allocator for Expr nodes.
///
/// Provides a flat, cache-friendly store of all expressions within a Program.
/// The arena index naturally serves as a stable numeric ID for each expression.
/// The arena owns every expression; IR nodes store ExprRef handles instead of
/// nested owning pointers.
class ExprArena {
  public:
    using Index = ExprRef::Index;
    static constexpr Index kInvalid = ExprRef::kInvalid;

    ExprArena() = default;
    ExprArena(const ExprArena &) = delete;
    ExprArena &operator=(const ExprArena &) = delete;
    ExprArena(ExprArena &&other) noexcept { move_from(std::move(other)); }
    ExprArena &operator=(ExprArena &&other) noexcept {
        if (this != &other) {
            clear();
            move_from(std::move(other));
        }
        return *this;
    }
    ~ExprArena() { clear(); }

    /// Allocate an expression in the arena and return its stable handle.
    [[nodiscard]] ExprRef make(ExprNode node,
                               SourceRangeOpt source_range = std::nullopt,
                               TypeRef resolved_type = {},
                               std::uint32_t id = 0) {
        const auto idx = static_cast<Index>(index_.size());
        auto *raw = new (allocate(sizeof(Expr), alignof(Expr))) Expr{
            .node = std::move(node),
            .source_range = std::move(source_range),
            .resolved_type = std::move(resolved_type),
            .id = id,
        };
        index_.push_back(raw);
        return ExprRef{*raw, idx};
    }

    /// Access by index (mutable).
    [[nodiscard]] Expr &get(Index idx) {
        return *index_.at(idx);
    }

    /// Access by index (const).
    [[nodiscard]] const Expr &get(Index idx) const {
        return *index_.at(idx);
    }

    /// Number of expressions registered.
    [[nodiscard]] std::size_t size() const noexcept { return index_.size(); }

    /// Whether the arena is empty.
    [[nodiscard]] bool empty() const noexcept { return index_.empty(); }

    /// Span over all registered expression pointers (for O(1) iteration).
    [[nodiscard]] std::span<Expr *const> span() noexcept { return index_; }

    /// Const span over all registered expression pointers.
    [[nodiscard]] std::span<const Expr *const> span() const noexcept {
        return {index_.data(), index_.size()};
    }

  private:
    static constexpr std::size_t kDefaultChunkBytes = 64U * 1024U;

    void clear() noexcept {
        for (auto iter = index_.rbegin(); iter != index_.rend(); ++iter) {
            std::destroy_at(*iter);
        }
        index_.clear();
        chunks_.clear();
        next_ = nullptr;
        remaining_ = 0;
    }

    void move_from(ExprArena &&other) noexcept {
        chunks_ = std::move(other.chunks_);
        index_ = std::move(other.index_);
        next_ = other.next_;
        remaining_ = other.remaining_;
        other.index_.clear();
        other.next_ = nullptr;
        other.remaining_ = 0;
    }

    [[nodiscard]] void *allocate(std::size_t size, std::size_t alignment) {
        if (size == 0) {
            size = 1;
        }

        void *cursor = next_;
        auto space = remaining_;
        auto *aligned = std::align(alignment, size, cursor, space);
        if (aligned == nullptr) {
            add_chunk(std::max(kDefaultChunkBytes, size + alignment));
            cursor = next_;
            space = remaining_;
            aligned = std::align(alignment, size, cursor, space);
        }
        assert(aligned != nullptr);
        auto *result = static_cast<std::byte *>(aligned);
        next_ = result + size;
        remaining_ = space - size;
        return result;
    }

    void add_chunk(std::size_t bytes) {
        auto chunk = std::make_unique<std::byte[]>(bytes);
        next_ = chunk.get();
        remaining_ = bytes;
        chunks_.push_back(std::move(chunk));
    }

    std::vector<std::unique_ptr<std::byte[]>> chunks_;
    std::vector<Expr *> index_;
    std::byte *next_{nullptr};
    std::size_t remaining_{0};
};

} // namespace ahfl::ir
