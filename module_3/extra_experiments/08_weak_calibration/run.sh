#!/bin/bash
# Esp. 8 — la calibrazione del weak scaling del report.
#
# Il weak del report scala NR con T ma tiene fissi max_key e P. Tre bracci a
# parita' di lavoro nominale per thread (2M tuple R), che separano gli effetti:
#
#   A  max_key = 5M,    P = 128      alpha cala,      tabella cresce  (= report)
#   B  max_key = 5M*T,  P = 128      alpha costante,  tabella cresce
#   C  max_key = 5M*T,  P = 128*T    alpha costante,  tabella costante
#
# A vs B isola il load factor, B vs C isola il footprint della tabella,
# C e' il weak scaling iso-granulare.
#
# T=20 e' escluso: P=128*T non sarebbe potenza di due e compute_shift()
# (common.hpp) assume P potenza di due.
#
# Da eseguire su un compute node Ivy Bridge (dentro sbatch/salloc esclusivo).
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

export OMP_PROC_BIND=close OMP_PLACES=cores

echo "[build] omp_ablation + alpha_probe (-march=native su $(hostname))"
g++ -O3 -std=c++20 -march=native -Wall -Wextra -fopenmp -I"$INC" \
    ../common/omp_ablation.cpp -o omp_ablation
g++ -O3 -std=c++20 -march=native -Wall -Wextra -I"$INC" \
    alpha_probe.cpp -o alpha_probe

SEED=42; BASE_NR=2000000; BASE_MK=5000000; BASE_P=128; REPS=5
THREADS="1 2 4 8 16 32"

arm_params() { # $1 = arm, $2 = T  ->  imposta MK e P
  case "$1" in
    A) MK=$BASE_MK;          P=$BASE_P ;;
    B) MK=$((BASE_MK * $2)); P=$BASE_P ;;
    C) MK=$((BASE_MK * $2)); P=$((BASE_P * $2)) ;;
  esac
}

# --- 1) weak scaling end-to-end sui tre bracci ---
CSV="$OUT/weak_calib.csv"
: > "$CSV"; first=1
echo "[1/2] weak end-to-end: 3 bracci x T x {loop,task}, uniform, reps=$REPS"
for ARM in A B C; do
  for T in $THREADS; do
    arm_params "$ARM" "$T"
    NR=$((BASE_NR * T)); NS=$((NR * 2))
    for MODE in loop task; do
      TMP=$(mktemp)
      ./omp_ablation -nr "$NR" -ns "$NS" -seed "$SEED" -max-key "$MK" \
                     -p "$P" -t "$T" -mode "$MODE" -reps "$REPS" > "$TMP"
      if [ $first = 1 ]; then
        head -1 "$TMP" | awk '{print "arm,max_key,"$0}' > "$CSV"; first=0
      fi
      tail -n +2 "$TMP" | awk -v a="$ARM" -v mk="$MK" '{print a","mk","$0}' >> "$CSV"
      rm -f "$TMP"
    done
    echo "   arm=$ARM T=$T NR=$NR max_key=$MK P=$P done"
  done
done

# --- 2) alpha e costo per chiave, thread singolo ---
# Chiude il cerchio: il braccio A deve mostrare alpha che crolla, B e C no.
CSV2="$OUT/alpha_probe.csv"
: > "$CSV2"; first=1
echo "[2/2] alpha effettivo e ns per chiave (thread singolo, 8 partizioni)"
for ARM in A B C; do
  for T in $THREADS; do
    arm_params "$ARM" "$T"
    NR=$((BASE_NR * T)); NS=$((NR * 2))
    TMP=$(mktemp)
    ./alpha_probe -nr "$NR" -ns "$NS" -seed "$SEED" -max-key "$MK" \
                  -p "$P" -npart 8 -reps 3 > "$TMP"
    if [ $first = 1 ]; then
      head -1 "$TMP" | awk '{print "arm,threads,"$0}' > "$CSV2"; first=0
    fi
    tail -n +2 "$TMP" | awk -v a="$ARM" -v t="$T" '{print a","t","$0}' >> "$CSV2"
    rm -f "$TMP"
    echo "   arm=$ARM T=$T done"
  done
done

echo "[ok] $(hostname): $CSV  $CSV2"
