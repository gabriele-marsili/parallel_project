#!/usr/bin/env bash
# Local validation: run the MPI and hybrid binaries across rank counts and
# input sizes, then compare (join_count, checksum1, checksum2) against the
# M3 sequential baseline. Verification happens entirely outside the measured
# region of each binary (rank 0 prints, this script greps).

set -euo pipefail
cd "$(dirname "$0")/.."

MPI=./hashjoin_mpi
HYB=./hashjoin_mpi_omp
SEQ=./hashjoin_seq

# --oversubscribe lets Open MPI run more ranks than physical cores on the
# laptop; on the cluster SLURM controls the rank count and the flag is unused.
MPIRUN_OPTS="--oversubscribe"

[ -x "$MPI" ] || { echo "$MPI missing, run 'make' first" >&2; exit 1; }
[ -x "$HYB" ] || { echo "$HYB missing, run 'make' first" >&2; exit 1; }
[ -x "$SEQ" ] || make hashjoin_seq >/dev/null

mkdir -p results
LOG=results/validation_local.log
: > "$LOG"

# Capture the three aggregates from a run's stdout.
grep_results() {
    awk -F= '
        /^join_count=/ { jc = $2 }
        /^checksum1=/  { c1 = $2 }
        /^checksum2=/  { c2 = $2 }
        END            { printf "%s|%s|%s\n", jc, c1, c2 }
    '
}

fail=0; ok=0

run_case() {
    local TAG=$1 NR=$2 NS=$3 SEED=$4 MAXK=$5 P=$6
    local args="-nr $NR -ns $NS -seed $SEED -max-key $MAXK -p $P"

    local seq_out
    seq_out=$($SEQ $args 2>/dev/null | grep_results)

    {
        printf "\n[%s] NR=%s NS=%s seed=%s max-key=%s P=%s\n" \
               "$TAG" "$NR" "$NS" "$SEED" "$MAXK" "$P"
        printf "  seq        : %s\n" "$seq_out"
    } | tee -a "$LOG"

    for R in 1 2 4 8; do
        if [ "$P" -lt "$R" ] || [ $(( P % R )) -ne 0 ]; then continue; fi
        local mpi_out
        mpi_out=$(mpirun $MPIRUN_OPTS -n $R $MPI $args 2>/dev/null | grep_results)
        local tag="mpi R=$R    "
        if [ "$mpi_out" = "$seq_out" ]; then
            printf "  %s: PASS  %s\n" "$tag" "$mpi_out" | tee -a "$LOG"
            ok=$((ok+1))
        else
            printf "  %s: FAIL  expected=%s got=%s\n" "$tag" "$seq_out" "$mpi_out" | tee -a "$LOG"
            fail=$((fail+1))
        fi
    done

    for cfg in "1 2" "2 2" "1 4" "2 4" "4 2"; do
        read -r R T <<<"$cfg"
        if [ "$P" -lt "$R" ] || [ $(( P % R )) -ne 0 ]; then continue; fi
        local hyb_out
        hyb_out=$(mpirun $MPIRUN_OPTS -n $R $HYB $args -t $T 2>/dev/null | grep_results)
        local tag="hyb R=$R T=$T"
        if [ "$hyb_out" = "$seq_out" ]; then
            printf "  %s: PASS  %s\n" "$tag" "$hyb_out" | tee -a "$LOG"
            ok=$((ok+1))
        else
            printf "  %s: FAIL  expected=%s got=%s\n" "$tag" "$seq_out" "$hyb_out" | tee -a "$LOG"
            fail=$((fail+1))
        fi
    done
}

# Tiny: triggers the naive O(N^2) verifier inside the binary too.
run_case tiny      200      200      1  1000     32
# Small: many partitions, low rank count.
run_case small    10000     20000    7  5000     32
# Medium: same scale as the M3 unit tests.
run_case medium   1000000   1000000  42 500000   32
# Wider: more partitions so P=128 split works for R=8.
run_case wide     500000    1000000  3  250000   128

echo
printf "Summary: %d PASS, %d FAIL\n" "$ok" "$fail" | tee -a "$LOG"
[ "$fail" -eq 0 ]
