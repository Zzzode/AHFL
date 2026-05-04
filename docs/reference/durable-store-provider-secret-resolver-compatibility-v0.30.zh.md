# AHFL V0.30 Durable Store Provider Secret Resolver Compatibility

V0.30 的兼容边界是 secret handle resolver placeholder。

## Stable Formats

- `ahfl.durable-store-import-provider-secret-resolver-request-plan.v1`
- `ahfl.durable-store-import-provider-secret-resolver-response-placeholder.v1`
- `ahfl.durable-store-import-provider-secret-policy-review.v1`
- `ahfl.provider-secret-handle.v1`
- `ahfl.provider-credential-lifecycle.v1`

## Compatibility Rules

1. artifact 可以包含 secret handle identity。
2. artifact 不能包含 secret value、credential material、token value 或 secret manager response。
3. ready request 必须来自 ready provider config snapshot。
4. ready response placeholder 的 lifecycle state 必须是 `placeholder_pending_resolution`。
