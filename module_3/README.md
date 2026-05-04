# Partitioned Hash Join with Duplicates — OpenMP Implementation

Parallel partitioned hash join (uniform and skewed keys, with duplicates) implemented in C++20 with OpenMP. Two parallelism strategies: **loop-level** (`#pragma omp parallel for`) and **task-based** (`#pragma omp task` with LPT ordering).

---

## Repository layout

```
module_3/
  src/
    hashjoin_seq.cpp        sequential baseline
    hashjoin_OpenMP.cpp     OpenMP parallel implementation (loop + task modes)
  include/
    common_structs.hpp      Record, JoinResult, PhaseTiming, PartitionedRelation
    generator.hpp           uniform and skewed relation generators
    join_phases.hpp         per-partition hash join kernel
    utilities_fns.hpp       CLI parsing, hashing helpers
    verifier.hpp            naive O(N^2) correctness verifier
  scripts/
    validate.sh             correctness matrix (loop+task x uniform+skewed x t in {1..16})
    run_strong.sh           strong scaling benchmark -> results/strong_scaling.csv
    run_weak.sh             weak scaling benchmark   -> results/weak_scaling.csv
    run_breakdown.sh        phase timing breakdown   -> results/breakdown.csv
    run_schedule.sh         OMP schedule sensitivity -> results/schedule_sensitivity.csv
    deploy_and_run.sh       end-to-end cluster automation
  results/
    cluster/                benchmark CSVs fetched from spmcluster
  report/
    report.tex, report.pdf, plots/
  makefile
```

---

## Build

| Target          | Command                   | Notes                              |
|-----------------|---------------------------|------------------------------------|
| Local (both)    | `make`                    | requires g++ >= 12, C++20, OpenMP  |
| Sequential only | `make seq`                |                                    |
| OMP only        | `make omp`                |                                    |
| Cluster         | `make cluster CXX=g++`    | adds `-march=native`               |
| Clean           | `make clean`              |                                    |
| Package         | `make tar`                | creates `../Modulo3_MarsiliGabriele.zip` |

---

## Running

### Sequential baseline

```bash
./hashjoin_seq -nr <NR> -ns <NS> -seed <SEED> -max-key <MAX_KEY> -p <P>
```

Example:
```bash
./hashjoin_seq -nr 1000000 -ns 2000000 -seed 42 -max-key 500000 -p 128
```

### OpenMP parallel

```
./hashjoin_omp -nr <NR> -ns <NS> -seed <SEED> -max-key <MAX_KEY> -p <P>
              [-mode loop|task]  (default: loop)
              [-t <THREADS>]     (default: all available cores)
              [-skew <RHO>]      (mixture weight in [0,1]; prob. that a key
                                  is drawn from the hot pool; 0 = uniform)
              [-hot <K>]         (number of hot partitions; default: 4)
```

Examples:
```bash
# loop mode, 8 threads, uniform keys
./hashjoin_omp -nr 1000000 -ns 2000000 -seed 42 -max-key 500000 -p 128 \
               -mode loop -t 8

# task mode, 16 threads, skewed keys (90% of keys hit the 4 hot partitions)
./hashjoin_omp -nr 1000000 -ns 2000000 -seed 42 -max-key 500000 -p 128 \
               -mode task -t 16 -skew 0.9 -hot 4
```

**P must be a power of two.**

Output lines: `join_count`, `checksum1`, `checksum2`, `time_sec`, per-phase timings.

---

## Correctness validation

```bash
bash scripts/validate.sh
```

Runs a correctness matrix:

- **Uniform** keys: each `(mode, t)` triple compared against `hashjoin_seq` (ground truth).
- **Skewed** keys: each `(mode, t)` compared against `t=1` of the same mode (seq has no skew support).
- **Cross-mode**: loop `t=4` vs task `t=4` on uniform input must agree.

Thread counts tested: `1 2 4 8 16`. Expected final output: `ALL CHECKS PASS`.  
Logs written to `results/validation_loop.log` and `results/validation_task.log`.

---

## Benchmarks

All scripts respect optional env vars `REPS` (repetitions per cell, default 5) and `THREADS` (space-separated list, default `"1 2 4 8 16"`).

```bash
# Strong scaling (fixed NR=10M, NS=20M; vary threads and mode)
bash scripts/run_strong.sh

# Weak scaling (NR/NS scale linearly with thread count)
bash scripts/run_weak.sh

# Phase breakdown (histogram, scatter, join timing per mode)
bash scripts/run_breakdown.sh

# Schedule sensitivity (static vs dynamic chunk sizes on join phase)
bash scripts/run_schedule.sh
```

Override defaults:
```bash
REPS=10 THREADS="1 4 8 16" bash scripts/run_strong.sh
```

Each script writes a CSV with a header row to `results/`.

---

## Deploy to cluster (automated)

```bash
bash scripts/deploy_and_run.sh
```

Steps performed automatically:

1. SSH reachability check to `spmcluster`.
2. `rsync` of sources, headers, scripts, and makefile.
3. Remote `make cluster` with `-march=native`.
4. Remote `bash scripts/validate.sh` — aborts on failure.
5. All four benchmark scripts in sequence.
6. `rsync` of CSVs back to `results/cluster/`.

---

## Results

Files in `results/cluster/`:

| File                       | Description                                           |
|----------------------------|-------------------------------------------------------|
| `strong_scaling.csv`       | Speedup vs threads, loop and task, uniform + skewed   |
| `weak_scaling.csv`         | Throughput (tuples/s) at constant per-thread load     |
| `breakdown.csv`            | Wall time per phase (histogram, scatter, join)        |
| `schedule_sensitivity.csv` | Join time for static and dynamic chunk-size variants  |
| `validation_loop.log`      | Per-test PASS/FAIL log, loop mode                     |
| `validation_task.log`      | Per-test PASS/FAIL log, task mode                     |
| `cluster_hw_info.txt`      | `lscpu` / memory snapshot from spmcluster             |

---

## Hardware (cluster)

| Property | Value                                          |
|----------|------------------------------------------------|
| CPU      | 2x Intel Xeon E5-2650 v3 @ 2.30 GHz (Haswell) |
| vCPU     | 2 sockets x 10 cores x 2 HT = 40              |
| RAM      | 128 GB                                         |
| Compiler | g++ 12.2.0                                     |
| OpenMP   | 4.5                                            |
