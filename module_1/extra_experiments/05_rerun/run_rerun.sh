#!/bin/bash
# Rerun CPU + CUDA su node09 (riproduzione Tabella 1 e Tabella 2 del report).
# Scrive in extra_experiments/05_rerun/results/ SENZA toccare i CSV consegnati.
set -e
cd "$(dirname "$0")/../.."     # -> module_1
OUT="extra_experiments/05_rerun/results"; mkdir -p "$OUT"
RAW="$OUT/rerun_node09.txt"
echo "NODE=$(hostname)  DATE=$(date)" | tee "$RAW"
g++ --version | head -1 | tee -a "$RAW"

make all >/dev/null 2>&1 || make all
SEED=42; REPS=11

echo "=== CPU: Sweep N (P=256) ===" | tee -a "$RAW"
for N in 1000000 10000000 50000000 100000000 200000000; do
  echo "--- N=$N P=256 ---" | tee -a "$RAW"
  for b in plain_baseline plain_autovec avx2; do
    echo "[$b]" | tee -a "$RAW"
    bin/$b $N 256 $SEED 0 $REPS | tee -a "$RAW"
  done
done

echo "=== CUDA: Sweep N (P=256) ===" | tee -a "$RAW"
export PATH=/usr/local/cuda-12.3/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda-12.3/lib64:$LD_LIBRARY_PATH
nvcc -std=c++17 -O3 -I include -gencode arch=compute_80,code=sm_80 src/cuda_kernel.cu -o bin/cuda_kernel
nvidia-smi --query-gpu=name,memory.total --format=csv,noheader | tee -a "$RAW"
for N in 1000000 10000000 50000000 100000000 200000000; do
  echo "--- CUDA N=$N P=256 ---" | tee -a "$RAW"
  bin/cuda_kernel $N 256 $SEED 0 $REPS | tee -a "$RAW"
done
echo "=== DONE ===" | tee -a "$RAW"
