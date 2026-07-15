#!/bin/bash
# Esp.1 — baseline di partenza vs mie modifiche, contributo isolato di ogni cambiamento.
# Da eseguire SUL compute node Ivy Bridge (dentro salloc), come il report M2.
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

echo "[build] seq_ablation (-march=native su $(hostname))"
g++ -O3 -std=c++20 -march=native -Wall -Wextra -I"$INC" seq_ablation.cpp -o seq_ablation

SEED=42; NR=10000000; NS=20000000; P=128; REPS=5

# --- 1) 4 varianti al punto operativo del report (max_key=1M) ---
CSV="$OUT/ablation_p128.csv"
echo "hash,map,NR,NS,P,max_key,hist_ms,scatter_ms,join_ms,total_ms,join_count" > "$CSV"
echo "[1/2] 4 varianti {mod,fib}x{umap,flat}  NR=$NR P=$P max_key=1M"
for H in mod fib; do for M in umap flat; do
  ./seq_ablation -nr $NR -ns $NS -seed $SEED -max-key 1000000 -p $P -hash $H -map $M -reps $REPS >> "$CSV"
  echo "   $H/$M done"
done; done

# --- 2) sweep densita chiavi: dove FlatCountMap conta di piu (chiavi quasi uniche) ---
# confronto umap vs flat a hash fissa (fib), su tutte le densita di max_key.
CSV2="$OUT/ablation_density.csv"
echo "hash,map,NR,NS,P,max_key,hist_ms,scatter_ms,join_ms,total_ms,join_count" > "$CSV2"
echo "[2/2] sweep max_key (umap vs flat, hash=fib)"
for MK in 100 1000 10000 100000 1000000 10000000; do
  for M in umap flat; do
    ./seq_ablation -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -hash fib -map $M -reps $REPS >> "$CSV2"
  done
  echo "   max_key=$MK done"
done

echo "[ok] $(hostname): $CSV  $CSV2"
