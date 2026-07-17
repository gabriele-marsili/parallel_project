#!/bin/bash
# Esp.7b - quale algoritmo sceglie la libreria nei QUATTRO PUNTI della curva di strong
# scaling del report, dove nodi e rank variano insieme (32 rank per nodo):
#   1 nodo/32 rank, 2 nodi/64 rank, 4 nodi/128 rank, 8 nodi/256 rank.
# L'esp.7 ha variato i rank a 8 nodi fissi: serve per isolare il rank count, ma non
# corrisponde ai punti misurati dal report. Qui si chiude il cerchio.
#SBATCH --job-name=m4x_curve
#SBATCH --partition=normal
#SBATCH --nodes=8
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:25:00
#SBATCH --exclusive
#SBATCH --output=slurm_%x_%j.out
#SBATCH --error=slurm_%x_%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$HOME/module_4/extra_experiments}/07_decision_function"
BIN=../bin/alltoallv_bench
OUT=results; mkdir -p "$OUT"
REPS=10
GLOBAL=150000000

CSV="$OUT/curve.csv"
echo "label,ranks,recs_per_rank,bytes_per_rank,rep,t_max_s,nodes" > "$CSV"

clear_mca() {
    unset OMPI_MCA_coll_tuned_use_dynamic_rules OMPI_MCA_coll_tuned_alltoallv_algorithm || true
}

# i quattro punti del report: 32 rank per nodo
for PAIR in "1 32" "2 64" "4 128" "8 256"; do
  set -- $PAIR; NODES=$1; RANKS=$2
  RECS=$((GLOBAL / RANKS))
  for CFG in default dyn_linear dyn_pairwise; do
    clear_mca
    case $CFG in
      default)      ;;
      dyn_linear)   export OMPI_MCA_coll_tuned_use_dynamic_rules=1
                    export OMPI_MCA_coll_tuned_alltoallv_algorithm=1 ;;
      dyn_pairwise) export OMPI_MCA_coll_tuned_use_dynamic_rules=1
                    export OMPI_MCA_coll_tuned_alltoallv_algorithm=2 ;;
    esac
    srun --nodes=$NODES --ntasks=$RANKS --ntasks-per-node=32 --mpi=pmix \
         $BIN -recs-per-rank $RECS -reps $REPS -label "$CFG" \
        | tail -n +2 | sed "s/$/,$NODES/" >> "$CSV"
    echo "   nodes=$NODES ranks=$RANKS cfg=$CFG done"
  done
done
clear_mca

echo "ALL_DONE_MARKER"
