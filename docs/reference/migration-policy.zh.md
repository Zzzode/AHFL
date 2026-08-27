# AHFL Migration Policy

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

## Release Migration Notes

### RFC 0012 — 结构化 workflow 执行 UX(`ahflc run` 输出 + runtime result 契约)

RFC 0012 是一次破坏性 CLI 输出与 runtime result 契约变更(实现见 breaking commit
`7c3ae2d2` 及后续切片)。不保留 forward-compatibility wrapper 或旧文本输出模式。

**影响面:**

1. 解析旧 `ahflc run` stdout 的 shell / Python / CTest smoke —— 旧英文标题
   (`=== AHFL Workflow Execution ===`、`Node Results`、`Token Budget Events`)已删除。
2. 直接读取 `WorkflowResult.execution_order` / string node name / string target 的
   runtime 消费者 —— 已改为 ID-based `ExecutionReport` + flat event store。
3. 使用 `--llm-observability <path>` 的调用者 —— 该 flag 已删除。
4. 依赖 `--input "$(...)"` shell substitution 的 example / 文档。

**迁移步骤:**

1. 人类调用者删除多余参数,改由 `ahfl.toml` 的 `[run]` / named profile 提供默认值;
   配置完整的工程可直接 `ahflc run`。
2. 文件输入改用 `[run].input` 或 `--input-file`。
3. 自动化改用 `--output-format json` 解析 `ahfl.run-report.v1`;流式观察改用
   `--output-format jsonl` 按 event/node/invocation ID 关联;provider audit 改用
   `--output-format jsonl --verbosity trace`。
4. runtime 消费者改用 ID-based `ExecutionReport` 与 flat event store。

**BREAKING CHANGE:** `ahflc run` now emits the structured human presentation by
default, removes `--llm-observability`, and replaces string-based `WorkflowResult`
fields with ID-based execution events and reports. Use `--output-format json` or
`jsonl` for automation and configure input/LLM defaults under `[run]` in `ahfl.toml`.
