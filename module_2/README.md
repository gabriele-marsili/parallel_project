# SPM Module 2 — Partitioned Hash Join with Duplicates

Parallel implementation of the partitioned hash join pipeline using C++ threads.

## Project Structure

```
module_2/
├── Makefile
├── README.md
├── include/
│   ├── common.hpp            # Module-1 hash function (XOR-fold + Fibonacci multiply-shift)
│   ├── common_structs.hpp    # Record, PartitionedRelation, JoinResult, PhaseTiming
│   ├── generator.hpp         # Deterministic key generation (splitmix64)
│   ├── join_phases.hpp       # Shared logic: exclusive_prefix_sum, join_one_partition
│   ├── utilities_fns.hpp     # CLI parsing, usage, is_power_of_two
│   └── verifier.hpp          # Naive O(N²) join verifier for small inputs
├── src/
│   ├── hashjoin_seq.cpp      # Sequential reference with phase timing
│   └── hashjoin_parallel.cpp # Parallel implementation with C++ threads
├── scripts/
│   ├── run_benchmarks.sh     # Collect strong/weak scaling + phase breakdown CSVs
│   └── bench_slurm.sh        # SLURM wrapper for cluster execution
├── analysis/
│   └── plot_results.py       # Generate report-ready plots from CSVs
└── results/                  # Benchmark outputs and plots
```

## Build

```bash
make            # builds hashjoin_seq and hashjoin_par
make clean      # removes binaries
```

Requires: `g++` with C++20 support and `-pthread`.

## Run

### Sequential (baseline)
```bash
./hashjoin_seq -nr 10000000 -ns 20000000 -seed 42 -max-key 1000000 -p 128
```

### Parallel
```bash
./hashjoin_par -nr 10000000 -ns 20000000 -seed 42 -max-key 1000000 -p 128 -t 8
```

Parameters:
- `-nr` / `-ns` : number of records in R / S
- `-seed` : deterministic RNG seed
- `-max-key` : keys generated in `[0, max-key)` — smaller = more duplicates
- `-p` : number of partitions (must be a power of 2)
- `-t` : number of threads (parallel only; defaults to `hardware_concurrency`)

Output (stdout, machine-parseable):
```
NR=... NS=... P=... seed=... max_key=... threads=...
join_count=...
checksum1=...
checksum2=...
time_sec=...
```

Phase breakdown is printed on stderr.

## Correctness Verification

### Automatic (small inputs)
For `NR ≤ 500` and `NS ≤ 500`, both binaries automatically run the naive `O(N²)` verifier and print `naive_verify=PASS` or `FAIL`.

```bash
./hashjoin_seq -nr 50 -ns 80 -seed 13 -max-key 8 -p 4
./hashjoin_par -nr 50 -ns 80 -seed 13 -max-key 8 -p 4 -t 4
```

### Cross-validation (large inputs)
Compare `join_count`, `checksum1`, `checksum2` between seq and par:

```bash
# Must produce identical join_count and checksums for all thread counts
for T in 1 2 4 8 16; do
    echo "=== t=$T ==="
    ./hashjoin_par -nr 1000000 -ns 2000000 -seed 42 -max-key 100000 -p 64 -t $T
done
```

### Thread Sanitizer
```bash
make hashjoin_par_tsan
./hashjoin_par_tsan -nr 100000 -ns 200000 -seed 42 -max-key 10000 -p 64 -t 8
```

## Benchmarking

### Local
```bash
bash scripts/run_benchmarks.sh
```
Produces `results/strong_scaling.csv`, `results/weak_scaling.csv`, `results/phase_breakdown.csv`.

### On spmcluster (SLURM)
```bash
sbatch scripts/bench_slurm.sh
```

### Generate Plots
```bash
python3 analysis/plot_results.py                 # PNG (default)
python3 analysis/plot_results.py --format pdf    # PDF for LaTeX
```

Plots are written to `results/plots/`:
- `strong_speedup.png` — Speedup vs. threads
- `strong_efficiency.png` — Efficiency vs. threads
- `weak_scaling.png` — Weak scaling efficiency
- `phase_breakdown.png` — Stacked bar chart of phase timings
- `phase_speedup.png` — Per-phase speedup

## Parallelization Strategy

| Phase | Approach | Notes |
|-------|----------|-------|
| **Histogram** | Thread-local histograms + sequential merge | Block distribution of input records; O(N/k) per thread + O(P×k) merge |
| **Prefix Sum** | Sequential | O(P), negligible |
| **Scatter** | Lock-free with pre-computed per-thread offsets | Each thread writes to non-overlapping output regions (zero contention) |
| **Join Local** | Cyclic distribution (thread $t$ owns partitions $t, t{+}k, t{+}2k, \ldots$) | Zero scheduling overhead; padded per-thread results avoid false sharing |
| **Accumulation** | Sequential reduce | O(P), negligible |

Thread count is dynamically adjusted per phase based on workload size to avoid overhead on small inputs.
