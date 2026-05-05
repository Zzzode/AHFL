# AHFL V0.56 Roadmap

V0.56 实现 LLM Provider 适配器。目标是将 AHFL Capability 与 OpenAI-compatible LLM API（如 GLM）打通，使 Agent 的 capability 调用可以由 LLM 真实完成，实现端到端的智能决策执行。

## 目标

1. 实现 `LLMProviderConfig`，支持 endpoint、model、API key 等配置。
2. 实现 HTTP Client 层，封装对 `/v1/chat/completions` 的调用。
3. 实现 Schema-driven Prompt 生成器，从 IR 类型（StructDecl/EnumDecl/CapabilityDecl）自动构造 system prompt。
4. 实现 JSON → Value 反序列化器，将 LLM 输出的 JSON 映射为 AHFL Value variant。
5. 实现 `LLMCapabilityProvider`，组合上述模块，注册到 CapabilityRegistry。
6. 支持 `response_format: json_object` 强制结构化输出。
7. 建立集成测试（使用真实 API 或 mock server）。

## 非目标

1. 多模型路由 / fallback 策略（v0.57+）。
2. Streaming 输出支持。
3. 函数调用 (Function Calling) 协议集成。
4. Token 用量追踪与预算控制。
5. Prompt 缓存与去重。

## 里程碑

- [ ] M0 冻结 LLM Provider 接口设计与依赖选型
- [ ] M1 实现 HTTP Client 与 LLMProviderConfig
- [ ] M2 实现 Schema-driven Prompt 生成器
- [ ] M3 实现 JSON → Value 反序列化
- [ ] M4 实现 LLMCapabilityProvider 并集成到 CapabilityRegistry
- [ ] M5 建立 E2E 集成测试（真实 GLM API 调用）

## 技术设计

### 核心架构

```
CapabilityRegistry
  └── LLMCapabilityProvider
        ├── LLMProviderConfig (endpoint, model, api_key, temperature)
        ├── PromptBuilder (IR types → system prompt + user prompt)
        ├── HttpClient (POST /v1/chat/completions)
        └── ResponseParser (JSON → Value)
```

### 调用流程

```
AgentRuntime
  → CallExpr("ClassifyMessage", [StringValue("My server crashed")])
  → CapabilityRegistry.invoke("ClassifyMessage", args)
  → LLMCapabilityProvider.invoke(name, args)
      1. PromptBuilder: 查找 CapabilityDecl + 返回类型 StructDecl/EnumDecl
         → 生成 system prompt (含 JSON schema 约束)
         → 生成 user prompt (含参数值)
      2. HttpClient: POST to GLM API
         → request: { model, messages, response_format: {type: "json_object"} }
      3. ResponseParser: JSON string → StructValue/EnumValue/...
  → 返回 Value 到 AgentRuntime
```

### Prompt 策略

给定 capability 声明：
```ahfl
capability ClassifyMessage(message: String) -> ClassifyResult;

struct ClassifyResult {
    category: Category;
    confidence: String;
}

enum Category { General, Billing, Technical }
```

生成的 system prompt：
```
You are performing the capability "ClassifyMessage".
You must respond with a JSON object matching this schema:
{
  "category": one of ["General", "Billing", "Technical"],
  "confidence": string
}
Do not include any extra fields or explanation.
```

生成的 user prompt：
```
Input parameters:
- message: "My server crashed"

Respond with the result JSON only.
```

### 依赖选型

| 依赖 | 候选方案 | 理由 |
|------|---------|------|
| HTTP Client | cpp-httplib (header-only) | 零依赖、header-only、项目轻量 |
| JSON 解析 | nlohmann/json | 社区标准、类型安全、已广泛验证 |

## 完成定义

V0.56 完成后，可以编写如下 `.ahfl` 文件并真实调用 LLM：

```ahfl
capability ClassifyMessage(message: String) -> ClassifyResult;

agent ClassifyAgent {
    input: SupportRequest;
    output: ClassifyResult;
    // ...
}
```

运行时配置：
```json
{
  "provider": "openai-compatible",
  "endpoint": "https://open.bigmodel.cn/api/paas/v4",
  "model": "glm-4",
  "api_key": "${GLM_API_KEY}"
}
```

执行后，ClassifyMessage 会真实调用 GLM-4 并返回结构化结果。
