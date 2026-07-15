#!/bin/bash
# Esp.5 — tetto di banda e histogram memory-bound. Su compute node Ivy Bridge (salloc).
set -euo pipefail
cd "$(dirname "$0")"
INC=../../include
OUT=results; mkdir -p "$OUT"

echo "[build] su $(hostname)"
g++ -O3 -std=c++20 -march=native -pthread -Wall -Wextra -I"$INC" mem_bw.cpp -o mem_bw

# info CPU/cache del nodo (per il roofline)
{ echo "=== node $(hostname) $(date) ==="; lscpu | grep -Ei 'Model name|Socket|Core|Thread|NUMA|L1d|L2|L3|MHz'; } > "$OUT/node_info.txt" 2>&1 || true

N=200000000  # 200M u64 = 1.6 GB, ben oltre L3
CSV="$OUT/mem_bw.csv"
echo "kernel,threads,N,bytes,seconds,GB_s,Gop_s" > "$CSV"
echo "[bw] read/copy/histo a 1 e 16 thread (N=200M)"
for T in 1 2 4 8 16; do
  for K in read copy histo; do
    ./mem_bw -kernel $K -t $T -n $N -reps 5 >> "$CSV"
  done
  echo "   t=$T done"
done

# likwid se disponibile (tetto indipendente dal nostro codice)
if command -v likwid-bench >/dev/null 2>&1; then
  echo "[likwid] load/copy single-core e full-node" | tee "$OUT/likwid.txt"
  for BENCH in load copy stream; do
    likwid-bench -t $BENCH -w S0:1GB:1  2>&1 | grep -Ei 'MByte|bandwidth' | sed "s/^/${BENCH}_1core: /"  >> "$OUT/likwid.txt" || true
    likwid-bench -t $BENCH -w S0:2GB:16 2>&1 | grep -Ei 'MByte|bandwidth' | sed "s/^/${BENCH}_16core: /" >> "$OUT/likwid.txt" || true
  done
else
  echo "likwid-bench non disponibile sul nodo" > "$OUT/likwid.txt"
fi

echo "[ok] $(hostname): $CSV"
