# AHFL Execution Demo

This example is an incident-triage workflow that executes one real LLM-backed
capability. It uses GLM through an OpenAI-compatible chat-completions endpoint
and reads the API token from `llm_config.glm.json`.

The workflow spans multiple AHFL modules:

- `types.ahfl` defines the request, output, and enum schemas.
- `intake.ahfl` contains `IntakeAgent`, which normalizes the incoming incident request.
- `decision.ahfl` contains `DecisionAgent`, which chooses `SelfServe` or `OnCall`.
- `response.ahfl` contains `ResponderAgent`, which calls `DraftIncidentSummary`
  through the configured LLM provider and then composes the final response.
- `main.ahfl` wires the three agents into `IncidentWorkflow`.

The example config is a local GLM config with an inline `api_key`. Treat it as a
machine-local secret file and do not publish it.

If you need to refresh it from your Claude GLM config, copy
`env.ANTHROPIC_AUTH_TOKEN` from `~/.claude/glm.json` into
`examples/execution-demo/llm_config.glm.json`.

Run the high-severity path:

```bash
cd examples/execution-demo
../../build/dev/src/tooling/cli/ahflc run \
  --input "$(tr -d '\n' < inputs/high-severity.json)" \
  --llm-config llm_config.glm.json
```

Run the low-risk path:

```bash
cd examples/execution-demo
../../build/dev/src/tooling/cli/ahflc run \
  --input "$(tr -d '\n' < inputs/low-risk.json)" \
  --llm-config llm_config.glm.json
```

Useful inspection commands:

```bash
cd examples/execution-demo
../../build/dev/src/tooling/cli/ahflc check
../../build/dev/src/tooling/cli/ahflc emit summary
../../build/dev/src/tooling/cli/ahflc emit execution-plan
```
