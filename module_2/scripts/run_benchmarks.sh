#!/bin/bash
# run_benchmarks.sh — Collect strong-scaling, weak-scaling, and phase-breakdown data.
#
# Output goes to results/ as CSV files ready for plot_results.py.
#
# Usage (local):
#   bash scripts/run_benchmarks.sh
#
# Usage (SLURM, embedded in sbatch script):
#   sbatch scripts/bench_slurm.sh

set -euo pipefail
cd "$(dirname "$0")/.."

# ── Configuration ──
SEQ=./hashjoin_seq
PAR=./hashjoin_par
RESULTS=results
REPS=5                           # repetitions per data point
SEED=42
MAX_KEY=1000000
P=128

# Thread counts to sweep (adapt to your cluster node)
THREADS="1 2 4 6 8 10 12 14 16 18 20 24 32"

# ── Build ──
echo "[*] Building..."
make -j 2>/dev/null

# ── Helper: extract field from stdout ──
get_field() { echo "$1" | grep "^$2=" | cut -d= -f2; }

# ── Helper: run and return best time over REPS repetitions ──
best_time() {
    local cmd="$1"
    local best=999999
    for ((r=0; r<REPS; r++)); do
        local out
        out=$($cmd 2>/dev/null)
        local t
        t=$(get_field "$out" "time_sec")
        if awk "BEGIN{exit !($t < $best)}"; then
            best=$t
        fi
    done
    echo "$best"
}

mkdir -p "$RESULTS"

# ════════════════════════════════════════════════════════════
# 1. STRONG SCALING
# ════════════════════════════════════════════════════════════
echo "[*] Strong scaling..."
NR=10000000
NS=20000000
STRONG_CSV="$RESULTS/strong_scaling.csv"
echo "threads,time_sec,NR,NS,P,seed,max_key" > "$STRONG_CSV"

# Sequential baseline
SEQ_TIME=$(best_time "$SEQ -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P")
echo "1,$SEQ_TIME,$NR,$NS,$P,$SEED,$MAX_KEY" >> "$STRONG_CSV"
echo "  seq: ${SEQ_TIME}s"

for T in $THREADS; do
    PAR_TIME=$(best_time "$PAR -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P -t $T")
    echo "$T,$PAR_TIME,$NR,$NS,$P,$SEED,$MAX_KEY" >> "$STRONG_CSV"
    echo "  t=$T: ${PAR_TIME}s"
done

# ════════════════════════════════════════════════════════════
# 2. WEAK SCALING
# ════════════════════════════════════════════════════════════
echo "[*] Weak scaling..."
BASE_NR=1000000
WEAK_CSV="$RESULTS/weak_scaling.csv"
echo "threads,time_sec,NR,NS,P,seed,max_key" > "$WEAK_CSV"

for T in $THREADS; do
    WNR=$((BASE_NR * T))
    WNS=$((WNR * 2))
    PAR_TIME=$(best_time "$PAR -nr $WNR -ns $WNS -seed $SEED -max-key $MAX_KEY -p $P -t $T")
    echo "$T,$PAR_TIME,$WNR,$WNS,$P,$SEED,$MAX_KEY" >> "$WEAK_CSV"
    echo "  t=$T (NR=${WNR}): ${PAR_TIME}s"
done

# ════════════════════════════════════════════════════════════
# 3. PHASE BREAKDOWN
# ════════════════════════════════════════════════════════════
echo "[*] Phase breakdown..."
PHASE_CSV="$RESULTS/phase_breakdown.csv"
echo "threads,histogram_R_ms,scatter_R_ms,histogram_S_ms,scatter_S_ms,join_local_ms,total_ms" > "$PHASE_CSV"

# Parse a phase timing line: "  Label : VALUE ms  (...)" -> extract VALUE (field 3)
parse_phase() {
    echo "$1" | grep "$2" | awk '{print $3}'
}

# Sequential
OUT=$($SEQ -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P 2>&1)
echo "1,$(parse_phase "$OUT" 'Histogram_R'),$(parse_phase "$OUT" 'Scatter_R'),$(parse_phase "$OUT" 'Histogram_S'),$(parse_phase "$OUT" 'Scatter_S'),$(parse_phase "$OUT" 'Join_local'),$(parse_phase "$OUT" 'TOTAL')" >> "$PHASE_CSV"

for T in $THREADS; do
    OUT=$($PAR -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P -t $T 2>&1)
    echo "$T,$(parse_phase "$OUT" 'Histogram_R'),$(parse_phase "$OUT" 'Scatter_R'),$(parse_phase "$OUT" 'Histogram_S'),$(parse_phase "$OUT" 'Scatter_S'),$(parse_phase "$OUT" 'Join_local'),$(parse_phase "$OUT" 'TOTAL')" >> "$PHASE_CSV"
done

echo ""
echo "[✓] Results written to $RESULTS/"
echo "    - strong_scaling.csv"
echo "    - weak_scaling.csv"
echo "    - phase_breakdown.csv"
echo ""
echo "[*] Generate plots with: python3 analysis/plot_results.py"
