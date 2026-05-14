#!/usr/bin/env bash
#SBATCH --job-name=m4_validate
#SBATCH --partition=normal
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=32
#SBATCH --time=00:10:00
#SBATCH --output=results/slurm-validate-%j.out
#SBATCH --error=results/slurm-validate-%j.err
#SBATCH --exclusive

# Cluster correctness: confirm that (join_count, checksum1, checksum2) match
# the M3 sequential baseline across rank counts and workloads, including
# multi-node runs that the laptop cannot exercise.

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"

MPI=./hashjoin_mpi
HYB=./hashjoin_mpi_omp
SEQ=../module_3/hashjoin_seq

[ -x "$SEQ" ] || (cd ../module_3 && make hashjoin_seq) >/dev/null

mkdir -p results
LOG=results/validation_cluster.log
: > "$LOG"

grep_results() {
    awk -F= '
        /^join_count=/ { jc=$2 }
        /^checksum1=/  { c1=$2 }
        /^checksum2=/  { c2=$2 }
        END            { printf "%s|%s|%s\n", jc, c1, c2 }
    '
}

fail=0; ok=0

run_case() {
    local TAG=$1 NR=$2 NS=$3 SEED=$4 MAXK=$5 P=$6 SKEW=$7 HOT=$8
    local args="-nr $NR -ns $NS -seed $SEED -max-key $MAXK -p $P"
    local ref_label="seq"
    local ref_out
    if [ "$SKEW" != "0" ]; then
        # M3's sequential reference doesn't accept -skew/-hot, so for skewed
        # inputs we use the MPI binary with one rank as the authoritative
        # reference — by construction it executes the same algorithm and uses
        # the same generator as the multi-rank configs.
        args="$args -skew $SKEW -hot $HOT"
        ref_label="mpi R=1"
        ref_out=$(srun --nodes=1 --ntasks=1 --mpi=pmix $MPI $args 2>/dev/null | grep_results)
    else
        ref_out=$($SEQ $args 2>/dev/null | grep_results)
    fi

    {
        printf "\n[%s] NR=%s NS=%s seed=%s maxk=%s P=%s skew=%s hot=%s\n" \
               "$TAG" "$NR" "$NS" "$SEED" "$MAXK" "$P" "$SKEW" "$HOT"
        printf "  %-12s  : %s\n" "$ref_label" "$ref_out"
    } | tee -a "$LOG"
    local seq_out=$ref_out

    for cfg in "1 32 1" "2 64 1" "4 128 1" "1 1 32" "2 2 32" "4 4 32"; do
        read -r N R T <<<"$cfg"
        if [ "$P" -lt "$R" ] || [ $((P % R)) -ne 0 ]; then continue; fi
        local BIN tag
        if [ "$T" -eq 1 ]; then
            BIN=$MPI; tag="mpi    N=$N R=$R"
            srun_args="--nodes=$N --ntasks=$R --ntasks-per-node=$((R / N)) --mpi=pmix"
        else
            BIN=$HYB; tag="hybrid N=$N R=$R T=$T"
            export OMP_NUM_THREADS=$T OMP_PROC_BIND=close OMP_PLACES=cores
            srun_args="--nodes=$N --ntasks=$R --ntasks-per-node=$((R / N)) --cpus-per-task=$T --mpi=pmix"
        fi
        local out
        out=$(srun $srun_args $BIN $args 2>/dev/null | grep_results)
        if [ "$out" = "$seq_out" ]; then
            printf "  %s: PASS  %s\n" "$tag" "$out" | tee -a "$LOG"
            ok=$((ok+1))
        else
            printf "  %s: FAIL  expected=%s got=%s\n" "$tag" "$seq_out" "$out" | tee -a "$LOG"
            fail=$((fail+1))
        fi
    done
}

run_case medium-uniform 1000000 2000000  42 500000   128 0   0
run_case medium-skewed  1000000 2000000  42 500000   128 0.9 4
run_case large-uniform  5000000 10000000 42 2500000  256 0   0

echo
printf "Summary: %d PASS, %d FAIL\n" "$ok" "$fail" | tee -a "$LOG"
[ "$fail" -eq 0 ]
