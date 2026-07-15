#!/bin/bash
# Esp.5 — sensibilità al numero di partizioni P (il report usa P=256, i
# moduli 2/3 usavano 128). Vincolo strutturale: P multiplo dei rank. Si
# misura sia l'ibrido (4 rank) sia il pure MPI (128 rank, quindi P>=128).
#SBATCH --job-name=m4x_psweep
#SBATCH --partition=normal
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:25:00
#SBATCH --exclusive
#SBATCH --output=slurm_%x_%j.out
#SBATCH --error=slurm_%x_%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$HOME/module_4/extra_experiments}/05_p_sweep"
HYB=../bin/threadlevel_bench
PURE=../bin/mpi_remap
OUT=results; mkdir -p "$OUT"

NR=50000000; NS=100000000; SEED=42; MK=25000000; REPS=3

export OMP_PROC_BIND=close OMP_PLACES=cores OMP_NUM_THREADS=32

# --- 1) ibrido: 4 rank x 32 thread ---
CSV="$OUT/p_sweep_hybrid.csv"
HEADER_DONE=0
echo "[1/2] hybrid, P sweep"
for P in 128 256 512 1024; do
  for WL in uniform skew; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t 32 -reps $REPS"
    [ "$WL" = skew ] && ARGS="$ARGS -skew 0.9 -hot 4"
    echo "   P=$P $WL"
    if [ $HEADER_DONE = 0 ]; then
      srun --nodes=4 --ntasks=4 --ntasks-per-node=1 --cpus-per-task=32 \
           --mpi=pmix $HYB $ARGS > "$CSV"
      HEADER_DONE=1
    else
      srun --nodes=4 --ntasks=4 --ntasks-per-node=1 --cpus-per-task=32 \
           --mpi=pmix $HYB $ARGS | tail -n +2 >> "$CSV"
    fi
  done
done

# --- 2) pure MPI: 128 rank (mapping mod, fasi locali sequenziali) ---
CSV2="$OUT/p_sweep_pure.csv"
HEADER_DONE=0
echo "[2/2] pure MPI, P sweep"
for P in 128 256 512 1024; do
  for WL in uniform skew; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -remap mod -reps $REPS"
    [ "$WL" = skew ] && ARGS="$ARGS -skew 0.9 -hot 4"
    echo "   P=$P $WL"
    if [ $HEADER_DONE = 0 ]; then
      srun --nodes=4 --ntasks=128 --ntasks-per-node=32 --mpi=pmix $PURE $ARGS > "$CSV2"
      HEADER_DONE=1
    else
      srun --nodes=4 --ntasks=128 --ntasks-per-node=32 --mpi=pmix $PURE $ARGS | tail -n +2 >> "$CSV2"
    fi
  done
done

echo "ALL_DONE_MARKER"
