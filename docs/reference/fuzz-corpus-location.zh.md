# Fuzz Crash Corpus — Location & Triage Convention

| Field | Value |
|---|---|
| **Owner** | QE / Toolchain rotation（当前 triage 值班人见 docs/plans/project-status.zh.md §4.6） |
| **Effective** | 合入本 PR 之日起生效 |
| **Related** | `tests/fuzz/README.md`（快速开始）；`.github/workflows/fuzz-cron.yml`（自动归档 cron） |

---

## 1. 为什么要有本约定

AHFL 有 3 个 libFuzzer target（§3）。当 libFuzzer 在 CI cron 或本地跑挂时，会生成形如：

```
crash-<sha1>
leak-<sha1>
timeout-<sha1>
oom-<sha1>
```

的样例文件。**在没有统一归档约定前**，这些文件通常出现在：
- 开发者个人电脑的 `~/fuzz/`（其他人看不到）
- Slack 消息附件（会过期）
- GitHub Actions artifact（保存 90 天后自动删除）

导致同一个 crash 被多人多次复现、mean-time-to-reproduce 很高。本约定解决这个问题：**所有 crash 样例与复现脚本统一归档到仓库内 `tests/fuzz/corpus/<YYYYMMDD>/` 目录**，与源码一起版本化。

---

## 2. 目录结构

```
tests/fuzz/
├── README.md                     ← 快速开始（新人先读这个）
├── CMakeLists.txt                ← 构建定义（libFuzzer / standalone 两种模式）
├── fuzz_parser.cpp               ← target 1：Frontend::parse_text
├── fuzz_typecheck.cpp            ← target 2：TypeChecker::check（全量 parse → resolve → typecheck）
├── fuzz_smv_emitter.cpp          ← target 3：SMV（NuSMV）backend IR emission
└── corpus/
    ├── 20260627/                 ← crash 发生日期（UTC）
    │   ├── README.md             ← 每批 crash 一张 triage 登记卡（必写）
    │   ├── fuzz_parser.crash-<sha1>    ← 原始 libFuzzer 输出，文件名前加 target 前缀
    │   ├── fuzz_parser.crash-<sha1>.stderr.txt  ← ASan/leak/timeout stderr 输出（如果有）
    │   ├── fuzz_typecheck.timeout-<sha1>
    │   └── repro.sh              ← 最小复现脚本（每批 1 个；见 §5）
    ├── 20260703/
    │   └── ...
    └── _TEMPLATE_README.md       ← 新批次的 README 登记卡模板（§6）
```

### 命名规则（强制执行）

1. 所有 corpus 子目录名 = UTC 日期 `YYYYMMDD`。同一日期不同来源的 crash 放同一目录，用 README 中的来源字段区分。
2. crash 原始文件：`{target_name}.{crash|leak|timeout|oom}-<sha1>`，其中 `target_name ∈ { fuzz_parser, fuzz_typecheck, fuzz_smv_emitter }`。
3. 若伴随 stderr/ASan 输出：在原始文件名后追加 `.stderr.txt`。若伴随输入 JSON 或其它元数据：追加 `.meta.json`。
4. **禁止提交大文件**：单个 crash 样例 > 1 MiB 时，只提交 SHA1 + repro.sh（里面附 artifact 下载链接），并在 README 标记 `[large-artifact]`。

---

## 3. Fuzz Targets 一览

| Target | Source | Entry point | 构建开关 | 默认种子目录 |
|---|---|---|---|---|
| `fuzz_parser` | `tests/fuzz/fuzz_parser.cpp` | `Frontend::parse_text("fuzz_input", text)` | `-DAHFL_ENABLE_FUZZING=ON` | `tests/fuzz/seeds/parser/`（空=首次运行自动创建） |
| `fuzz_typecheck` | `tests/fuzz/fuzz_typecheck.cpp` | Full `parse → resolve → typecheck` | 同上 | `tests/fuzz/seeds/typecheck/` |
| `fuzz_smv_emitter` | `tests/fuzz/fuzz_smv_emitter.cpp` | SMV IR → NuSMV-in text emission pipeline | 同上 | `tests/fuzz/seeds/smv_emitter/` |

> **Standalone 模式（`AHFL_FUZZ_STANDALONE=1`）**：不链接 libFuzzer、等价于 smoke 测试。CI `build-and-test` 工作流就跑这个模式（见 `.github/workflows/ci.yml`），确保 fuzz harness 代码本身能编译 + 对合法/非法输入不 crash。

