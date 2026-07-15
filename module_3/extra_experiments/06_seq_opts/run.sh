#!/bin/bash
# Esp.6 — la superlinearità apparente (eff ~1.22 a T=4 nel report) è un
# artefatto della baseline: il binario OpenMP contiene il prefetch software,
# hashjoin_seq no. Qui la baseline sequenziale riceve le stesse ottimizzazioni
# e si rimisura il rapporto a T=1: se l'offset 1.44x si riduce a ~1, la
# spiegazione è confermata.
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

export OMP_PROC_BIND=close OMP_PLACES=cores

echo "[build] seq_opts + omp_ablation (-march=native su $(hostname))"
g++ -O3 -std=c++20 -march=native -Wall -Wextra -I"$INC" \
    ../common/seq_opts.cpp -o seq_opts
g++ -O3 -std=c++20 -march=native -Wall -Wextra -fopenmp -I"$INC" \
    ../common/omp_ablation.cpp -o omp_ablation

SEED=42; NR=10000000; NS=20000000; MK=5000000; P=128; REPS=5

# --- 1) baseline seq: senza opt, con prefetch scatter, con probe, con entrambi ---
CSV="$OUT/seq_variants.csv"
: > "$CSV"; first=1
echo "[1/2] seq con pf {0/0, 12/0, 0/8, 12/8}, uniforme"
for CFG in "0 0" "12 0" "0 8" "12 8"; do
  set -- $CFG
  ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -pf-scatter $1 -pf-probe $2 -reps $REPS"
  if [ $first = 1 ]; then ./seq_opts $ARGS >> "$CSV"; first=0
  else ./seq_opts $ARGS | tail -n +2 >> "$CSV"; fi
  echo "   pf=$1/$2 done"
done

# --- 2) OpenMP loop a T basso per il rapporto con le due baseline ---
CSV2="$OUT/omp_lowT.csv"
: > "$CSV2"; first=1
echo "[2/2] omp loop T {1,2,4}, uniforme"
for T in 1 2 4; do
  ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode loop -reps $REPS"
  if [ $first = 1 ]; then ./omp_ablation $ARGS >> "$CSV2"; first=0
  else ./omp_ablation $ARGS | tail -n +2 >> "$CSV2"; fi
  echo "   T=$T done"
done

echo "[ok] $(hostname): $CSV  $CSV2"
