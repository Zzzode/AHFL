# AHFL Migration Policy V0.1

AHFL 仍处于未成熟阶段，不维护前向兼容。架构、命名、artifact schema、CLI 表面和内部路径都可以按当前最佳设计 aggressive refactor。

## 当前政策

1. Breaking change 允许发生，但必须在同一次变更中同步实现、测试、golden、文档和命令参考。
2. 旧 schema / 旧 CLI / 旧 include path 不需要 shim；如果保留旧入口，只能作为明确标注的迁移临时层，并应有删除计划。
3. 文档中的 `compatibility` 历史文件只用于回溯旧版本行为，不再作为当前维护入口。
4. 现有 provider governance artifact 中的 compatibility / schema compatibility 命名表示 release gate 或 schema drift evidence，不表示对旧消费者的前向兼容承诺。
5. 每次 schema 或 artifact 格式破坏性变更都应记录影响面，并给出 build、CTest、golden 或针对性回归证据。

## 推荐做法

- 新增领域边界时优先替换旧抽象，而不是并行维护两套同义接口。
- 删除 facade、alias、wrapper 前先确认调用点，再一次性更新真实 leaf header / registry / tests。
- 对外可见 command、artifact id、JSON field 如果发生变化，应把旧输出从 golden 中清理掉，而不是同时接受新旧两套格式。
- 历史文档可以保留为 archaeology，但当前入口必须指向 `docs/spec/`、`docs/design/`、`docs/reference/` 中的最新维护文档。
