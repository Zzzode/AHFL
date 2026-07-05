# AHFL VS Code LSP 扩展打包与发布参考

| 项目 | 内容 |
|------|------|
| 文档类型 | reference |
| 适用范围 | `tools/vscode` VS Code extension scaffold |
| 当前状态 | 可本地打包，可由 CI 产出 platform VSIX artifact，可手动发布 Marketplace；已有 Marketplace package inventory gate、platform VSIX install smoke、hover、completion、rename、watched-files、Problems diagnostics transcript 与 diagnostics publish/recovery extension test；toolchain sysroot 通过 `workspace/configuration` 按 resource scope 拉取，workspace folder extension 序列仍需扩展验证 |

---

## 一、发布形态

`tools/vscode` 是 VS Code 客户端扩展。面向普通 VS Code 用户发布时，主产物应是 platform-specific VSIX：

1. VSIX 内包含语言客户端、语法高亮、snippet、文件关联和配置项。
2. VSIX 内包含当前平台匹配的 release `ahfl-lsp` 服务端二进制。
3. 扩展默认优先启动内置 `server/ahfl-lsp`；用户显式配置 `ahfl.serverPath` 时才使用外部 server。

不能把一个本机开发用的 `build/dev/.../ahfl-lsp` 放进跨平台 VSIX。正确做法是按平台生成 VSIX，例如：

```text
tools/vscode/dist/ahfl-language-<version>-darwin-arm64.vsix
tools/vscode/dist/ahfl-language-<version>-linux-x64.vsix
```

## 二、本地开发调试 LSP 服务端

本地调试 VS Code 扩展时可以使用 Debug LSP：

```bash
cmake --build --preset build-dev --target ahfl-lsp
```

开发构建的默认路径是：

```text
build/dev/src/tooling/lsp/ahfl-lsp
```

本地调试扩展时，可在 VS Code 设置中配置：

```json
{
  "ahfl.serverPath": "<repo>/build/dev/src/tooling/lsp/ahfl-lsp"
}
```

该路径只适合本机开发，不应用作正式分发或公开发布的 server artifact。

## 三、本地运行扩展

在仓库根目录先确保 LSP 二进制可用，然后运行：

```bash
corepack enable
corepack prepare pnpm@10.10.0 --activate
cd tools/vscode
pnpm install --frozen-lockfile --ignore-scripts
pnpm run compile
code --extensionDevelopmentPath="$(pwd)" /Users/bytedance/Develop/AHFL
```

打开任意 `.ahfl` 文件后，扩展会按 `ahfl.serverPath` 启动 language server。

### Corelib 开发 sysroot 配置

开发 AHFL 仓库自身或修改 `std/` 时，workspace 必须显式把当前 checkout 作为 sysroot：

```json
{
  "ahfl.toolchain.sysroot": "${workspaceFolder}"
}
```

该设置是 resource-scoped。扩展会在初始化时发送
`initializationOptions.ahfl.toolchain.defaultSysroot` / `bundledSysroot` /
`profiles[]`，并在
server 发起 `workspace/configuration` 请求时返回展开后的
`ahfl.toolchain.sysroot`。`${workspaceFolder}` 和相对路径由扩展按对应
workspace folder 展开，server 只接收规范化后的工具链根或空字符串。

配置为空字符串表示“没有显式 resource 配置”。这种情况下，platform VSIX
通过 `bundledSysroot` 使用随扩展打包的 `<extension>/std/ahfl.toml` 作为 fallback；
扩展不得通过 server process environment 注入 `AHFL_SYSROOT`。

### LSP workspace navigation index

Server 会在 PackageGraph 与 active sysroot profile 之上构建独立的
`LspWorkspaceIndex`。这个索引只服务 IDE 导航，不会扩大当前文件的
import 可见性，也不会让未 import 的 `std::prelude`、`std::fmt` 或
其他 exported module 符号变成可调用符号。

请求语义如下：

1. `textDocument/definition` 返回声明位置。primitive type 返回 canonical
   primitive home，不再把 `std::fmt`、`std::json` 等 impl module 混进
   definition 结果。
