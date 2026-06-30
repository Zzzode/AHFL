# `try?` operator (`expr?`) — implementation plan

> **Status:** Grammar + parser scaffolding landed (commit pending); AST / frontend / sema / lowering are the multi-day implementation that this plan specifies. Unblocked now that the compiler modernization refactor is committed (AHFL.g4 / ast.hpp / typecheck_expr.cpp are clean).

## What `?` does

`expr?` propagates failure by early-return:
- `e: Option<T>` in a fn returning `Option<R>`: if `e` is `None`, return `None`; else yield the inner `T`.
- `e: Result<T, E>` in a fn returning `Result<R, F>` (where `E: F`): if `Err(e)`, return `Err(e)`; else yield `T`.

It is **expression-position early-return**, so it touches grammar, AST, frontend, sema, HIR, and IR.

## Implemented this increment (scaffolding)

- `grammar/AHFL.g4` `postfixExpr`: added the `'?'` suffix arm (`primaryExpr (... | '?')*`). `?` is now a recognized postfix token.
- Parser regenerated (`scripts/regenerate-parser.sh`, ANTLR 4.13.1, sha256-locked) — `AHFLLexer.{cpp,h}` + `AHFLParser.{cpp,h}` updated.
- State: the grammar accepts `e?`; the frontend reports `unsupported postfix expression suffix` until the TryExpr AST + handler land (build is green).

## Remaining implementation (multi-day)

### 1. AST (`include/ahfl/compiler/frontend/ast.hpp`) — 4 spots
- `ExprSyntaxKind`: add `Try` after `UnwrapExpr`.
- `struct TryExpr { Owned<ExprSyntax> operand; SourceRange range; };` (mirror `IndexAccessExpr`).
- `ExprSyntax` variant: add `TryExpr`.
- Visitor: add the `Try` kind case + the `TryExpr` visit lambda (mirror `IndexAccessExpr`, ~4 lambda sites).

### 2. Frontend (`src/compiler/syntax/frontend/frontend.cpp`)
- In `build_postfix_expr`'s suffix loop (the arm before `throw std::logic_error("unsupported postfix expression suffix")`), add:
  ```
  if (token_text == "?") {
      auto try_expr = make_expr_syntax(ast::ExprSyntaxKind::Try, span_range(result->range, <qmark range>));
      std::get<ast::TryExpr>(try_expr->node).operand = std::move(result);
      result = std::move(try_expr);
      child_index += 1;  // '?' is a single terminal in the (suffix)* loop
      continue;
  }
  ```
  (Verify `child_index += 1` against ANTLR's child list for a single-token alternation; `[` advances 3 for `[ expr ]`, `?` is 1.)

### 3. AST printer + desugar (`ast_printer.cpp`, `desugar.cpp`)
- Add `TryExpr` cases (print + pass-through).

### 4. Sema — the hard part (`src/compiler/semantics/typecheck_expr.cpp`)
- Typecheck `operand` → must be `Option<T>` or `Result<T, E>`.
- The enclosing fn must return a compatible `Option<_>` / `Result<_, F>` (resolve via the current fn signature in the typechecker context).
- Result type of `e?` is `T`.
- Emit an early-return for the None/Err arm. AHFL already has early-return machinery (the fn body lowering); `?` needs to splice a branch + return into the current block. This is the crux — model it as a desugar to `match e { Some(x) => x, None => <return None> }` if the typechecker can lower `<return>` in expression context, else a dedicated TryExpr typed-HIR node.

### 5. typed HIR + IR lowering (`typed_hir*.cpp`, `typed_hir_lower.cpp`, IR emit)
- Lower TryExpr to: eval operand → branch on Some/Ok → early-return None/Err, else yield value. Reuse the existing return + match lowering.

### 6. Tests
- `tests/integration/`: `try_option.ahfl` (`fn f(o: Option<Int>) -> Option<Int> { let x = o?; Some(x+1) }`), `try_result.ahfl`, negatives (try? on non-Option, in non-Option-returning fn).
- prelude re-export + cookbook entry.

## Sequencing
AST → frontend (verifies `e?` parses to TryExpr) → ast_printer/desugar → sema (verifies typecheck) → HIR/IR lowering (verifies run) → tests. Each stage is independently verifiable; commit per stage.

## Decision context (RFC §3.2 / D3)
D3 adopted the Rust-style `expr?` postfix (workplan decision register). `try?` is forbidden in predicate / contract / flow-handler / workflow-return contexts (only fn bodies) — enforce in sema.
