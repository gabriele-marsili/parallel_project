#!/bin/bash
# Esp.2 — misura di alpha (latenza) e beta (1/banda) della rete con un
# ping-pong, inter-nodo e intra-nodo. Il report usa il modello di Hockney
# (R-1)*alpha + beta*V per spiegare il weak scaling senza aver misurato i
# coefficienti: qui si misurano, e il fit avviene nel plot.
#SBATCH --job-name=m4x_hockney
#SBATCH --partition=normal
#SBATCH --nodes=2
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:15:00
#SBATCH --exclusive
#SBATCH --output=slurm_%x_%j.out
#SBATCH --error=slurm_%x_%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$HOME/module_4/extra_experiments}/02_hockney"
BIN=../bin/pingpong_bench
OUT=results; mkdir -p "$OUT"

CSV="$OUT/pingpong.csv"
echo "label,bytes,iters,t_oneway_us,bw_MBps" > "$CSV"

echo "[1/2] ping-pong inter-nodo (rank 0 e 1 su nodi diversi)"
srun --nodes=2 --ntasks=2 --ntasks-per-node=1 --mpi=pmix \
     $BIN -label inter | tail -n +2 >> "$CSV"

echo "[2/2] ping-pong intra-nodo (rank 0 e 1 sullo stesso nodo)"
srun --nodes=1 --ntasks=2 --ntasks-per-node=2 --mpi=pmix \
     $BIN -label intra | tail -n +2 >> "$CSV"

echo "ALL_DONE_MARKER"
