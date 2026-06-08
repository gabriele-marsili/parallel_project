#!/usr/bin/env bash
# Sync sources to spmcluster, build, smoke-test, then submit the SLURM jobs.
# Usage:
#   bash scripts/deploy_and_run.sh                  # full sequence
#   bash scripts/deploy_and_run.sh --sync-only      # rsync + build, nothing else
#   bash scripts/deploy_and_run.sh --validate-only  # only the cluster validation
#   bash scripts/deploy_and_run.sh --bench-only     # skip validation, submit benches
#   bash scripts/deploy_and_run.sh --fetch          # pull CSV/log artefacts back

set -euo pipefail
cd "$(dirname "$0")/.."

REMOTE="spmcluster"
REMOTE_DIR="~/module_4"
REPS=${REPS:-3}

DO_SYNC=true
DO_BUILD=true
DO_VALIDATE=true
DO_BENCH=true
DO_FETCH=false
ONLY_FETCH=false
for arg in "$@"; do
    case $arg in
        --sync-only)     DO_VALIDATE=false; DO_BENCH=false ;;
        --validate-only) DO_BENCH=false ;;
        --bench-only)    DO_VALIDATE=false ;;
        --fetch)         ONLY_FETCH=true; DO_SYNC=false; DO_BUILD=false;
                         DO_VALIDATE=false; DO_BENCH=false; DO_FETCH=true ;;
        --skip-sync)     DO_SYNC=false; DO_BUILD=false ;;
    esac
done

log() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }

if $DO_SYNC; then
    ssh -o ConnectTimeout=10 -o BatchMode=yes "$REMOTE" "true" \
        || { echo "cannot reach $REMOTE (VPN? ssh config?)" >&2; exit 1; }

    # module_4 is self-contained: vendored seq source + real headers, no module_3.
    log "syncing module_4 to $REMOTE:$REMOTE_DIR"
    ssh "$REMOTE" "mkdir -p module_4"
    rsync -av --delete \
          --exclude='hashjoin_mpi' --exclude='hashjoin_mpi_omp' \
          --exclude='hashjoin_seq' \
          --exclude='*.o' --exclude='.DS_Store' \
          --exclude='results/cluster' \
          --exclude='*.pdf' --exclude='*.aux' --exclude='*.log' \
          ./ "$REMOTE:$REMOTE_DIR/"
fi

if $DO_BUILD; then
    log "building on $REMOTE"
    ssh "$REMOTE" "
        set -e
        # The cluster ships Open MPI 5 wrapper as mpicxx, GCC 12 as g++.
        module load openmpi5 2>/dev/null || true
        export PATH=/opt/ohpc/pub/mpi/openmpi5-gnu12/5.0.3/bin:\$PATH
        # 'make cluster' builds mpi, hybrid and the vendored seq baseline together.
        cd $REMOTE_DIR && make clean && make cluster MPICXX=mpicxx CXX=g++ 2>&1 | tail -5
        ./hashjoin_mpi -nr 200 -ns 200 -seed 1 -max-key 1000 -p 16 2>/dev/null | grep -E 'join_count|naive_verify' || true
    "
fi

if $DO_VALIDATE; then
    log "submitting cluster validation"
    ssh "$REMOTE" "cd $REMOTE_DIR && sbatch scripts/validate_cluster.sh"
fi

if $DO_BENCH; then
    log "submitting sequential baseline + strong/weak/breakdown"
    ssh "$REMOTE" "cd $REMOTE_DIR && \
        REPS=$REPS sbatch scripts/run_seq_baseline.sh && \
        REPS=$REPS sbatch scripts/run_strong.sh && \
        REPS=$REPS sbatch scripts/run_weak.sh && \
        REPS=$REPS sbatch scripts/run_breakdown.sh"
    ssh "$REMOTE" "squeue -u \$USER 2>&1"
fi

if $DO_FETCH || $ONLY_FETCH; then
    log "fetching CSV/log artefacts"
    mkdir -p results/cluster
    rsync -av \
        --include='*.csv' --include='*.log' --include='slurm-*.out' \
        --include='slurm-*.err' --exclude='*' \
        "$REMOTE:$REMOTE_DIR/results/" results/cluster/
    # Also pull hw info so the report can cite the actual cluster topology.
    ssh "$REMOTE" "
        echo '=== lscpu ==='; lscpu
        echo '=== numactl ==='; numactl --hardware 2>/dev/null || echo 'no numactl'
        echo '=== meminfo ==='; grep MemTotal /proc/meminfo
        echo '=== mpicxx ==='; mpicxx --version | head -1
        echo '=== openmpi ==='; mpirun --version | head -1
    " > results/cluster/cluster_hw_info.txt 2>&1 || true
    ls -lh results/cluster/
fi

log "done"
