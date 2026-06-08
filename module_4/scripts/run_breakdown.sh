#!/usr/bin/env bash
#SBATCH --job-name=m4_breakdown
#SBATCH --partition=normal
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:15:00
#SBATCH --output=results/slurm-breakdown-%j.out
#SBATCH --error=results/slurm-breakdown-%j.err
#SBATCH --exclusive

# Focused breakdown at a single (representative) node count, varying only
# rank/thread mix and workload. Same parser as the strong-scaling sweep, the
# csv has one row per measured cell, with the rank-max time of each phase.

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"

MPI=./hashjoin_mpi
HYB=./hashjoin_mpi_omp

mkdir -p results

NODES=${NODES:-4}
NR=${NR:-50000000}
NS=${NS:-100000000}
SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-25000000}
P=${P:-256}
REPS=${REPS:-5}
CORES_PER_NODE=${CORES_PER_NODE:-32}

CSV=results/breakdown.csv
echo "impl,workload,rho,hot,nr,ns,p,nodes,ranks,threads,run_id,t_total_s,t_hist_local,t_scatter_local,t_comm_sizes,t_comm_payload,t_hist_post,t_scatter_post,t_join,t_reduce,join_count,checksum1,checksum2" > "$CSV"

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
    local IMPL=$1 WL=$2 RHO=$3 HOT=$4 NODES=$5 RANKS=$6 THREADS=$7 RID=$8

    local args="-nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P"
    [ "$WL" = "skewed" ] && args="$args -skew $RHO -hot $HOT"

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
    echo "$IMPL,$WL,$RHO,$HOT,$NR,$NS,$P,$NODES,$RANKS,$THREADS,$RID,${TIME_JC%%,*},$PHASES,${TIME_JC#*,}" >> "$CSV"
    rm -f "$OUT" "$ERR"
}

echo "breakdown nodes=$NODES NR=$NR NS=$NS REPS=$REPS"

for WL in uniform skewed; do
    RHO=0.0; HOT=0
    [ "$WL" = "skewed" ] && RHO=0.9 && HOT=4

    RANKS_MPI=$((NODES * CORES_PER_NODE))
    for ((RID=1; RID<=REPS; RID++)); do
        printf "  mpi    %-7s ranks=%d run=%d\n" "$WL" "$RANKS_MPI" "$RID"
        run_cell mpi "$WL" "$RHO" "$HOT" "$NODES" "$RANKS_MPI" 1 "$RID"
    done

    for ((RID=1; RID<=REPS; RID++)); do
        printf "  hybrid %-7s ranks=%d threads=%d run=%d\n" "$WL" "$NODES" "$CORES_PER_NODE" "$RID"
        run_cell hybrid "$WL" "$RHO" "$HOT" "$NODES" "$NODES" "$CORES_PER_NODE" "$RID"
    done
done

echo "wrote $CSV ($(wc -l < "$CSV") lines)"
