# RFC 后续工作优先级

审计日期：2026-07-08

本计划只记录 `docs/rfcs/` 当前 canonical RFC 的完成度和后续优先级。RFC 的规范文本仍以对应 RFC、`docs/spec/` 和 `docs/reference/` 为准；本文件用于排期、复核和避免把已完成实现、稳定化出口、未来研究项混在一起。

## 状态总览

| RFC | Registry status | 当前判断 | 后续动作 |
| --- | --- | --- | --- |
| RFC 0001: Enum Variant Payload Forms | `stabilized` | 完成。enum variant payload、constructor、pattern、if-let/e2e 覆盖已纳入稳定语义。 | 只保留回归测试和诊断码契约维护。 |
| RFC 0002: Optional Narrowing in Pattern Matching | `stabilized` | 完成。`FlowFacts`、`if let`、match-arm narrowing 与内建 Option/Result predicate narrowing 已稳定。 | 只保留回归测试。 |
| RFC 0003: Match Exhaustiveness Diagnostics | `implemented` | 核心诊断已完成；不能稳定化为完整 pattern usefulness 系统。 | P2：nested/literal/range/payload destructuring 稳定后，另开完整 usefulness matrix RFC。 |
| RFC 0004: Native gRPC Transport | `draft` | 未完成。现有 runtime gRPC 路径是 `grpc_json_transcoding`，不等价于 native gRPC/Protobuf transport；native gRPC machine gate 已禁止在 Go 决策前引入 build flag、C++ dependency wiring 或 proto service contract，并通过 `ahfl.native_grpc_decision_evidence.v1` 固化 accepted / implementing 的证据门。 | P3：先做 runtime owner decision gate、benchmark 和三平台构建评估，再决定是否进入 accepted/implementing。 |
| RFC 0005: Package Configuration System | `stabilized` | 完成。TOML manifest、workspace、PackageGraph、lockfile、sysroot std、CLI/LSP/formatter/test helper 迁移与 release evidence archive 均已落库；RFC0010 已明确 registry/publishing 不回填破坏 v1 manifest identity。 | 只保留回归测试和 release evidence 维护。 |
| RFC 0006: Corelib Development Sysroot | `stabilized` | 完成。source sysroot、corelib 开发路径、VSIX bundled sysroot、普通用户工程和 multi-root toolchain profile release evidence 均已落库。 | 只保留发布矩阵证据维护；多版本 toolchain distribution 另行 RFC。 |
| RFC 0007: LSP Workspace Navigation Index | `stabilized` | v1 完成。semantic graph 与 navigation index 分离、primitive home、impl/reference/navigation 索引、stable fingerprint、rename、CodeLens/reference/implementation v1、open overlay invalidation、process-local previous-index fact remap 和 multi-root profile isolation 均已落库。 | 只保留 UX / performance polish；public API compatibility gate 已转入 RFC0010。 |
| RFC 0008: Single-File Primitive and Std Resolution | `implemented` | 完成。detached source unit、primitive home、std dependency/import gate、primitive facade method visibility、`ahflc init --single-file` 显式 package scaffold 已落库。 | 只保留 detached/package 边界回归；不改变 RFC0008 核心语义。 |
| RFC 0009: Symbol Visibility and Public API Surface | `stabilized` | 完成。语义实现、public API artifact 工具链和 release evidence archive 均已落库；non-std package snapshot/docs/diff 基线由 `ctest -L release-evidence-archive` 覆盖。 | 只保留回归测试；registry、semver 和 publishing metadata 已转入 RFC0010。 |
| RFC 0010: Registry Publishing and SemVer Gates | `stabilized` | 完成。v1 设计边界已通过 owner review；manifest v2、registry resolver、source archive、显式 `registry resolve` lockfile CLI、public API snapshot artifact fetch、publish dry run、publish-time SemVer gate、real registry upload、package yank 和 RFC0010 release evidence 均已落库。release evidence archive 已覆盖 registry resolve、publish dry-run、real upload、yank 和 SemVer rejection。 | 只保留 local fixture registry evidence 维护；workspace-mode registry fetch 不进入 v1 产品化，后续如需做必须另行显式决策。 |
| RFC 0011: Pattern Usefulness Matrix | `draft` | 新增。RFC0003 后续 nested/literal/range/payload destructuring usefulness matrix 已拆成独立 RFC；matrix core、`TypedProgram::patterns` 首批基础设施、match consumer typed-row 迁移、if-let statement typed pattern root、if-let unreachable-else matrix diagnostic、if-let narrowing typed-pattern consumer、if-let 通用 `PatternSyntax` 语法/AST 迁移、match-arm narrowing typed-pattern consumer、LSP rendered witness arm quick fix v1、结构化 witness diagnostic payload、unreachable match arm quick fix、if-let unreachable else quick fix、redundant or-pattern branch quick fix、typed-pattern-driven pattern binding hover v1、typed-pattern-driven enum variant/struct payload field completion v1、typed-pattern-driven enum pattern payload signatureHelp v1、client-gated enum pattern payload destructuring snippets、非 Bool open literal usefulness、Int range pattern source surface v1、signed Int range bounds、bounded Int matrix infrastructure、非枚举 bounded Int interval analysis、source-level `Int(min,max)` 初始切片、大型嵌套 bounded Int product symbolic analysis、bounded Int literal singleton inference、bounded Int arithmetic range inference（含非零 `/` 和 singleton/保守 `%`）和当前 pattern diagnostic taxonomy 已落库。 | P2：后续更深 destructuring editing UX 继续消费 typed pattern HIR；range/numeric 后续集中在更深非 literal refinement propagation、完整精确 modulo interval、Float/Decimal numeric semantics 和 LSP editing polish。 |

