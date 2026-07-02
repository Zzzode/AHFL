# tests/fuzz — Fuzzing Suite

AHFL 仓库的 **libFuzzer** 测试目录。对 fuzzing 本身不熟悉的同学先读 [Google libFuzzer Tutorial](https://llvm.org/docs/LibFuzzer.html)。

---

## 目录速查

```
tests/fuzz/
├── CMakeLists.txt         Build 定义（两种模式切换，见下）
├── fuzz_parser.cpp        Frontend parse 入口
├── fuzz_typecheck.cpp     Parse → resolve → typecheck 全链路
├── fuzz_smv_emitter.cpp   SMV/NuSMV 后端 IR emission
├── corpus/                归档的历史 crash 样例（详见 corpus-location.md）
└── seeds/                 可选：各 target 的初始语料（可空）
```

**相关文档**：
- **Triage / 归档约定（必读）** → [`docs/reference/fuzz-corpus-location.zh.md`](../../docs/reference/fuzz-corpus-location.zh.md)
- **自动化 CI cron（每早 11:17 北京时）** → [`.github/workflows/fuzz-cron.yml`](../../.github/workflows/fuzz-cron.yml)
- **本地 build preset** → `CMakePresets.json` 中的 `fuzz` / `build-fuzz`（如果存在）

---

## 两种构建模式

| Mode | CMake 开关 | 什么时候用 | 产出 |
|---|---|---|---|
| **Standalone smoke**（默认） | `AHFL_ENABLE_FUZZING=OFF` | 常规 CI（`ci.yml`）里确保 fuzz harness 本身能编、能跑、不 crash。 | `fuzz_*_check` 可执行文件（普通 main()，每个输入跑 5 条固定 smoke case） |
| **libFuzzer** | `AHFL_ENABLE_FUZZING=ON` | 本地调试 / cron 归档真跑 fuzz。 | `fuzz_parser` / `fuzz_typecheck` / `fuzz_smv_emitter`（链接 `-fsanitize=fuzzer,address`，`LLVMFuzzerTestOneInput` 入口） |

Standalone 模式是 CI 的默认配置——不要奇怪 `ctest --preset test-dev` 里看到 "`ahfl.fuzz.parser_check ... PASS`"。那不是真跑 fuzz，只是 smoke 验证 harness 没坏。

---

## 本地跑 libFuzzer（最短步骤）

```bash
cd <AHFL_ROOT>

# 1. 配置 + 构建（需要 clang，gcc 不支持 fuzzer sanitizer）
cmake --preset fuzz                       # = cmake --preset dev -DAHFL_ENABLE_FUZZING=ON
cmake --build --preset build-fuzz -j8

# 2. 先跑 30 秒看看
mkdir -p /tmp/fuzz_corpus_parser
./build-fuzz/tests/fuzz/fuzz_parser /tmp/fuzz_corpus_parser \
  -max_total_time=30 -timeout=30 -rss_limit_mb=4096

# 3. 如果有 crash/leak/timeout/oom，
#    → 按 docs/reference/fuzz-corpus-location.zh.md §2 把样例放进 tests/fuzz/corpus/YYYYMMDD/
#    → 填 repro.sh 和 README.md
#    → 开 PR（title: "fuzz: archive crash batch YYYYMMDD"）
```

跑全链路 typecheck 和 SMV emitter 同理：把 `fuzz_parser` 换成 `fuzz_typecheck` 或 `fuzz_smv_emitter`。

---

## 复现历史 crash（最快路径）

```bash
cd <AHFL_ROOT>
cmake --preset fuzz && cmake --build --preset build-fuzz -j8
# 举例：复现 2026-06-27 的批次
./tests/fuzz/corpus/20260627/repro.sh
```

`repro.sh` 输出每个样本：
- `[CONFIRMED] rc=<非 0> (crashed as expected)` → bug 还在，符合预期
- `[FIXED?] exit=0 expected crash` → 可能已经被某个 PR 顺便修了，去登记卡 §4 打勾
- `[SKIP] ... binary not built` → 你没开 `-DAHFL_ENABLE_FUZZING=ON`，或 build preset 走错了

---

## 常见问题 FAQ

**Q1: `cmake --preset fuzz` 报错找不到 preset？**
→ `CMakePresets.json` 若未配置 `fuzz` preset，改用：
   ```bash
   cmake -S . -B build-fuzz -G Ninja \
     -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
     -DCMAKE_BUILD_TYPE=RelWithDebInfo \
     -DAHFL_ENABLE_FUZZING=ON
   cmake --build build-fuzz -j8
   ```

**Q2: 编译到 fuzz target 报 `unrecognized option '-fsanitize=fuzzer'`？**
→ 用了 gcc。确认 `CMAKE_CXX_COMPILER=clang++`；没有 clang 的话 `sudo apt-get install clang`（ubuntu）或 `xcode-select --install` / `brew install llvm`（macos）。

**Q3: macOS 上 libFuzzer 跑不动？**
→ 建议 fuzz 一律在 Linux / 容器里跑。macOS 14+ 的 sanitizer 与系统库符号有偶发冲突，不值得排查。日常本地跑 standalone 模式就够；真要 fuzz → 走 GitHub cron。

**Q4: Crash 复现不了（跑 standalone smoke 不崩，但 fuzz 模式崩）？**
→ standalone 模式只跑 5 条固定 case（`AHFL_FUZZ_STANDALONE=1` 的 main）。fuzz 模式跑完整 `LLVMFuzzerTestOneInput`，且启用了 ASan/UBSan。若还是不崩，检查 `repro.sh` 里的二进制路径是否是 `build-fuzz/`（不是 `build-int/`）。

**Q5: `artifact_prefix` 指向了哪？**
→ cron workflow 里用 `fuzz-output/<target>/`；本地跑若没指定则写在当前工作目录。推荐始终显式指定 `-artifact_prefix=/tmp/fuzz_out/` 避免污染仓库。

**Q6: 新增一个 fuzz target 怎么加？**
1. 在本目录新建 `fuzz_<newthing>.cpp`，仿照 `fuzz_parser.cpp` 写 `LLVMFuzzerTestOneInput` + 对应的 `AHFL_FUZZ_STANDALONE` main。
2. 在 `tests/fuzz/CMakeLists.txt` 里依样画葫芦加两条（libFuzzer + standalone）。
3. 在 `.github/workflows/fuzz-cron.yml` 的 `jobs.run-fuzz.strategy.matrix.target` 里追加 `<newthing>`。
4. 按 §2 命名约定，在 `docs/reference/fuzz-corpus-location.zh.md §3 Targets 一览` 里追加一行。

---

**Last updated**：与 `docs/reference/fuzz-corpus-location.zh.md` 同版本（每次新增 target / 新增归档字段时同步更新）。
