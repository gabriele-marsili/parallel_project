#!/bin/bash
# Esp.5 — NUMA first-touch dei buffer di partizione. Il codice consegnato fa
# la prima scrittura in parallel for schedule(static) così ogni pagina finisce
# sul nodo NUMA del thread che poi vi scrive nello scatter. Ablation: first
# touch sequenziale (tutte le pagine sul nodo del master) e, se numactl è
# disponibile, policy interleave.
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

export OMP_PROC_BIND=close OMP_PLACES=cores

echo "[build] omp_ablation (-march=native su $(hostname))"
g++ -O3 -std=c++20 -march=native -Wall -Wextra -fopenmp -I"$INC" \
    ../common/omp_ablation.cpp -o omp_ablation

SEED=42; NR=10000000; NS=20000000; MK=5000000; P=128; REPS=5
BIN=./omp_ablation

CSV="$OUT/firsttouch.csv"
: > "$CSV"; first=1
echo "[1/1] firsttouch {par,seq} x T {8,16,32} x workload {uniform,skew} (+interleave se numactl)"
for T in 8 16 32; do
  for FT in par seq; do
    for WL in uniform skew; do
      ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode loop -firsttouch $FT -reps $REPS"
      [ "$WL" = skew ] && ARGS="$ARGS -skew 0.9 -hot 4"
      if [ $first = 1 ]; then $BIN $ARGS >> "$CSV"; first=0
      else $BIN $ARGS | tail -n +2 >> "$CSV"; fi
    done
  done
  # interleave: la policy di allocazione round-robin sovrascrive il first-touch
  if command -v numactl >/dev/null 2>&1; then
    for WL in uniform skew; do
      ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode loop -firsttouch par -reps $REPS"
      [ "$WL" = skew ] && ARGS="$ARGS -skew 0.9 -hot 4"
      numactl --interleave=all $BIN $ARGS | tail -n +2 | sed 's/,par,/,interleave,/' >> "$CSV"
    done
  fi
  echo "   T=$T done"
done

echo "[ok] $(hostname): $CSV"
