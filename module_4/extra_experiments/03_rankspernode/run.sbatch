#!/bin/bash
# Esp.3 — il continuum fra pure MPI (32 rank/nodo) e hybrid (1 rank/nodo),
# che il report testa solo agli estremi. A 4 nodi si varia rank-per-nodo
# in {1,2,4,8,16,32} con thread OpenMP complementari (rpn*T = 32). Il
# candidato naturale a battere entrambi è 2 rank/nodo (uno per socket:
# fan-out piccolo + località NUMA). Driver: threadlevel_bench (riusa la
# pipeline consegnata in mpi_pipeline.hpp, livello funneled).
#SBATCH --job-name=m4x_rpn
#SBATCH --partition=normal
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:30:00
#SBATCH --exclusive
#SBATCH --output=slurm_%x_%j.out
#SBATCH --error=slurm_%x_%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$HOME/module_4/extra_experiments}/03_rankspernode"
BIN=../bin/threadlevel_bench
OUT=results; mkdir -p "$OUT"

NR=50000000; NS=100000000; SEED=42; MK=25000000; P=256; REPS=5
NODES=4

CSV="$OUT/rankspernode.csv"
HEADER_DONE=0

export OMP_PROC_BIND=close OMP_PLACES=cores

for RPN in 1 2 4 8 16 32; do
  T=$((32 / RPN))
  RANKS=$((NODES * RPN))
  export OMP_NUM_THREADS=$T
  for WL in uniform skew; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -reps $REPS"
    [ "$WL" = skew ] && ARGS="$ARGS -skew 0.9 -hot 4"
    echo "[rpn=$RPN T=$T ranks=$RANKS $WL]"
    if [ $HEADER_DONE = 0 ]; then
      srun --nodes=$NODES --ntasks=$RANKS --ntasks-per-node=$RPN \
           --cpus-per-task=$T --mpi=pmix $BIN $ARGS > "$CSV"
      HEADER_DONE=1
    else
      srun --nodes=$NODES --ntasks=$RANKS --ntasks-per-node=$RPN \
           --cpus-per-task=$T --mpi=pmix $BIN $ARGS | tail -n +2 >> "$CSV"
    fi
  done
done

# Cross-check: il binario pure MPI consegnato a 128 rank, uniforme, per
# confermare che threadlevel_bench a (32 rpn, T=1) gli è equivalente.
XCSV="$OUT/crosscheck_pure.txt"
: > "$XCSV"
for RID in 1 2 3; do
  srun --nodes=4 --ntasks=128 --ntasks-per-node=32 --mpi=pmix \
       ../../hashjoin_mpi -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P \
      >> "$XCSV" 2>&1 || echo "hashjoin_mpi non disponibile" >> "$XCSV"
done

echo "ALL_DONE_MARKER"
