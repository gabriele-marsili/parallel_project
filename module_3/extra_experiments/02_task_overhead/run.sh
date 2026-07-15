#!/bin/bash
# Esp.2 — costo del modello a task sulle fasi regolari (histogram/scatter) e
# necessità del nowait sul single. Il report attribuisce il gap loop-vs-task
# (fino a 31% a T=16 uniforme) al dispatch dei task: qui lo si isola variando
# il numero di task per fase a T fisso, e si misura il nowait con un binario
# compilato senza (stesso sorgente, -DNO_NOWAIT).
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

export OMP_PROC_BIND=close OMP_PLACES=cores

echo "[build] omp_ablation + omp_ablation_nonowait (-march=native su $(hostname))"
g++ -O3 -std=c++20 -march=native -Wall -Wextra -fopenmp -I"$INC" \
    ../common/omp_ablation.cpp -o omp_ablation
g++ -O3 -std=c++20 -march=native -Wall -Wextra -fopenmp -I"$INC" -DNO_NOWAIT \
    ../common/omp_ablation.cpp -o omp_ablation_nonowait

SEED=42; NR=10000000; NS=20000000; MK=5000000; P=128; REPS=5

# --- 1) sweep del numero di task per fase, T=16 uniforme ---
# tchunks=16 replica il consegnato (un task per thread); valori più alti
# aumentano i dispatch a lavoro totale identico. Il loop è il riferimento.
CSV="$OUT/task_chunks.csv"
: > "$CSV"; first=1
echo "[1/2] sweep tchunks (task, T=16 uniforme) + riferimento loop"
for TC in 16 32 64 128 256 512 1024; do
  ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t 16 -mode task -tchunks $TC -reps $REPS"
  if [ $first = 1 ]; then ./omp_ablation $ARGS >> "$CSV"; first=0
  else ./omp_ablation $ARGS | tail -n +2 >> "$CSV"; fi
  echo "   tchunks=$TC done"
done
./omp_ablation -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t 16 -mode loop -reps $REPS | tail -n +2 >> "$CSV"

# --- 2) nowait on/off sul single del join task ---
# Senza nowait i T-1 worker aspettano alla barriera implicita del single
# finché l'emissione dei task non è finita, invece di consumarli subito.
CSV2="$OUT/nowait.csv"
: > "$CSV2"; first=1
echo "[2/2] nowait vs no-nowait (task) su T x workload"
for T in 8 16 32; do
  for WL in uniform skew; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode task -reps $REPS"
    [ "$WL" = skew ] && ARGS="$ARGS -skew 0.9 -hot 4"
    if [ $first = 1 ]; then ./omp_ablation $ARGS >> "$CSV2"; first=0
    else ./omp_ablation $ARGS | tail -n +2 >> "$CSV2"; fi
    # il binario no-nowait viene marcato riscrivendo la colonna mode
    ./omp_ablation_nonowait $ARGS | tail -n +2 | sed 's/^task,/task_nonowait,/' >> "$CSV2"
  done
  echo "   T=$T done"
done

echo "[ok] $(hostname): $CSV  $CSV2"
