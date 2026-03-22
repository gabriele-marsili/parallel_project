#!/bin/bash
# ============================================================================
# run_bench.sh — Main benchmark script for node09 (SLURM sbatch)
# ============================================================================
#
# Submit with:  sbatch scripts/run_bench.sh
#
# This script runs all CPU implementations across several (N, P) configurations
# and saves results to results/bench_cpu.txt
# ============================================================================

#SBATCH --job-name=spm_m1_bench
#SBATCH --output=results/bench_%j.out
#SBATCH --error=results/bench_%j.err
#SBATCH --partition=gpu-excl
#SBATCH --nodelist=node09
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --time=00:25:00

echo "============================================"
echo "SPM Module 1 — CPU Benchmark"
echo "Node: $(hostname)"
echo "Date: $(date)"
echo "============================================"
echo ""

# Build
make clean
make all

OUTFILE="results/bench_cpu.txt"
echo "# SPM Module 1 CPU Benchmark Results" > $OUTFILE
echo "# Node: $(hostname), Date: $(date)" >> $OUTFILE
echo "# Format: binary N P seed key_space reps" >> $OUTFILE
echo "" >> $OUTFILE

SEED=42
REPS=11

# ============================================================================
# Experiment 1: Sweep N with fixed P=256
# ============================================================================
echo "=== Experiment 1: Sweep N (P=256) ===" | tee -a $OUTFILE
for N in 1000000 10000000 50000000 100000000 200000000; do
    P=256
    echo "" | tee -a $OUTFILE
    echo "--- N=$N P=$P ---" | tee -a $OUTFILE
    echo "[baseline]" | tee -a $OUTFILE
    bin/plain_baseline $N $P $SEED 0 $REPS | tee -a $OUTFILE
    echo "[autovec]" | tee -a $OUTFILE
    bin/plain_autovec $N $P $SEED 0 $REPS | tee -a $OUTFILE
    echo "[avx2]" | tee -a $OUTFILE
    bin/avx2 $N $P $SEED 0 $REPS | tee -a $OUTFILE
done

# ============================================================================
# Experiment 2: Sweep P with fixed N=100M
# ============================================================================
echo "" | tee -a $OUTFILE
echo "=== Experiment 2: Sweep P (N=100M) ===" | tee -a $OUTFILE
for P in 2 4 8 16 32 64 128 256 512 1024; do
    N=100000000
    echo "" | tee -a $OUTFILE
    echo "--- N=$N P=$P ---" | tee -a $OUTFILE
    echo "[baseline]" | tee -a $OUTFILE
    bin/plain_baseline $N $P $SEED 0 $REPS | tee -a $OUTFILE
    echo "[autovec]" | tee -a $OUTFILE
    bin/plain_autovec $N $P $SEED 0 $REPS | tee -a $OUTFILE
    echo "[avx2]" | tee -a $OUTFILE
    bin/avx2 $N $P $SEED 0 $REPS | tee -a $OUTFILE
done

# ============================================================================
# Experiment 3: Sweep key_space (duplicate rate) with N=100M, P=256
# ============================================================================
echo "" | tee -a $OUTFILE
echo "=== Experiment 3: Sweep key_space (N=100M, P=256) ===" | tee -a $OUTFILE
for KS in 0 1000 100000 10000000 1000000000; do
    N=100000000
    P=256
    echo "" | tee -a $OUTFILE
    echo "--- N=$N P=$P key_space=$KS ---" | tee -a $OUTFILE
    echo "[baseline]" | tee -a $OUTFILE
    bin/plain_baseline $N $P $SEED $KS $REPS | tee -a $OUTFILE
    echo "[avx2]" | tee -a $OUTFILE
    bin/avx2 $N $P $SEED $KS $REPS | tee -a $OUTFILE
done

echo ""
echo "=== Benchmark complete ==="
echo "Results saved to $OUTFILE"
