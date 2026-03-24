# SPM Module 1 — Partitioned Hash Join: Partition Mapping Kernel

## Project Structure

```
module_1/
├── Makefile                  # Build system
├── README.md                 # This file
├── include/
│   └── common.hpp            # Shared types, hash function, key generation, timing
├── src/
│   ├── plain.cpp             # Plain C++ (compiled as baseline + autovec)
│   ├── avx2.cpp              # AVX2 intrinsics version
│   └── cuda_kernel.cu        # CUDA version (optional)
├── scripts/
│   ├── run_bench.sh          # SLURM script for CPU benchmarks on node09
│   └── run_cuda.sh           # SLURM script for CUDA benchmarks on node09
├── results/                  # Benchmark output and vectorization reports
├── report/                   # PDF report (max 4 pages)
└── guide/                    # LaTeX implementation guide
```

## Build Instructions

### On spmcluster (node09)

```bash
# Build all CPU targets
make all

# Build CUDA target (requires nvcc)
make cuda

# Clean
make clean
```

### Binaries produced

| Binary              | Description                                      |
|---------------------|--------------------------------------------------|
| `bin/plain_baseline`| Plain C++, auto-vectorization **disabled**        |
| `bin/plain_autovec` | Plain C++, auto-vectorization **enabled**         |
| `bin/avx2`          | AVX2 intrinsics                                   |
| `bin/cuda_kernel`   | CUDA (optional)                                   |

## Usage

```bash
./bin/<binary> <N> <P> [seed] [key_space] [reps]
```

| Parameter  | Description                                     | Default |
|------------|-------------------------------------------------|---------|
| `N`        | Number of keys                                  | required|
| `P`        | Number of partitions (must be power of 2)       | required|
| `seed`     | RNG seed for reproducible key generation        | 42      |
| `key_space`| Key universe size (0 = full 64-bit range)       | 0       |
| `reps`     | Number of benchmark repetitions                 | 11      |

## Correctness Verification

### Checksum verification
All implementations compute and display a **FNV-1a checksum** over the output array. Matching checksums across implementations confirms identical outputs.

### Element-wise comparison
For **N ≤ 32**, all implementations print element-by-element output, allowing manual inspection and exact comparison.

### AVX2 built-in check
The `avx2` binary internally runs **both** the scalar reference and AVX2 kernel, then compares checksums and reports PASS/FAIL.

### Quick correctness test
```bash
make test_correctness
```

## Running Benchmarks on spmcluster

```bash
# CPU benchmarks (submits to node09 via SLURM)
sbatch scripts/run_bench.sh

# CUDA benchmarks (requires GPU partition)
sbatch scripts/run_cuda.sh

# Check job status
squeue --me
```

## Vectorization Report

After building `autovec`, GCC vectorization reports are saved to:
- `results/vec_report_optimized.txt` — successfully vectorized loops
- `results/vec_report_missed.txt` — missed vectorization opportunities

## Design Choices

### Hash Function: XOR-fold + Fibonacci Multiply-Shift (32-bit)
- **h(k) = ((k_lo ⊕ k_hi) × A₃₂) >> (32 − log₂P)** where A₃₂ = 0x9E3779B9
- XOR-fold preserves entropy from both halves of the 64-bit key
- Only XOR + mul32 + shift — no division/modulo
- **SIMD-native**: `_mm256_mullo_epi32` (vpmulld) is a native AVX2 instruction
  (unlike mul64, which would require 3× vpmuludq decomposition)
- Excellent distribution from golden-ratio constant (Knuth, TAOCP Vol.3)

### P as power of 2
- Enables bit-shift instead of modulo for partition mapping
- Critical for SIMD: shift amount is uniform across all lanes

## Analysis and Plots

```bash
# Parse raw benchmark results into CSV
python3 analysis/parse_results.py results/bench_cpu.txt results/bench_cuda.txt

# Generate all plots
python3 analysis/plot_results.py
```

Plots are saved to `results/plots/`.

## Test Environment

- **Cluster**: spmcluster.unipi.it, node09 (gpu-excl partition, exclusive access)
- **CPU**: AMD EPYC 7301 (Zen 1 / Naples), 2 sockets × 16 cores × 2 threads = 64 CPUs, DDR4-2666
- **GPU**: NVIDIA A30 (HBM2, 993 GB/s, PCIe 4.0 x16)
- **Compiler**: GCC 12.2, nvcc (CUDA 12.3)
- **OS**: Linux (OpenHPC)
