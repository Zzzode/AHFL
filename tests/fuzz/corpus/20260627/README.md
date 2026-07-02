# Crash batch 2026-06-27

| Field | Value |
|---|---|
| **Date (UTC)** | 2026-06-27 |
| **Source** | cron-20 |
| **Discovered by** | QE-bot + 本地复现（@zode） |
| **libFuzzer / Sanitizer versions** | clang 18.1.8, ubuntu:24.04 + macos 14 (本地复现无 sanitizer 也 OOM) |
| **AHFL commit** | 0ac88cbd (wave-18 top commit 候选) |
| **Total samples** | 1 |
| **Status** | Open（NuSMV backend 升级候选） |

## 1. 触发方式

cron 2026-06-27 03:17 UTC；`fuzz_smv_emitter -max_total_time=1200 -rss_limit_mb=4096`。在 RSS 超过 4 GB 时触发 OOM kill。

## 2. 样本列表

| # | File | Target | Kind | 初步现象 | Issue / PR |
|---|---|---|---|---|---|
| 1 | fuzz_smv_emitter.oom-c3f9e1a7b5d2 | fuzz_smv_emitter | oom | 构造了 128 层嵌套的 `agent → transition → flow → temporal → agent` 循环引用；NuSMV SMV 文本产出时 2^N 展开最终 RSS 达到 13.4 GB（进程被 rss_limit 杀掉） | #1306（NuSMV 后端 O(n^depth) 状态爆炸处置） |

## 3. Root cause 与修复进度

- 样本 1：SMVSpec 构造把每个 agent 的每个 transition × temporal clause 做笛卡尔乘积（`MODULE` 间的 `ASSIGN` 链式）；当 flow 声明了 128 个状态 × 每个状态有 4 种 transition × 2 种 temporal clause = 2^N 的组合爆炸。
- 建议方案：
  1. BMC CLI `--bmc-depth`（Wave-19 Lane 2 已交付 C1）强制限制展开深度；
  2. SMV emitter 在 depth > 64 时 emit early warning + abort；
  3. 未来 LTS（labeled transition system）模式代替 naive state enumeration。
- PR 状态：Draft（Wave-20 C1 BMC CLI depth + 限制已部分缓解）。

## 4. 复现结果

| Sample | Default | After BMC depth=32 cap | Notes |
|---|---|---|---|
| 1 | OOM (13.4GB, killed by rss_limit) | PASS / < 256 MB | Wave-19 的 --bmc-depth 默认值已设为 64，本 batch 样本在 depth=32 下 21 秒内完成无 OOM |
