#!/bin/bash
# Esp.1 — anatomia di MPI_Alltoallv fuori dalla pipeline: sweep del rank
# count (a volume globale fisso e a volume per rank fisso), algoritmo
# forzato via MCA a 128 rank, e sweep del volume a 128 rank. Riproduce e
# circoscrive il dip a 4 nodi / 128 rank del report con buffer sintetici.
#SBATCH --job-name=m4x_a2av
#SBATCH --partition=normal
#SBATCH --nodes=8
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:25:00
#SBATCH --exclusive
#SBATCH --output=slurm_%x_%j.out
#SBATCH --error=slurm_%x_%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$HOME/module_4/extra_experiments}/01_alltoallv_anatomy"
BIN=../bin/alltoallv_bench
OUT=results; mkdir -p "$OUT"
REPS=10

# I record globali della pipeline uniforme: NR+NS = 150M (1.2 GB).
GLOBAL=150000000

run_bench() { # ranks nodes recs label csv
    local RANKS=$1 NODES=$2 RECS=$3 LABEL=$4 CSV=$5
    srun --nodes=$NODES --ntasks=$RANKS --ntasks-per-node=$((RANKS / NODES)) \
         --mpi=pmix $BIN -recs-per-rank $RECS -reps $REPS -label "$LABEL" \
        | tail -n +2 >> "$CSV"
}

# --- 1) sweep rank count a volume GLOBALE fisso (strong, come il report) ---
CSV="$OUT/rank_sweep_strong.csv"
echo "label,ranks,recs_per_rank,bytes_per_rank,rep,t_max_s" > "$CSV"
echo "[1/4] rank sweep strong (8 nodi, volume globale $GLOBAL)"
for RANKS in 8 16 32 64 128 256; do
  run_bench $RANKS 8 $((GLOBAL / RANKS)) strong "$CSV"
  echo "   ranks=$RANKS done"
done

# --- 2) sweep rank count a volume PER RANK fisso (weak) ---
CSV2="$OUT/rank_sweep_weak.csv"
echo "label,ranks,recs_per_rank,bytes_per_rank,rep,t_max_s" > "$CSV2"
echo "[2/4] rank sweep weak (6M record/rank, come 2M+4M del report)"
for RANKS in 8 16 32 64 128 256; do
  run_bench $RANKS 8 6000000 weak "$CSV2"
  echo "   ranks=$RANKS done"
done

# --- 3) algoritmo forzato a 128 rank, 4 vs 8 nodi ---
CSV3="$OUT/algo_forcing.csv"
echo "label,ranks,recs_per_rank,bytes_per_rank,rep,t_max_s,nodes" > "$CSV3"
echo "[3/4] algoritmi {default, linear, pairwise} a 128 rank, 4 e 8 nodi"
RECS=$((GLOBAL / 128))
for NODES in 4 8; do
  for ALGO in default 1 2; do
    if [ "$ALGO" = default ]; then
      unset OMPI_MCA_coll_tuned_use_dynamic_rules OMPI_MCA_coll_tuned_alltoallv_algorithm || true
      LABEL=default
    else
      export OMPI_MCA_coll_tuned_use_dynamic_rules=1
      export OMPI_MCA_coll_tuned_alltoallv_algorithm=$ALGO
      LABEL=algo$ALGO
    fi
    srun --nodes=$NODES --ntasks=128 --ntasks-per-node=$((128 / NODES)) \
         --mpi=pmix $BIN -recs-per-rank $RECS -reps $REPS -label "$LABEL" \
        | tail -n +2 | sed "s/$/,$NODES/" >> "$CSV3"
    echo "   nodes=$NODES algo=$LABEL done"
  done
done
unset OMPI_MCA_coll_tuned_use_dynamic_rules OMPI_MCA_coll_tuned_alltoallv_algorithm || true

# --- 4) sweep del volume a 128 rank su 4 nodi (algoritmo default) ---
CSV4="$OUT/volume_sweep.csv"
echo "label,ranks,recs_per_rank,bytes_per_rank,rep,t_max_s" > "$CSV4"
echo "[4/4] volume sweep a 128 rank / 4 nodi"
for RECS in 146484 292969 585938 1171875 2343750 4687500; do
  srun --nodes=4 --ntasks=128 --ntasks-per-node=32 \
       --mpi=pmix $BIN -recs-per-rank $RECS -reps $REPS -label volsweep \
      | tail -n +2 >> "$CSV4"
  echo "   recs/rank=$RECS done"
done

echo "ALL_DONE_MARKER"
