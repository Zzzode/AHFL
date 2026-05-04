# AHFL V0.44 Roadmap

V0.44 建立 artifact schema / version compatibility checker。目标是让 provider production 相关 artifact 的 version、source chain、compatibility reference 与 golden schema drift 在进入真实集成前被明确发现。

## 目标

1. 定义 schema / version compatibility check artifact。
2. 校验 provider production artifact 的 format version 与 source chain。
3. 检查 compatibility ref、registry ref、readiness ref 与 audit ref 的版本边界。
4. 建立 schema drift failure summary、golden regression 与 `ahfl-v0.44` 测试标签。

## 非目标

1. 引入外部 schema registry 服务
2. 自动迁移旧 artifact
3. 放宽既有 golden contract
4. 读取运行环境 secret 或 provider endpoint

## 里程碑

- [ ] M0 冻结 schema compatibility scope
- [ ] M1 定义 version check input / output model
- [ ] M2 实现 source chain 与 reference version 校验
- [ ] M3 输出 schema drift 与 incompatible version summary
- [ ] M4 建立 docs、golden、CI 标签和 migration guidance

## 完成定义

V0.44 完成后，AHFL 可以在真实 provider integration 前定位 artifact schema / version 不兼容问题，并保持 provider production evidence 的版本边界稳定。
