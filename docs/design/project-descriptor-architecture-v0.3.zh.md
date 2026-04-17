# AHFL Core V0.3 Project Descriptor Architecture

本文冻结 AHFL Core V0.3 在 `manifest` / `workspace` 方向的最小 project model，作为后续 frontend、CLI 和 project-aware regression 的统一设计约束。

关联文档：

- [source-graph-v0.2.zh.md](./source-graph-v0.2.zh.md)
- [module-loading-v0.2.zh.md](./module-loading-v0.2.zh.md)
- [module-resolution-rules-v0.2.zh.md](./module-resolution-rules-v0.2.zh.md)
- [cli-pipeline-architecture-v0.2.zh.md](./cli-pipeline-architecture-v0.2.zh.md)
- [roadmap-v0.3.zh.md](../plan/roadmap-v0.3.zh.md)

适用范围：

- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/frontend.cpp`
- `src/frontend/project.cpp`
- `src/cli/ahflc.cpp`
- `docs/reference/project-usage-v0.3.zh.md`
- `docs/reference/cli-commands-v0.3.zh.md`

## 目标

1. 为 V0.3 冻结最小可用的 project descriptor 模型。
2. 明确 `manifest` / `workspace` 与现有 `ProjectInput` / `SourceGraph` 的关系。
3. 明确哪些职责属于 CLI，哪些属于 frontend loader，哪些不应进入 resolver/typecheck/validate。

## 非目标

本文不定义：

1. package registry 或 remote import
2. 多版本依赖解析
3. build graph / 增量编译缓存
4. runtime deployment / Native packaging
5. 复杂 profile、feature flag 或 target matrix

## 设计立场

V0.3 引入 `manifest` / `workspace` 的目的不是替换 V0.2 的 module loading 规则，而是把当前隐含在命令行里的项目输入显式化。

换句话说：

```text
manifest / workspace
  -> ProjectInput
  -> source graph loading
  -> resolve / typecheck / validate / IR / backend
```

其中：

1. `manifest` / `workspace` 负责声明项目输入。
2. frontend loader 继续负责按 `search root + module path` 规则构建 source graph。
3. resolver 之后的阶段不感知 `manifest` 文件本身。

## 术语

### Project Manifest

描述一个 AHFL project 的声明式输入文件。

它表达：

- project 名称
- entry source 集
- search root 集

它不表达：

- module -> file 的覆盖映射
- import alias 规则
- runtime 部署信息

### Workspace Manifest

描述一组 AHFL projects 的聚合文件。

它表达：

- workspace 名称
- project manifest 列表

它不表达：

- 跨 project 重新导出的模块系统
- 独立于 project manifest 的 module lookup 规则

### Project Descriptor

对 `project manifest` 与 `workspace manifest` 的统称。

### Descriptor Root

descriptor 文件所在目录。V0.3 中，descriptor 内的相对路径都相对于它解析。

## 为什么需要这一层

V0.2 的 project-aware 用法已经可用，但仍有三个问题：

1. `--search-root` 和 entry source 只存在于命令行，难以复现和共享。
2. 多入口 project 的编译意图没有稳定声明位置。
3. CLI 当前通过参数组合隐式进入 project-aware 模式，不利于后续继续扩展。

因此 V0.3 的最小目标不是“做复杂项目系统”，而是先把 project 输入模型显式化。

## V0.3 的最小文件模型

### 1. 单 Project

V0.3 约定单 project descriptor 文件名为：

- `ahfl.project.json`

最小字段：

```json
{
  "format_version": "ahfl.project.v0.3",
  "name": "refund-audit",
  "search_roots": ["."],
  "entry_sources": ["app/main.ahfl"]
}
```

字段含义：

1. `format_version`
   - descriptor 格式版本，不等于语言版本或 IR 版本。
2. `name`
   - 人类可读的 project 名称，不参与 canonical naming。
3. `search_roots`
   - 供 loader 使用的根目录列表；相对路径相对于 descriptor root。
4. `entry_sources`
   - 入口源码路径列表；相对路径相对于 descriptor root。

最小约束：

1. `search_roots` 不能为空。
2. `entry_sources` 不能为空。
3. 相对路径不得逃逸 descriptor root 的规范化边界。
4. `search_roots` 和 `entry_sources` 在规范化后不得重复。

### 2. Workspace

V0.3 约定 workspace descriptor 文件名为：

- `ahfl.workspace.json`

最小字段：

```json
{
  "format_version": "ahfl.workspace.v0.3",
  "name": "examples",
  "projects": [
    "examples/refund/ahfl.project.json",
    "examples/review/ahfl.project.json"
  ]
}
```

字段含义：

1. `format_version`
   - workspace descriptor 格式版本。
2. `name`
   - workspace 名称，仅用于展示和诊断。
3. `projects`
   - project manifest 路径列表；相对路径相对于 workspace descriptor root。

最小约束：

1. `projects` 不能为空。
2. 每个 project path 必须显式指向一个 project manifest 文件。
3. workspace 不直接声明 search root、entry source 或 module lookup 规则。

## 为什么采用两层而不是一层

V0.3 仍然保留 `project` 和 `workspace` 的区分，原因是：

1. `project` 是 frontend loader 真正需要解析成 `ProjectInput` 的对象。
2. `workspace` 只是多个 projects 的聚合与选择入口。
3. 若把两者混成一个文件，很容易让 workspace 开始偷偷承载 module lookup 或 dependency semantics。

因此：

1. `project manifest` 是编译输入对象。
2. `workspace manifest` 是编排对象，不是编译对象。

## 与现有 SourceGraph 的关系

V0.3 不推翻 V0.2 的 `SourceGraph` / `ProjectInput` 模型，只在其上增加更高一层输入描述。

推荐转换链路：

```text
project manifest
  -> ProjectInput { entry_files, search_roots }
  -> parse_project(ProjectInput)
  -> ProjectParseResult { graph, diagnostics }
