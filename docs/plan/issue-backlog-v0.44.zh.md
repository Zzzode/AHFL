# AHFL V0.44 Issue Backlog

V0.44 聚焦 artifact schema / version compatibility checker。

## Planned

- [ ] 定义 schema / version compatibility check artifact。
- [ ] 校验 provider production artifact format version。
- [ ] 校验 source chain 完整性。
- [ ] 校验 compatibility、registry、readiness 与 audit reference 的版本边界。
- [ ] 输出 schema drift 与 incompatible version summary。
- [ ] 添加 CLI emission、golden、direct tests 与 `ahfl-v0.44` 标签。

## Deferred

- [ ] 引入外部 schema registry 服务。
- [ ] 自动迁移旧 artifact。
- [ ] 放宽既有 golden contract。
- [ ] 读取运行环境 secret 或 provider endpoint。