2. `textDocument/implementation` 返回类型或 trait 的 impl 候选。corelib
   开发和普通用户工程都通过 workspace/sysroot index 找到 exported std
   modules 中的 primitive impl。
3. `textDocument/references` 优先使用当前 semantic snapshot 的可见引用，
   并按需合并 workspace/sysroot index 中未打开文件的 resolved references。
4. `workspace/symbol` 与 CodeLens 使用 index facts；如果 snapshot 还没有
   index，CodeLens 才退回文本级 fallback。

VS Code 客户端不需要特殊命令或非标准 payload。跳转请求返回标准
`Location[]`；如果需要查看所有 impl，应使用 VS Code 的
“Go to Implementations”，而不是期待 Cmd-click 的 definition 展开 impl
列表。

### Detached single-file mode

当打开的 `.ahfl` 文件在当前 workspace folder 边界内找不到 `ahfl.toml` 时，
server 进入 `DetachedSourceUnit`。该模式不会隐式加载 `std` package，也不会因为
workspace 中存在 sibling `std/` 目录而把当前文件绑定到 sysroot package。

用户可见行为：

1. 发布 `N::detached_source_unit` information diagnostic。
2. `String`、`Bool`、`Int`、`Duration` 等 language primitive type 仍可用于
   `definition` / `typeDefinition`，目标来自 active ToolchainProfile 的
   `SysrootPrimitiveIndex`。
3. 如果 active sysroot 缺少某个 primitive canonical home，或没有可用 default
   ToolchainProfile，发布 `W::primitive_home_unavailable` warning。
4. `implementation` 不为 detached primitive 返回 std facade impl candidates。
5. completion/code action 不提供 import completion、organize imports 或自动导入
   `std::*` 的修复动作。

完整规则见 [single-file-mode.zh.md](./single-file-mode.zh.md)。

## 四、本地打包 VSIX

面向用户的 VSIX 应通过仓库根目录脚本生成：

```bash
scripts/package-vscode-vsix-release.sh
```

该脚本会：

1. 在需要时配置 `cmake --preset release`。
2. 运行 `cmake --build --preset build-release --target ahfl-lsp`。
3. 把 release `ahfl-lsp` staging 到 `tools/vscode/server/ahfl-lsp`。
4. 把 `std/ahfl.toml` 和 corelib `.ahfl` 源码 staging 到 `tools/vscode/std/`。
5. 使用当前平台对应的 VS Code target 打包 VSIX。

输出文件：

```text
tools/vscode/dist/ahfl-language-<version>-<target>.vsix
```

本机安装：

```bash
code --install-extension tools/vscode/dist/ahfl-language-<version>-<target>.vsix
```

发布包内容预检与安装 smoke：

```bash
cd tools/vscode
pnpm run test:package-inventory
pnpm run test:vsix-install
```

`test:package-inventory` 必须确认 VSIX 清单包含内置 `server/ahfl-lsp` 和
bundled `std/ahfl.toml`。缺少 bundled std 会导致未配置
`ahfl.toolchain.sysroot` 的普通用户工程无法获得默认标准库。

如果只调试客户端扩展，可以从 `tools/vscode` 生成 client-only VSIX：

```bash
cd tools/vscode
pnpm install --frozen-lockfile --ignore-scripts
pnpm run package
```

输出文件：

```text
tools/vscode/dist/ahfl-language.vsix
```

Client-only VSIX 不适合作为普通用户主安装包；它需要用户另外通过 PATH 或 `ahfl.serverPath` 提供 `ahfl-lsp`。

安装后验证：

1. 打开包含 `.ahfl` 文件的工作区。
2. 对 platform VSIX，确认扩展可自动启动内置 `server/ahfl-lsp`。
3. 在 `.ahfl` 文件中触发 diagnostics、hover、completion。Server 通过 `textDocument/diagnostic` 和 `workspace/diagnostic` full report 的 pull 模式提供诊断，并通过 `$/diagnostic/refresh` 通知客户端拉取更新。
4. 对 primitive type 触发 definition 与 implementation：definition 应跳到 primitive home，implementation 应列出 exported std modules 中的相关 impl。
5. 修改未打开的 imported `.ahfl` 文件后，确认 watched-files notification 会触发 workspace diagnostics 刷新。
6. workspace folder 切换目前只有 server handler/protocol 级证据；extension-host 序列仍作为后续验证项。
7. 如需排查 server 输出，可临时设置环境变量 `AHFL_LSP_TRACE=1` 后启动 VS Code。

