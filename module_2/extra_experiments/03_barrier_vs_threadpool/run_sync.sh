#!/bin/bash
# Esp.3 — barrier vs thread pool+queue. Su compute node Ivy Bridge (salloc).
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

echo "[build] su $(hostname)"
g++ -O3 -std=c++20 -march=native -pthread -Wall -Wextra -I"$INC" sync_bench.cpp -o sync_bench

SEED=42; NR=10000000; NS=20000000; P=128

# --- (1) end-to-end sulla pipeline reale: barrier vs pool, al variare di k ---
CSV="$OUT/pipeline.csv"
echo "sync,threads,NR,NS,P,total_ms,join_count" > "$CSV"
echo "[1/2] pipeline barrier vs pool"
for T in 1 2 4 8 16 20 24 32; do
  for S in barrier pool; do
    ./sync_bench -sync $S -t $T -nr $NR -ns $NS -seed $SEED -max-key 1000000 -p $P -reps 5 >> "$CSV"
  done
  echo "   t=$T done"
done

# --- (2) costo puro di un confine di fase (fasi vuote) ---
CSV2="$OUT/microsync.csv"
echo "sync,threads,rounds,ns_per_phase_sync" > "$CSV2"
echo "[2/2] microsync (costo puro per barriera)"
for T in 1 2 4 8 16 20 24 32; do
  for S in barrier pool; do
    ./sync_bench -sync $S -t $T -microsync 200000 -reps 7 >> "$CSV2"
  done
  echo "   t=$T done"
done

echo "[ok] $(hostname): $CSV  $CSV2"
