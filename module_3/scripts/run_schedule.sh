#!/usr/bin/env bash
# run_schedule.sh — schedule sensitivity for the loop-level join phase
#
# Uses the hashjoin_omp_runtime binary (compiled with -DRUNTIME_SCHEDULE), which
# honours OMP_SCHEDULE at runtime. Build it with:
#
#   make hashjoin_omp_runtime
#
# Output: results/schedule_sensitivity.csv

set -euo pipefail

# Default thread affinity: bind ai core, no migrazioni
: "${OMP_PROC_BIND:=close}"
: "${OMP_PLACES:=cores}"
export OMP_PROC_BIND OMP_PLACES

cd "$(dirname "$0")/.."

OMP=./hashjoin_omp_runtime
[ -f "$OMP" ] || { echo "Compile with 'make hashjoin_omp_runtime' first"; exit 1; }
[ -x "$OMP" ] || { echo "ERROR: $OMP not executable." >&2; exit 1; }

mkdir -p results

NR=${NR:-10000000}
NS=${NS:-20000000}
SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-5000000}
P=${P:-128}
T=${T:-8}              # fixed thread count for this experiment
REPS=${REPS:-5}
WORKLOADS=(uniform skewed)

# schedule policies to sweep (honoured only if binary uses schedule(runtime))
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
    local MODE=loop WL=$1 RHO=$2 HOT=$3 SCHED=$4 RID=$5

    local args="-nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P -t $T -mode $MODE"
    [ "$WL" = "skewed" ] && args="$args -skew $RHO -hot $HOT"

    local TMPF
    TMPF=$(mktemp)
    local stdout
    stdout=$(OMP_SCHEDULE="$SCHED" $OMP $args 2>"$TMPF")

    local JC C1 C2 TTOTAL PHASES
    JC=$(echo "$stdout" | awk -F= '/^join_count=/{print $2}')
    C1=$(echo "$stdout" | awk -F= '/^checksum1=/{print $2}')
    C2=$(echo "$stdout" | awk -F= '/^checksum2=/{print $2}')
    TTOTAL=$(echo "$stdout" | awk -F= '/^time_sec=/{print $2}')
    PHASES=$(parse_phases "$TMPF")
    rm -f "$TMPF"

    local SCHED_LABEL="${SCHED//,/-}"   # "dynamic,1" → "dynamic-1" for CSV safety
    echo "loop,$WL,$RHO,$HOT,$NR,$NS,$P,$T,$SCHED_LABEL,$RID,$TTOTAL,$PHASES,$JC,$C1,$C2" >> "$CSV"
}

echo "=== Schedule sensitivity: mode=loop  t=$T  NR=$NR REPS=$REPS ==="
echo "    Schedules: ${SCHEDULES[*]}"

for WL in "${WORKLOADS[@]}"; do
    RHO=0.0; HOT=0
    [ "$WL" = "skewed" ] && RHO=0.9 && HOT=4
    for SCHED in "${SCHEDULES[@]}"; do
        for ((RID=1; RID<=REPS; RID++)); do
            printf "  %-8s  OMP_SCHEDULE=%-12s  run=%d ...\n" "$WL" "$SCHED" "$RID"
            run_cell "$WL" "$RHO" "$HOT" "$SCHED" "$RID"
        done
    done
done

echo ""
echo "Done. CSV: $CSV  ($(wc -l < "$CSV") lines)"
