#!/usr/bin/env bash
#SBATCH --job-name=m4_4v8
#SBATCH --partition=normal
#SBATCH --nodes=8
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:25:00
#SBATCH --output=results/slurm-4v8-%j.out
#SBATCH --error=results/slurm-4v8-%j.err
#SBATCH --exclusive

# Isolate rank count from node count for the 4-node strong-scaling dip (uniform).
# Pure MPI, default collective, many reps:
#   A) 4 nodes x 32 = 128 ranks   the dip
#   B) 8 nodes x 32 = 256 ranks   the recovery
#   C) 8 nodes x 16 = 128 ranks   same 128 ranks as A but spread over 8 nodes
# C behaving like A points to the rank count, like B to the node count

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"

MPI=./hashjoin_mpi
[ -x "$MPI" ] || { echo "$MPI missing, build with 'make cluster' first" >&2; exit 1; }
mkdir -p results

NR=${NR:-50000000}; NS=${NS:-100000000}; SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-25000000}; P=${P:-256}
REPS=${REPS:-15}
ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P"   # uniform

echo "=== queue at start (other jobs on the fabric?) ==="; squeue -a 2>&1
echo "=== allocated: ${SLURM_NODELIST:-unknown} ==="

CSV=results/confirm_4v8.csv
echo "cell,nodes,ranks,ranks_per_node,run_id,t_total_s,t_comm_payload_s,join_count" > "$CSV"

run_cell() {
    local CELL=$1 NODES=$2 RANKS=$3
    local RPN=$((RANKS / NODES))
    for ((RID=1; RID<=REPS; RID++)); do
        local ERR OUT
        ERR=$(mktemp); OUT=$(mktemp)
        srun --nodes=$NODES --ntasks=$RANKS --ntasks-per-node=$RPN --mpi=pmix \
             $MPI $ARGS >"$OUT" 2>"$ERR"
        local T JC CP
        T=$(awk -F=  '/^time_sec=/{print $2}'   "$OUT")
        JC=$(awk -F= '/^join_count=/{print $2}' "$OUT")
        CP=$(awk '/Comm_payload/{printf "%.6f", $3/1000}' "$ERR")
        echo "$CELL,$NODES,$RANKS,$RPN,$RID,$T,$CP,$JC" >> "$CSV"
        printf "  %s nodes=%d ranks=%3d/%2dpn run=%2d  t=%s  comm=%s\n" \
            "$CELL" "$NODES" "$RANKS" "$RPN" "$RID" "$T" "$CP"
        rm -f "$OUT" "$ERR"
    done
}

run_cell A 4 128   # dip
run_cell B 8 256   # recovery
run_cell C 8 128   # 128 ranks spread over 8 nodes (diagnostic)

echo "wrote $CSV"
