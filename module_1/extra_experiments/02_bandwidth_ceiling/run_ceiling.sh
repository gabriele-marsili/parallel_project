#!/bin/bash
# Eseguito su node09 dentro srun (single-core). Ancora il tetto di banda.
set -e
cd "$(dirname "$0")"
echo "NODE=$(hostname)"; g++ --version | head -1
echo "=== CPU ==="; lscpu | grep -E 'Model name|Socket|Core|Thread|L3' | head -6

echo; echo "=== STREAM single-core (McCalpin, double) ==="
gcc -O3 -march=native -fopenmp stream_triad.c -o stream_triad
OMP_NUM_THREADS=1 OMP_PROC_BIND=close OMP_PLACES=cores ./stream_triad

echo; echo "=== mem_ceiling: pattern MATCHED al kernel (read u64 + write u32) ==="
g++ -std=c++20 -O3 -march=native mem_ceiling.cpp -o mem_ceiling
./mem_ceiling 100000000 11

echo; echo "=== banda RAGGIUNTA dai kernel del modulo (N=100M, P=256) ==="
cd ../..
make all >/dev/null 2>&1 || make all
for b in plain_baseline plain_autovec avx2; do
  echo "--- $b ---"; bin/$b 100000000 256 42 0 11 2>&1 | grep -E 'throughput|median' || true
done
echo "=== DONE ==="
