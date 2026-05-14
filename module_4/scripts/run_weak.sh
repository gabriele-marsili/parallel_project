#!/usr/bin/env bash
#SBATCH --job-name=m4_weak
#SBATCH --partition=normal
#SBATCH --nodes=8
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:25:00
#SBATCH --output=results/slurm-weak-%j.out
#SBATCH --error=results/slurm-weak-%j.err
#SBATCH --exclusive

# Weak scaling on spmcluster: the workload per rank stays constant while the
# rank count (and total workload) grows linearly with the number of nodes.

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"

MPI=./hashjoin_mpi
HYB=./hashjoin_mpi_omp
[ -x "$MPI" ] || { echo "$MPI missing — run make first" >&2; exit 1; }
[ -x "$HYB" ] || { echo "$HYB missing — run make first" >&2; exit 1; }

mkdir -p results

# Per-rank workload (uniform only — the skewed generator keeps the hot keys
# global so a per-rank scaling would distort the comparison).
NR_PER_RANK=${NR_PER_RANK:-2000000}
NS_PER_RANK=${NS_PER_RANK:-4000000}
SEED=${SEED:-42}
P=${P:-256}
REPS=${REPS:-3}
NODES_LIST=(${NODES:-1 2 4 8})
CORES_PER_NODE=${CORES_PER_NODE:-32}

# max_key chosen so the expected join cardinality per rank stays comparable.
MAX_KEY=${MAX_KEY:-25000000}

CSV=results/weak_scaling.csv
echo "impl,workload,nr,ns,p,nodes,ranks,threads,run_id,t_total_s,t_hist_local,t_scatter_local,t_comm_sizes,t_comm_payload,t_hist_post,t_scatter_post,t_join,t_reduce,join_count,checksum1,checksum2" > "$CSV"

parse_results() {
    awk -F= '
        /^join_count=/ { jc=$2 }
        /^checksum1=/  { c1=$2 }
        /^checksum2=/  { c2=$2 }
        /^time_sec=/   { t=$2 }
        END            { printf "%s,%s,%s,%s", t, jc, c1, c2 }
    '
}

run_cell() {
    local IMPL=$1 NODES=$2 RANKS=$3 THREADS=$4 NR=$5 NS=$6 RID=$7

    local args="-nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P"

    local BIN srun_extra
    if [ "$IMPL" = "mpi" ]; then
        BIN=$MPI
        srun_extra=""
    else
        BIN=$HYB
        args="$args -t $THREADS"
        srun_extra="--cpus-per-task=$THREADS"
        export OMP_NUM_THREADS=$THREADS
        export OMP_PROC_BIND=close
        export OMP_PLACES=cores
    fi

    local ERR OUT
    ERR=$(mktemp); OUT=$(mktemp)
    srun --nodes=$NODES --ntasks=$RANKS --ntasks-per-node=$((RANKS / NODES)) \
         $srun_extra --mpi=pmix $BIN $args >"$OUT" 2>"$ERR"

    local TIME_JC PHASES
    TIME_JC=$(parse_results <"$OUT")
    PHASES=$(bash scripts/_parse_phases.sh "$ERR")

    echo "$IMPL,uniform,$NR,$NS,$P,$NODES,$RANKS,$THREADS,$RID,${TIME_JC%%,*},$PHASES,${TIME_JC#*,}" >> "$CSV"
    rm -f "$OUT" "$ERR"
}

echo "weak NR_per_rank=$NR_PER_RANK NS_per_rank=$NS_PER_RANK P=$P REPS=$REPS nodes=${NODES_LIST[*]}"

for NODES in "${NODES_LIST[@]}"; do
    RANKS_MPI=$((NODES * CORES_PER_NODE))
    NR=$((NR_PER_RANK * RANKS_MPI))
    NS=$((NS_PER_RANK * RANKS_MPI))
    if [ "$RANKS_MPI" -le "$P" ] && [ $((P % RANKS_MPI)) -eq 0 ]; then
        for ((RID=1; RID<=REPS; RID++)); do
            printf "  mpi    nodes=%d ranks=%d NR=%d run=%d\n" "$NODES" "$RANKS_MPI" "$NR" "$RID"
            run_cell mpi "$NODES" "$RANKS_MPI" 1 "$NR" "$NS" "$RID"
        done
    fi

    # Hybrid weak scaling: keep per-rank load the same; one rank per node.
    NR_H=$((NR_PER_RANK * NODES))
    NS_H=$((NS_PER_RANK * NODES))
    for ((RID=1; RID<=REPS; RID++)); do
        printf "  hybrid nodes=%d ranks=%d threads=%d NR=%d run=%d\n" \
               "$NODES" "$NODES" "$CORES_PER_NODE" "$NR_H" "$RID"
        run_cell hybrid "$NODES" "$NODES" "$CORES_PER_NODE" "$NR_H" "$NS_H" "$RID"
    done
done

echo "wrote $CSV ($(wc -l < "$CSV") lines)"
