# AHFL Execution Demo

This example is the AHFL beta reference workflow. It is an incident-triage
workflow that executes one real LLM-backed capability through an
OpenAI-compatible GLM endpoint.

The workflow spans multiple AHFL modules:

- `types.ahfl` defines the request, output, and enum schemas.
- `intake.ahfl` contains `IntakeAgent`, which normalizes the incoming incident request.
- `decision.ahfl` contains `DecisionAgent`, which chooses `SelfServe` or `OnCall`.
- `response.ahfl` contains `ResponderAgent`, which calls `DraftIncidentSummary`
  through the configured LLM provider and then composes the final response.
- `main.ahfl` wires the three agents into `IncidentWorkflow`.

The committed `llm_config.example.json` contains only an explicit secret handle:

```json
"api_key_secret": "env:AHFL_GLM_API_KEY"
```

Export the key in the process environment. Never write the key into a JSON
configuration file:

```bash
export AHFL_GLM_API_KEY='...'
```

Run the high-severity path:

```bash
cd examples/execution-demo
../../build/dev/src/tooling/cli/ahflc run \
  --input "$(tr -d '\n' < inputs/high-severity.json)" \
  --llm-config llm_config.example.json
```

Run the low-risk path:

```bash
cd examples/execution-demo
../../build/dev/src/tooling/cli/ahflc run \
  --input "$(tr -d '\n' < inputs/low-risk.json)" \
  --llm-config llm_config.example.json
```

Useful inspection commands:

```bash
cd examples/execution-demo
../../build/dev/src/tooling/cli/ahflc check
../../build/dev/src/tooling/cli/ahflc emit summary
../../build/dev/src/tooling/cli/ahflc emit execution-plan
```
