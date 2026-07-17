#!/bin/bash
# Esp.2.3 - la scelta dell'algoritmo nel regime weak DEL REPORT.
# Il weak del report tiene 32 rank per nodo e varia i nodi (1/2/4/8), quindi i rank vanno
# 32/64/128/256 e rank-per-nodo resta COSTANTE a 32: a far crescere il volume per NIC e'
# la quota off-node (da 0 a 0.875), non rpn. Un rank sweep a nodi fissi misurerebbe un
# weak diverso (rpn da 1 a 32) e i suoi punti intermedi non esistono nel report.
#
# UN JOB PER NUMERO DI NODI, lanciato cosi':
#     for N in 1 2 4 8; do sbatch --nodes=$N 02_hockney/run_weak_algo.sbatch; done
# Variare --nodes fra srun dentro una sola allocazione fa morire lo stato PMIx di Open MPI
# 5.0.3 (SIGSEGV in mca_pml_ob1_recv_frag_callback_match, poi collective timeout su ogni
# srun successiva): un'allocazione per configurazione lo evita.
# Due passate con l'ordine degli algoritmi invertito, cosi' il default non paga il fatto di
# girare sempre per primo.
#SBATCH --job-name=m4x_weak
#SBATCH --partition=normal
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:12:00
#SBATCH --exclusive
#SBATCH --output=slurm_%x_%j.out
#SBATCH --error=slurm_%x_%j.err

set -uo pipefail
cd "${SLURM_SUBMIT_DIR:-$HOME/module_4/extra_experiments}/02_hockney"
BIN=../bin/alltoallv_bench
OUT=results; mkdir -p "$OUT"
REPS=5                 # x2 passate = 10 misure per configurazione
PER_RANK=6000000       # 6M record = 48 MB per rank, come il weak del report

NODES=${SLURM_JOB_NUM_NODES:?serve --nodes}
RANKS=$((NODES * 32))
CSV="$OUT/weak_algo_${NODES}nodi.csv"
echo "label,ranks,recs_per_rank,bytes_per_rank,rep,t_max_s,nodes,pass" > "$CSV"

set_mca() {
    unset OMPI_MCA_coll_tuned_use_dynamic_rules OMPI_MCA_coll_tuned_alltoallv_algorithm || true
    case $1 in
      dyn_linear)   export OMPI_MCA_coll_tuned_use_dynamic_rules=1
                    export OMPI_MCA_coll_tuned_alltoallv_algorithm=1 ;;
      dyn_pairwise) export OMPI_MCA_coll_tuned_use_dynamic_rules=1
                    export OMPI_MCA_coll_tuned_alltoallv_algorithm=2 ;;
    esac
}

echo "[weak] $NODES nodi, $RANKS rank (32/nodo), $((PER_RANK*8/1000000)) MB per rank"
for PASS in 1 2; do
  if [ "$PASS" = 1 ]; then CFGS="default dyn_linear dyn_pairwise"
  else                     CFGS="dyn_pairwise dyn_linear default"; fi
  for CFG in $CFGS; do
    set_mca "$CFG"
    TMP=$(mktemp)
    if srun --ntasks=$RANKS --ntasks-per-node=32 --mpi=pmix \
            $BIN -recs-per-rank $PER_RANK -reps $REPS -label "$CFG" > "$TMP" 2>/dev/null \
       && [ "$(wc -l < "$TMP")" -gt 1 ]; then
        tail -n +2 "$TMP" | sed "s/$/,$NODES,$PASS/" >> "$CSV"
        echo "   cfg=$CFG pass=$PASS done"
    else
        echo "   CELLA MANCANTE: cfg=$CFG pass=$PASS"
    fi
    rm -f "$TMP"
  done
done

echo "ALL_DONE_MARKER"
