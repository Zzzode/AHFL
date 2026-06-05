#pragma once

#include "ahfl/compiler/frontend/ast.hpp"

#include <string_view>

namespace ahfl {

struct ConstExprSyntaxResult {
    bool is_const{false};
    std::string_view reason;
};

[[nodiscard]] ConstExprSyntaxResult classify_const_expr_syntax(const ast::ExprSyntax &expr);

} // namespace ahfl