---

## 4. 本地复现（Quick Start）

```bash
# 1. 切换到仓库根
cd <AHFL_ROOT>

# 2. 用 libFuzzer 模式配置 + 构建（= 打开 ASan + fuzzer sanitizer）
cmake --preset fuzz     # 等价于：cmake --preset dev -DAHFL_ENABLE_FUZZING=ON
cmake --build --preset build-fuzz -j8

# 3. 对某个具体 crash 文件复现：直接把 crash 文件作为唯一参数跑 target
./build-fuzz/tests/fuzz/fuzz_parser tests/fuzz/corpus/20260627/fuzz_parser.crash-abc123

# 4. 需要 ASan symbolized 输出时：
export ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer)
export ASAN_OPTIONS=symbolize=1:abort_on_error=1:halt_on_error=1
./build-fuzz/tests/fuzz/fuzz_parser ./tests/fuzz/corpus/20260627/fuzz_parser.crash-abc123 \
    2>&1 | tee ./tests/fuzz/corpus/20260627/fuzz_parser.crash-abc123.stderr.txt
```

> 若需要复现 timeout：添加 libFuzzer flag `-timeout=<N>`（秒）。推荐 `timeout=30`，与 cron 工作流一致（§7）。

---

## 5. 每批 crash 的 `repro.sh` 最小复现脚本

每一个 `corpus/<YYYYMMDD>/` 目录**必须**包含 1 个 `repro.sh`，它：

1. 是 `set -euo pipefail` 的 bash 脚本。
2. 接受一个可选参数 `$1` = 构建目录（默认 `build-fuzz`）。
3. 对目录中的每一个 crash 样例，以 1 次非 fuzzing 运行方式调用对应的 target。
4. Exit code 0 = 所有 crash 样例仍能复现；非零 = 某些 crash 已修复（可删除）。

最小骨架（可直接复制使用）：

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="${1:-$ROOT/build-fuzz}"
DATE="$(basename "$(dirname "${BASH_SOURCE[0]}")")"
CORPUS_DIR="$ROOT/tests/fuzz/corpus/$DATE"

CRASHES=(
  # 一行一个: <target_name> <crash_file_basename> <expected_exit:nonzero=still_crashes>
  "fuzz_parser     fuzz_parser.crash-abc123def456        nonzero"
  "fuzz_typecheck  fuzz_typecheck.timeout-aabbccdd        nonzero"
)

echo "CORPUS_DATE=$DATE  BUILD_DIR=$BUILD_DIR"
echo "------------------------------------------------------------"

any_fail=0
for row in "${CRASHES[@]}"; do
  read -r target file expected <<< "$row"
  bin="$BUILD_DIR/tests/fuzz/$target"
  input="$CORPUS_DIR/$file"
  if [ ! -x "$bin" ]; then
    echo "[SKIP] $target binary not built at $bin"
    continue
  fi
  echo ""
  echo "===== $file ====="
  set +e
  "$bin" "$input" 2>&1 | head -60
  rc=${PIPESTATUS[0]}
  set -e
  case "$expected" in
    nonzero) if [ "$rc" -eq 0 ]; then echo "[FIXED?] exit=0 expected crash"; any_fail=1; fi
             else echo "[CONFIRMED] rc=$rc (crashed as expected)" ;;
    zero)    if [ "$rc" -ne 0 ]; then echo "[REGRESSED] rc=$rc expected pass"; any_fail=1; fi
             else echo "[CONFIRMED] rc=0 (passed as expected)" ;;
  esac
done

echo ""
echo "============================================================"
if [ "$any_fail" -eq 0 ]; then
  echo "ALL SAMPLES IN EXPECTED STATE."
  exit 0
else
  echo "SOME SAMPLES CHANGED STATE (see above)."
  exit 1
fi
```

**注意：crash 文件列出来的顺序必须与目录中的真实文件一致**。登记新一批 crash 时请务必先填本脚本再 commit。

---

## 6. 每批 crash 的 README 登记卡模板

`tests/fuzz/corpus/_TEMPLATE_README.md`（每次新建一批时复制并改名填充）：

```markdown
# Crash batch <YYYY-MM-DD>

