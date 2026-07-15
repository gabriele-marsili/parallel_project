#!/bin/bash
# Esp.4 — prefetch software: ablation on/off per fase e sweep della distanza.
# Il report sceglie distanza 12 nello scatter e 8 nel probe e attribuisce al
# prefetch dello scatter gran parte del gap con il Modulo 2 (~56 ms a T=8):
# qui si misura il contributo di ciascun prefetch e la sensibilità alla
# distanza, invece di affermarli.
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

# --- 1) ablation 2x2 (scatter on/off x probe on/off) a T in {1,8,16} ---
CSV="$OUT/prefetch_ablation.csv"
: > "$CSV"; first=1
echo "[1/3] ablation 2x2 x T {1,8,16}, uniforme"
for T in 1 8 16; do
  for PS in 12 0; do
    for PP in 8 0; do
      ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode loop -pf-scatter $PS -pf-probe $PP -reps $REPS"
      if [ $first = 1 ]; then $BIN $ARGS >> "$CSV"; first=0
      else $BIN $ARGS | tail -n +2 >> "$CSV"; fi
    done
  done
  echo "   T=$T done"
done

# --- 2) sweep distanza scatter (probe fisso a 8) ---
CSV2="$OUT/pf_scatter_sweep.csv"
: > "$CSV2"; first=1
echo "[2/3] sweep pf-scatter {0,2,4,8,12,16,24,32,48}, T {1,16}, uniforme"
for T in 1 16; do
  for D in 0 2 4 8 12 16 24 32 48; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode loop -pf-scatter $D -pf-probe 8 -reps $REPS"
    if [ $first = 1 ]; then $BIN $ARGS >> "$CSV2"; first=0
    else $BIN $ARGS | tail -n +2 >> "$CSV2"; fi
  done
  echo "   T=$T done"
done

# --- 3) sweep distanza probe (scatter fisso a 12) ---
CSV3="$OUT/pf_probe_sweep.csv"
: > "$CSV3"; first=1
echo "[3/3] sweep pf-probe {0,2,4,8,12,16,24,32,48}, T {1,16}, uniforme"
for T in 1 16; do
  for D in 0 2 4 8 12 16 24 32 48; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode loop -pf-scatter 12 -pf-probe $D -reps $REPS"
    if [ $first = 1 ]; then $BIN $ARGS >> "$CSV3"; first=0
    else $BIN $ARGS | tail -n +2 >> "$CSV3"; fi
  done
  echo "   T=$T done"
done

echo "[ok] $(hostname): $CSV  $CSV2  $CSV3"
