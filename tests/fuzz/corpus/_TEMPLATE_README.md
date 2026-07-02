# Crash batch <YYYY-MM-DD>

（复制本文件到 `tests/fuzz/corpus/<YYYYMMDD>/README.md` 并按实填充；字段说明见 `docs/reference/fuzz-corpus-location.zh.md §6`）

| Field | Value |
|---|---|
| **Date (UTC)** | YYYY-MM-DD |
| **Source** | cron-? / local-run / Slack attachment / user-reported |
| **Discovered by** | @github-handle |
| **libFuzzer / Sanitizer versions** | e.g. clang 18.1.8, ubuntu:24.04 |
| **AHFL commit** | `<sha>` |
| **Total samples** | N |
| **Status** | Open / Triaged / Partially-Fixed / Closed |

## 1. 触发方式

（一行说明）

## 2. 样本列表

| # | File | Target | Kind (crash/leak/timeout/oom) | 初步现象（一句话） | Issue / PR |
|---|---|---|---|---|---|

## 3. Root cause 与修复进度

## 4. 复现结果（随每个修复 PR 回填）

（运行 `./tests/fuzz/corpus/<YYYYMMDD>/repro.sh build-fuzz` 输出摘要表）
