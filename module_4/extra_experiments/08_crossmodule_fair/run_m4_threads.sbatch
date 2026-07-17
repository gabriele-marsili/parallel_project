#!/bin/bash
# Esp.8 - effetto dell'hyper-threading sull'ibrido di M4.
# Il nodo ha 16 core fisici e 32 CPU logiche. M3 su uniform perde il 18% passando da 16 a
# 32 thread (i due thread per core si contendono le porte di load/store su un kernel
# memory-bound). M4 gira sempre a 32: qui si misura se paga lo stesso prezzo.
# --cpus-per-task=32 assegna tutte le logiche, OMP_NUM_THREADS + OMP_PLACES=cores sceglie
# quanti core usare davvero. Un job per numero di nodi (variare --nodes fra srun rompe PMIx).
#SBATCH --job-name=m4ht
#SBATCH --partition=normal
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=32
#SBATCH --time=00:12:00
#SBATCH --exclusive
#SBATCH --output=slurm_%x_%j.out
set -uo pipefail
cd "$HOME/module_4"
export OMP_PROC_BIND=close OMP_PLACES=cores
NODES=${SLURM_JOB_NUM_NODES:?}
NR=50000000; NS=100000000; SEED=42; MK=25000000; P=256
echo "impl,workload,nodes,threads,run_id,t_total_s,join_count"
for T in 16 32; do
  export OMP_NUM_THREADS=$T
  for WL in uniform skewed; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T"
    [ "$WL" = skewed ] && ARGS="$ARGS -skew 0.9 -hot 4"
    for RUN in 1 2 3 4 5; do
      OUT=$(srun --ntasks=$NODES --ntasks-per-node=1 --cpus-per-task=32 --mpi=pmix \
            ./hashjoin_mpi_omp $ARGS 2>/dev/null)
      TT=$(echo "$OUT" | grep -oE "time_sec=[0-9.]+" | cut -d= -f2)
      JC=$(echo "$OUT" | grep -oE "join_count=[0-9]+" | cut -d= -f2)
      echo "m4_hybrid,${WL},${NODES},${T},${RUN},${TT},${JC}"
    done
  done
done
echo "ALL_DONE_MARKER"
