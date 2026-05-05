# AHFL V0.47 Roadmap

V0.47 建立 operator approval workflow artifact。目标是把 production integration 的人工审批输入、审批决定、拒绝原因与证据链表达为 machine-readable artifact，但不自动批准或绕过组织流程。

## 目标

1. 定义 approval request artifact。
2. 定义 approval decision / rejection artifact。
3. 绑定 release evidence archive、config validation 与 readiness evidence。
4. 输出 operator-facing approval receipt 与 blocking reason summary。

## 非目标

1. 自动批准生产发布
2. 替代组织审批系统
3. 接入外部 ticket / change-management 系统
4. 允许未审批真实 provider traffic

## 里程碑

- [x] M0 冻结 approval workflow artifact scope
- [x] M1 定义 approval request / decision / receipt model
- [x] M2 实现 evidence binding 与 rejection reason summary
- [x] M3 输出 operator-facing approval receipt
- [ ] M4 建立 docs、golden、CI 标签和 migration guidance

## 完成定义

V0.47 完成后，AHFL 可以清晰表达 provider production integration 是否已获 operator approval，并把审批依据、拒绝原因和证据引用纳入可审计 artifact。
