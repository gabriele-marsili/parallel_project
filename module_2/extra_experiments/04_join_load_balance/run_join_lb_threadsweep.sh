#!/bin/bash
# Esp.4 (extra) — thread sweep: tempo della fase join vs numero di thread, per strategia,
# sotto carico uniforme (skew=0) e skewed (0.9, hot=4). Su compute node Ivy Bridge.
#
# I CSV consegnati (join_lb.csv, join_lb_sweep.csv) fissano threads=16 e variano strategia/skew.
# Questo script varia INVECE il numero di thread, per mostrare come scala la fase join.
# Produce results/join_lb_threadsweep.csv, poi in locale: python3 plot_join_lb_threads.py
#
# Come lanciarlo sul cluster (partizione normal, MaxTime 30 min; questo gira in ~3 min):
#   srun --partition=normal --nodes=1 --ntasks=1 --cpus-per-task=32 --exclusive \
#        --time=00:15:00 bash run_join_lb_threadsweep.sh
# Oppure via sbatch (aggiungendo l'header #SBATCH corrispondente).
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

echo "[build] su $(hostname) (compilo sul nodo per -march=native)"
g++ -O3 -std=c++20 -march=native -pthread -Wall -Wextra -I"$INC" join_lb.cpp -o join_lb

SEED=42; NR=10000000; NS=20000000; P=128; MK=1000000; REPS=8
THREADS="1 2 4 8 12 16 20 24 32"
HDR="strategy,skew,hot,threads,P,wall_mean_ms,wall_std_ms,imbalance,sched_ms,max_part_frac,join_count"

CSV="$OUT/join_lb_threadsweep.csv"
echo "$HDR" > "$CSV"

for PAIR in "0.0 4" "0.9 4"; do
  RHO="${PAIR% *}"; HOT="${PAIR#* }"
  echo "[skew=$RHO hot=$HOT] sweep thread $THREADS"
  for STRAT in cyclic block dynamic lpt; do
    for T in $THREADS; do
      ./join_lb -strategy "$STRAT" -nr $NR -ns $NS -seed $SEED -max-key $MK \
                -p $P -t "$T" -skew "$RHO" -hot "$HOT" -reps $REPS >> "$CSV"
    done
    echo "   $STRAT done (skew=$RHO)"
  done
done

echo "[ok] $(hostname): $CSV"
