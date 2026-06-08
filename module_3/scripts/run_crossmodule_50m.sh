#!/usr/bin/env bash
#SBATCH --job-name=m3_xmod
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --time=00:20:00
#SBATCH --output=results/slurm-xmod-%j.out
#SBATCH --error=results/slurm-xmod-%j.err
#SBATCH --exclusive

# Re-run the M3 OpenMP loop join at the Module 4 input size (50M/100M,
# P=256, max_key=25M) so its single-node time is comparable with M4.
# Uniform at 32 threads; skewed at 16 and 32 threads (16 was best in M3).

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"
: "${OMP_PROC_BIND:=close}"; : "${OMP_PLACES:=cores}"; export OMP_PROC_BIND OMP_PLACES

# build on the compute node so -march=native targets its ISA (Ivy Bridge)
g++ -O3 -std=c++20 -Wall -Wextra -fopenmp -march=native -Iinclude \
    src/hashjoin_OpenMP.cpp -o hashjoin_omp

NR=${NR:-50000000}; NS=${NS:-100000000}; SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-25000000}; P=${P:-256}; REPS=${REPS:-5}
RHO=0.9; HOT=4

mkdir -p results/cluster
CSV=results/cluster/crossmodule_50m.csv
echo "impl,workload,threads,run_id,t_total_s,join_count" > "$CSV"

run() {  # workload threads [extra args...]
    local WL=$1 T=$2; shift 2
    export OMP_NUM_THREADS=$T
    for ((RID=1; RID<=REPS; RID++)); do
        OUT=$(./hashjoin_omp -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY \
              -p $P -t $T -mode loop "$@" 2>/dev/null)
        T_S=$(echo "$OUT" | awk -F= '/^time_sec=/{print $2}')
        JC=$(echo "$OUT" | awk -F= '/^join_count=/{print $2}')
        echo "m3_loop,$WL,$T,$RID,$T_S,$JC" >> "$CSV"
        printf "  m3 %-7s t=%d run=%d  t=%s\n" "$WL" "$T" "$RID" "$T_S"
    done
}

run uniform 32
run skewed  16 -skew $RHO -hot $HOT
run skewed  32 -skew $RHO -hot $HOT
echo "wrote $CSV"
