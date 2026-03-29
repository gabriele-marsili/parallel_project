# SPM Module 1 — Partition Mapping Kernel

## Project Structure

```
module_1/
├── Makefile                  # Build system
├── include/
│   └── common.hpp            # Shared types, hash function, key generation, timing
├── src/
│   ├── plain.cpp             # Plain C++ (compiled as baseline + autovec)
│   ├── avx2.cpp              # AVX2 intrinsics version
│   ├── cuda_kernel.cu        # CUDA version (optional)
│   └── dataset_creator.cpp   # Binary dataset generator
├── scripts/
│   ├── run_bench.sh          # SLURM script for CPU benchmarks
│   └── run_cuda.sh           # SLURM script for CUDA benchmarks
├── analysis/                 # Python scripts for parsing results and plotting
├── results/                  # Benchmark output, CSV, vectorization reports
│   └── plots/                # Generated figures
└── report/                   # PDF report (LaTeX source)
```

## Build

On spmcluster (node09):

```bash
make all        # builds baseline, autovec, avx2, dataset_creator
make cuda       # builds CUDA target (requires nvcc)
make clean
```

### Binaries

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
| `seed`     | RNG seed                                        | 42      |
| `key_space`| Key universe size (0 = full 64-bit range)       | 0       |
| `reps`     | Benchmark repetitions                           | 11      |

## Correctness Verification

**Checksum**: all implementations compute an FNV-1a checksum over the output array. Matching checksums across implementations confirms identical output.

**Element-wise comparison**: for N ≤ 32, every implementation prints element-by-element output for manual inspection.

**AVX2 built-in check**: the `avx2` binary runs both a scalar reference and the AVX2 kernel internally, compares checksums, and reports OK/FAIL before benchmarking.

**Quick test**:
```bash
make test_correctness   # runs all implementations with N=16, P=4
```

## Benchmarks

```bash
# CPU (submits to node09 via SLURM)
sbatch scripts/run_bench.sh

# CUDA
sbatch scripts/run_cuda.sh

# Parse results and generate plots
python3 analysis/parse_results.py
python3 analysis/plot_results.py
```

## Vectorization Report

After building `autovec`, GCC reports are saved to:
- `results/vec_report_optimized.txt` — successfully vectorized loops
- `results/vec_report_missed.txt` — missed opportunities (empty = none missed)

## Dataset Creator

`dataset_creator` generates binary key files that can be loaded via `mmap` (zero-copy). In Module 1 the kernel binaries generate keys in-memory, so the dataset creator is not used directly during benchmarks. It is included for future modules where separating data generation from kernel timing will be needed (e.g., for multi-threaded partitioning where all threads must start from the same pre-generated dataset).

```bash
bin/dataset_creator              # create default datasets (1M, 10M, 100M, 200M)
bin/dataset_creator --list       # list existing datasets
bin/dataset_creator --custom -N 50000000
```

## Memory Requirements

At N=200M the kernel allocates ~2.4 GB (1.6 GB input + 0.8 GB output). The cluster nodes have sufficient RAM; on machines with less than 4 GB, use smaller N values.

## Test Environment

- **CPU**: AMD EPYC 7301 (Zen 1), DDR4-2666
- **GPU**: NVIDIA A30 (HBM2, 993 GB/s, PCIe 4.0 x16)
- **Compiler**: GCC 12.2, nvcc (CUDA 12.3)
- **Node**: spmcluster node09, exclusive allocation
