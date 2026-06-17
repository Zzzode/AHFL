#!/bin/sh
if [ ! -f "$1" ]; then
    echo "missing model file: $1" >&2
    exit 2
fi

echo "*** PARSE ERROR: fake checker integration failure" >&2
exit 3