```

其中：

1. `project manifest` 不直接持有 `SourceGraph`。
2. `SourceGraph` 不反向引用 manifest AST 或原始 JSON 文本。
3. workspace 也不直接生成 `SourceGraph`；它先选择 project，再下沉到 `ProjectInput`。

## 与 module loading 规则的关系

V0.3 明确保持：

1. module -> path 规则仍然由 [module-loading-v0.2.zh.md](./module-loading-v0.2.zh.md) 定义。
2. `manifest` 只提供 search roots 和 entry sources，不提供“某模块强制映射到某路径”的覆盖表。
3. resolver 继续只消费 source graph，不消费 descriptor 文件。

因此 V0.3 不允许：

1. 在 manifest 中声明自定义 module 映射表
2. 在 workspace 中注入新的 import 规则
3. 通过 descriptor 绕过 source graph loader

## CLI 边界

V0.3 对 CLI 的约束是：

1. CLI 可以新增 `--project <path>` 或 `--workspace <path>` 一类入口。
2. CLI 仍然只负责参数解析、descriptor 选择和驱动 frontend 入口。
3. CLI 不直接承担 module lookup、source graph 构建或 resolver 逻辑。

兼容立场：

1. V0.2 的 `--search-root ... <entry.ahfl>` 路径继续保留，作为兼容入口。
2. descriptor 驱动入口是新增正式能力，不要求立即移除兼容入口。
3. 若用户同时提供 descriptor 和底层 `--search-root`，后续实现必须明确定义冲突规则，而不是隐式合并。

## Frontend 边界

frontend 在 V0.3 中建议分成两步：

1. descriptor 解析
2. `ProjectInput -> SourceGraph` 装载

但这两步都仍属于 frontend 层，而不是 resolver 或 CLI 层。

当前实现已经按这个边界落地为两个编译单元：

1. `src/frontend/project.cpp`
   - descriptor 读取与字段校验
   - workspace -> project 选择
   - `ProjectInput -> SourceGraph` 装载
2. `src/frontend/frontend.cpp`
   - 单文件解析
   - AST lowering
   - AST / project AST 调试输出

推荐对象形状：

```cpp
struct ProjectDescriptor {
    std::string format_version;
    std::string name;
    std::vector<std::filesystem::path> search_roots;
    std::vector<std::filesystem::path> entry_sources;
};

