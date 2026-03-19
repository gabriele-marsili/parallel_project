#!/bin/bash
# ============================================================================
# run_cuda.sh — CUDA benchmark script for node09 (SLURM sbatch)
# ============================================================================
#
# Submit with:  sbatch scripts/run_cuda.sh
# ============================================================================

#SBATCH --job-name=spm_m1_cuda
#SBATCH --output=results/cuda_%j.out
#SBATCH --error=results/cuda_%j.err
#SBATCH --partition=gpu-exclusive
#SBATCH --nodelist=node09
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --gres=gpu:1
#SBATCH --time=00:15:00

echo "============================================"
echo "SPM Module 1 — CUDA Benchmark"
echo "Node: $(hostname)"
echo "Date: $(date)"
echo "GPU: $(nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null || echo 'N/A')"
echo "============================================"

# Build CUDA
make cuda

OUTFILE="results/bench_cuda.txt"
echo "# SPM Module 1 CUDA Benchmark Results" > $OUTFILE
echo "# Node: $(hostname), Date: $(date)" >> $OUTFILE
echo "" >> $OUTFILE

SEED=42
REPS=11

echo "=== CUDA: Sweep N (P=256) ===" | tee -a $OUTFILE
for N in 1000000 10000000 50000000 100000000 200000000; do
    P=256
    echo "" | tee -a $OUTFILE
    echo "--- N=$N P=$P ---" | tee -a $OUTFILE
    bin/cuda_kernel $N $P $SEED 0 $REPS | tee -a $OUTFILE
done

echo "" | tee -a $OUTFILE
echo "=== CUDA: Sweep P (N=100M) ===" | tee -a $OUTFILE
for P in 2 16 64 256 1024; do
    N=100000000
    echo "" | tee -a $OUTFILE
    echo "--- N=$N P=$P ---" | tee -a $OUTFILE
    bin/cuda_kernel $N $P $SEED 0 $REPS | tee -a $OUTFILE
done

echo ""
echo "=== CUDA Benchmark complete ==="
