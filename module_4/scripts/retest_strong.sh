#!/usr/bin/env bash
#SBATCH --job-name=m4_retest
#SBATCH --partition=normal
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:20:00
#SBATCH --output=results/slurm-retest-%j.out
#SBATCH --error=results/slurm-retest-%j.err
#SBATCH --exclusive

# Reproducibility check for the 4-node strong-scaling dip on uniform.
# Pure MPI, default collective, many reps at 1/2/4 nodes.
# Logs the queue state at start and the node set per run.

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"

MPI=./hashjoin_mpi
[ -x "$MPI" ] || { echo "$MPI missing, build with 'make cluster' first" >&2; exit 1; }
mkdir -p results

NR=${NR:-50000000}; NS=${NS:-100000000}; SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-25000000}; P=${P:-256}
REPS=${REPS:-12}
ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P"   # uniform

echo "=== queue at job start (other jobs sharing the fabric?) ==="
squeue -a 2>&1
echo "=== allocated to this job: ${SLURM_NODELIST:-unknown} ==="

CSV=results/retest_strong.csv
echo "nodes,ranks,run_id,nodelist,t_total_s,t_comm_payload_s,join_count" > "$CSV"

for NODES in 1 2 4; do
    RANKS=$((NODES * 32))
    for ((RID=1; RID<=REPS; RID++)); do
        ERR=$(mktemp); OUT=$(mktemp)
        srun --nodes=$NODES --ntasks=$RANKS --ntasks-per-node=32 --mpi=pmix \
             $MPI $ARGS >"$OUT" 2>"$ERR"
        T=$(awk -F=  '/^time_sec=/{print $2}'   "$OUT")
        JC=$(awk -F= '/^join_count=/{print $2}' "$OUT")
        CP=$(awk '/Comm_payload/{printf "%.6f", $3/1000}' "$ERR")
        NL=$(scontrol show hostnames "$SLURM_NODELIST" 2>/dev/null | head -$NODES | paste -sd'|' -)
        echo "$NODES,$RANKS,$RID,$NL,$T,$CP,$JC" >> "$CSV"
        printf "  nodes=%d ranks=%3d run=%2d  t=%s  comm=%s\n" "$NODES" "$RANKS" "$RID" "$T" "$CP"
        rm -f "$OUT" "$ERR"
    done
done
echo "wrote $CSV"
