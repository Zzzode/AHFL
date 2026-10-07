#!/bin/sh
# Fake NuSMV-compatible checker for the bounded-model-checking path that
# REFUTES the first LTLSPEC. Mirrors fake_smv_checker_bmc_pass.sh but emits
# a BMC counterexample for the first specification instead of a bounded pass.
set -u

script=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        -source)
            script="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

if [ -z "$script" ] || [ ! -f "$script" ]; then
    echo "missing checker source script: ${script:-<none>}" >&2
    exit 2
fi

model=$(sed -n 's/^read_model -i //p' "$script" | head -n 1)

if [ ! -f "$model" ]; then
    echo "missing model file: $model" >&2
    exit 2
fi

ltl_count=$(grep -c '^LTLSPEC' "$model")
invar_count=$(grep -c '^INVARSPEC' "$model")

# Refute the first LTLSPEC with a BMC counterexample.
if [ "$ltl_count" -ge 1 ]; then
    echo "-- specification LTLSPEC G FALSE is false"
    echo "-- as demonstrated by the following execution sequence"
    echo "Trace Description: BMC Counterexample"
    echo "Trace Type: Counterexample"
    echo "-> State: 1.1 <-"
    echo "  workflow__formal_flow_workflow_ReviewWorkflow__node__first__phase = AHFL_NODE_RUNNING"
    echo "-> State: 1.2 <-"
    echo "  workflow__formal_flow_workflow_ReviewWorkflow__node__first__phase = AHFL_NODE_FAILED"
fi

# Remaining LTLSPECs pass boundedly.
index=2
while [ "$index" -le "$ltl_count" ]; do
    echo "-- no counterexample found with bound 0"
    index=$((index + 1))
done

# Invariants pass.
index=1
while [ "$index" -le "$invar_count" ]; do
    echo "-- invariant AHFL_FAKE_INVAR_${index} is true"
    index=$((index + 1))
done