## 优先级

### P0：Registry 健康度

状态：完成。

1. RFC 0003 已补齐 registry 强制章节结构。
2. `scripts/check-rfc.py` 已恢复通过。
3. 后续所有 RFC 变更必须继续保持 canonical file、frontmatter、章节顺序、相对链接和 Mermaid 图规则。

### P1：稳定化阻塞项

状态：完成。当前没有 canonical RFC 处于 P1 稳定化阻塞状态。

1. RFC 0009 public API artifact 工具链。
   - 状态：完成。
   - 已提供 `emit public-api` JSON snapshot，从 resolver visibility、alias、API-reachability facts 生成 public surface。
   - 已提供 `emit public-api-docs` 面向用户的 Markdown docs 输出。
   - 已提供 `emit public-api-diff`，用结构化 API identity、`SymbolId` / `AliasDefId` / signature facts 比较 public surface，不用 display string 作为 canonical identity。
   - 已由 `scripts/generate-release-evidence-archive.py` 固定 non-std package 的 snapshot/docs/diff release evidence，RFC 0009 已推进到 `stabilized`。

2. RFC 0005 / 0006 / 0007 release evidence 稳定化复核。
   - 状态：完成。
   - RFC 0005 的 v1 package identity contract 已完成；不要再补 legacy descriptor 或 std 特判。
   - RFC 0006 的 VSIX bundled sysroot、repo source-sysroot、普通用户工程三条路径已进入 release evidence archive。
   - RFC 0007 的 ordinary user package、source-sysroot、VSIX bundled sysroot 和 multi-root toolchain profile contract 已进入 release evidence archive。
   - RFC 0005 / 0006 / 0007 已推进到 `stabilized`；后续只做回归维护和 evidence 维护。
   - 后续 registry / publishing / semver 只能新增 RFC，不得回填破坏 RFC 0005 v1 manifest identity contract。

3. RFC 0010 registry / publishing / SemVer 稳定化出口。
   - 状态：完成。
   - RFC 0010 已推进到 `stabilized` / `stable-artifact`。
   - `ahfl.release_evidence_archive.v1` 当前包含 `rfc0010.registry_publish.dry_run_semver_gate`、`rfc0010.registry_publish.upload`、`rfc0010.registry_resolve.lockfile`、`rfc0010.registry_yank.local_fixture` 和 `rfc0010.registry_publish.semver_rejection`。
   - 公开 CLI reference 已覆盖 registry dependency resolution、package publish、SemVer gate 和 package yank workflow。
   - workspace-mode registry fetch 不进入 v1 产品化；后续若需要，必须另行 owner decision / RFC，不得回填 RFC 0005 / RFC 0009。

### P2：明确有价值但应等待语义成熟

