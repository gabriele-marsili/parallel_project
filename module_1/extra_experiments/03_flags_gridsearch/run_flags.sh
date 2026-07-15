#!/bin/bash
# Grid search dei flag di compilazione per il kernel scalare (src/plain.cpp).
# Eseguito su node09 dentro srun. Compila plain.cpp con set di flag diversi e
# misura il throughput a N=100M, P=256. Motiva i flag scelti nel Makefile.
set -e
cd "$(dirname "$0")/../.."      # -> module_1
OUT="extra_experiments/03_flags_gridsearch/results"
mkdir -p "$OUT"
CSV="$OUT/flags_node09.csv"
echo "label,flags,median_ms,throughput_Mkeys_s" > "$CSV"
echo "NODE=$(hostname)"; g++ --version | head -1
N=100000000; P=256

run() {  # $1=label  $2=flags
  local bin="/tmp/pg_$$"
  g++ $2 -std=c++20 -I include src/plain.cpp -o "$bin" 2>/dev/null
  local line med tput
  line=$("$bin" $N $P 42 0 11 | grep -E 'throughput')
  med=$(echo "$line" | sed -E 's/.*median=([0-9.]+).*/\1/')
  tput=$(echo "$line" | sed -E 's/.*throughput=([0-9.]+).*/\1/')
  printf "%-26s %10s ms  %10s Mkeys/s   [%s]\n" "$1" "$med" "$tput" "$2"
  echo "\"$1\",\"$2\",$med,$tput" >> "$CSV"
  rm -f "$bin"
}

# dal naive (nessun flag) fino alla config consegnata e oltre
run "O0 (naive)"              "-O0"
run "O1"                      "-O1"
run "O2"                      "-O2"
run "O3"                      "-O3"
run "O3+march"               "-O3 -march=native"
run "O3+march+novec (baseline)" "-O3 -march=native -mavx2 -mfma -fno-tree-vectorize"
run "O3+march+vec (autovec)"    "-O3 -march=native -mavx2 -mfma -ftree-vectorize -DAUTOVEC_ENABLED"
run "O3+march+vec+unroll"       "-O3 -march=native -mavx2 -mfma -ftree-vectorize -funroll-loops -DAUTOVEC_ENABLED"
run "Ofast+march"            "-Ofast -march=native"
echo "=== CSV -> $CSV ==="; cat "$CSV"
