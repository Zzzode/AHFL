# AHFL Core V0.2 Source Graph

本文冻结 AHFL Core V0.2 在 project/source graph 与 frontend 公共边界上的当前设计，作为 Issue 04、Issue 05、Issue 07 的实现约束。

适用范围：

- `docs/design/module-loading-v0.2.zh.md`
- `docs/design/compiler-phase-boundaries-v0.2.zh.md`
- `docs/plan/roadmap-v0.2.zh.md`
- `include/ahfl/support/source.hpp`
- `include/ahfl/frontend/frontend.hpp`
- `src/frontend/frontend.cpp`
- `src/cli/ahflc.cpp`

## 目标

1. 为 V0.2 定义稳定的 project/source graph 数据模型
2. 扩展 frontend 公共边界，使其既能兼容单文件，也能承载 project-aware 装载
3. 让 resolver/typecheck/validate 消费“已装载的源码集合”，而不是自行接触文件系统

## 设计原则

1. loader 细节留在 frontend
2. parse tree 生命周期仍只停留在 `frontend.cpp`
3. resolver/typecheck/validate 不直接遍历文件系统
4. 单文件兼容模式继续可用
5. project-aware 模式是单文件模式之上的显式扩展，不是隐式副作用

## 分层后的对象模型

V0.2 推荐引入三层对象，而不是继续用单个 `ParseResult` 同时承担所有角色。

### 1. Source Artifact

描述单个物理源文件的原始信息。

建议字段：

- `source_id`
- `path`
- `display_name`
- `content`

这一层属于 `support/source.hpp` 可承载的低层对象，不包含 AST 和语义结果。

### 2. Source Unit

描述单个源文件在 frontend 之后的稳定产物。

建议字段：

- `source_id`
- `module_name`
- `SourceFile`
- `Owned<ast::Program>`
- 该 source 内声明的 import 请求
- syntax / lowering diagnostics 的来源归属

这一层是 frontend 与后续语义阶段之间的最小单元。

### 3. Source Graph

描述一组 source units 及其 import 边。

建议字段：

- `entry_sources`
- `sources`
- `module_to_source`
- `import_edges`

约束：

1. source graph 只描述装载结果与所有权关系。
2. source graph 本身不携带 resolved symbol 或 typed expression。
3. source graph 可以被 resolver 直接消费，但不应反向依赖 resolver。

## 建议的公共类型边界

本文不要求最终代码完全使用以下命名，但要求边界形状等价。

在 `include/ahfl/support/source.hpp` 中，V0.2 可以新增：

```cpp
struct SourceId {
    std::size_t value{0};
};

struct SourceArtifact {
    SourceId id;
    std::filesystem::path path;
    SourceFile source;
};

struct ImportRequest {
    std::string module_name;
    std::string alias;
    SourceRange range;
};
```

在 `include/ahfl/frontend/frontend.hpp` 中，V0.2 可以新增：

```cpp
struct SourceUnit {
    SourceId id;
    std::string module_name;
    SourceFile source;
    Owned<ast::Program> program;
    std::vector<ImportRequest> imports;
};

struct SourceGraph {
    std::vector<SourceId> entry_sources;
    std::vector<SourceUnit> sources;
    std::unordered_map<std::string, SourceId> module_to_source;
};

struct ProjectInput {
    std::vector<std::filesystem::path> entry_files;
    std::vector<std::filesystem::path> search_roots;
};

struct ProjectParseResult {
    SourceGraph graph;
    DiagnosticBag diagnostics;
};
```

这些类型的意图比精确命名更重要：

1. 单文件结果要能提升为 graph 中的一个 source unit。
2. project-aware 入口要返回整组 source units，而不是只返回一个 `ast::Program`。
3. diagnostics 仍然集中管理，不把 parser context 泄漏到公共头。

## Frontend 公共入口的扩展方向

当前 V0.1 入口是：

- `parse_file(path)`
- `parse_text(name, text)`

V0.2 推荐扩展为：

1. 保留现有单文件入口，作为兼容 wrapper
2. 新增 project-aware 入口，例如 `parse_project(ProjectInput)`
3. 由 `parse_file(path)` 在兼容模式下返回单文件结果
4. 由 project-aware CLI 使用 `parse_project(...)`

重要约束：

1. `parse_file` 不自动偷偷递归读 imports。
2. `parse_project` 才是唯一允许构建 source graph 的公共入口。
3. project-aware 模式与单文件兼容模式必须在 API 形态上可区分。

## 与现有六阶段流水线的关系

V0.2 不改变大阶段顺序，仍然是：

1. `parse`
2. `ast`
3. `resolve`
4. `typecheck`
5. `validate`
6. `emit`

变化在于输入粒度：

1. V0.1 更接近“单 source -> AST”
2. V0.2 变成“source graph -> 每个 source 的 AST -> 跨 source 语义阶段”

因此：

1. parse/ast 仍由 frontend 持有
2. resolver 的输入从单个 `ast::Program` 扩展到一组 source units
3. 后续阶段都只消费 source graph 与 AST/语义对象，不消费文件系统

## CLI 边界

V0.2 对 CLI 的约束是：

1. `src/cli/ahflc.cpp` 继续只负责参数分发和驱动流水线
2. CLI 可以新增 project-aware 参数，例如 entry file 与 search root
3. CLI 不应直接承担 module lookup 和 source graph 构建逻辑

换句话说，source graph 是 frontend 边界，不是 CLI 边界。

## Diagnostics 归属

引入 source graph 后，diagnostics 需要能表达两类来源：

1. 单个 source 内的 range 错误
2. graph 级所有权/装载错误

V0.2 约束：

1. graph 级错误仍应尽量附着到具体 source file 或 import range。
2. 无法附着到具体源码片段时，至少附着到 entry source 或相关 owner source。
3. 不允许把 graph 级错误伪装成 generic parse error。

## 所有权与生命周期

V0.2 继续遵守：

1. AST 的树状拥有关系使用 `Owned<T>`
2. parse tree 生命周期只存在于 `frontend.cpp`
3. `SourceGraph` 拥有 source-level frontend 结果
4. resolver/typecheck/validate 只能借用 source graph，不拥有 parse tree

## 对后续实现的约束

Issue 04 之后，project/source graph 相关实现必须遵守以下规则：

1. 不要把文件系统遍历逻辑扩散进 resolver/typecheck/validate。
2. 不要让 `ParseResult` 在 V0.2 中继续膨胀成“单文件模式和项目模式的万能容器”。
3. 单文件兼容模式应通过 wrapper 复用 V0.2 新边界，而不是平行维护两套完全不同的前端。
4. 若后续要增加 manifest、workspace 或 build graph，优先在 source graph 之上增量扩展，而不是推翻本层对象模型。
