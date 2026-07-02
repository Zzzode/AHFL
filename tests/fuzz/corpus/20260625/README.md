# Crash batch 2026-06-25

| Field | Value |
|---|---|
| **Date (UTC)** | 2026-06-25 |
| **Source** | cron-19 |
| **Discovered by** | QE-bot |
| **libFuzzer / Sanitizer versions** | clang 18.1.8, ubuntu:24.04 |
| **AHFL commit** | a3f8921c（wave-18 start commit 候选） |
| **Total samples** | 1 |
| **Status** | Open（timeout，性能优化未启动） |

## 1. 触发方式

cron job 2026-06-25 03:17 UTC 跑 `fuzz_typecheck -max_total_time=1200 -timeout=120`。样本在 libFuzzer 内置 120 秒超时后触发 `timeout-` 文件生成。

## 2. 样本列表

| # | File | Target | Kind | 初步现象 | Issue / PR |
|---|---|---|---|---|---|
| 1 | fuzz_typecheck.timeout-d41c6b2a9e8f | fuzz_typecheck | timeout | >147s 处理 60KB 级嵌套 Option<Option<...<Struct<T>>>> 重复单态化结构；Type::unify() 在深层嵌套下 O(n²) | #1302（type unifier 性能升级） |

## 3. Root cause 与修复进度

- 样本 1：`TypeEnvironment::normalize_type_key + unify()` 组合在深度 > 500 的嵌套单态化参数下退化为 n² 匹配。缓存策略在 Wave-19 之前未引入（Wave-19 的 Construct Hover / P4-02 都不涉及 type unifier 路径）。
- 建议方案：在 `TypeContext::make_struct` / `make_enum` 内部对 type_args 的 normalize_type_key 结果做 key cache；嵌套深度 > 64 的类型直接做 sha1 散列 + 长度限制。
- 优先级：P2（不影响 correctness，影响 fuzz CI 稳定性）。

## 4. 复现结果

| Sample | Before fix | After fix | Notes |
|---|---|---|---|
| 1 | timeout 147s | TBD | 必须开 libFuzzer 模式 + `-timeout=30` 复现（默认 `-timeout=120` 等待太长） |