1. RFC 0003 后续 pattern usefulness matrix。
   - 状态：RFC 0011 已新增；`PatternUsefulnessContext` / finite constructor matrix 第一条基础设施切片已落库并有单元测试；RFC 0003 `match_exhaustiveness` 已通过 matrix core 执行 enum coverage，并已接入 typed enum-payload lowering 覆盖 nested enum payload witness、Bool payload literal witness、struct payload 字段名 witness rendering、non-Bool open literal 默认 witness、Int range source pattern v1、signed Int range bounds、bounded Int matrix infrastructure、非枚举 bounded Int interval analysis、source-level `Int(min,max)` 初始切片、大型嵌套 bounded Int product symbolic analysis、bounded Int literal singleton inference、bounded Int arithmetic range inference（含非零 `/` 和 singleton/保守 `%`）和 redundant or-branch warning；`TypedProgram::patterns` 已记录 `match` pattern 的 typed fact flat store 并覆盖 JSON round-trip；常规 `match` exhaustiveness 已迁移为消费 typed pattern root rows；`if let` statement 已记录 typed pattern root index 并覆盖 serialization/monomorphization remap；`if let` unreachable-else warning 已通过 typed-row matrix consumer 发出；`if let` flow narrowing、payload binding introduction 和 match-arm flow narrowing 已改为消费 typed pattern root；`if let` 源语法、AST、formatter、semantic tokens、IR lowering 和 typechecker 已迁移到通用 `PatternSyntax`，旧 `IfLetPatternSyntax` 路径已删除；当前 pattern diagnostic taxonomy 已与 shipped `typecheck.MATCH_*` / `typecheck.UNREACHABLE_IF_LET_ELSE` / `typecheck.INVALID_RANGE_PATTERN` code 对齐；LSP 已能优先消费 `Diagnostic.data["missing_witnesses"]` 插入 rendered missing witness arms，并在 witness 不可安全提取或旧诊断无结构化 payload 时回退 rendered-message 解析 / wildcard arm；`MATCH_UNREACHABLE_ARM` 已提供 source-safe arm 删除 quick fix，覆盖单行 arm 和多行 struct payload destructuring arm；`UNREACHABLE_IF_LET_ELSE` 已提供 source-safe 删除不可达 else branch quick fix；`MATCH_REDUNDANT_PATTERN` 已提供 source-safe 删除 redundant or-pattern branch quick fix；pattern binding 声明位点 hover 已通过 `TypedProgram::patterns` 展示 match / if-let binding 名称和类型；pattern completion 已通过 `TypedProgram::patterns` 的 `matched_type` 在 match / if-let / nested payload pattern 内只返回当前 scrutinee enum 的 variant，并在 struct variant payload braces 内返回尚未出现的 payload field；client 支持 snippet 时，pattern completion 已能返回 tuple/struct enum payload destructuring snippet；pattern payload signatureHelp 已通过 typed variant pattern facts 展示 tuple/struct enum payload 签名和 active parameter。
   - 仍需等 struct payload destructuring、更深非 literal refinement propagation、完整精确 modulo interval、Float/Decimal numeric semantics 和未来 pattern binding UX 的语言语义稳定后进入 accepted / full implementing。
   - RFC 0011 以完整 usefulness matrix 为目标，避免在 RFC 0003 内提前冻结半成熟 pattern 域。
   - 剩余核心工作：future 更深 completion / code-action editing UX 继续消费 typed pattern HIR；Int range、`Int(min,max)` 初始 surface、bounded Int literal singleton inference 和 bounded Int arithmetic range inference 已落库，后续不要扩展成字符串解析捷径，而应继续沿用 AST / TypedPattern / IR 一等 numeric range/bounds fact。

2. RFC 0007 二期 LSP index。
   - incremental workspace index 已补一层：watched file invalidation 和 open-document overlay revision key 均按实际引用 source path 收敛，避免无关打开文件触发当前 package snapshot/index rebuild。
   - fact-level incremental remap 已落地第一版：`LspWorkspaceIndexInput::previous_index` 允许 snapshot / workspace-root / sysroot index 在同一 scope 的 overlay revision 变化后，把未变 `SourceUnitId` 的 symbol/reference/impl facts 通过 `SymbolFact::fingerprint` remap 到当前 `DefId` flat store 后复用；handler tests 覆盖只改一个 source 时未变 source 的 fact reuse 和 remapped reference/impl 查询。
   - stable DefId fingerprint 已落库：`SymbolFact::fingerprint` 提供 numeric drift-detection identity，handler tests 覆盖重建稳定性和 previous-index remap。
   - rename v1 已落库：`prepareRename` / `textDocument/rename` 返回 `WorkspaceEdit`，并拒绝 keyword 与 same-module conflict；跨 package API compatibility guard 不应硬塞进 rename handler，后续由 RFC 0010 的 public API diff / registry publish gate 承接。
   - CodeLens/reference/implementation v1 已落库：覆盖 unopened project source、lazy sysroot、path dependency exports、open overlay、partial facts、stable ordering 和 primitive impl candidates；后续只保留 UX polish。
   - 不进入 RFC 0007 v1 的范围：磁盘持久索引、remote index server、跨 checkout cache、后台 daemon 共享索引和跨 public API 的 rename 兼容性 gate。
   - 继续坚持 semantic source graph 不被导航需求污染。

### P3：需要决策或产品化触发

1. RFC 0004 native gRPC transport。
   - 状态：decision gate 已落到 `docs/plans/native-grpc-decision-gate.zh.md` 和 `docs/plans/native-grpc-decision-evidence.json`，并由 `scripts/check-native-grpc-gate.py` / `ahfl.runtime.native_grpc_gate` / CI rfc-check job 机器执行。
   - 先完成 owner decision gate、benchmark、三平台构建和 feature flag 策略。
   - 当前 `GrpcJsonTranscoding*` 实现和 runtime capability binding tests 只能证明 JSON transcoding path 成熟，不能算 native gRPC/Protobuf RFC 完成度；RFC0004 为 `draft` 时，仓库禁止引入 native gRPC build flag、C++ gRPC/Protobuf dependency wiring 和 native proto service contract。
   - 没有 Go 决策前不要进入大规模实现。

