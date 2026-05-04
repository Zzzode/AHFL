# AHFL V0.41 Durable Store Provider Registry Selection Compatibility

V0.41 的兼容边界是 provider registry、selection plan 与 capability negotiation review。

## Stable Formats

- `ahfl.durable-store-import-provider-registry.v1`
- `ahfl.durable-store-import-provider-selection-plan.v1`
- `ahfl.durable-store-import-provider-capability-negotiation-review.v1`

## Compatibility Rules

1. registry 必须保留 compatibility report identity。
2. registry 必须显式记录 primary provider、fallback provider 与 registered provider count。
3. selection plan 必须显式输出 selected provider、fallback provider、selection status 与 fallback policy。
4. fallback policy 必须只允许 audited fallback。
5. blocked selection 必须要求 operator review。
6. capability negotiation review 是 reviewer-facing projection，不替代 machine-facing selection plan。
