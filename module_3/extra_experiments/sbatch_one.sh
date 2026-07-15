#!/bin/bash
# Sottomette UN esperimento come job SLURM esclusivo su un nodo Ivy Bridge.
# Uso: sbatch --job-name=m3_expN sbatch_one.sh <cartella_esperimento>
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --output=slurm_%x_%j.out
#SBATCH --error=slurm_%x_%j.err

cd "${SLURM_SUBMIT_DIR:-$HOME/module_3/extra_experiments}"
EXP="$1"
echo "=== $EXP su $(hostname) ==="
bash "$EXP/run.sh"
echo "ALL_DONE_MARKER"
