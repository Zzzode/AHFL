# AHFL V0.37 Roadmap

V0.37 冻结 recovery / resume contract。目标是支持 crash recovery、partial write recovery 与 resume token 的 artifact 边界。

## 目标

1. 定义 recovery checkpoint for provider write。
2. 定义 resume token placeholder 与恢复 eligibility。
3. 定义 partial write recovery plan。
4. 定义 recovery review summary。

## 非目标

1. production recovery daemon
2. remote lock manager
3. provider-specific transaction rollback
4. operator console

## 里程碑

- [x] M0 冻结 recovery / resume scope
- [x] M1 定义 recovery checkpoint / resume token model
- [x] M2 定义 partial write recovery plan
- [x] M3 覆盖 crash-before-call、crash-after-call、unknown-commit paths
- [x] M4 建立 golden、docs、CI 标签

## 完成定义

V0.37 完成后，系统能稳定表达 provider write 的恢复计划，但不要求生产 daemon 自动执行恢复。
