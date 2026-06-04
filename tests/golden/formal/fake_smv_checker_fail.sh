#!/bin/sh
if [ ! -f "$1" ]; then
    echo "missing model file: $1" >&2
    exit 2
fi

echo "-- specification LTLSPEC G FALSE is false"
echo "Trace Description: LTL Counterexample"
echo "Trace Type: Counterexample"
echo "-> State: 1.1 <-"
echo "  workflow__formal_flow_workflow_ReviewWorkflow__node__first__phase = AHFL_NODE_RUNNING"
echo "-> State: 1.2 <-"
echo "  workflow__formal_flow_workflow_ReviewWorkflow__node__first__phase = AHFL_NODE_FAILED"
