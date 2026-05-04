# AHFL V0.38 Issue Backlog

本文与 [roadmap-v0.38.zh.md](./roadmap-v0.38.zh.md) 对齐，目标是冻结 provider failure taxonomy v1。

## [x] Issue 01

标题：定义 taxonomy v1

验收标准：

1. taxonomy 覆盖 authentication、authorization、network、throttling、conflict、schema mismatch、provider internal、unknown、blocked
2. 每类 failure 记录 category、retryability、operator action、security sensitivity
3. taxonomy report 不持久化 raw provider error

## [x] Issue 02

标题：接入 adapter response normalization

验收标准：

1. timeout 映射 network retryable
2. throttled 映射 rate limit retryable
3. conflict 映射 duplicate review
4. schema mismatch 与 provider failure 映射 non-retryable

## [x] Issue 03

标题：定义 taxonomy review

验收标准：

1. review 输出 failure kind、retryability 与 operator recommendation
2. recovery plan summary 进入 redacted failure summary
3. secret-bearing error 不进入 artifact

## [x] Issue 04

标题：覆盖 mock adapter failure matrix

验收标准：

1. direct matrix 覆盖 success、timeout、throttle、conflict、failure、schema mismatch
2. golden 覆盖 single-file、project manifest、workspace
3. `ahfl-v0.38` 标签可用
