# AHFL V0.40 Roadmap

V0.40 建立 provider compatibility suite。目标是为每个 provider adapter 提供一致的 fixture、golden、capability matrix 与 compatibility report。

## 目标

1. 定义 provider compatibility test manifest。
2. 定义 provider fixture matrix。
3. 定义 compatibility report artifact。
4. 覆盖 mock adapter 与 alpha real adapter。

## 非目标

1. 对所有云厂商 SDK 的完整认证
2. production SLA benchmark
3. 外部服务默认 CI 依赖
4. 自动发布 provider adapter

## 里程碑

- [x] M0 冻结 compatibility suite scope
- [x] M1 定义 test manifest / fixture matrix
- [x] M2 定义 compatibility report
- [x] M3 接入 mock 与 alpha provider
- [x] M4 更新 docs、CI opt-in、contributor guide

## 完成定义

V0.40 完成后，新 provider adapter 可以按统一 compatibility suite 接入和回归。
