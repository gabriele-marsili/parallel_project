#!/bin/bash
#SBATCH --job-name=hashjoin_suppl
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --exclusive
#SBATCH --time=00:29:00
#SBATCH --output=results/bench_suppl_%j.out
#SBATCH --error=results/bench_suppl_%j.err

cd "${SLURM_SUBMIT_DIR:-$(dirname "$0")/..}"
mkdir -p results

echo "Compiling on $(hostname)..."
g++ -O3 -std=c++20 -Wall -Wextra -march=native -pthread -Iinclude src/hashjoin_seq.cpp -o hashjoin_seq
g++ -O3 -std=c++20 -Wall -Wextra -march=native -pthread -Iinclude src/hashjoin_parallel.cpp -o hashjoin_par
echo "Compile OK"
echo "Node: $(hostname) | Cores: $(nproc) | CPU: $(lscpu | grep 'Model name' | sed 's/.*: *//')"
echo "Date: $(date)"
echo ""

SEED=42; MK=1000000; REPS=5

best_of() {
    # best_of <binary> <args>
    local bin="$1"; shift
    local best=""
    for r in $(seq 1 $REPS); do
        local t=$(./"$bin" $@ 2>/dev/null | grep '^time_sec=' | cut -d= -f2)
        if [ -z "$best" ]; then best=$t
        elif [ "$(echo "$t < $best" | bc -l)" = "1" ]; then best=$t; fi
    done
    echo "$best"
}

ALL_T="1 2 4 8 12 16 20 22 24 26 28 30 32"

# ════════════════════════════════════════════════
# A. Strong scaling — P=128, granular thread count
# ════════════════════════════════════════════════
echo "[A] Strong scaling P=128, full T coverage..."
echo "nr,ns,threads,time_seq,time_par" > results/strong_scaling_full.csv

for NR in 10000000 20000000; do
    NS=$((NR * 2))
    echo "  NR=$NR"
    best_seq=$(best_of hashjoin_seq -nr $NR -ns $NS -seed $SEED -max-key $MK -p 128)
    echo "    seq: ${best_seq}s"
    for T in $ALL_T; do
        best_par=$(best_of hashjoin_par -nr $NR -ns $NS -seed $SEED -max-key $MK -p 128 -t $T)
        echo "$NR,$NS,$T,$best_seq,$best_par" >> results/strong_scaling_full.csv
        echo "    T=$T: ${best_par}s"
    done
done

# ════════════════════════════════════════════════
# B. Strong scaling — P=512, granular thread count
# ════════════════════════════════════════════════
echo "[B] Strong scaling P=512, full T coverage..."
echo "nr,ns,threads,time_seq,time_par" > results/strong_scaling_p512.csv

for NR in 10000000 20000000; do
    NS=$((NR * 2))
    echo "  NR=$NR"
    best_seq=$(best_of hashjoin_seq -nr $NR -ns $NS -seed $SEED -max-key $MK -p 512)
    echo "    seq: ${best_seq}s"
    for T in $ALL_T; do
        best_par=$(best_of hashjoin_par -nr $NR -ns $NS -seed $SEED -max-key $MK -p 512 -t $T)
        echo "$NR,$NS,$T,$best_seq,$best_par" >> results/strong_scaling_p512.csv
        echo "    T=$T: ${best_par}s"
    done
done

# ════════════════════════════════════════════════
# C. Weak scaling — P=512, granular thread count
# ════════════════════════════════════════════════
echo "[C] Weak scaling P=512, full T coverage..."
echo "threads,nr,ns,time_sec" > results/weak_scaling_p512.csv

for T in $ALL_T; do
    WNR=$((1000000 * T)); WNS=$((WNR * 2))
    best=$(best_of hashjoin_par -nr $WNR -ns $WNS -seed $SEED -max-key $MK -p 512 -t $T)
    echo "$T,$WNR,$WNS,$best" >> results/weak_scaling_p512.csv
    echo "  T=$T NR=$WNR: ${best}s"
done

echo ""
echo "[✓] Supplement done!"
echo "=== CSV output ==="
for f in results/strong_scaling_full.csv results/strong_scaling_p512.csv results/weak_scaling_p512.csv; do
    echo "--- $f ---"; cat "$f"; echo
done