## 五、Marketplace 手动发布

发布前需要：

1. Visual Studio Marketplace publisher。
2. 具有 Marketplace Manage 权限的 personal access token。
3. `tools/vscode/package.json` 中的 `publisher` 与 Marketplace publisher 一致。
4. 已通过 `scripts/package-vscode-vsix-release.sh` 生成对应平台 VSIX。
5. 已通过 `pnpm run test:package-inventory` 验证 Marketplace 发布包清单。
6. 已通过 `pnpm run test:vsix-install` 验证 platform VSIX 可安装且包含可执行 release `ahfl-lsp`。

先从仓库根目录生成 platform VSIX：

```bash
scripts/package-vscode-vsix-release.sh
```

再从 `tools/vscode` 发布刚刚预检过的 platform VSIX artifact：

```bash
cd tools/vscode
pnpm run publish:vsix -- dist/ahfl-language-<version>-<target>.vsix --pat "$VSCE_PAT"
```

如果只是预检扩展包内容，不发布 Marketplace，使用：

```bash
pnpm run test:package-inventory
```

当前本地 `vsce` 没有真正的 `publish --dry-run` 选项；该门禁使用 `vsce ls --no-dependencies`
离线验证将进入 VSIX/Marketplace 包的文件清单。

官方流程参考 Visual Studio Code 的 Publishing Extensions 文档：

```text
https://code.visualstudio.com/api/working-with-extensions/publishing-extension
```

## 六、GitHub Actions 发布流程

`.github/workflows/vscode-extension.yml` 提供三种产物/入口：

1. push / pull request：在 Linux/macOS runner 上构建 release LSP，并打包内置该 server 的 platform VSIX。
2. push / pull request：运行 Marketplace package inventory gate，确认发布清单包含 extension client、TextMate、snippet 与内置 release `ahfl-lsp`，且不包含 `src/`、`test/`、`dist/` 等开发产物。
3. push / pull request：上传 platform VSIX artifact。
4. workflow dispatch：当输入 `publish=true` 且仓库配置了 `VSCE_PAT` secret 时，发布 platform VSIX 到 Marketplace。

手动发布前必须确认：

1. `VSCE_PAT` 只配置在 GitHub secrets，不写入仓库。
2. `tools/vscode/package.json` 的 `version` 已递增。
3. `tools/vscode/CHANGELOG.md` 已更新。
4. 对应版本的 platform VSIX 已包含 release `ahfl-lsp`。
5. `pnpm run test:package-inventory` 与 `pnpm run test:vsix-install` 已通过。

## 七、验证门禁

扩展打包相关改动至少运行：

```bash
scripts/package-vscode-vsix-release.sh
```

只验证客户端编译时可运行：

```bash
cd tools/vscode
pnpm install --frozen-lockfile --ignore-scripts
pnpm run compile
```

验证 VS Code extension host 下的 hover、completion、rename、watched-files 与 diagnostics publish/recovery：

```bash
cmake --build --preset build-dev --target ahfl-lsp
cd tools/vscode
pnpm run test:extension
```

生成并校验 Problems diagnostics transcript：

```bash
cd tools/vscode
pnpm run test:problems-transcript
```

验证 Marketplace package inventory 与 platform VSIX 安装：

```bash
cd tools/vscode
pnpm run test:package-inventory
pnpm run test:vsix-install
```

仓库级变更还应运行：

```bash
git diff --check
```

如果 LSP server 或协议行为也发生变化，追加：

```bash
cmake --build --preset build-release --target ahfl-lsp
cmake --build --preset build-dev --target ahfl_tooling_lsp_json_rpc_tests ahfl_tooling_lsp_handler_tests
ctest --preset test-dev --output-on-failure -L ahfl-lsp
```
