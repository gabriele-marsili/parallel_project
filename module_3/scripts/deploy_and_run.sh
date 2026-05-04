#!/usr/bin/env bash
# deploy_and_run.sh — transfer module_3 to spmcluster, build, validate, benchmark
#
# Usage (from module_3/):
#   bash scripts/deploy_and_run.sh [--validate-only] [--bench-only]
#
# Assumes SSH alias "spmcluster" is configured in ~/.ssh/config.
# Requires VPN active when off-campus.

set -euo pipefail
cd "$(dirname "$0")/.."

REMOTE="spmcluster"
REMOTE_DIR="~/module_3"
REPS=${REPS:-3}          # benchmark repetitions per cell (set higher on cluster)
THREADS=${THREADS:-"1 2 4 8 16"}

MODE_VALIDATE=true
MODE_BENCH=true

for arg in "$@"; do
    case $arg in
        --validate-only) MODE_BENCH=false ;;
        --bench-only)    MODE_VALIDATE=false ;;
    esac
done

log() { echo "[$(date +%H:%M:%S)] $*"; }

# ── 1. Sanity check ───────────────────────────────────────────────────────────
log "Testing SSH connection..."
ssh -o ConnectTimeout=10 -o BatchMode=yes "$REMOTE" "echo 'SSH OK'" \
    || { echo "ERROR: cannot reach $REMOTE. Check VPN and SSH config."; exit 1; }

# ── 2. Transfer sources ───────────────────────────────────────────────────────
log "Syncing module_3/ → $REMOTE:$REMOTE_DIR ..."
# Create remote directory first (not directly in home to avoid rsync chmod issue)
ssh "$REMOTE" "mkdir -p module_3"
rsync -av --exclude='*.o' --exclude='hashjoin_seq' --exclude='hashjoin_omp' \
    --exclude='.DS_Store' --exclude='results/cluster' \
    --exclude='*.pdf' --exclude='*.aux' --exclude='*.log' \
    ./ "$REMOTE:$REMOTE_DIR/"

# ── 3. Detect compiler on cluster ─────────────────────────────────────────────
log "Detecting compiler..."
CLUSTER_CXX=$(ssh "$REMOTE" "
    for cxx in g++-15 g++-14 g++-13 g++ c++; do
        if command -v \$cxx &>/dev/null; then echo \$cxx; break; fi
    done
")
log "  Compiler: $CLUSTER_CXX"

# ── 4. Build with -march=native ───────────────────────────────────────────────
log "Building on cluster (make cluster CXX=$CLUSTER_CXX)..."
ssh "$REMOTE" "cd $REMOTE_DIR && make cluster CXX=$CLUSTER_CXX 2>&1"

# ── 5. Sanity run ─────────────────────────────────────────────────────────────
log "Sanity check (seq, 200 records, naive verifier)..."
ssh "$REMOTE" "cd $REMOTE_DIR && \
    ./hashjoin_seq -nr 200 -ns 200 -seed 1 -max-key 1000 -p 16 2>/dev/null | \
    grep -E 'join_count|naive_verify'"

# ── 6. Validation ─────────────────────────────────────────────────────────────
if $MODE_VALIDATE; then
    log "Running validation (loop + task)..."
    ssh "$REMOTE" "cd $REMOTE_DIR && bash scripts/validate.sh" 2>&1
    log "Fetching validation logs..."
    mkdir -p results/cluster
    scp "$REMOTE:$REMOTE_DIR/results/validation_loop.log" results/cluster/
    scp "$REMOTE:$REMOTE_DIR/results/validation_task.log" results/cluster/
    log "  Saved to results/cluster/validation_*.log"
fi

# ── 7. Benchmarks ─────────────────────────────────────────────────────────────
if $MODE_BENCH; then
    log "Running strong scaling (REPS=$REPS, THREADS=$THREADS)..."
    ssh "$REMOTE" "cd $REMOTE_DIR && REPS=$REPS THREADS='$THREADS' bash scripts/run_strong.sh" 2>&1

    log "Running weak scaling..."
    ssh "$REMOTE" "cd $REMOTE_DIR && REPS=$REPS THREADS='$THREADS' bash scripts/run_weak.sh" 2>&1

    log "Running phase breakdown..."
    ssh "$REMOTE" "cd $REMOTE_DIR && REPS=$REPS THREADS='$THREADS' bash scripts/run_breakdown.sh" 2>&1

    log "Running schedule sensitivity (mode=loop, t=8)..."
    ssh "$REMOTE" "cd $REMOTE_DIR && REPS=$REPS T=8 bash scripts/run_schedule.sh" 2>&1

    # ── 8. Collect hardware info ───────────────────────────────────────────────
    log "Collecting hardware info..."
    ssh "$REMOTE" "
        echo '=== lscpu ===' && lscpu
        echo '=== numactl ===' && numactl --hardware 2>/dev/null || echo 'numactl not available'
        echo '=== meminfo ===' && grep MemTotal /proc/meminfo
        echo '=== g++ version ===' && $CLUSTER_CXX --version | head -1
        echo '=== OpenMP version ===' && echo | $CLUSTER_CXX -fopenmp -dM -E - 2>/dev/null | grep _OPENMP
    " > results/cluster_hw_info.txt 2>&1 || true

    # ── 9. Fetch results ───────────────────────────────────────────────────────
    log "Fetching results..."
    mkdir -p results/cluster
    scp "$REMOTE:$REMOTE_DIR/results/strong_scaling.csv"       results/cluster/ 2>/dev/null || true
    scp "$REMOTE:$REMOTE_DIR/results/weak_scaling.csv"         results/cluster/ 2>/dev/null || true
    scp "$REMOTE:$REMOTE_DIR/results/breakdown.csv"            results/cluster/ 2>/dev/null || true
    scp "$REMOTE:$REMOTE_DIR/results/schedule_sensitivity.csv" results/cluster/ 2>/dev/null || true

    log "Results saved in results/cluster/"
    ls -lh results/cluster/
fi

log "Done."
