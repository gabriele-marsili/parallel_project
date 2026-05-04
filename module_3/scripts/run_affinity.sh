#!/usr/bin/env bash
# run_affinity.sh — thread affinity sensitivity (Lec. 18)
#
# Varies: OMP_PROC_BIND ∈ {close, spread, master}
#         OMP_PLACES    ∈ {cores, threads, sockets}
# Fixed:  mode=loop  workload=uniform  NR=10M NS=20M  t=THREADS_MAX
#
# Expectation on NUMA:
#   close/cores  → better cache locality for join hash table
#   spread/cores → better memory bandwidth for histogram + scatter (memory-bound)
#
# Output: results/affinity.csv

set -euo pipefail
cd "$(dirname "$0")/.."

OMP=./hashjoin_omp
[ -x "$OMP" ] || { echo "ERROR: $OMP not found. Run 'make' first." >&2; exit 1; }

mkdir -p results

NR=${NR:-10000000}
NS=${NS:-20000000}
SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-5000000}
P=${P:-128}
T=${T:-16}
REPS=${REPS:-5}

# (bind,places) pairs to test
declare -a BINDS=("close"  "spread" "master" "close"   "spread")
declare -a PLACES=("cores" "cores"  "cores"  "threads" "threads")

CSV=results/affinity.csv

echo "impl,workload,nr,ns,p,threads,proc_bind,places,run_id,t_total_s,t_hist_r,t_scatter_r,t_hist_s,t_scatter_s,t_join,join_count,checksum1,checksum2" > "$CSV"

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
    local BIND=$1 PL=$2 RID=$3

    local args="-nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P -t $T -mode loop"

    local TMPF
    TMPF=$(mktemp)
    local stdout
    stdout=$(OMP_PROC_BIND="$BIND" OMP_PLACES="$PL" $OMP $args 2>"$TMPF")

    local JC C1 C2 TTOTAL PHASES
    JC=$(echo "$stdout" | awk -F= '/^join_count=/{print $2}')
    C1=$(echo "$stdout" | awk -F= '/^checksum1=/{print $2}')
    C2=$(echo "$stdout" | awk -F= '/^checksum2=/{print $2}')
    TTOTAL=$(echo "$stdout" | awk -F= '/^time_sec=/{print $2}')
    PHASES=$(parse_phases "$TMPF")
    rm -f "$TMPF"

    echo "loop,uniform,$NR,$NS,$P,$T,$BIND,$PL,$RID,$TTOTAL,$PHASES,$JC,$C1,$C2" >> "$CSV"
}

echo "=== Affinity: mode=loop  t=$T  NR=$NR  REPS=$REPS ==="

for ((I=0; I<${#BINDS[@]}; I++)); do
    BIND="${BINDS[$I]}"
    PL="${PLACES[$I]}"
    for ((RID=1; RID<=REPS; RID++)); do
        printf "  OMP_PROC_BIND=%-8s OMP_PLACES=%-10s run=%d ...\n" "$BIND" "$PL" "$RID"
        run_cell "$BIND" "$PL" "$RID"
    done
done

echo ""
echo "Done. CSV: $CSV  ($(wc -l < "$CSV") lines)"
