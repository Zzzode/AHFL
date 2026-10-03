# AHFL Execution Demo

This example is the AHFL beta reference workflow. It is an incident-triage
workflow that executes one real LLM-backed capability through an
OpenAI-compatible endpoint.

The workflow spans multiple AHFL modules:

- `types.ahfl` defines the request, output, and enum schemas.
- `intake.ahfl` contains `IntakeAgent`, which normalizes the incoming incident request.
- `decision.ahfl` contains `DecisionAgent`, which chooses `SelfServe` or `OnCall`.
- `response.ahfl` contains `ResponderAgent`, which calls `DraftIncidentSummary`
  through the configured LLM provider and then composes the final response.
- `main.ahfl` wires the three agents into `IncidentWorkflow`.

## Run it

Configure the LLM provider **once**, in the user-global config file
`~/.ahfl/llm_config.json`, then run with a bare command:

```bash
mkdir -p ~/.ahfl
cp llm_config.example.json ~/.ahfl/llm_config.json
# edit ~/.ahfl/llm_config.json: set endpoint, model, and credentials
```

```bash
cd examples/execution-demo
../../build/dev/src/tooling/cli/ahflc run
../../build/dev/src/tooling/cli/ahflc run --profile low-risk
```

The manifest supplies the workflow entry, the default input, and the
`low-risk` profile. It deliberately does **not** pin `llm_config`, so `run`
falls through to `~/.ahfl/llm_config.json` (config precedence:
`--llm-config` > profile `llm_config` > `[run].llm_config` >
`~/.ahfl/llm_config.json`).

The `endpoint` value must be the base URL to which `/chat/completions` is
appended. OpenAI-compatible relays that serve `/v1/chat/completions`
therefore need the `/v1` suffix in `endpoint`
(e.g. `https://your-relay.example.com/v1`).

### Credentials in the global config

The global file lives outside the repository and is never committed. It can
name an explicit secret handle (`api_key_secret`, see below) or carry the
provider key directly in the `api_key` field. The committed
`llm_config.example.json` is only a template: it uses the env-handle form

```json
"api_key_secret": "env:AHFL_GLM_API_KEY"
```

To use the template unchanged, export the named variable instead of editing
the file:

```bash
export AHFL_GLM_API_KEY='...'
../../build/dev/src/tooling/cli/ahflc run --llm-config llm_config.example.json
```

`--llm-config` also lets a one-off run point at any other config file. A
repository gate verifies the committed template never carries a literal key;
real credentials belong in `~/.ahfl/llm_config.json` or the named
environment variable, never in a committed file.

## Useful inspection commands

```bash
cd examples/execution-demo
../../build/dev/src/tooling/cli/ahflc check
../../build/dev/src/tooling/cli/ahflc emit summary
../../build/dev/src/tooling/cli/ahflc emit execution-plan
```
