#!/bin/bash
# Esp. 9 — confronto M2 vs M3 a parità di parametri.
#
# Il confronto del report (tab:m2) non è a parità: M2 gira con max_key=1M e prende il
# best of 5 (module_2/scripts/bench_slurm.sh:24,35-48), M3 con max_key=5M e media di 3,
# e lo speedup di M2 è calcolato sulla baseline di M3. Le tre asimmetrie favoriscono M2.
#
# Qui: stessi binari, stessi parametri, stessa statistica, entrambe le baseline
# sequenziali misurate a ogni max_key. Ogni rep è una riga: media e best si calcolano a
# valle, così l'effetto della statistica è visibile invece che scelto.
#
# Da eseguire su un compute node Ivy Bridge (dentro sbatch/salloc esclusivo).
set -euo pipefail
cd "$(dirname "$0")"
M2=../../../module_2
M3=../..
OUT=results; mkdir -p "$OUT"

export OMP_PROC_BIND=close OMP_PLACES=cores

echo "[build] M2 e M3 su $(hostname)"
(cd "$M2" && make -s CXX=g++ 2>&1 | tail -1) || true
(cd "$M3" && make -s cluster CXX=g++ 2>&1 | tail -1) || true

for b in "$M2/hashjoin_par" "$M2/hashjoin_seq" "$M3/hashjoin_omp" "$M3/hashjoin_seq"; do
    [ -x "$b" ] || { echo "manca $b" >&2; exit 1; }
done

SEED=42; NR=10000000; NS=20000000; P=128; REPS=5
THREADS="1 2 4 8 16 32"

CSV="$OUT/fair_compare.csv"
echo "impl,max_key,threads,rep,t_s,join_count" > "$CSV"

run_one() { # $1=etichetta $2=max_key $3=threads $4=rep $5...=comando
    local LABEL=$1 MK=$2 T=$3 RID=$4; shift 4
    local OUTP JC TS
    OUTP=$("$@" 2>/dev/null)
    TS=$(echo "$OUTP" | awk -F= '/^time_sec=/{print $2}')
    JC=$(echo "$OUTP" | awk -F= '/^join_count=/{print $2}')
    echo "$LABEL,$MK,$T,$RID,$TS,$JC" >> "$CSV"
}

for MK in 1000000 5000000; do
    echo "[max_key=$MK] baseline sequenziali"
    for RID in $(seq 1 $REPS); do
        run_one m2_seq "$MK" 1 "$RID" \
            "$M2/hashjoin_seq" -nr $NR -ns $NS -seed $SEED -max-key "$MK" -p $P
        run_one m3_seq "$MK" 1 "$RID" \
            "$M3/hashjoin_seq" -nr $NR -ns $NS -seed $SEED -max-key "$MK" -p $P
    done

    for T in $THREADS; do
        for RID in $(seq 1 $REPS); do
            run_one m2_par "$MK" "$T" "$RID" \
                "$M2/hashjoin_par" -nr $NR -ns $NS -seed $SEED -max-key "$MK" -p $P -t "$T"
            run_one m3_loop "$MK" "$T" "$RID" \
                "$M3/hashjoin_omp" -nr $NR -ns $NS -seed $SEED -max-key "$MK" -p $P -t "$T" -mode loop
            run_one m3_task "$MK" "$T" "$RID" \
                "$M3/hashjoin_omp" -nr $NR -ns $NS -seed $SEED -max-key "$MK" -p $P -t "$T" -mode task
        done
        echo "   max_key=$MK T=$T done"
    done
done

echo "[ok] $(hostname): $CSV ($(wc -l < "$CSV") righe)"
