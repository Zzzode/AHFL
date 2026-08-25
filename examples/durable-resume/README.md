# Durable Resume — a minimal RFC 0022 demo

This example is the roadmap north-star as a runnable program: embed AHFL, run a
formally verified workflow, have a capability return `PENDING`, **suspend**,
persist a resume record, then in a fresh process **cold-start resume** to a
deterministic result.

One workflow, one agent, one async capability:

- `ReplyWorkflow` → `ReplyAgent` → `DraftReply(request) -> Reply`.
- `DraftReply` is the "await an LLM" call. A host that isn't ready yet returns
  `AHFL_CAP_PENDING`; the workflow suspends at that node.

## Run it (two processes, shell only)

`ahflc run` needs an LLM config, but this demo never contacts it — `DraftReply`
is suspended and its result is injected on resume — so any well-formed config
works:

```sh
cd examples/durable-resume
echo '{"endpoint":"http://127.0.0.1:1/v1","model":"unused","api_key_secret":"K"}' > /tmp/llm.json
export K=unused
WF=durable_resume::main::ReplyWorkflow

# 1. Run. --suspend-capability forces DraftReply to PENDING, so the workflow
#    suspends and writes a resume record to the recovery store. Exit 0: the run
#    is durably parked, not failed.
ahflc run --manifest ahfl.toml --workflow "$WF" --llm-config /tmp/llm.json \
  --suspend-capability durable_resume::main::DraftReply \
  --recovery-store /tmp/reply.snapshot

# 2. Resume — a fresh process whose only link to run 1 is the snapshot on disk.
#    --resume-pending-result supplies what the host eventually drafted. The
#    workflow replays from the memo, injects this result at the pending call,
#    and completes. DraftReply is NOT re-invoked.
ahflc run --manifest ahfl.toml --workflow "$WF" --llm-config /tmp/llm.json \
  --recovery-store /tmp/reply.snapshot \
  --resume-pending-result '{"_type":"durable_resume::main::Reply","id":"T-42","answer":"Use the reset link."}'
```

Run 2 completes with `{"id":"T-42","answer":"Use the reset link."}` — the
injected reply, identical to what a synchronous run that returned it directly
would produce.

## What this shows

- **Synchronous surface, async landing.** The `.ahfl` source has no `async` /
  `await`; `DraftReply(...)` reads as a plain call. Suspension is invisible to
  the language (RFC 0021 / RFC 0022).
- **Durable, cross-process.** The resume record is pure serializable data
  (recovery schema `v2`, a stable artifact). Run 2 shares nothing with run 1 but
  the on-disk snapshot.
- **Exactly-once + deterministic.** On resume the completed calls replay from the
  memo and are never re-invoked; the final result is deterministic. `DraftReply`
  is declared `durable_write`, so a write-ahead intent (a stable idempotency key)
  is recorded before dispatch — add `--intent-log /tmp/intent.jsonl` to either
  run to capture it. The key is logged once on the suspend run and NOT re-logged
  on resume (the memoized call is replayed, not re-dispatched), which is exactly
  what a host uses to dedup an effect that committed just before a crash.

`--suspend-capability` is a shell affordance for driving the demo. A real host
returns `AHFL_CAP_PENDING` from its own `ahfl_host.h` implementation — see
[`docs/reference/host-abi.zh.md`](../../docs/reference/host-abi.zh.md) and
[`docs/spec/core-language.zh.md`](../../docs/spec/core-language.zh.md) §3.4.1.
