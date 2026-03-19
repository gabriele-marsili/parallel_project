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

### Hash Function: Multiply-Shift (Fibonacci Hashing)
- **h(k) = (A × k) >> (64 − log₂P)** where A = 0x9E3779B97F4A7C15
- Universal hashing guarantee (Ferragina, Ch.8 §8.3.1)
- Only multiply + shift — no division/modulo
- SIMD-friendly: maps to AVX2 `_mm256_mul_epu32` decomposition
- Excellent distribution from golden-ratio constant (Knuth, TAOCP Vol.3)

### P as power of 2
- Enables bit-shift instead of modulo for partition mapping
- Critical for SIMD: shift amount is uniform across all lanes
