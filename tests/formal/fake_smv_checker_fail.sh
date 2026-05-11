#!/bin/sh
if [ ! -f "$1" ]; then
    echo "missing model file: $1" >&2
    exit 2
fi

echo "-- specification LTLSPEC G FALSE is false"
echo "Trace Description: LTL Counterexample"
echo "Trace Type: Counterexample"
echo "-> State: 1.1 <-"
