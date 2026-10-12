# AHFL Execution Demo

Use this incident-triage workflow to normalize a request, choose self-service
or on-call handling, and draft a response through an OpenAI-compatible LLM
endpoint. You need a built or installed `ahflc` and access to a model service.

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
cd examples/execution-demo
mkdir -p ~/.ahfl
test -e ~/.ahfl/llm_config.json || cp llm_config.example.json ~/.ahfl/llm_config.json
# edit ~/.ahfl/llm_config.json: set endpoint, model, and credentials
```

```bash
../../build/dev/src/tooling/cli/ahflc run
../../build/dev/src/tooling/cli/ahflc run --profile low-risk
```

The template only requires endpoint, model, and the credential reference. Token
budgets and JSON mode have built-in defaults; omitted temperature uses the
model service default. See [the execution tutorial](../../docs/tutorials/execution.zh.md)
for optional overrides.

The manifest supplies the workflow entry, the default input, and the
`low-risk` profile. This example reads `~/.ahfl/llm_config.json` by default.
To choose a different provider for one run, use `--llm-config`; the full
configuration order is documented in the [execution tutorial](../../docs/tutorials/execution.zh.md).

The `endpoint` value must be the base URL to which `/chat/completions` is
appended. OpenAI-compatible relays that serve `/v1/chat/completions`
therefore need the `/v1` suffix in `endpoint`
(e.g. `https://your-relay.example.com/v1`).

### Credentials in the global config

The global file lives outside the repository and is never committed. It can
name an explicit secret handle (`api_key_secret`, see below) or carry the
provider key directly in the `api_key` field. The
`llm_config.example.json` template reads the key from an environment variable:

```json
"api_key_secret": "env:AHFL_LLM_API_KEY"
```

To use the template unchanged, export the named variable instead of editing
the file:

```bash
export AHFL_LLM_API_KEY='...'
../../build/dev/src/tooling/cli/ahflc run --llm-config llm_config.example.json
```

Keep credentials in `~/.ahfl/llm_config.json` or the named environment
variable. Do not add a real API key to a file you commit.

A successful run prints the response produced by `ResponderAgent`. To inspect
node and capability events as structured data, add `--output-format json`.
Connection, authentication, or model-output errors appear in the execution
report; check the endpoint, key, and model before retrying.

## Inspect the workflow without calling the model

From `examples/execution-demo`, run:

```bash
../../build/dev/src/tooling/cli/ahflc check
../../build/dev/src/tooling/cli/ahflc emit summary
../../build/dev/src/tooling/cli/ahflc emit execution-plan
```
