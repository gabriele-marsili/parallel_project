#!/bin/bash
#SBATCH --job-name=m2_extra
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --output=extra_experiments/slurm_extra_%j.out
#SBATCH --error=extra_experiments/slurm_extra_%j.err

cd "${SLURM_SUBMIT_DIR:-$HOME/module_2}"
bash extra_experiments/run_all.sh
echo "ALL_DONE_MARKER"
