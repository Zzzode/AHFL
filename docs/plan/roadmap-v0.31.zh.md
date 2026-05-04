# AHFL V0.31 Roadmap

V0.31 开始实现 local host harness，但限定为 test-only、sandbox-first、默认 no-network。目标是把 V0.26 的 simulated receipt 推进为受控 host harness execution record。

## 目标

1. 定义 local host harness execution request。
2. 定义 sandbox policy、timeout policy、exit status 与 captured diagnostic summary。
3. 实现 no-network / no-secret / no-filesystem-write 的 test harness。
4. 将 harness result 映射为 stable execution record。

## 非目标

1. production host process manager
2. network-enabled provider execution
3. secret injection、host environment secret
4. provider SDK real call

## 里程碑

- [ ] M0 冻结 local harness sandbox scope
- [ ] M1 定义 execution request / record model
- [ ] M2 实现 test-only harness runner
- [ ] M3 覆盖 timeout、nonzero exit、blocked source、sandbox denial
- [ ] M4 建立 CLI、golden、ASan CI 标签

## 完成定义

V0.31 完成后，仓库可以在受控测试环境启动 local harness，但默认仍不能访问网络、secret 或生产 filesystem。
