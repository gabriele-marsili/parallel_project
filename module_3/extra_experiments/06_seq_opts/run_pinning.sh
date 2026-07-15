#!/bin/bash
# Esp.6 follow-up — test dell'ipotesi pinning sul residuo 634 vs 566 ms.
# Il binario OpenMP gira con OMP_PROC_BIND=close (inchiodato al core),
# seq_opts no. Qui seq_opts (pf 12/8) viene rilanciato:
#   a) libero (controllo, come nel run originale)
#   b) taskset -c 0 (stesso core del thread OpenMP a T=1)
#   c) numactl --cpunodebind=0 --membind=0 (se disponibile: pinning anche della memoria)
# più omp T=1 come riferimento nello stesso job. Se il gap scatter si chiude
# in (b)/(c), il residuo era migrazione/NUMA; se resta, punta al codegen.
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
SARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -pf-scatter 12 -pf-probe 8 -reps $REPS"

echo "[1/4] seq_opts libero (controllo)"
./seq_opts $SARGS > "$OUT/pinning_free.csv"

echo "[2/4] seq_opts taskset -c 0"
taskset -c 0 ./seq_opts $SARGS > "$OUT/pinning_taskset.csv"

if command -v numactl >/dev/null 2>&1; then
  echo "[3/4] seq_opts numactl --cpunodebind=0 --membind=0"
  numactl --cpunodebind=0 --membind=0 ./seq_opts $SARGS > "$OUT/pinning_numactl.csv"
else
  echo "[3/4] numactl non disponibile, salto"
fi

echo "[4/4] omp loop T=1 (riferimento, stesso job)"
./omp_ablation -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P \
    -t 1 -mode loop -reps $REPS > "$OUT/pinning_omp_t1.csv"

echo "[ok] $(hostname): $OUT/pinning_*.csv"
