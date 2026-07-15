#!/bin/bash
# Esp.6 — MPI_THREAD_FUNNELED vs MPI_THREAD_MULTIPLE sulla stessa pipeline
# ibrida. Il report sostiene che MULTIPLE aggiunge locking interno senza
# beneficio per questo codice (tutte le collettive fuori dalle regioni
# OpenMP): qui la differenza viene misurata invece che affermata.
#SBATCH --job-name=m4x_tlevel
#SBATCH --partition=normal
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:15:00
#SBATCH --exclusive
#SBATCH --output=slurm_%x_%j.out
#SBATCH --error=slurm_%x_%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$HOME/module_4/extra_experiments}/06_threadlevel"
BIN=../bin/threadlevel_bench
OUT=results; mkdir -p "$OUT"

NR=50000000; NS=100000000; SEED=42; MK=25000000; P=256; REPS=5

export OMP_PROC_BIND=close OMP_PLACES=cores OMP_NUM_THREADS=32

CSV="$OUT/threadlevel.csv"
HEADER_DONE=0
for TL in funneled multiple; do
  for WL in uniform skew; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t 32 -threadlevel $TL -reps $REPS"
    [ "$WL" = skew ] && ARGS="$ARGS -skew 0.9 -hot 4"
    echo "[threadlevel=$TL $WL]"
    if [ $HEADER_DONE = 0 ]; then
      srun --nodes=4 --ntasks=4 --ntasks-per-node=1 --cpus-per-task=32 \
           --mpi=pmix $BIN $ARGS > "$CSV"
      HEADER_DONE=1
    else
      srun --nodes=4 --ntasks=4 --ntasks-per-node=1 --cpus-per-task=32 \
           --mpi=pmix $BIN $ARGS | tail -n +2 >> "$CSV"
    fi
  done
done

echo "ALL_DONE_MARKER"
