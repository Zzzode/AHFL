# AHFL V0.55 Roadmap

V0.55 建立 Capability Bridge。目标是实现真实的外部能力调用，将 IR CallExpr 连接到实际的 HTTP/gRPC/本地函数执行。至此，AHFL 完成从源码到真实执行的完整链路。

## 目标

1. 定义 `CapabilityRegistry` 与 `CapabilityBridge` 接口。
2. 实现 `FunctionCapability`（本地函数绑定）。
3. 实现 `HTTPCapability`（HTTP API 调用）。
4. 实现 `GRPCCapability`（gRPC 服务调用）。
5. 实现 capability 结果到 Value 的转换。
6. 实现错误处理与重试策略（retry、retry_on、timeout）。
7. 建立 CLI emission、单元测试与 `ahfl-v0.55` 测试标签。

## 非目标

1. Provider SDK 适配器（v0.56+）。
2. Secret 管理集成。
3. 分布式 capability 调用。
4. Capability 缓存与去重。

## 里程碑

- [x] M0 冻结 capability bridge scope 与接口定义
- [x] M1 实现 CapabilityRegistry 与 FunctionCapability
- [x] M2 实现 HTTPCapability 与错误处理
- [x] M3 实现 retry/retry_on/timeout 策略
- [x] M4 建立 docs、tests、CLI emission 与标签

## 完成定义

V0.55 完成后，AHFL 可以真实调用外部能力。例如：

```ahfl
// 本地函数绑定
capability OrderQuery(order_id: String) -> OrderInfo;
// => 绑定到 order_service.get_order(order_id)

// HTTP 调用
capability AuditDecision(order: OrderInfo, request: RefundRequest) -> AuditReply;
// => POST https://audit-service.example.com/decide

// gRPC 调用
capability TicketCreate(order_id: String, reason: String) -> String;
// => gRPC call to TicketService.CreateTicket
```

至此，AHFL 完成从 `.ahfl` 源码到真实执行的完整链路：

```
.ahfl → Parser → Resolver → Typecheck → IR → ExecutionPlan
  → WorkflowRuntime → AgentRuntime → StatementExecutor → ExpressionEvaluator
  → CapabilityBridge → 真实外部服务
```
