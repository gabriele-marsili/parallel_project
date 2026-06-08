#!/usr/bin/env bash
#SBATCH --job-name=m4_seq
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --time=00:10:00
#SBATCH --output=results/slurm-seq-%j.out
#SBATCH --error=results/slurm-seq-%j.err
#SBATCH --exclusive

# Sequential M3 baseline timed on a compute node so the speedup is fair.
# Same NR/NS/seed/max-key/P as the MPI campaign.

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"

SEQ=./hashjoin_seq
[ -x "$SEQ" ] || { echo "$SEQ missing, run 'make hashjoin_seq' (or 'make cluster') first" >&2; exit 1; }

mkdir -p results

NR=${NR:-50000000}
NS=${NS:-100000000}
SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-25000000}
P=${P:-256}
REPS=${REPS:-5}

RHO=${RHO:-0.9}
HOT=${HOT:-4}

CSV=results/seq_baseline.csv
echo "workload,rho,hot,nr,ns,p,run_id,t_total_s,join_count,checksum1,checksum2" > "$CSV"

# Per-phase breakdown of the sequential baseline, printed on stderr by
# PhaseTiming::print. The seq counterpart of breakdown.csv.
BCSV=results/seq_breakdown.csv
echo "workload,rho,hot,nr,ns,p,run_id,hist_r_ms,scatter_r_ms,hist_s_ms,scatter_s_ms,join_ms,accum_ms,total_ms" > "$BCSV"

# First float on a "  Label : VALUE ms ( PCT% )" line is the millisecond value
# (the second, if present, is the percentage). Tolerant of extra spaces.
phase_ms() { grep "$2" "$1" | grep -oE '[0-9]+\.[0-9]+' | head -1; }

run_one() {
    local LABEL=$1; local SKEW=$2; local HOTQ=$3
    for ((RID=1; RID<=REPS; RID++)); do
        OUT=$(mktemp); ERR=$(mktemp)
        srun --nodes=1 --ntasks=1 --cpus-per-task=1 $SEQ \
            -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P \
            -skew $SKEW -hot $HOTQ >"$OUT" 2>"$ERR"
        JC=$(awk -F= '/^join_count=/{print $2}' "$OUT")
        C1=$(awk -F= '/^checksum1=/{print $2}' "$OUT")
        C2=$(awk -F= '/^checksum2=/{print $2}' "$OUT")
        T=$(awk -F=  '/^time_sec=/{print $2}'  "$OUT")
        echo "$LABEL,$SKEW,$HOTQ,$NR,$NS,$P,$RID,$T,$JC,$C1,$C2" >> "$CSV"

        HR=$(phase_ms "$ERR" 'Histogram_R'); SR=$(phase_ms "$ERR" 'Scatter_R')
        HS=$(phase_ms "$ERR" 'Histogram_S'); SS=$(phase_ms "$ERR" 'Scatter_S')
        JL=$(phase_ms "$ERR" 'Join_local'); AC=$(phase_ms "$ERR" 'Accumulation')
        TOT=$(phase_ms "$ERR" 'TOTAL')
        echo "$LABEL,$SKEW,$HOTQ,$NR,$NS,$P,$RID,$HR,$SR,$HS,$SS,$JL,$AC,$TOT" >> "$BCSV"

        rm -f "$OUT" "$ERR"
        printf "  seq %s run=%d t=%s  (scatter_R=%s scatter_S=%s join=%s ms)\n" \
            "$LABEL" "$RID" "$T" "$SR" "$SS" "$JL"
    done
}

run_one uniform 0.0 0
run_one skewed  $RHO $HOT

echo "wrote $CSV"
echo "wrote $BCSV"
