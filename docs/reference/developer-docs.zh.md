# AHFL Developer Documentation

本目录汇集面向 AHFL 编译器/验证器贡献者的开发类参考文档。

- [`error-codes.zh.md`](./error-codes.zh.md) —— 诊断错误码速查（typecheck 稳定码、触发条件、最小复现、修复建议）
- [`formal-subset.zh.md`](../design/formal-subset.zh.md) —— 已验证子语言（verified subset）边界、判定规则与演进计划（草稿占位，wave-16 g-4）
- [`incremental-cache.zh.md`](../design/incremental-cache.zh.md) —— 编译器增量缓存的 key 构成、失效策略与性能约定（草稿占位，wave-17）

## 索引约定

- 文件名使用 kebab-case 小写加连字符。
- 英文内容为主，中文术语放在括号中；涉及用户可见错误码时保留原名。
- 代码示例统一使用三引号 `ahfl` fenced block；最小复现需确保可被词法/语法解析器接受。
