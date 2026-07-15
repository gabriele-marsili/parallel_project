#!/bin/bash
# Esp.3 — decomposizione del vantaggio della variante task sotto skew:
# quanto viene dall'ordinamento LPT e quanto dallo split intra-partizione.
# Più: verifica empirica del tetto strutturale sul join (senza split la
# partizione hot più pesante, ~22.6% del lavoro, limita lo speedup del join
# a ~1/0.226 = 4.4x) e sensibilità alla soglia hot (hotmul).
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
SKEW="-skew 0.9 -hot 4"

# --- 1) ordine x split, T=16 skew: la matrice 3x2 che separa i contributi ---
CSV="$OUT/order_split.csv"
: > "$CSV"; first=1
echo "[1/3] order {lpt,natural,random} x split {on,off}, T=16 skew"
for ORDER in lpt natural random; do
  for HM in 8 0; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t 16 -mode task -order $ORDER -hotmul $HM $SKEW -reps $REPS"
    if [ $first = 1 ]; then $BIN $ARGS >> "$CSV"; first=0
    else $BIN $ARGS | tail -n +2 >> "$CSV"; fi
  done
  echo "   order=$ORDER done"
done

# --- 2) tetto H/T: speedup del join vs T, split on/off, + loop dynamic1 ---
CSV2="$OUT/ceiling.csv"
: > "$CSV2"; first=1
echo "[2/3] sweep T, task split on/off + loop dynamic1, skew"
for T in 1 2 4 8 16 32; do
  for HM in 8 0; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode task -order lpt -hotmul $HM $SKEW -reps $REPS"
    if [ $first = 1 ]; then $BIN $ARGS >> "$CSV2"; first=0
    else $BIN $ARGS | tail -n +2 >> "$CSV2"; fi
  done
  $BIN -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode loop -sched dynamic1 $SKEW -reps $REPS | tail -n +2 >> "$CSV2"
  echo "   T=$T done"
done

# --- 3) sensibilità alla soglia hot: hotmul sweep a T=16 skew ---
CSV3="$OUT/hotmul_sweep.csv"
: > "$CSV3"; first=1
echo "[3/3] hotmul {1,2,4,8,16,32,64}, T=16 skew"
for HM in 1 2 4 8 16 32 64; do
  ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t 16 -mode task -order lpt -hotmul $HM $SKEW -reps $REPS"
  if [ $first = 1 ]; then $BIN $ARGS >> "$CSV3"; first=0
  else $BIN $ARGS | tail -n +2 >> "$CSV3"; fi
  echo "   hotmul=$HM done"
done

echo "[ok] $(hostname): $CSV  $CSV2  $CSV3"
