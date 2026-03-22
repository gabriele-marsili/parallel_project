#!/bin/bash
# ============================================================================
# generate_test_data.sh — Esegue i benchmark locali (solo baseline + autovec)
# e salva l'output nel formato atteso da parse_results.py.
#
# Serve per testare la pipeline di analisi in locale prima del cluster.
# Su Apple Silicon non ci sono AVX2 né CUDA, quindi i grafici mostreranno
# solo le due implementazioni scalari.
# ============================================================================

set -e
cd "$(dirname "$0")/.."

# compila
make baseline autovec datasets 2>&1 | tail -3

mkdir -p results

OUTFILE="results/bench_cpu.txt"
echo "# SPM Module 1 CPU Benchmark Results (LOCAL TEST)" > "$OUTFILE"
echo "# Node: $(hostname), Date: $(date)" >> "$OUTFILE"
echo "" >> "$OUTFILE"

SEED=42
REPS=11

echo "=== Experiment 1: Sweep N (P=256) ===" | tee -a "$OUTFILE"
for N in 1000000 10000000 50000000 100000000; do
    P=256
    echo "" | tee -a "$OUTFILE"
    echo "--- N=$N P=$P ---" | tee -a "$OUTFILE"
    echo "[baseline]" | tee -a "$OUTFILE"
    bin/plain_baseline $N $P $SEED 0 $REPS | tee -a "$OUTFILE"
    echo "[autovec]" | tee -a "$OUTFILE"
    bin/plain_autovec $N $P $SEED 0 $REPS | tee -a "$OUTFILE"
done

echo "" | tee -a "$OUTFILE"
echo "=== Experiment 2: Sweep P (N=100M) ===" | tee -a "$OUTFILE"
for P in 2 4 8 16 32 64 128 256 512 1024; do
    N=100000000
    echo "" | tee -a "$OUTFILE"
    echo "--- N=$N P=$P ---" | tee -a "$OUTFILE"
    echo "[baseline]" | tee -a "$OUTFILE"
    bin/plain_baseline $N $P $SEED 0 $REPS | tee -a "$OUTFILE"
    echo "[autovec]" | tee -a "$OUTFILE"
    bin/plain_autovec $N $P $SEED 0 $REPS | tee -a "$OUTFILE"
done

echo "" | tee -a "$OUTFILE"
echo "=== Experiment 3: Sweep key_space (N=100M, P=256) ===" | tee -a "$OUTFILE"
for KS in 0 1000 100000 10000000 1000000000; do
    N=100000000
    P=256
    echo "" | tee -a "$OUTFILE"
    echo "--- N=$N P=$P key_space=$KS ---" | tee -a "$OUTFILE"
    echo "[baseline]" | tee -a "$OUTFILE"
    bin/plain_baseline $N $P $SEED $KS $REPS | tee -a "$OUTFILE"
done

echo ""
echo "=== Test benchmark completato ==="
echo "Risultati in: $OUTFILE"
echo ""
echo "Ora esegui:"
echo "  python3 analysis/parse_results.py"
echo "  python3 analysis/plot_results.py"
