# AHFL V0.42 Durable Store Provider Production Readiness Compatibility

V0.42 的兼容边界是 production readiness evidence、readiness review 与 operator-facing readiness report。

## Stable Formats

- `ahfl.durable-store-import-provider-production-readiness-evidence.v1`
- `ahfl.durable-store-import-provider-production-readiness-review.v1`
- `ahfl.durable-store-import-provider-production-readiness-report.v1`

## Compatibility Rules

1. evidence 必须保留 negotiation review、compatibility report、audit event、recovery plan 与 taxonomy report 的 source identity。
2. evidence 必须显式输出 security、recovery、compatibility、observability 与 registry evidence flags。
3. review 必须显式输出 release gate 与 blocking issue count。
4. release gate 必须区分 `ready_for_production_review`、`conditional` 与 `blocked`。
5. report 是 operator-facing projection，不替代安全审计、合规审计、组织变更流程或环境配置。
6. V0.42 artifact 不得隐式启用真实 provider traffic。
