#!/bin/bash
# Esp.7 - quale algoritmo sceglie la libreria, e a quale rank count sbaglia.
# L'esp.1 ha forzato gli algoritmi solo a 128 rank, lasciando aperta la domanda:
# il default degrada perche' la decision function cambia scelta salendo di rank,
# oppure perche' l'algoritmo che sceglie collassa sopra una soglia di concorrenza?
# Qui si forza ogni algoritmo a OGNI rank count e si confrontano le curve.
# La config dyn_ignore (dynamic_rules=1 + algorithm=0) isola l'effetto della sola
# attivazione delle dynamic rules da quello della scelta dell'algoritmo.
#SBATCH --job-name=m4x_decision
#SBATCH --partition=normal
#SBATCH --nodes=8
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:30:00
#SBATCH --exclusive
#SBATCH --output=slurm_%x_%j.out
#SBATCH --error=slurm_%x_%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$HOME/module_4/extra_experiments}/07_decision_function"
BIN=../bin/alltoallv_bench
OUT=results; mkdir -p "$OUT"
REPS=10
GLOBAL=150000000          # NR+NS del report: 150M record = 1.2 GB

CSV="$OUT/decision.csv"
echo "label,ranks,recs_per_rank,bytes_per_rank,rep,t_max_s" > "$CSV"

clear_mca() {
    unset OMPI_MCA_coll_tuned_use_dynamic_rules OMPI_MCA_coll_tuned_alltoallv_algorithm || true
}

# --- 1) sweep completo: ogni algoritmo a ogni rank count, volume globale fisso ---
echo "[1/2] sweep rank x algoritmo (8 nodi, volume globale $GLOBAL record)"
for RANKS in 8 16 32 64 128 256; do
  RECS=$((GLOBAL / RANKS))
  for CFG in default dyn_ignore dyn_linear dyn_pairwise; do
    clear_mca
    case $CFG in
      default)      ;;                                              # nessuna variabile: la fixed decision
      dyn_ignore)   export OMPI_MCA_coll_tuned_use_dynamic_rules=1
                    export OMPI_MCA_coll_tuned_alltoallv_algorithm=0 ;;
      dyn_linear)   export OMPI_MCA_coll_tuned_use_dynamic_rules=1
                    export OMPI_MCA_coll_tuned_alltoallv_algorithm=1 ;;
      dyn_pairwise) export OMPI_MCA_coll_tuned_use_dynamic_rules=1
                    export OMPI_MCA_coll_tuned_alltoallv_algorithm=2 ;;
    esac
    srun --nodes=8 --ntasks=$RANKS --ntasks-per-node=$((RANKS / 8 > 0 ? RANKS / 8 : 1)) \
         --mpi=pmix $BIN -recs-per-rank $RECS -reps $REPS -label "$CFG" \
        | tail -n +2 >> "$CSV"
    echo "   ranks=$RANKS cfg=$CFG done"
  done
done
clear_mca

# --- 2) quale modulo/algoritmo viene selezionato: verbose a 64 e 128 rank ---
echo "[2/2] selezione del modulo coll (verbose) a 64 e 128 rank"
for RANKS in 64 128; do
  RECS=$((GLOBAL / RANKS))
  srun --nodes=8 --ntasks=$RANKS --ntasks-per-node=$((RANKS / 8)) --mpi=pmix \
       --mca coll_base_verbose 10 $BIN -recs-per-rank $RECS -reps 1 -label verbose \
      > "$OUT/verbose_${RANKS}.log" 2>&1 || true
  echo "   verbose ranks=$RANKS -> $OUT/verbose_${RANKS}.log ($(wc -l < "$OUT/verbose_${RANKS}.log") righe)"
done

echo "ALL_DONE_MARKER"
