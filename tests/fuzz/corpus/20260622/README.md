# Crash batch 2026-06-22

| Field | Value |
|---|---|
| **Date (UTC)** | 2026-06-22 |
| **Source** | cron-18（GitHub Actions fuzz-cron） |
| **Discovered by** | QE-bot（cron automatic） |
| **libFuzzer / Sanitizer versions** | clang 18.1.8, ubuntu:24.04（cron runner default） |
| **AHFL commit** | c0e12c8f（wave-15 基线提交） |
| **Total samples** | 1 |
| **Status** | Triaged / 已定位 root cause（Frontend ANTLR） |

## 1. 触发方式

cron job 2026-06-22 03:17 UTC；`fuzz_parser -max_total_time=1200`（20 min）跑在 ubuntu:24.04 runner。输入触发了 ANTLR C++ runtime 在 unmatched surrogate 附近的 `std::bad_any_cast`。

## 2. 样本列表

| # | File | Target | Kind (crash/leak/timeout/oom) | 初步现象（一句话） | Issue / PR |
|---|---|---|---|---|---|
| 1 | fuzz_parser.crash-a7b3c1f9e8d2 | fuzz_parser | crash | ANTLR4 runtime `std::bad_any_cast` on 孤立 surrogate 输入；ASan 栈指向 `ANTLRParseTreeWalker::walk()` → `ahfl::AHFLParserVisitor` | #1294（frontend: isolated surrogate 处理） |

## 3. Root cause 与修复进度

- 样本 1：**Frontend lexer 在 AST printer → text_of(Token) 遇到孤立 surrogate（UTF-16 half）时，字符串拼接把它转换为 std::string 的 UTF-8 过程中抛出异常，异常在 ANTLR C++ runtime 的 destructor 里析构时再次抛出**。
- 修复策略：`Frontend::parse_text` 把 lexer 初始化阶段输入的所有 bytes 走一次 UTF-8 验证；非法 UTF-8 直接在入口报错不进入 ANTLR。
- 跟踪 PR：#1294（Draft，需要进一步确认 ANTLR 版本差异）。

## 4. 复现结果（随每个修复 PR 回填）

（运行 `./tests/fuzz/corpus/20260622/repro.sh build-fuzz` 的输出摘要）

| Sample | Before fix | After PR #1294 | Notes |
|---|---|---|---|
| 1 | crash rc=134 | TBD | 需要 libFuzzer 模式；build-int 的 standalone smoke 模式不触发 |
