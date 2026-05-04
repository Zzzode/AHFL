# AHFL V0.39 Roadmap

V0.39 建立 observability / audit event sink。目标是输出 structured telemetry 与 audit event，让 provider execution 可审计、可归档、可人工 review。

## 目标

1. 定义 provider execution audit event。
2. 定义 telemetry summary 与 redaction policy。
3. 定义 operator review event projection。
4. 覆盖 success、failure、retry、recovery paths。

## 非目标

1. production metrics backend
2. SIEM vendor integration
3. secret-bearing log output
4. host-level raw telemetry dump

## 里程碑

- [x] M0 冻结 audit event scope
- [x] M1 定义 structured event model
- [x] M2 定义 redaction 与 sensitivity validation
- [x] M3 接入 provider execution chain
- [x] M4 建立 golden、docs、CI 标签

## 完成定义

V0.39 完成后，provider execution 有稳定审计事件输出，但不绑定具体观测平台。
