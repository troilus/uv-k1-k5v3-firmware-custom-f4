#!/usr/bin/env bash
# Host tests for the AIS decoder: synthesize over-the-air-like 38.4 kHz
# discriminator audio with gen_ais.py (golden fields via pyais), demodulate
# it with the freestanding decoder, and diff against the golden output.
#
#   CC=gcc PY=python3 ./run_tests.sh
set -euo pipefail
cd "$(dirname "$0")"

CC=${CC:-gcc}
PY=${PY:-python3}

mkdir -p out
$PY gen_ais.py out

$CC -O2 -Wall -Wextra -o out/host_ais host_ais.c ../ais_decode.c

rc=0
for v in clean noise dc clock multi badcrc type5 real; do
  if ./out/host_ais "out/$v.u16" > "out/$v.got" 2>/dev/null; then :; fi
  if diff -u "out/$v.exp" "out/$v.got"; then
    echo "PASS  $v"
  else
    echo "FAIL  $v"; rc=1
  fi
done
exit $rc
