#!/usr/bin/env bash
#SBATCH --job-name=m4_seq
#SBATCH --partition=normal
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --time=00:10:00
#SBATCH --output=results/slurm-seq-%j.out
#SBATCH --error=results/slurm-seq-%j.err
#SBATCH --exclusive

# Sequential M3 baseline timed on a compute node so the speedup is honest.
# Uses the same NR/NS/seed/max-key/P as the MPI campaign.

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"

SEQ=../module_3/hashjoin_seq
[ -x "$SEQ" ] || { echo "$SEQ missing — run 'make -C ../module_3 hashjoin_seq' first" >&2; exit 1; }

mkdir -p results

NR=${NR:-50000000}
NS=${NS:-100000000}
SEED=${SEED:-42}
MAX_KEY=${MAX_KEY:-25000000}
P=${P:-256}
REPS=${REPS:-5}

CSV=results/seq_baseline.csv
echo "workload,nr,ns,p,run_id,t_total_s,join_count,checksum1,checksum2" > "$CSV"

for ((RID=1; RID<=REPS; RID++)); do
    OUT=$(mktemp)
    srun --nodes=1 --ntasks=1 --cpus-per-task=1 $SEQ \
        -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P >"$OUT" 2>/dev/null
    JC=$(awk -F= '/^join_count=/{print $2}' "$OUT")
    C1=$(awk -F= '/^checksum1=/{print $2}' "$OUT")
    C2=$(awk -F= '/^checksum2=/{print $2}' "$OUT")
    T=$(awk -F=  '/^time_sec=/{print $2}'  "$OUT")
    echo "uniform,$NR,$NS,$P,$RID,$T,$JC,$C1,$C2" >> "$CSV"
    rm -f "$OUT"
    printf "  seq uniform run=%d t=%s\n" "$RID" "$T"
done

echo "wrote $CSV"
