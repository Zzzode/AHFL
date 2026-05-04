# AHFL V0.28 Roadmap

V0.28 在 V0.27 SDK adapter request / response artifact boundary 之后，冻结 provider SDK adapter interface。目标是让未来真实 provider adapter 可以被注册、发现、验证和替换，但仍不要求接入真实云 SDK 或数据库 client。

## 目标

1. 定义 provider SDK adapter C++ interface 的最小 ABI。
2. 定义 provider registry、adapter descriptor、capability descriptor 与版本字段。
3. 定义 provider error normalization 的第一版 taxonomy。
4. 让 adapter interface 可以消费 V0.27 request plan，但默认只返回 placeholder / mock result。

## 非目标

1. 真实 provider SDK 调用
2. secret manager、credential exchange、network endpoint
3. provider-specific payload schema 深度建模
4. production registry discovery 或动态插件加载

## 里程碑

- [x] M0 冻结 adapter interface scope 与 non-goals
- [x] M1 定义 adapter descriptor / registry model
- [x] M2 定义 capability descriptor 与 unsupported capability diagnostics
- [x] M3 定义 provider error normalization skeleton
- [x] M4 建立 direct tests、CLI review、golden、docs、CI 标签

## 完成定义

V0.28 完成后，仓库应能稳定回答“某个 provider adapter 宣称支持什么、输入是什么、错误如何归一化”，但仍不能真实证明 provider write 已发生。
