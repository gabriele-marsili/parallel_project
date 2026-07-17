#!/bin/bash
# Esp.1 — sensibilità alla schedule del join (loop) estesa oltre il report.
# Il report la misura solo a T=8: qui T in {4,8,16,32}, chunk diversi, e il
# costo del dispatch a granularità fine (P alto = iterazioni piccole).
# Da eseguire su un compute node Ivy Bridge (dentro sbatch/salloc esclusivo).
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

export OMP_PROC_BIND=close OMP_PLACES=cores

echo "[build] omp_ablation (-march=native su $(hostname))"
g++ -O3 -std=c++20 -march=native -Wall -Wextra -fopenmp -I"$INC" \
    ../common/omp_ablation.cpp -o omp_ablation

SEED=42; NR=10000000; NS=20000000; MK=5000000; REPS=5
BIN=./omp_ablation

# --- 1) schedule x T x workload, P=128 (punto operativo del report) ---
CSV="$OUT/schedule_sweep.csv"
: > "$CSV"; first=1
echo "[1/2] sweep schedule x T x workload (P=128)"
for T in 4 8 16 32; do
  for SCHED in static static1 dynamic1 dynamic4 dynamic16 guided1 guided16; do
    for WL in uniform skew; do
      ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p 128 -t $T -mode loop -sched $SCHED -reps $REPS"
      [ "$WL" = skew ] && ARGS="$ARGS -skew 0.9 -hot 4"
      if [ $first = 1 ]; then $BIN $ARGS >> "$CSV"; first=0
      else $BIN $ARGS | tail -n +2 >> "$CSV"; fi
    done
  done
  echo "   T=$T done"
done

# --- 2) granularità fine: P alto assottiglia il lavoro per iterazione ---
# static vs dynamic1 a T=16, su entrambi i workload: su uniforme il gap misura
# l'overhead di dispatch di dynamic quando ogni iterazione è piccola, su skew
# dice se il bilanciamento ripaga quell'overhead al crescere di P.
CSV2="$OUT/schedule_granularity.csv"
: > "$CSV2"; first=1
echo "[2/2] granularità: P in {128,512,2048,4096,8192}, static vs dynamic1, T=16, uniform+skew"
for P in 128 512 2048 4096 8192; do
  for SCHED in static dynamic1; do
    for WL in uniform skew; do
      ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t 16 -mode loop -sched $SCHED -reps $REPS"
      [ "$WL" = skew ] && ARGS="$ARGS -skew 0.9 -hot 4"
      if [ $first = 1 ]; then $BIN $ARGS >> "$CSV2"; first=0
      else $BIN $ARGS | tail -n +2 >> "$CSV2"; fi
    done
  done
  echo "   P=$P done"
done

echo "[ok] $(hostname): $CSV  $CSV2"
