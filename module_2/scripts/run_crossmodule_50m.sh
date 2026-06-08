#!/usr/bin/env bash
#SBATCH --job-name=m2_xmod
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --time=00:20:00
#SBATCH --output=results/slurm-xmod-%j.out
#SBATCH --error=results/slurm-xmod-%j.err
#SBATCH --exclusive

# Re-run the M2 C++ threads join at the Module 4 input size (50M/100M,
# P=256, max_key=25M) so its single-node time is directly comparable with M4.
# Uniform only: M2 has no skewed generator.

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"

# build on the compute node so -march=native targets its ISA (Ivy Bridge)
g++ -O3 -std=c++20 -Wall -Wextra -march=native -pthread -Iinclude \
    src/hashjoin_parallel.cpp -o hashjoin_par

NR=${NR:-50000000}; NS=${NS:-100000000}; SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-25000000}; P=${P:-256}; T=${T:-32}; REPS=${REPS:-5}

mkdir -p results
CSV=results/crossmodule_50m.csv
echo "impl,workload,threads,run_id,t_total_s,join_count" > "$CSV"

for ((RID=1; RID<=REPS; RID++)); do
    OUT=$(./hashjoin_par -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P -t $T 2>/dev/null)
    T_S=$(echo "$OUT" | awk -F= '/^time_sec=/{print $2}')
    JC=$(echo "$OUT" | awk -F= '/^join_count=/{print $2}')
    echo "m2_threads,uniform,$T,$RID,$T_S,$JC" >> "$CSV"
    printf "  m2 uniform t=%d run=%d  t=%s\n" "$T" "$RID" "$T_S"
done
echo "wrote $CSV"
