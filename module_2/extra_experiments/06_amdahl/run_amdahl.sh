#!/bin/bash
# Esp.6 — misura la frazione seriale LETTERALE (merge+prefix) al variare di k.
# Su compute node Ivy Bridge (salloc).
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

echo "[build] su $(hostname)"
g++ -O3 -std=c++20 -march=native -pthread -Wall -Wextra -I"$INC" serial_fraction.cpp -o serial_fraction

SEED=42; NR=10000000; NS=20000000; MK=1000000

# frazione seriale letterale a P=128 e P=512 (per l'argomento "f cala con P")
for P in 128 512; do
  CSV="$OUT/serial_fraction_p${P}.csv"
  echo "threads,total_ms,serial_ms,serial_frac" > "$CSV"
  echo "[serial] P=$P"
  for T in 1 2 4 8 12 16 20 24 32; do
    ./serial_fraction -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -reps 5 >> "$CSV"
    echo "   t=$T done"
  done
done
echo "[ok] $(hostname): $OUT/serial_fraction_p128.csv  serial_fraction_p512.csv"
