#!/bin/bash
# M3 variante task ai parametri del confronto cross-module di M4 (NR=50M, NS=100M, P=256,
# max_key=25M): il CSV crossmodule_m3_50m.csv contiene solo m3_loop, ma sui dati di M3 a
# 10M la task batte la loop su skewed. Qui si misura a 50M, per chiudere il confronto.
#SBATCH --job-name=m3task
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=32
#SBATCH --time=00:10:00
#SBATCH --exclusive
#SBATCH --output=slurm_%x_%j.out
set -uo pipefail
cd "$HOME/module_3"
export OMP_PROC_BIND=close OMP_PLACES=cores
NR=50000000; NS=100000000; SEED=42; MK=25000000; P=256
echo "impl,workload,threads,run_id,t_total_s,join_count"
for MODE in loop task; do
  for WL in uniform skewed; do
    for T in 16 32; do
      export OMP_NUM_THREADS=$T
      ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T -mode $MODE"
      [ "$WL" = skewed ] && ARGS="$ARGS -skew 0.9 -hot 4"
      for RUN in 1 2 3 4 5; do
        OUT=$(./hashjoin_omp $ARGS 2>/dev/null | tr -d '\r')
        TT=$(echo "$OUT" | grep -oE "time_sec=[0-9.]+" | cut -d= -f2)
        JC=$(echo "$OUT" | grep -oE "join_count=[0-9]+" | cut -d= -f2)
        echo "m3_${MODE},${WL},${T},${RUN},${TT},${JC}"
      done
    done
  done
done
echo "ALL_DONE_MARKER"
