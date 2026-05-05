# AHFL V0.56 Issue Backlog

V0.56 聚焦 LLM Provider 适配器，打通 AHFL Capability 与 OpenAI-compatible API。

## Planned

- [ ] 定义 `LLMProviderConfig` 结构（endpoint, model, api_key, temperature, max_tokens, response_format）。
- [ ] 选型并引入 HTTP 客户端依赖（cpp-httplib header-only）。
- [ ] 选型并引入 JSON 库依赖（nlohmann/json header-only）。
- [ ] 实现 `HttpClient` 封装（POST 请求、header 管理、超时配置、错误处理）。
- [ ] 实现 `PromptBuilder`：从 IR Program 中查找 CapabilityDecl + 关联类型。
- [ ] 实现 `PromptBuilder`：StructDecl → JSON Schema 描述生成。
- [ ] 实现 `PromptBuilder`：EnumDecl → 枚举值约束描述生成。
- [ ] 实现 `PromptBuilder`：组装 system prompt + user prompt。
- [ ] 实现 `ResponseParser`：JSON string → Value (String, Int, Bool, Float)。
- [ ] 实现 `ResponseParser`：JSON object → StructValue（字段递归解析）。
- [ ] 实现 `ResponseParser`：JSON string → EnumValue（枚举值匹配验证）。
- [ ] 实现 `ResponseParser`：处理 Optional/List/嵌套 Struct。
- [ ] 实现 `LLMCapabilityProvider`：组合 PromptBuilder + HttpClient + ResponseParser。
- [ ] 实现 `LLMCapabilityProvider`：注册到 CapabilityRegistry 的 adapter 接口。
- [ ] 实现配置文件加载（JSON 格式，支持环境变量 `${ENV_VAR}` 展开）。
- [ ] 添加 `ahfl-v0.56` CMake 测试标签。
- [ ] 编写单元测试：PromptBuilder 生成正确的 prompt。
- [ ] 编写单元测试：ResponseParser 正确解析各类 JSON → Value。
- [ ] 编写集成测试：使用 mock HTTP server 验证完整调用链路。
- [ ] 编写 E2E 测试：使用真实 GLM API 执行多 Agent workflow。

## Deferred

- [ ] 多 Provider 路由与 fallback（v0.57+）。
- [ ] Function Calling 协议支持。
- [ ] Streaming 响应处理。
- [ ] Token 用量统计与预算控制。
- [ ] Prompt 模板自定义 / 用户覆写。
- [ ] 调用结果缓存（相同输入 → 跳过 LLM 调用）。
- [ ] mTLS / OAuth2 认证支持。
- [ ] 调用链追踪（OpenTelemetry 集成）。
