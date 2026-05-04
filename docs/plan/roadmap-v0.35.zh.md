# AHFL V0.35 Roadmap

V0.35 冻结 idempotency / retry contract。目标是让 durable write 可以安全重试，避免重复写入或状态漂移。

## 目标

1. 定义 idempotency key namespace。
2. 定义 retry token placeholder 与 retry eligibility。
3. 定义 duplicate write detection summary。
4. 将 retry decision 与 provider failure taxonomy 对齐。

## 非目标

1. production retry scheduler
2. distributed lock service
3. provider-specific exactly-once 保证
4. 隐式重试真实 provider 写入

## 里程碑

- [ ] M0 冻结 idempotency scope
- [ ] M1 定义 retry token / duplicate detection model
- [ ] M2 覆盖 retryable / non-retryable failure paths
- [ ] M3 接入 mock adapter matrix
- [ ] M4 建立 compatibility 与 golden

## 完成定义

V0.35 完成后，系统可以明确判断一次 provider write 是否允许重试，以及如何避免重复写入。
