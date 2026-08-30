#pragma once

#include "ahfl/compiler/semantics/types.hpp"

#include <cstdint>
#include <vector>

namespace ahfl {

inline constexpr std::uint32_t kInvalidAdjustmentNode = UINT32_MAX;

// RFC 0026 P4: the canonical, compositional adjustment witness produced by
// Sema for an accepted annotated-let boundary. This lives in a small shared
// header because both the relation solver (producer) and Typed HIR (owner)
// consume the same data model; neither side re-classifies a successful
// relation from its endpoint types.
//
// A node owns a full (source, target) pair. Its ops name orthogonal dimensions,
// while every unnamed dimension must already be equal. For example,
// List<Int(0,0)>(4) -> List<Int>(8) is one node containing CapacityWiden and
// TypeArg{0}; that TypeArg points to an IntWiden child. No intermediate type is
// invented. TypeArg carries no variance field: its child direction was chosen
// from the declaration variance SSOT when the witness was constructed.
enum class TypedAdjustmentOpKind {
    IntWiden,
    StringWiden,
    CapacityWiden,
    TypeArg,
    FnParam,
    FnReturn,
    VariantToEnum,
    ToAny,
    FromNever,
};

struct TypedAdjustmentOp {
    TypedAdjustmentOpKind kind{TypedAdjustmentOpKind::IntWiden};
    std::uint32_t arg_index{0};
    std::uint32_t child{kInvalidAdjustmentNode};
};

struct TypedAdjustmentNode {
    TypePtr source{nullptr};
    TypePtr target{nullptr};
    std::vector<TypedAdjustmentOp> ops;
};

struct TypedAdjustmentPlan {
    TypePtr source{nullptr};
    TypePtr target{nullptr};
    std::vector<TypedAdjustmentNode> nodes;
    std::uint32_t root{0};
};

} // namespace ahfl
