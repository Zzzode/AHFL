#!/bin/sh
# CTest driver for the AHFL_SMV_CHECKER-gated real-NuSMV bounded-model-checking
# test. The test is always registered so `ctest -R real_smv_bmc` selects it in
# every checkout; when no executable checker was configured (or it vanished),
# the driver exits 77 which CTest records as SKIP.
#
# Usage: run_real_smv_bmc.sh <ahflc> <checker> <bmc-depth> <fixture.ahfl>
set -u

ahflc=$1
checker=$2
depth=$3
fixture=$4

if [ -z "${checker}" ] || [ ! -x "${checker}" ]; then
    echo "SKIP: no executable NuSMV/nuXmv checker; set AHFL_SMV_CHECKER" >&2
    exit 77
fi

"${ahflc}" verify \
    --model-checker "${checker}" \
    --bmc-depth "${depth}" \
    "${fixture}"
exit $?
