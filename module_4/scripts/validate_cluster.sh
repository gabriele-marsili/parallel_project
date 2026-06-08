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
# the sequential reference (hashjoin_seq) across the full rank range and both
# workloads, all on the cluster: small rank counts (R=1,2,4,8) on a single
# node for the edge cases, and R=32,64,128 across 1/2/4 nodes for scale.

set -euo pipefail
cd "$SLURM_SUBMIT_DIR"

MPI=./hashjoin_mpi
HYB=./hashjoin_mpi_omp
SEQ=./hashjoin_seq

[ -x "$SEQ" ] || make hashjoin_seq >/dev/null

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
    if [ "$SKEW" != "0" ]; then
        args="$args -skew $SKEW -hot $HOT"
    fi
    # hashjoin_seq is the independent reference for both workloads: it accepts
    # -skew/-hot and uses the same generator as the MPI binaries. On small
    # inputs it also self-checks against an O(N^2) naive join.
    local ref_raw ref_out
    ref_raw=$($SEQ $args 2>/dev/null)
    ref_out=$(echo "$ref_raw" | grep_results)

    {
        printf "\n[%s] NR=%s NS=%s seed=%s maxk=%s P=%s skew=%s hot=%s\n" \
               "$TAG" "$NR" "$NS" "$SEED" "$MAXK" "$P" "$SKEW" "$HOT"
        printf "  %-12s  : %s\n" "seq" "$ref_out"
    } | tee -a "$LOG"
    local seq_out=$ref_out

    # Small inputs: the reference also passes the O(N^2) naive check.
    if [ "$NR" -le 500 ] && [ "$NS" -le 500 ]; then
        local nv
        nv=$(echo "$ref_raw" | awk -F= '/^naive_verify=/{print $2}')
        printf "  naive(seq)   : %s\n" "$nv" | tee -a "$LOG"
        if [ "$nv" = "PASS" ]; then ok=$((ok+1)); else fail=$((fail+1)); fi
    fi

    # cfg = "nodes ranks threads". Pure MPI (T=1): small rank counts on one
    # node (R=1,2,4,8) plus R=32,64,128 across nodes; hybrid (T=32): one rank
    # per node. R=1 exercises the no-communication path, R=2 the minimal swap.
    for cfg in "1 1 1" "1 2 1" "1 4 1" "1 8 1" \
               "1 32 1" "2 64 1" "4 128 1" \
               "1 1 32" "2 2 32" "4 4 32"; do
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

run_case small-uniform  200      400      42 100      16  0   0
run_case small-skewed   200      400      42 100      16  0.9 4
run_case medium-uniform 1000000 2000000  42 500000   128 0   0
run_case medium-skewed  1000000 2000000  42 500000   128 0.9 4
run_case large-uniform  5000000 10000000 42 2500000  256 0   0

echo
printf "Summary: %d PASS, %d FAIL\n" "$ok" "$fail" | tee -a "$LOG"
[ "$fail" -eq 0 ]
