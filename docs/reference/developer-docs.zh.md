# AHFL Developer Documentation

本目录汇集面向 AHFL 编译器/验证器贡献者的开发类参考文档。

- [`error-codes.zh.md`](./error-codes.zh.md) —— 诊断错误码速查（typecheck 稳定码、触发条件、最小复现、修复建议）
- [`formal-subset.zh.md`](../design/formal-subset.zh.md) —— 已验证子语言（verified subset）边界、判定规则与演进计划（草稿占位，wave-16 g-4）
- [`incremental-cache.zh.md`](../design/incremental-cache.zh.md) —— 编译器增量缓存的 key 构成、失效策略与性能约定（草稿占位，wave-17）
- [`lsp-vscode-extension.zh.md`](./lsp-vscode-extension.zh.md) —— VS Code extension、bundled sysroot、LSP workspace navigation index 与发布验证门禁

## LSP workspace navigation index

LSP 的导航层遵循 [RFC 0007](../rfcs/0007-lsp-workspace-navigation-index.zh.md)：

1. `SemanticSnapshot` 仍只表达当前文件、显式 import 闭包和 typechecker 必需的依赖。
2. `LspWorkspaceIndex` 表达 IDE 导航事实：workspace exports、active sysroot exports、open document overlay、声明、引用、impl 和 primitive home。
3. Index facts 可以服务 `definition`、`typeDefinition`、`implementation`、`references`、workspace symbol 和 CodeLens，但不得写回 resolver scope，也不得改变 diagnostics 的可见性。
4. canonical identity 使用 `SourceUnitId`、`DefId`、`WorkspaceImplId`、`ReferenceFactId`、`PrimitiveKind` 和 `TypeKey`；字符串只用于显示、诊断和排序兜底。

修改 `src/tooling/lsp/workspace_index.*`、`analysis_service.*` 或
`server.cpp` 的导航行为时，至少运行：

```bash
cmake --build --preset build-dev --target ahfl-lsp ahfl_tooling_lsp_handler_tests
ctest --preset test-dev --output-on-failure -L v0.58-lsp
python3 scripts/check-rfc.py
```

## 索引约定

- 文件名使用 kebab-case 小写加连字符。
- 英文内容为主，中文术语放在括号中；涉及用户可见错误码时保留原名。
- 代码示例统一使用三引号 `ahfl` fenced block；最小复现需确保可被词法/语法解析器接受。