struct WorkspaceDescriptor {
    std::string format_version;
    std::string name;
    std::vector<std::filesystem::path> projects;
};
```

这里的设计意图比命名更重要：

1. project descriptor 仍然只表达“怎样得到 `ProjectInput`”。
2. workspace descriptor 仍然只表达“有哪些 project 可选”。
3. descriptor 解析错误属于 project/frontend diagnostics，不进入后续语义阶段。

## Diagnostics 分层

V0.3 需要新增一层 descriptor 级错误，但仍应与 source graph / resolver 错误分层：

### Descriptor 错误

例如：

1. descriptor 文件不存在
2. `format_version` 不受支持
3. 缺失 `search_roots` 或 `entry_sources`
4. 相对路径非法或重复
5. workspace 指向的 project manifest 不存在
6. workspace 中存在重复 project 名称，导致 `--project-name` 选择歧义

### Loader 错误

例如：

1. module owner 缺失
2. module/file mismatch
3. missing import target
4. duplicate module owner

### Resolver / Checker 错误

例如：

1. unknown type
2. duplicate declaration
3. ambiguous callable
4. workflow / contract 语义错误

换句话说，descriptor 负责“项目输入是否成立”，loader 负责“文件集合是否成立”，resolver/checker 负责“语言语义是否成立”。

## Workspace 的刻意限制

V0.3 workspace 明确保持极小：

1. 不负责 project 间依赖解析。
2. 不负责统一 search root 继承。
3. 不负责把多个 projects 拼成一个共享 source graph。
4. 不负责版本选择或 profile 选择。

它的作用只是：

1. 聚合多个 projects
2. 让 CLI 或工具链能按名字或路径选择其中一个 project

## 对仓库布局的影响

V0.3 不强制仓库必须采用固定目录名，但建议：

```text
repo/
  ahfl.workspace.json
  projects/
    refund/
      ahfl.project.json
      app/main.ahfl
      lib/types.ahfl
```

理由是：

1. descriptor root 与 source root 的关系更清晰。
2. project 相对路径不会隐式依赖当前 shell cwd。
3. 后续测试夹具可更自然地做多 project 覆盖。

## 示例

### Project

```json
{
  "format_version": "ahfl.project.v0.3",
  "name": "refund-audit",
  "search_roots": ["."],
  "entry_sources": ["app/main.ahfl", "app/admin.ahfl"]
}
```

它表示：

1. descriptor root 是当前 `ahfl.project.json` 所在目录。
2. loader 在该目录下把 `.` 当作 search root。
3. 入口源码有两个，后续由 frontend 统一生成一个 project input。

### Workspace

```json
{
  "format_version": "ahfl.workspace.v0.3",
  "name": "ahfl-examples",
  "projects": [
    "projects/refund/ahfl.project.json",
    "projects/review/ahfl.project.json"
  ]
}
```

它表示：

1. workspace 本身不定义 entry sources。
2. 真正参与编译的是被选中的某个 project manifest。

## 当前不承诺的能力

以下能力明确不在 V0.3 的 project descriptor 设计承诺范围内：

1. descriptor 内的 environment variable 插值
2. 条件化 search root 或 target-specific root
3. descriptor 继承、include 或模板化
4. auto-discovery 最近的 descriptor 文件
5. workspace 级共享依赖图

## 对后续实现的约束

Issue 02 之后，manifest / workspace 相关实现必须遵守以下规则：

1. descriptor 只向 frontend 提供 project 输入，不绕开现有 source graph loader。
2. descriptor 不改变 canonical naming、import alias 或 resolver lookup 规则。
3. 新增 CLI 入口时，不得把 descriptor 解析结果散落到多个局部变量里；应下沉为统一 `ProjectInput`。
4. 若后续要支持更复杂项目系统，应新增更高层设计文档，而不是直接膨胀本文的最小模型。

## 当前状态

本文冻结了 V0.3 的最小 project descriptor 设计边界；当前仓库已实现：

1. `ahfl.project.json` 的 frontend 解析与 diagnostics
2. `ahfl.workspace.json` 的 frontend 解析与 project 选择
3. CLI 的 `--project <path>` 与 `--workspace <path> --project-name <name>` 入口

project-aware AST/debug 扩展与更完整的 reference 文档仍属于后续工作。
