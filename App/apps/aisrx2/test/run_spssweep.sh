#!/usr/bin/env bash
# C decoder on ship162's known-good IQ at several samples/symbol.
cd "$(dirname "$0")"
for sps in 4 8 16; do
  case $sps in 4) fs=38400;; 8) fs=76800;; 16) fs=153600;; esac
  gcc -O2 -DAIS_SPS=$sps -DAIS_FS=${fs}u -o out/df$sps diag_frame.c ../ais_decode.c
  echo "=== C decoder, $sps sps ($fs) on ship162 known IQ chan B ==="
  ./out/df$sps out/ais96k_b_$fs.u16 2>/dev/null
done
