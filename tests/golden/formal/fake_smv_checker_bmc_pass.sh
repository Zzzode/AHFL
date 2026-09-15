#!/bin/sh
# Fake NuSMV-compatible checker for the bounded-model-checking path.
#
# ahflc invokes BMC checkers with `-source <script>`, unlike the BDD path
# which passes the model file directly. This fixture extracts the model path
# and the bound K from the generated source script and emits the exact
# per-bound progress lines NuSMV 2.6.0 prints for a bounded LTL pass:
#
#   -- no counterexample found with bound 0
#   ...
#   -- no counterexample found with bound K
#
# with the bound counter restarting at 0 between specifications (the only
# signal separating consecutive bounded passes). Invariants that survive the
# bound print the een-sorensson inductive-proof line.
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
depth=$(sed -n 's/^check_ltlspec_bmc -k //p' "$script" | head -n 1)

if [ ! -f "$model" ]; then
    echo "missing model file: $model" >&2
    exit 2
fi
case "$depth" in
    ''|*[!0-9]*)
        depth=20
        ;;
esac

ltl_count=$(grep -c '^LTLSPEC' "$model")
invar_count=$(grep -c '^INVARSPEC' "$model")

index=1
while [ "$index" -le "$ltl_count" ]; do
    bound=0
    while [ "$bound" -le "$depth" ]; do
        echo "-- no counterexample found with bound $bound"
        bound=$((bound + 1))
    done
    index=$((index + 1))
done

index=1
while [ "$index" -le "$invar_count" ]; do
    # een-sorensson reports an inductively-proven invariant with the verdict
    # line directly (no per-bound progress lines), matching NuSMV 2.6.0.
    echo "-- invariant AHFL_FAKE_INVAR_${index} is true"
    index=$((index + 1))
done
