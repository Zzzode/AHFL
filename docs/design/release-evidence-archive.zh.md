# AHFL Release Evidence Archive

本文定义 AHFL release evidence archive 的仓库级边界。它不是审批系统，也不是外部发布平台；它是一个可复跑、可归档的证据生成入口，用来证明 RFC、spec、reference 与真实工具链产物之间的契约已经同步。

关联文档：

- [project-status.zh.md](../plans/project-status.zh.md)
- [rfc-follow-up-priorities.zh.md](../plans/rfc-follow-up-priorities.zh.md)
- [lsp-vscode-extension.zh.md](../reference/lsp-vscode-extension.zh.md)

## 目标

1. 提供确定性的 `ahfl.release_evidence_archive.v1` manifest。
2. 汇总 CLI、package graph、sysroot、VSIX bundled sysroot、public API snapshot/docs/diff 等 release-facing evidence。
3. 记录每个 evidence item 的覆盖 RFC、执行命令、输出 artifact、digest 和通过状态。
4. 让 `implemented` RFC 推进到 `stabilized` 时有可检查证据，而不是只依赖 RFC 文本。
5. 禁止把 client-only VSIX、legacy descriptor、implicit std discovery 或其他过渡路径作为发布证据。

## 非目标

1. 上传 release archive 到外部系统。
2. 生成组织级发布审批。
3. 包含 secret value、credential、token 或完整本机绝对路径。
4. 执行真实 provider mutation。
5. 替代 feature-specific golden tests；archive 只汇总发布证据，不取代单元/集成测试。

## Manifest Schema

顶层 schema 为 `ahfl.release_evidence_archive.v1`。

| 字段 | 类型 | 描述 |
| --- | --- | --- |
| schema | string | 固定为 `ahfl.release_evidence_archive.v1` |
| generated_at | string | UTC ISO 8601 时间戳；测试中可固定为确定值 |
| is_release_ready | bool | 所有 evidence item 是否通过 |
| total_evidence_count | int | evidence item 总数 |
| passed_evidence_count | int | 通过数 |
| failed_evidence_count | int | 失败数 |
| evidence_items | array | 排序后的 evidence item 列表 |

### Evidence Item

| 字段 | 类型 | 描述 |
| --- | --- | --- |
| id | string | 稳定 evidence identity |
| type | string | `command-output` / `repository-contract` |
| covers | array<string> | 覆盖的 RFC 或 release gate |
| status | string | `passed` / `failed` |
| summary | string | 面向 reviewer 的简短说明 |
| command | array<string> or null | 可复跑命令；使用 `${repo}`、`${ahflc}` 这类占位符避免本机路径 |
| artifacts | array | 输出 artifact 的相对路径与 digest |

## 当前证据集合

`scripts/generate-release-evidence-archive.py` 生成当前仓库级 archive，输出到指定目录：

```bash
scripts/generate-release-evidence-archive.py \
  --ahflc build/dev/src/tooling/cli/ahflc \
  --repo-root . \
  --out-dir build/dev/release-evidence
```

当前 evidence item：

| Evidence ID | 覆盖 | 证明内容 |
| --- | --- | --- |
| `rfc0005.package_graph.non_std_package` | RFC0005 | 非 std package 可解析 PackageGraph，包含 root/path/sysroot packages |
| `rfc0005.lockfile.non_std_package` | RFC0005 | v1 lockfile 固定 package identity 与 dependency edges |
| `rfc0006.user_package_with_repo_sysroot` | RFC0005, RFC0006 | 普通用户 package 可显式使用 repo source sysroot |
| `rfc0006.source_sysroot_corelib_development` | RFC0006 | active std package 可作为 source sysroot 开发，不触发 duplicate std/package 特判错误 |
| `rfc0006.vsix_bundled_sysroot_contract` | RFC0006, RFC0007 | platform VSIX 发布链路必须 staging release `ahfl-lsp`、bundled `std`，并运行 package inventory 与 install smoke |
| `rfc0007.lsp_multi_root_toolchain_profiles` | RFC0006, RFC0007 | LSP multi-root workspace toolchain profiles 通过 `profiles[]`、workspace-scoped profile merge 和 mixed-profile diagnostic contract 覆盖 |
| `rfc0009.public_api.snapshot.non_std_package` | RFC0009 | 非 std package 生成 package-relative public API JSON snapshot，且 dependency API 不泄漏 |
| `rfc0009.public_api.docs.non_std_package` | RFC0009 | public API Markdown docs 从 visibility facts 生成 |
| `rfc0009.public_api.diff.baseline` | RFC0009 | 两个 public API snapshot 可通过结构化 diff 比较 |

## Determinism

1. Archive manifest 和子 artifact 必须使用稳定 key 排序。
2. 输出 artifact 中的 repo 内路径必须使用相对路径或 `${repo}` 占位符。
3. Public API snapshot 的 `package.manifest` 与 entry `source` 必须以 package root 为基准，不能写入 `/Users/...`、CI workspace path 或临时目录。
4. Artifact digest 基于归档后的 normalized artifact 计算。
5. 测试默认 timestamp 为 `1970-01-01T00:00:00Z`；真实 release 可以传入当前 UTC 时间。

## VSIX Bundled Sysroot Gate

RFC0006 的 VSIX evidence 不能由 client-only VSIX 代替。有效发布链路必须满足：

1. `.github/workflows/vscode-extension.yml` 调用 `scripts/package-vscode-vsix-release.sh`。
2. packaging script 构建 release `ahfl-lsp`，并 staging 到 `tools/vscode/server/ahfl-lsp`。
3. packaging script staging `std/ahfl.toml` 和 `std/*.ahfl` 到 `tools/vscode/std/`。
4. `pnpm run test:package-inventory` 必须确认 VSIX 清单包含 bundled server 与 bundled std。
5. `pnpm run test:vsix-install` 必须确认 platform VSIX 可安装，且安装后仍包含可执行 bundled server。
6. VS Code extension 通过 `bundledSysroot` 初始化 toolchain，不通过 `AHFL_SYSROOT` 进程环境注入 sysroot。

## Durable Store Provider Archive

Durable store import provider 仍可以生成 domain-specific release evidence archive manifest。该 manifest 属于 provider feature 的发布证据之一；仓库级 `ahfl.release_evidence_archive.v1` 可以把它作为普通 evidence artifact 引用，但两者不是同一个 schema。

## 完成定义

功能完成后必须满足：

1. `ctest --preset test-dev -L release-evidence-archive --output-on-failure` 通过。
2. `scripts/generate-release-evidence-archive.py` 可生成 `release-evidence-archive.json`。
3. RFC0005/RFC0006/RFC0007/RFC0009 的 release-facing evidence item 均在 archive manifest 中出现。
4. `scripts/check-rfc.py` 通过，RFC 状态只能在 evidence 存在后推进。
