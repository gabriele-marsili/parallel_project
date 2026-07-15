#!/bin/bash
# Esp.4 — distribuzione del carico nella fase join. Su compute node Ivy Bridge (salloc).
# Misura mean +/- std su MOLTE ripetizioni: serve a dimostrare che le differenze fra
# strategie sono dentro il rumore di misura (il report dichiara +/-3%).
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

echo "[build] su $(hostname)"
g++ -O3 -std=c++20 -march=native -pthread -Wall -Wextra -I"$INC" join_lb.cpp -o join_lb

SEED=42; NR=10000000; NS=20000000; P=128; MK=1000000; T=16
HDR="strategy,skew,hot,threads,P,wall_mean_ms,wall_std_ms,imbalance,sched_ms,max_part_frac,join_count"

CSV="$OUT/join_lb.csv"
echo "$HDR" > "$CSV"
echo "[1/3] uniforme (skew=0), 15 ripetizioni"
for STRAT in cyclic block dynamic lpt; do
  ./join_lb -strategy $STRAT -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -skew 0 -reps 15 >> "$CSV"
  echo "   $STRAT done"
done
echo "[2/3] skew=0.9 hot=4, 15 ripetizioni"
for STRAT in cyclic block dynamic lpt; do
  ./join_lb -strategy $STRAT -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -skew 0.9 -hot 4 -reps 15 >> "$CSV"
  echo "   $STRAT done"
done

CSV2="$OUT/join_lb_sweep.csv"
echo "$HDR" > "$CSV2"
echo "[3/3] sweep skew"
for RHO in 0.0 0.3 0.6 0.8 0.9 0.95; do
  for STRAT in cyclic dynamic lpt; do
    ./join_lb -strategy $STRAT -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -skew $RHO -hot 4 -reps 10 >> "$CSV2"
  done
  echo "   skew=$RHO done"
done

echo "[ok] $(hostname): $CSV  $CSV2"
