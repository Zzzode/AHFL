#!/bin/sh
if [ ! -f "$1" ]; then
    echo "missing model file: $1" >&2
    exit 2
fi

spec_count=$(grep -c '^LTLSPEC' "$1")
index=1
while [ "$index" -le "$spec_count" ]; do
    echo "-- specification LTLSPEC AHFL_FAKE_${index} is true"
    index=$((index + 1))
done