| Field | Value |
|---|---|
| **Date (UTC)** | YYYY-MM-DD |
| **Source** | cron-18 / local-run / Slack attachment / user-reported |
| **Discovered by** | @github-handle |
| **libFuzzer / Sanitizer versions** | `clang++ --version` + `ldd --version`（cron 自动填 ubuntu:24.04 default） |
| **AHFL commit** | `<sha>`（crash 产生时的 HEAD；cron 自动填 `$GITHUB_SHA`） |
| **Total samples** | N |
| **Status** | Open / Triaged / Partially-Fixed / Closed |

## 1. 触发方式

（例如：cron job 2026-06-27 03:00 UTC；`fuzz_parser -max_total_time=3600` 跑了 1 小时）

## 2. 样本列表

| # | File | Target | Kind (crash/leak/timeout/oom) | 初步现象（一句话） | Issue / PR |
|---|---|---|---|---|---|
| 1 | fuzz_parser.crash-abc123 | fuzz_parser | crash | ANTLR4 runtime `std::bad_any_cast` → see ASan stderr.txt | #1234 |
| 2 | fuzz_typecheck.timeout-aabb | fuzz_typecheck | timeout | >120s 处理 60KB 嵌套类型 | #1236 |

## 3. Root cause 与修复进度

- 样本 1：原因 = Frontend lexer 对孤立 surrogate 处理异常。修复 PR #1234 已合入 develop。
- 样本 2：原因 = `Type::unify()` 在深层嵌套下 O(n^2)。跟踪 issue #1236。

## 4. 复现结果（随每个修复 PR 回填）

（运行 `./corpus/YYYYMMDD/repro.sh build-fuzz` 的输出摘要）

| Sample | Before fix | After PR #1234 | After PR #1236 |
|---|---|---|---|
| 1 | crash | pass ✅ | pass ✅ |
| 2 | timeout (147s) | timeout (146s) | 0.6s ✅ |
```

---

## 7. GitHub Actions Cron 自动归档（`.github/workflows/fuzz-cron.yml`）

**调度**：每日 03:17 UTC（避过大多数团队 00:00–03:00 的 CI 拥堵窗口；与 PB-01 §2.4 的 "off-minute" 原则一致）。

**工作内容**：
1. 对 3 个 target 分别跑 `-max_total_time=1200`（每个 20 min）。
2. 如果产生了任何 crash / leak / oom / timeout 文件：
   - 以 `fuzz-artifacts-<YYYYMMDD>` 命名上传 GitHub Actions artifact。
   - 自动提交一个 **draft PR**：按 §2 命名约定把这些样例归档到 `tests/fuzz/corpus/YYYYMMDD/`，并把 §6 的模板 README（预填 Date/Source/Discovered-by=QE-bot/libFuzzer/AHFL-commit）+ §5 repro.sh（预填样本行）一起带上。PR 标题 = `fuzz: archive crash batch YYYYMMDD`。PR body 附 artifact 链接。
3. 如果没有产生 crash：正常结束，不发 PR。

详细内容见 `.github/workflows/fuzz-cron.yml` 源文件。

---

## 8. Triage 值班（谁处理每个 crash batch）

- QE 轮值表见 `docs/plans/project-status.zh.md §4.6`（每月更新一次）。
- 处理 SLA：每个 crash batch 的 draft PR 合入后，值班人需在 **3 个工作日内** 完成 §6.2 中 "初步现象 + issue 链接" 两列的填写。不需要在此 SLA 内修好，只要分类正确、挂上 issue 即可。
- 可直接在 draft PR 内 review 后改为 ready-for-review 合并；**crash 样例不做代码 review 要求**（二进制样例无法审；登记卡与 repro.sh 有填写即合入标准）。

---

## 9. Cleanup / 留存规则

- **保留 2 年**：所有 crash 样例保留 2 年（从 crash 发生之日起），2 年后若对应 issue 已 Close 且 2 个 release 都没再出现 → 删除，保留 README 登记卡做历史索引。
- **已修复确认**：若某样本 `repro.sh build-fuzz` 连续 3 次 CI 跑都返回 exit=0（= 不再复现），则 README.Section 4 记录为 `ConfirmedFixed`，可以把该样本单独移到 `tests/fuzz/corpus/_archive/<YYYYMMDD>/`（但要保留文件名让 repro.sh 还能找到；或者在 repro.sh 中移除该样本行并注明）。