2. RFC 0008 单文件模式产品化。
   - 状态：完成。
   - 已落库 `ahflc init --single-file <input.ahfl>`：新文件生成 starter module，已有裸文件补 module header，已有 module 文件沿用 module prefix/export path，已有 manifest 拒绝覆盖。
   - 这是开发体验增强，不改变 RFC 0008 的核心语义完成度：detached mode 仍不隐式 import std 或 workspace graph。

3. RFC 0010 registry / publishing / SemVer。
   - 状态：完成。RFC 已进入 `stabilized`；owner review 已确认 manifest v2、SemVer-gated publishing、digest-separated registry identity 和显式 registry resolution 是 v1 边界；workspace-mode registry fetch 不进入 v1 产品化，LSP/workspace analysis 保持默认 network-free。
   - 已落库：`emit public-api-diff --semver-gate --from <old> --to <new>` 用结构化 public API diff facts 执行 SemVer gate；`manifest_version = 2` 已接受 registry dependency 语法，并在未解析 registry package 时 fail-closed；`ahfl.registry.index.v1` metadata parser/printer 已落库并对未知字段、非法 digest、本地 dependency 泄漏 fail-closed；registry candidate selection 已按 exact/caret/tilde requirement 选择最高非 yanked 版本，并允许 locked yanked 版本复现；`ahfl.source_archive.v1` normalized source archive builder/parser 已落库，覆盖 source-only 过滤、换行归一化、root manifest requirement、digest metadata 和 fail-closed manifest validation；remote registry artifact fetch 已落库，可拉取 package index metadata、registry index、source archive manifest 和 source archive payload，并且只在 transport unavailable 时回退缓存；registry public API snapshot artifact fetch 已落库，会校验 `public_api_sha256`、`ahfl.public_api.v1` schema，并且 live invalid response 不回退缓存；`ahflc package archive --manifest <ahfl.toml> --out <dir>` 已落库，能生成 source archive metadata/payload artifacts；verified source archive payload materialization 已落库，payload/archive/file digest drift、重复路径、路径逃逸和非空输出目录均 fail closed；archive-backed registry `PackageInput` construction 已落库，会在生成 PackageGraph input 前校验 registry metadata、source archive digest、manifest digest 和 dependency metadata；manifest-mode CLI PackageGraph registry dependency resolution 已落库，会把 root/transitive registry dependencies 拉取、校验、materialize 成 resolver-provided registry packages；`ahflc registry resolve --manifest <ahfl.toml> --lockfile <ahfl.lock>` 已落库，会通过同一 resolver 写出 registry package/edge digest metadata；PackageGraph 已能消费 resolver 提供的 registry package，校验 registry id / exact/caret/tilde version requirement，并把 source archive、manifest、public API digest 写入 package graph JSON 和 lockfile drift 检查；`ahflc package publish --dry-run --manifest <ahfl.toml> --registry <id> --out <dir>` 已落库，能本地生成 source archive、public API snapshot、registry index metadata 和 `ahfl.publish_dry_run.v1` evidence；`package publish --dry-run --semver-gate --from <previous-version>` 已落库，会从 registry metadata 拉取 previous public API snapshot，执行 publish-time SemVer gate，并在成功时记录 previous snapshot evidence；`ahflc package publish --manifest <ahfl.toml> --registry <id> --out <dir>` 已落库，会在本地 gate 通过后上传 `ahfl.registry.publish_request.v1` 并写出 `ahfl.publish.v1` evidence；`ahflc package yank <package>@<version> --registry <id>` 已落库，会校验 registry 返回同 coordinate 的 `yanked=true` metadata；release evidence archive 已用本地 fixture registry 覆盖 dry-run SemVer pass、real upload、yank 和 SemVer rejection。
   - 后续不能继续把 registry source、version range、publishing metadata 或 public API compatibility gate 回填到 RFC 0005 / RFC 0009。
   - 后续只维护 local fixture registry evidence；不再把 RFC0010 作为 P1 阻塞项。

## 执行原则

1. 禁止恢复 legacy descriptor、implicit std discovery 或 exported-module-equals-public-symbol 这类旧路径。
2. 新的 public API、LSP、package graph 工作必须使用结构化 ID 和 flat-store facts；字符串只用于 source spelling、diagnostic 和展示。
3. `implemented` 到 `stabilized` 必须有 spec/reference/release evidence，不以“代码大体能跑”替代稳定化。
4. 后续 RFC 如果只是产品化入口或 registry/publishing 扩展，不应回填破坏 RFC 0005 v1 manifest identity contract。
