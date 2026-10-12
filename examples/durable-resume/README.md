# Suspend and resume a workflow

Use this example to save a workflow while a capability result is pending,
then resume it in a separate process. You need `ahflc` on `PATH` and a writable
recovery-store path.

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
REPLY_DEMO_DIR=$(mktemp -d)
printf '%s\n' '{"endpoint":"http://127.0.0.1:1/v1","model":"unused","api_key_secret":"env:AHFL_RESUME_DEMO_KEY"}' > "$REPLY_DEMO_DIR/llm.json"
export AHFL_RESUME_DEMO_KEY=unused
WF=durable_resume::main::ReplyWorkflow

# 1. Run. --suspend-capability forces DraftReply to PENDING, so the workflow
#    suspends and writes a resume record to the recovery store. Exit 0: the run
#    is durably parked, not failed.
ahflc run --manifest ahfl.toml --workflow "$WF" --llm-config "$REPLY_DEMO_DIR/llm.json" \
  --suspend-capability durable_resume::main::DraftReply \
  --recovery-store "$REPLY_DEMO_DIR/reply.snapshot"

# 2. Resume — a fresh process whose only link to run 1 is the snapshot on disk.
#    --resume-pending-result supplies what the host eventually drafted. The
#    workflow replays from the memo, injects this result at the pending call,
#    and completes. DraftReply is NOT re-invoked.
ahflc run --manifest ahfl.toml --workflow "$WF" --llm-config "$REPLY_DEMO_DIR/llm.json" \
  --recovery-store "$REPLY_DEMO_DIR/reply.snapshot" \
  --resume-pending-result '{"_type":"durable_resume::main::Reply","id":"T-42","answer":"Use the reset link."}'
```

Run 2 completes with `{"id":"T-42","answer":"Use the reset link."}` — the
injected reply, identical to what a synchronous run that returned it directly
would produce.

## Inspect the recovery behavior

- **Write capability calls normally.** `DraftReply(...)` is a plain call in
  the `.ahfl` source; you do not need to add `async` or `await` to suspend it.
- **Durable, cross-process.** The resume record is pure serializable data
  that can be stored on disk. Run 2 shares nothing with run 1 but
  the on-disk snapshot.
- **Reuse completed results.** On resume the completed calls replay from the
  memo and are never re-invoked; the final result is deterministic. `DraftReply`
  is declared `durable_write`, so a write-ahead intent (a stable idempotency key)
  is recorded before dispatch — add `--intent-log "$REPLY_DEMO_DIR/intent.jsonl"` to either
  run to capture it. The key is logged once on the suspend run and NOT re-logged
  on resume (the memoized call is replayed, not re-dispatched), which is exactly
  what a host uses to deduplicate an effect that committed just before a crash.
  The host must honor that key; the recovery record alone cannot guarantee that
  an external write happens only once.

Use `--suspend-capability` to simulate a pending result from the CLI. A real host
returns `AHFL_CAP_PENDING` from its own `ahfl_host.h` implementation — see
[`docs/reference/host-abi.zh.md`](../../docs/reference/host-abi.zh.md) and
[`docs/spec/core-language.zh.md`](../../docs/spec/core-language.zh.md) §3.4.1.
