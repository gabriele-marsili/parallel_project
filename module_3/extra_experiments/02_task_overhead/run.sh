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

# --- 1) sweep del numero di task per fase x numero di thread, uniforme ---
# tchunks=T replica il consegnato (un task per thread); valori più alti
# aumentano i dispatch a lavoro totale identico. Il loop allo stesso T è il
# riferimento: si varia T per entrambi i modelli, mai per uno solo, altrimenti
# il confronto misurerebbe la scalabilità del loop invece del costo del dispatch.
CSV="$OUT/task_chunks.csv"
: > "$CSV"; first=1
echo "[1/3] sweep tchunks x T x workload (task) + riferimento loop allo stesso T e carico"
for T in 8 16 32; do
  for WL in uniform skew; do
    SK=""; [ "$WL" = skew ] && SK="-skew 0.9 -hot 4"
    for TC in 16 32 64 128 256 512 1024; do
      ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode task -tchunks $TC -reps $REPS $SK"
      if [ $first = 1 ]; then ./omp_ablation $ARGS >> "$CSV"; first=0
      else ./omp_ablation $ARGS | tail -n +2 >> "$CSV"; fi
    done
    ./omp_ablation -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode loop -reps $REPS $SK \
      | tail -n +2 >> "$CSV"
  done
  echo "   T=$T done (uniform+skew)"
done

# --- 2) nowait on/off sul single del join task ---
# Senza nowait i T-1 worker aspettano alla barriera implicita del single
# finché l'emissione dei task non è finita, invece di consumarli subito.
CSV2="$OUT/nowait.csv"
: > "$CSV2"; first=1
echo "[2/3] nowait vs no-nowait (task) su T x workload"
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

# --- 3) controllo di riproducibilità ---
# La stessa configurazione (T=16, 16 task, uniforme) misurata in task_chunks.csv e in
# nowait.csv è risultata sistematicamente diversa (~15%, range delle rep disgiunti) pur
# essendo lo stesso binario sullo stesso nodo. Qui la si rimisura in slot distribuiti nel
# tempo: gli slot pari sono preceduti da un'invocazione del binario nonowait (come accade
# in nowait.csv), i dispari no. Se il tempo dipende dallo slot -> deriva temporale del
# nodo; se dipende dalla parità -> è lo stato lasciato dall'invocazione precedente.
CSV3="$OUT/repro.csv"
: > "$CSV3"; first=1
CANON="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t 16 -mode task -tchunks 16 -reps $REPS"
echo "[3/3] riproducibilità: stessa config in 8 slot (pari = preceduti da nonowait)"
for SLOT in 1 2 3 4 5 6 7 8; do
  if [ $((SLOT % 2)) -eq 0 ]; then
    ./omp_ablation_nonowait $CANON > /dev/null 2>&1   # perturbazione, output scartato
  fi
  if [ $first = 1 ]; then
    ./omp_ablation $CANON | sed "1s/^mode,/slot,mode,/; 2,\$s/^/$SLOT,/" >> "$CSV3"; first=0
  else
    ./omp_ablation $CANON | tail -n +2 | sed "s/^/$SLOT,/" >> "$CSV3"
  fi
  echo "   slot $SLOT done"
done

echo "[ok] $(hostname): $CSV  $CSV2  $CSV3"
