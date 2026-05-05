# AHFL V0.55 Issue Backlog

V0.55 聚焦 Capability Bridge。

## Planned

- [x] 定义 CapabilityBridge 接口（invoke(name, args) → Value）。
- [x] 定义 CapabilityRegistry（注册/查找/绑定 capability 实现）。
- [x] 定义 CapabilityBindingConfig（JSON 格式的绑定配置）。
- [x] 实现 FunctionCapability（本地 C++ 函数或 shared library 绑定）。
- [x] 实现 HTTPCapability（HTTP POST/GET + JSON 序列化/反序列化）。
- [x] 实现 GRPCCapability（gRPC channel + protobuf 序列化）。
- [x] 实现 Value ↔ JSON 双向转换。
- [x] 实现 retry 策略（retry count + exponential backoff）。
- [x] 实现 retry_on 策略（错误类型匹配）。
- [x] 实现 timeout 策略（per-call deadline）。
- [x] 集成 CallExpr 求值到 Expression Evaluator。
- [x] 建立 capability bridge 单元测试（mock HTTP/gRPC server）。
- [x] 添加 `ahfl-v0.55` 测试标签。

## Deferred

- [ ] Secret 管理集成（v0.56+）。
- [ ] mTLS / OAuth2 认证。
- [ ] Capability 调用缓存与幂等。
- [ ] 分布式 capability routing。
- [ ] 调用链追踪（OpenTelemetry）。
