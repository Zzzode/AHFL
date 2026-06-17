#!/bin/sh
if [ ! -f "$1" ]; then
    echo "missing model file: $1" >&2
    exit 2
fi

sleep 5
echo "-- specification LTLSPEC AHFL_FAKE_TIMEOUT is true"
