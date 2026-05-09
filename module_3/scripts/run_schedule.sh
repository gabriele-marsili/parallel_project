#!/usr/bin/env bash
# Schedule sensitivity for the loop-level join. Requires hashjoin_omp_runtime
# (build with `make hashjoin_omp_runtime`), which honours OMP_SCHEDULE.

set -euo pipefail

: "${OMP_PROC_BIND:=close}"
: "${OMP_PLACES:=cores}"
export OMP_PROC_BIND OMP_PLACES

cd "$(dirname "$0")/.."

OMP=./hashjoin_omp_runtime
[ -x "$OMP" ] || { echo "build first: make hashjoin_omp_runtime" >&2; exit 1; }

mkdir -p results

NR=${NR:-10000000}
NS=${NS:-20000000}
SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-5000000}
P=${P:-128}
T=${T:-8}
REPS=${REPS:-5}
WORKLOADS=(uniform skewed)
SCHEDULES=(static "static,1" "dynamic,1" "dynamic,4" "guided,1")

CSV=results/schedule_sensitivity.csv
echo "impl,workload,rho,hot,nr,ns,p,threads,schedule,run_id,t_total_s,t_hist_r,t_scatter_r,t_hist_s,t_scatter_s,t_join,join_count,checksum1,checksum2" > "$CSV"

parse_phases() {
    awk '
    /Histogram_R/ { hr=$3/1000 }
    /Scatter_R/   { sr=$3/1000 }
    /Histogram_S/ { hs=$3/1000 }
    /Scatter_S/   { ss=$3/1000 }
    /Join_local/  { jl=$3/1000 }
    END { printf "%.9f,%.9f,%.9f,%.9f,%.9f", hr+0, sr+0, hs+0, ss+0, jl+0 }
    ' "$1"
}

run_cell() {
    local WL=$1 RHO=$2 HOT=$3 SCHED=$4 RID=$5
    local args="-nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P -t $T -mode loop"
    [ "$WL" = "skewed" ] && args="$args -skew $RHO -hot $HOT"

    local TMPF stdout
    TMPF=$(mktemp)
    stdout=$(OMP_SCHEDULE="$SCHED" $OMP $args 2>"$TMPF")

    local JC C1 C2 TTOTAL PHASES
    JC=$(echo "$stdout"     | awk -F= '/^join_count=/{print $2}')
    C1=$(echo "$stdout"     | awk -F= '/^checksum1=/{print $2}')
    C2=$(echo "$stdout"     | awk -F= '/^checksum2=/{print $2}')
    TTOTAL=$(echo "$stdout" | awk -F= '/^time_sec=/{print $2}')
    PHASES=$(parse_phases "$TMPF")
    rm -f "$TMPF"

    local label="${SCHED//,/-}"
    echo "loop,$WL,$RHO,$HOT,$NR,$NS,$P,$T,$label,$RID,$TTOTAL,$PHASES,$JC,$C1,$C2" >> "$CSV"
}

echo "schedule  mode=loop t=$T NR=$NR REPS=$REPS  policies=${SCHEDULES[*]}"

for WL in "${WORKLOADS[@]}"; do
    RHO=0.0; HOT=0
    [ "$WL" = "skewed" ] && RHO=0.9 && HOT=4
    for SCHED in "${SCHEDULES[@]}"; do
        for ((RID=1; RID<=REPS; RID++)); do
            printf "  %-8s OMP_SCHEDULE=%-12s run=%d\n" "$WL" "$SCHED" "$RID"
            run_cell "$WL" "$RHO" "$HOT" "$SCHED" "$RID"
        done
    done
done

echo "wrote $CSV ($(wc -l < "$CSV") lines)"
