#!/bin/bash
# Esp.5 (perf) — MISURA il traffico DRAM reale dell'histogram con perf, per confermare
# l'intensità operazionale I = 0.125. Su compute node Ivy Bridge (salloc).
#
# Idea: il kernel histo legge la chiave (8 B) da DRAM e scrive hist[pid] (P elementi, in L1).
# Se I=0.125 = 1 op / 8 byte è corretto, il traffico DRAM deve essere ~8 byte per chiave, cioè
# ~1 miss LLC (linea da 64 B) ogni 8 chiavi (8 chiavi da 8 B = una cache line). perf conta le
# LLC-load-misses: LLC-load-misses * 64 / (N*reps) deve dare ~8 byte/chiave.
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"
g++ -O3 -std=c++20 -march=native -pthread -Wall -Wextra -I"$INC" mem_bw.cpp -o mem_bw

N=200000000; REPS=20   # 20 passate sullo stesso array -> init ammortizzata
LOG="$OUT/perf_oi.txt"; : > "$LOG"

run_perf() {
  local K=$1
  echo "===== kernel=$K  N=$N  reps=$REPS  (single core) =====" | tee -a "$LOG"
  perf stat -e LLC-load-misses,LLC-loads,instructions,cycles \
    ./mem_bw -kernel $K -t 1 -n $N -reps $REPS 2>> "$LOG" 1>/dev/null || true
  echo "" >> "$LOG"
}
run_perf read
run_perf histo

# estrai byte/chiave e istr/chiave
python3 - "$LOG" "$N" "$REPS" <<'PY' | tee "$OUT/perf_oi_summary.txt"
import sys,re
log=open(sys.argv[1]).read(); N=int(sys.argv[2]); R=int(sys.argv[3])
blocks=log.split("===== ")
def num(s):
    return int(s.replace(',','').replace('.',''))
for b in blocks[1:]:
    k=b.split()[0].split('=')[1]
    def get(ev):
        m=re.search(r'([\d.,]+)\s+'+re.escape(ev), b)
        return int(m.group(1).replace(',','').replace('.','')) if m else None
    llm=get('LLC-load-misses'); ins=get('instructions')
    tot=N*R
    if llm: print(f"{k:6s}: LLC-load-misses={llm:>14d}  -> {llm*64/tot:6.2f} byte/chiave (DRAM)")
    if ins: print(f"{k:6s}: instructions   ={ins:>14d}  -> {ins/tot:6.2f} istr/chiave")
PY
echo "[ok] $(hostname): $LOG  $OUT/perf_oi_summary.txt"
