#!/bin/bash
# Esp.2 — anatomia FlatCountMap + false sharing. Su compute node Ivy Bridge (salloc).
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

echo "[build] su $(hostname)"
g++ -O3 -std=c++20 -march=native -Wall -Wextra -I"$INC" flatmap_bench.cpp -o flatmap_bench
g++ -O3 -std=c++20 -march=native -Wall -Wextra loadfactor_bench.cpp -o loadfactor_bench
g++ -O3 -std=c++20 -march=native -pthread -Wall -Wextra false_sharing.cpp -o false_sharing

SEED=42

# --- (a)(b) impl a parita di dimensione partizione tipica (P=512 -> ~20k distinct/part) ---
# nR ~ 40k, nS ~ 80k per partizione (10M/256 ~ 40k). distinct=20k come al P ottimale.
CSV="$OUT/flatmap_impl.csv"
echo "impl,distinct,nR,nS,table_bytes,build_ns_per_key,probe_ns_per_key,join_count" > "$CSV"
echo "[1/2] impl {umap,flat1,flat2,flat4} @ distinct=20k"
for IMPL in umap flat1 flat2 flat4; do
  ./flatmap_bench -impl $IMPL -distinct 20000 -nr 40000 -ns 80000 -seed $SEED -reps 9 >> "$CSV"
  echo "   $IMPL done"
done

# --- (c) residenza cache: cresce distinct -> cresce la tabella (attraversa L2 poi L3) ---
CSV2="$OUT/flatmap_cache.csv"
echo "impl,distinct,nR,nS,table_bytes,build_ns_per_key,probe_ns_per_key,join_count" > "$CSV2"
echo "[cache] sweep distinct (flat2 vs umap): tabella da <L2 a >L3"
for D in 1000 4000 16000 64000 262144 1048576 4194304 16777216; do
  NR=$((D * 2)); NS=$((D * 4))
  for IMPL in flat2 umap; do
    ./flatmap_bench -impl $IMPL -distinct $D -nr $NR -ns $NS -seed $SEED -reps 5 >> "$CSV2"
  done
  echo "   distinct=$D done"
done

# --- load factor: probe vs alpha (patologia del linear probing vicino al 100%) ---
CSV4="$OUT/loadfactor.csv"
echo "alpha_target,alpha_actual,n_distinct,table_slots,probe_ns_per_key" > "$CSV4"
echo "[lf] sweep load factor (D=100k distinti)"
for A in 0.10 0.25 0.50 0.70 0.80 0.90 0.95 0.98; do
  ./loadfactor_bench -distinct 100000 -alpha $A -reps 9 >> "$CSV4"
done

# --- false sharing: accumulatori padded vs packed al crescere di k ---
CSV3="$OUT/false_sharing.csv"
echo "mode,threads,iters,time_ms,checksum" > "$CSV3"
echo "[2/2] false sharing padded vs packed"
for T in 1 2 4 8 16 32; do
  for MODE in packed padded; do
    ./false_sharing -mode $MODE -t $T -iters 200000000 -reps 5 >> "$CSV3"
  done
  echo "   t=$T done"
done

echo "[ok] $(hostname): $CSV  $CSV2  $CSV3"
