#!/bin/bash
#SBATCH --job-name=hashjoin_m2
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --exclusive
#SBATCH --time=00:30:00
#SBATCH --output=results/bench_%j.out
#SBATCH --error=results/bench_%j.err

cd "${SLURM_SUBMIT_DIR:-$(dirname "$0")/..}"
mkdir -p results

# Compile ON the compute node to use -march=native safely
echo "Compiling on $(hostname)..."
g++ -O3 -std=c++20 -Wall -Wextra -march=native -pthread -Iinclude src/hashjoin_seq.cpp -o hashjoin_seq
g++ -O3 -std=c++20 -Wall -Wextra -march=native -pthread -Iinclude src/hashjoin_parallel.cpp -o hashjoin_par
echo "Compile OK"

echo "Node: $(hostname) | Cores: $(nproc) | CPU: $(lscpu | grep 'Model name' | sed 's/.*: *//')"
echo "Date: $(date)"
echo ""

SEED=42; MK=1000000; P=128; REPS=5

# ════════════════════════════════════════════════
echo "[1/5] Strong scaling..."
echo "nr,ns,threads,time_seq,time_par" > results/strong_scaling.csv

for NR in 10000000 20000000; do
    NS=$((NR * 2))
    echo "  NR=$NR"

    # Sequential baseline (best of REPS)
    best_seq=""
    for r in $(seq 1 $REPS); do
        t=$(./hashjoin_seq -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P 2>/dev/null | grep '^time_sec=' | cut -d= -f2)
        if [ -z "$best_seq" ]; then best_seq=$t
        elif [ "$(echo "$t < $best_seq" | bc -l)" = "1" ]; then best_seq=$t; fi
    done
    echo "    seq: ${best_seq}s"

    for T in 1 2 4 8 10 14 20 24 32; do
        best_par=""
        for r in $(seq 1 $REPS); do
            t=$(./hashjoin_par -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T 2>/dev/null | grep '^time_sec=' | cut -d= -f2)
            if [ -z "$best_par" ]; then best_par=$t
            elif [ "$(echo "$t < $best_par" | bc -l)" = "1" ]; then best_par=$t; fi
        done
        echo "$NR,$NS,$T,$best_seq,$best_par" >> results/strong_scaling.csv
        echo "    t=$T: ${best_par}s"
    done
done

# ════════════════════════════════════════════════
echo "[2/5] Weak scaling (NR_BASE=1M per thread)..."
echo "threads,nr,ns,time_sec" > results/weak_scaling.csv
for T in 1 2 4 8 10 14 20 24 32; do
    WNR=$((1000000 * T)); WNS=$((WNR * 2))
    best=""
    for r in $(seq 1 $REPS); do
        t=$(./hashjoin_par -nr $WNR -ns $WNS -seed $SEED -max-key $MK -p $P -t $T 2>/dev/null | grep '^time_sec=' | cut -d= -f2)
        if [ -z "$best" ]; then best=$t
        elif [ "$(echo "$t < $best" | bc -l)" = "1" ]; then best=$t; fi
    done
    echo "$T,$WNR,$WNS,$best" >> results/weak_scaling.csv
    echo "  t=$T NR=$WNR: ${best}s"
done

# ════════════════════════════════════════════════
echo "[2b] Weak scaling (NR_BASE=5M per thread — larger workload)..."
echo "threads,nr,ns,time_sec" > results/weak_scaling_5M.csv
for T in 1 2 4 8 10 14 20 24 32; do
    WNR=$((5000000 * T)); WNS=$((WNR * 2))
    best=""
    for r in $(seq 1 $REPS); do
        t=$(./hashjoin_par -nr $WNR -ns $WNS -seed $SEED -max-key $MK -p $P -t $T 2>/dev/null | grep '^time_sec=' | cut -d= -f2)
        if [ -z "$best" ]; then best=$t
        elif [ "$(echo "$t < $best" | bc -l)" = "1" ]; then best=$t; fi
    done
    echo "$T,$WNR,$WNS,$best" >> results/weak_scaling_5M.csv
    echo "  t=$T NR=$WNR: ${best}s"
done

# ════════════════════════════════════════════════
echo "[3/5] Phase breakdown..."
echo "threads,histogram_R_ms,scatter_R_ms,histogram_S_ms,scatter_S_ms,join_local_ms,total_ms" > results/phase_breakdown.csv
NR=10000000; NS=20000000

pp() { echo "$1" | grep "$2" | awk '{print $3}'; }

OUT=$(./hashjoin_seq -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P 2>&1)
echo "1,$(pp "$OUT" Histogram_R),$(pp "$OUT" Scatter_R),$(pp "$OUT" Histogram_S),$(pp "$OUT" Scatter_S),$(pp "$OUT" Join_local),$(pp "$OUT" TOTAL)" >> results/phase_breakdown.csv

for T in 1 2 4 8 10 14 20 24 32; do
    OUT=$(./hashjoin_par -nr $NR -ns $NS -seed $SEED -max-key $MK -p $P -t $T 2>&1)
    echo "$T,$(pp "$OUT" Histogram_R),$(pp "$OUT" Scatter_R),$(pp "$OUT" Histogram_S),$(pp "$OUT" Scatter_S),$(pp "$OUT" Join_local),$(pp "$OUT" TOTAL)" >> results/phase_breakdown.csv
done

# ════════════════════════════════════════════════
echo "[4/5] Partition sensitivity..."
echo "P,threads,time_sec" > results/partition_sensitivity.csv
NR=10000000; NS=20000000; T=20
for PP in 16 32 64 128 256 512 1024 2048 4096; do
    best=""
    for r in $(seq 1 $REPS); do
        t=$(./hashjoin_par -nr $NR -ns $NS -seed $SEED -max-key $MK -p $PP -t $T 2>/dev/null | grep '^time_sec=' | cut -d= -f2)
        if [ -z "$best" ]; then best=$t
        elif [ "$(echo "$t < $best" | bc -l)" = "1" ]; then best=$t; fi
    done
    echo "$PP,$T,$best" >> results/partition_sensitivity.csv
    echo "  P=$PP: ${best}s"
done

# ════════════════════════════════════════════════
echo "[5/5] Duplicate density..."
echo "max_key,threads,time_seq,time_par" > results/duplicate_density.csv
NR=10000000; NS=20000000; T=20
for DMK in 100 1000 10000 100000 1000000 10000000; do
    best_s=""; best_p=""
    for r in $(seq 1 $REPS); do
        ts=$(./hashjoin_seq -nr $NR -ns $NS -seed $SEED -max-key $DMK -p $P 2>/dev/null | grep '^time_sec=' | cut -d= -f2)
        tp=$(./hashjoin_par -nr $NR -ns $NS -seed $SEED -max-key $DMK -p $P -t $T 2>/dev/null | grep '^time_sec=' | cut -d= -f2)
        if [ -z "$best_s" ]; then best_s=$ts; else
            if [ "$(echo "$ts < $best_s" | bc -l)" = "1" ]; then best_s=$ts; fi; fi
        if [ -z "$best_p" ]; then best_p=$tp; else
            if [ "$(echo "$tp < $best_p" | bc -l)" = "1" ]; then best_p=$tp; fi; fi
    done
    echo "$DMK,$T,$best_s,$best_p" >> results/duplicate_density.csv
    echo "  max_key=$DMK: seq=${best_s}s par=${best_p}s"
done

echo ""
echo "[✓] Done!"
echo "=== CSV files ==="
for f in results/*.csv; do echo "--- $f ---"; cat "$f"; echo; done
