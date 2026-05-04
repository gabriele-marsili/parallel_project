#!/usr/bin/env bash
# run_strong.sh — strong scaling benchmark
#
# Fixed: NR=10M NS=20M P=128 SEED=42
# Varies: mode ∈ {loop, task}  workload ∈ {uniform, skewed}  threads ∈ THREADS
# Reps:   REPS repetitions per cell (default 5)
#
# Output: results/strong_scaling.csv
#
# Usage on cluster: OMP_PROC_BIND=close OMP_PLACES=cores ./scripts/run_strong.sh

set -euo pipefail

# Default thread affinity: bind ai core, no migrazioni
: "${OMP_PROC_BIND:=close}"
: "${OMP_PLACES:=cores}"
export OMP_PROC_BIND OMP_PLACES

cd "$(dirname "$0")/.."

OMP=./hashjoin_omp
[ -x "$OMP" ] || { echo "ERROR: $OMP not found. Run 'make' first." >&2; exit 1; }

mkdir -p results

NR=${NR:-10000000}
NS=${NS:-20000000}
SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-5000000}
P=${P:-128}
REPS=${REPS:-5}
THREADS=(${THREADS:-1 2 4 8 16})

CSV=results/strong_scaling.csv

echo "impl,workload,rho,hot,nr,ns,p,threads,run_id,t_total_s,t_hist_r,t_scatter_r,t_hist_s,t_scatter_s,t_join,join_count,checksum1,checksum2" > "$CSV"

parse_phases() {  # parse phase timings from stderr file → comma-separated seconds
    awk '
    /Histogram_R/ { hr=$3/1000 }
    /Scatter_R/   { sr=$3/1000 }
    /Histogram_S/ { hs=$3/1000 }
    /Scatter_S/   { ss=$3/1000 }
    /Join_local/  { jl=$3/1000 }
    END { printf "%.9f,%.9f,%.9f,%.9f,%.9f", hr+0, sr+0, hs+0, ss+0, jl+0 }
    ' "$1"
}

run_cell() {  # mode workload rho hot t run_id
    local MODE=$1 WL=$2 RHO=$3 HOT=$4 T=$5 RID=$6

    local args="-nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P -t $T -mode $MODE"
    [ "$WL" = "skewed" ] && args="$args -skew $RHO -hot $HOT"

    local TMPF
    TMPF=$(mktemp)
    local stdout
    stdout=$($OMP $args 2>"$TMPF")

    local JC C1 C2 TTOTAL PHASES
    JC=$(echo "$stdout" | awk -F= '/^join_count=/{print $2}')
    C1=$(echo "$stdout" | awk -F= '/^checksum1=/{print $2}')
    C2=$(echo "$stdout" | awk -F= '/^checksum2=/{print $2}')
    TTOTAL=$(echo "$stdout" | awk -F= '/^time_sec=/{print $2}')
    PHASES=$(parse_phases "$TMPF")
    rm -f "$TMPF"

    echo "$MODE,$WL,$RHO,$HOT,$NR,$NS,$P,$T,$RID,$TTOTAL,$PHASES,$JC,$C1,$C2" >> "$CSV"
}

echo "=== Strong scaling: NR=$NR NS=$NS P=$P REPS=$REPS ==="
echo "    Threads: ${THREADS[*]}"

for MODE in loop task; do
    for WL in uniform skewed; do
        RHO=0.0; HOT=0
        [ "$WL" = "skewed" ] && RHO=0.9 && HOT=4
        for T in "${THREADS[@]}"; do
            for ((RID=1; RID<=REPS; RID++)); do
                printf "  %-4s %-8s t=%-3d run=%d ...\n" "$MODE" "$WL" "$T" "$RID"
                run_cell "$MODE" "$WL" "$RHO" "$HOT" "$T" "$RID"
            done
        done
    done
done

echo ""
echo "Done. CSV: $CSV  ($(wc -l < "$CSV") lines)"
