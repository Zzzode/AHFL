# AHFL V0.31 Issue Backlog

本文与 [roadmap-v0.31.zh.md](./roadmap-v0.31.zh.md) 对齐，目标是建立 test-only、sandbox-first local host harness。

## [x] Issue 01

标题：冻结 local harness sandbox scope

验收标准：

1. sandbox policy 默认 `test_only=true`
2. network、secret、filesystem write、host env 全部默认禁止
3. validation 拒绝 sandbox escape flags

## [x] Issue 02

标题：定义 execution request / record model

验收标准：

1. request 只消费 V0.30 secret policy review
2. record 只消费 harness request
3. record 表达 exit code、timeout、sandbox denial 与 captured diagnostics

## [x] Issue 03

标题：实现 test-only harness runner

验收标准：

1. runner 不启动真实 provider SDK
2. runner 不访问网络、secret 或生产 filesystem
3. success、nonzero exit、timeout、sandbox denied 都有 stable mapping

## [x] Issue 04

标题：定义 harness review

验收标准：

1. review 只消费 execution record
2. success path next action 指向 V0.32 SDK payload materialization
3. failure path 保留 failure attribution

## [x] Issue 05

标题：建立 CLI、golden、ASan CI 标签

验收标准：

1. direct regression 覆盖 sandbox denial、timeout、nonzero exit
2. golden 覆盖 single-file、project manifest、workspace
3. `ahfl-v0.31` 标签可用
