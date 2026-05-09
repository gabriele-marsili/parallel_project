#!/usr/bin/env bash
# Correctness matrix: (loop|task) x (uniform|skewed) x threads, plus cross-mode
# (loop t=4 vs task t=4) on both workloads. Uniform is checked against the
# sequential baseline; skewed is checked against the same mode at t=1, since
# the sequential binary has no skew support.

set -euo pipefail
cd "$(dirname "$0")/.."

SEQ=./hashjoin_seq
OMP=./hashjoin_omp

if [ ! -x "$SEQ" ] || [ ! -x "$OMP" ]; then
    echo "binaries not found; run make first" >&2
    exit 1
fi

mkdir -p results

NR=1000000
NS=2000000
SEED=42
MAX_KEY=500000
P=128
THREADS=(1 2 4 8 16)

LOG_LOOP=results/validation_loop.log
LOG_TASK=results/validation_task.log

get_triple() {
    echo "$1" | grep -E "^(join_count|checksum1|checksum2)=" | sort
}

run_omp() {          # mode t [skew hot]
    local mode=$1 t=$2 skew=${3:-0.0} hot=${4:-0}
    local args="-nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P -t $t -mode $mode"
    [ "$skew" != "0.0" ] && [ "$skew" != "0" ] && args="$args -skew $skew -hot $hot"
    $OMP $args 2>/dev/null
}

run_seq() {
    $SEQ -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P 2>/dev/null
}

validate_mode() {
    local MODE=$1 LOG=$2
    local ERRORS=0

    { echo "=== Validation started: $(date) ==="; echo "mode=$MODE NR=$NR NS=$NS P=$P SEED=$SEED"; } \
        | tee "$LOG"

    local SEQ_OUT SEQ_REF
    SEQ_OUT=$(run_seq)
    SEQ_REF=$(get_triple "$SEQ_OUT")

    echo "" | tee -a "$LOG"
    echo "--- workload=uniform  (reference = hashjoin_seq) ---" | tee -a "$LOG"
    echo "  ref: $(echo "$SEQ_REF" | tr '\n' '  ')" | tee -a "$LOG"

    for T in "${THREADS[@]}"; do
        local OUT TRIPLE STATUS
        OUT=$(run_omp "$MODE" "$T")
        TRIPLE=$(get_triple "$OUT")
        if [ "$TRIPLE" = "$SEQ_REF" ]; then
            STATUS="PASS"
        else
            STATUS="FAIL  [got: $(echo "$TRIPLE" | tr '\n' '  ')]"
            ERRORS=$((ERRORS + 1))
        fi
        printf "  t=%-3d %s\n" "$T" "$STATUS" | tee -a "$LOG"
    done

    local SREF
    SREF=$(get_triple "$(run_omp "$MODE" 1 0.9 4)")

    echo "" | tee -a "$LOG"
    echo "--- workload=skewed  rho=0.9 hot=4  (reference = t=1 same mode) ---" | tee -a "$LOG"
    echo "  ref: $(echo "$SREF" | tr '\n' '  ')" | tee -a "$LOG"

    for T in "${THREADS[@]}"; do
        local OUT TRIPLE STATUS
        OUT=$(run_omp "$MODE" "$T" 0.9 4)
        TRIPLE=$(get_triple "$OUT")
        if [ "$TRIPLE" = "$SREF" ]; then
            STATUS="PASS"
        else
            STATUS="FAIL  [got: $(echo "$TRIPLE" | tr '\n' '  ')]"
            ERRORS=$((ERRORS + 1))
        fi
        printf "  t=%-3d %s\n" "$T" "$STATUS" | tee -a "$LOG"
    done

    echo "" | tee -a "$LOG"
    echo "--- cross-mode: loop vs task  t=4  workload=uniform ---" | tee -a "$LOG"
    local LOOP_T TASK_T
    LOOP_T=$(get_triple "$(run_omp loop 4)")
    TASK_T=$(get_triple "$(run_omp task 4)")
    if [ "$LOOP_T" = "$TASK_T" ]; then
        echo "  PASS" | tee -a "$LOG"
    else
        echo "  FAIL [loop: $(echo "$LOOP_T" | tr '\n' '  ')] [task: $(echo "$TASK_T" | tr '\n' '  ')]" | tee -a "$LOG"
        ERRORS=$((ERRORS + 1))
    fi

    echo "" | tee -a "$LOG"
    echo "--- cross-mode: loop vs task  t=4  workload=skewed ---" | tee -a "$LOG"
    local LOOP_S TASK_S
    LOOP_S=$(get_triple "$(run_omp loop 4 0.9 4)")
    TASK_S=$(get_triple "$(run_omp task 4 0.9 4)")
    if [ "$LOOP_S" = "$TASK_S" ]; then
        echo "  PASS" | tee -a "$LOG"
    else
        echo "  FAIL [loop: $(echo "$LOOP_S" | tr '\n' '  ')] [task: $(echo "$TASK_S" | tr '\n' '  ')]" | tee -a "$LOG"
        ERRORS=$((ERRORS + 1))
    fi

    echo "" | tee -a "$LOG"
    if [ "$ERRORS" -eq 0 ]; then
        echo "=== $MODE VALIDATION: ALL PASS ===" | tee -a "$LOG"
    else
        echo "=== $MODE VALIDATION: $ERRORS ERRORS ===" | tee -a "$LOG"
    fi

    return $ERRORS
}

TOTAL=0
validate_mode loop "$LOG_LOOP" || TOTAL=$((TOTAL + $?))
echo "" | tee -a "$LOG_LOOP"

validate_mode task "$LOG_TASK" || TOTAL=$((TOTAL + $?))
echo "" | tee -a "$LOG_TASK"

echo ""
echo "============================================"
if [ "$TOTAL" -eq 0 ]; then
    echo "RESULT: ALL CHECKS PASS (loop + task)"
else
    echo "RESULT: $TOTAL TOTAL ERRORS"
fi
echo "  loop log : $LOG_LOOP"
echo "  task log : $LOG_TASK"
echo "============================================"

exit $TOTAL
