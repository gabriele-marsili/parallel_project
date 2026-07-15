#!/bin/bash
# Esp.4 — (a) quantifica l'imbalance strutturale sotto skew (volume ricevuto
# per rank, max vs media: il "pavimento" che il report cita senza numero) e
# misura se un remapping consapevole dei pesi (greedy LPT dall'istogramma
# globale, disponibile prima dello scambio) recupera qualcosa senza arrivare
# alla replicazione del build side. (b) metodologia di timing: le stesse run
# senza MPI_Barrier davanti alle collettive, per mostrare quanto skew di
# arrivo assorbirebbe la collettiva.
#SBATCH --job-name=m4x_remap
#SBATCH --partition=normal
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:25:00
#SBATCH --exclusive
#SBATCH --output=slurm_%x_%j.out
#SBATCH --error=slurm_%x_%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$HOME/module_4/extra_experiments}/04_remap_imbalance"
BIN=../bin/mpi_remap
OUT=results; mkdir -p "$OUT"

NR=50000000; NS=100000000; SEED=42; MK=25000000; P=256; REPS=5

# --- 1) mod vs greedy x uniforme vs skew, 128 rank ---
CSV="$OUT/remap.csv"
HEADER_DONE=0
echo "[1/2] remap {mod,greedy} x workload, 128 rank / 4 nodi"
for REMAP in mod greedy; do
  for WL in uniform skew; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -remap $REMAP -reps $REPS"
    [ "$WL" = skew ] && ARGS="$ARGS -skew 0.9 -hot 4"
    echo "   remap=$REMAP $WL"
    if [ $HEADER_DONE = 0 ]; then
      srun --nodes=4 --ntasks=128 --ntasks-per-node=32 --mpi=pmix $BIN $ARGS > "$CSV"
      HEADER_DONE=1
    else
      srun --nodes=4 --ntasks=128 --ntasks-per-node=32 --mpi=pmix $BIN $ARGS | tail -n +2 >> "$CSV"
    fi
  done
done

# anche a 8 rank (1 per nodo x 4? no: 2 per nodo) il pavimento resta: la
# partizione hot e' indivisibile. Configurazione 8 rank su 4 nodi, solo skew.
for REMAP in mod greedy; do
  srun --nodes=4 --ntasks=8 --ntasks-per-node=2 --mpi=pmix \
       $BIN -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -remap $REMAP \
       -skew 0.9 -hot 4 -reps $REPS | tail -n +2 >> "$CSV"
  echo "   remap=$REMAP skew 8 rank done"
done

# --- 2) barrier on/off, mapping mod ---
CSV2="$OUT/barrier.csv"
HEADER_DONE=0
echo "[2/2] barrier {1,0} x workload, mod, 128 rank"
for B in 1 0; do
  for WL in uniform skew; do
    ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -remap mod -barrier $B -reps $REPS"
    [ "$WL" = skew ] && ARGS="$ARGS -skew 0.9 -hot 4"
    echo "   barrier=$B $WL"
    if [ $HEADER_DONE = 0 ]; then
      srun --nodes=4 --ntasks=128 --ntasks-per-node=32 --mpi=pmix $BIN $ARGS > "$CSV2"
      HEADER_DONE=1
    else
      srun --nodes=4 --ntasks=128 --ntasks-per-node=32 --mpi=pmix $BIN $ARGS | tail -n +2 >> "$CSV2"
    fi
  done
done

echo "ALL_DONE_MARKER"
