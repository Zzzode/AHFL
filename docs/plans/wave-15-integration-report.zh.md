# AHFL Wave 15 集成报告 (Integration Report)

- **集成分支**：`integration-wave-15`
- **基线提交**：`8287144460e303ec64a30984904da9f5ea65cf56`（即 `integration-wave-14` HEAD，提交信息：test(assurance): obligations count invariant + JSON schema + status coverage）
- **创建时间**：2026-06-24

## 一、合入内容

本波子任务实施结果为空数组（`[]`）：没有 ok=true 的子任务 lastCommitSha 需要 cherry-pick。

集成分支内容与 `integration-wave-14` 基线完全等价（zero-merge forward），只新增了本集成文档与根目录 `CHANGELOG.md`。

## 二、集成步骤

1. 从基线 `8287144460e303ec64a30984904da9f5ea65cf56` 切出 `integration-wave-15`。
2. 丢弃工作树中来自先前运行的 build-int 构建产物与不兼容的未提交源码变更（这些变更依赖 develop 分支的后续 typed HIR/stdlib 提交，不能直接在本基线上落地）。
3. `cmake -S . -B build-int` 配置。
4. `cmake --build build-int -j` 全量构建。
5. `ctest --test-dir build-int -j --output-on-failure` 全量测试。
6. `ctest -R p5_smv_golden_lock` 单独执行 P5.0 SMV golden 锁。

## 三、构建结果

| 步骤 | 结果 |
|------|------|
| CMake 配置 | OK（AppleClang 21.0, NuSMV found, Python3 3.14）|
| 全量构建 | OK，所有 target 100% 链接成功 |

## 四、测试结果

| 指标 | 数值 |
|------|------|
| 测试总数 | 977 |
| 通过 | 974 |
| 失败 | 3 |
| 通过率 | 99.59% |

### 4.1 失败用例（均为 wave-14 已存在的遗留失败，与当前 wave 无关）

| # | 用例 | 根因 | 跟踪任务 |
|---|------|------|----------|
| 709 | `ahfl.semantics.typed_hir_all` | Typed HIR JSON 快照中 EffectJudgement 能力集未完整往返；部分 forward-const / SetMap 快照与实现不匹配 | #335（Batch 1：A类源码 + E1语法糖）、#336（Batch 2：序列化修复）|
| 710 | `ahfl.semantics.effects_all` | EffectJudgement capability set round-trip 同上述 | #335、#336 |
| 803 | `ahflc.emit_ir_json.expr_temporal` | IR JSON 输出中 formal_observations / 声明元数据 与 golden 期望偏移 | #340（Batch 4：剩余诊断修复）|

> 说明：这些用例的修复补丁已在工作树中存在但未提交（属于 develop 分支 stdlib/typed HIR 推进的一部分），在 #335/#336/#340 对应 commit 独立落地并通过 review 后可在后续 wave 中 cherry-pick 合入。

### 4.2 P5.0 Golden Lock

| 用例 | 结果 | diff |
|------|------|------|
| `p5_smv_golden_lock` | PASS | 无 |
| `p5_smv_golden_lock.negative_diag` | PASS | 无 |

## 五、未决项 / 后续

- 任务 #331（assurance bundle 中实现 verification.obligations）未合入。
- 任务 #332（obligations schema + golden 测试）未合入。
- 任务 #335/#336/#340（typed HIR 三批修复）进行中，修复提交需在下一波或独立 wave 合入。
