#!/usr/bin/env bash
# Sync sources to spmcluster, build, validate, and run the four benchmarks.
# Usage:  bash scripts/deploy_and_run.sh [--validate-only|--bench-only]

set -euo pipefail
cd "$(dirname "$0")/.."

REMOTE="spmcluster"
REMOTE_DIR="~/module_3"
REPS=${REPS:-3}
THREADS=${THREADS:-"1 2 4 8 16"}

DO_VALIDATE=true
DO_BENCH=true
for arg in "$@"; do
    case $arg in
        --validate-only) DO_BENCH=false ;;
        --bench-only)    DO_VALIDATE=false ;;
    esac
done

log() { echo "[$(date +%H:%M:%S)] $*"; }

ssh -o ConnectTimeout=10 -o BatchMode=yes "$REMOTE" "true" \
    || { echo "cannot reach $REMOTE (VPN? ssh config?)" >&2; exit 1; }

ssh "$REMOTE" "mkdir -p module_3"
rsync -av --exclude='*.o' --exclude='hashjoin_seq' --exclude='hashjoin_omp' \
    --exclude='.DS_Store' --exclude='results/cluster' \
    --exclude='*.pdf' --exclude='*.aux' --exclude='*.log' \
    ./ "$REMOTE:$REMOTE_DIR/"

CXX=$(ssh "$REMOTE" '
    for c in g++-15 g++-14 g++-13 g++ c++; do
        command -v $c >/dev/null 2>&1 && { echo $c; break; }
    done')
log "compiler: $CXX"

ssh "$REMOTE" "cd $REMOTE_DIR && make cluster CXX=$CXX 2>&1"

ssh "$REMOTE" "cd $REMOTE_DIR && \
    ./hashjoin_seq -nr 200 -ns 200 -seed 1 -max-key 1000 -p 16 2>/dev/null | \
    grep -E 'join_count|naive_verify'"

if $DO_VALIDATE; then
    ssh "$REMOTE" "cd $REMOTE_DIR && bash scripts/validate.sh"
    mkdir -p results/cluster
    scp "$REMOTE:$REMOTE_DIR/results/validation_loop.log" results/cluster/
    scp "$REMOTE:$REMOTE_DIR/results/validation_task.log" results/cluster/
fi

if $DO_BENCH; then
    for s in run_strong run_weak run_breakdown; do
        ssh "$REMOTE" "cd $REMOTE_DIR && REPS=$REPS THREADS='$THREADS' bash scripts/$s.sh"
    done
    ssh "$REMOTE" "cd $REMOTE_DIR && REPS=$REPS T=8 bash scripts/run_schedule.sh"

    ssh "$REMOTE" "
        echo '=== lscpu ==='; lscpu
        echo '=== numactl ==='; numactl --hardware 2>/dev/null || echo 'numactl unavailable'
        echo '=== meminfo ==='; grep MemTotal /proc/meminfo
        echo '=== compiler ==='; $CXX --version | head -1
        echo '=== openmp ==='; echo | $CXX -fopenmp -dM -E - 2>/dev/null | grep _OPENMP
    " > results/cluster_hw_info.txt 2>&1 || true

    mkdir -p results/cluster
    for f in strong_scaling weak_scaling breakdown schedule_sensitivity; do
        scp "$REMOTE:$REMOTE_DIR/results/$f.csv" results/cluster/ 2>/dev/null || true
    done
    ls -lh results/cluster/
fi

log "done"
