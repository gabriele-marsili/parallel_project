#!/usr/bin/env bash
#SBATCH --job-name=m4_a2av
#SBATCH --partition=normal
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:20:00
#SBATCH --output=results/slurm-a2av-%j.out
#SBATCH --error=results/slurm-a2av-%j.err
#SBATCH --exclusive

# Probe for the 4-node (128-rank) strong-scaling dip on uniform.
# The 4 nodes are fixed for the whole job, so run-to-run spread is not placement.
# Compares the default collective against each forced Alltoallv algorithm:
# if the spread goes away once the algorithm is pinned, it is the algorithm choice.

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"

MPI=./hashjoin_mpi
[ -x "$MPI" ] || { echo "$MPI missing, build with 'make cluster' first" >&2; exit 1; }
mkdir -p results

NR=${NR:-50000000}; NS=${NS:-100000000}; SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-25000000}; P=${P:-256}
REPS=${REPS:-10}
ARGS="-nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P"   # uniform workload

CSV=results/alltoallv_probe.csv
echo "config,algo,run_id,nodelist,t_total_s,t_comm_payload_s,join_count" > "$CSV"

NODELIST="${SLURM_NODELIST:-unknown}"
echo "allocated nodes: $NODELIST"

run_config() {
    local CFG=$1 ALGO=$2
    if [ "$CFG" = "default" ]; then
        unset OMPI_MCA_coll_tuned_use_dynamic_rules OMPI_MCA_coll_tuned_alltoallv_algorithm || true
    else
        export OMPI_MCA_coll_tuned_use_dynamic_rules=1
        export OMPI_MCA_coll_tuned_alltoallv_algorithm=$ALGO
    fi
    for ((RID=1; RID<=REPS; RID++)); do
        local ERR OUT
        ERR=$(mktemp); OUT=$(mktemp)
        srun --nodes=4 --ntasks=128 --ntasks-per-node=32 --mpi=pmix \
             $MPI $ARGS >"$OUT" 2>"$ERR"
        local T JC CP
        T=$(awk -F=  '/^time_sec=/{print $2}'   "$OUT")
        JC=$(awk -F= '/^join_count=/{print $2}' "$OUT")
        CP=$(awk '/Comm_payload/{printf "%.6f", $3/1000}' "$ERR")
        echo "$CFG,$ALGO,$RID,$NODELIST,$T,$CP,$JC" >> "$CSV"
        printf "  %-8s algo=%2s run=%2d  t=%s  comm=%s\n" "$CFG" "$ALGO" "$RID" "$T" "$CP"
        rm -f "$OUT" "$ERR"
    done
}

run_config default -1   # fixed decision function (campaign default, no MCA set)
run_config fixed    1   # basic linear, forced
run_config fixed    2   # pairwise, forced

echo "wrote $CSV"
