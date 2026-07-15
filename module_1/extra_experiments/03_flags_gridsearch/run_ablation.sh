#!/bin/bash
# Ablation dei flag PARTENDO dalla config autovec consegnata: toglie/cambia un flag
# alla volta per isolarne il contributo. Su node09, N=100M, P=256.
set -e
cd "$(dirname "$0")/../.."      # -> module_1
OUT="extra_experiments/03_flags_gridsearch/results"; mkdir -p "$OUT"
CSV="$OUT/ablation_node09.csv"
echo "config,flags,median_ms,throughput_Mkeys_s" > "$CSV"
echo "NODE=$(hostname)"; g++ --version | head -1
N=100000000; P=256

run() {  # $1=label  $2=flags
  local bin="/tmp/pa_$$"
  g++ $2 -std=c++20 -I include src/plain.cpp -o "$bin" 2>/dev/null
  local line med tput
  line=$("$bin" $N $P 42 0 11 | grep -E 'throughput')
  med=$(echo "$line" | sed -E 's/.*median=([0-9.]+).*/\1/')
  tput=$(echo "$line" | sed -E 's/.*throughput=([0-9.]+).*/\1/')
  printf "%-28s %10s Mkeys/s   [%s]\n" "$1" "$tput" "$2"
  echo "\"$1\",\"$2\",$med,$tput" >> "$CSV"
  rm -f "$bin"
}

BASE="-march=native -mavx2 -mfma -ftree-vectorize -DAUTOVEC_ENABLED"
run "autovec (consegnato)"        "-O3 $BASE"
run "-- tolgo vettorizzazione"    "-O3 -march=native -mavx2 -mfma -fno-tree-vectorize"
run "-- tolgo march=native"       "-O3 -mavx2 -mfma -ftree-vectorize -DAUTOVEC_ENABLED"
run "-- O3 -> O2"                 "-O2 $BASE"
run "-- O3 -> O1"                 "-O1 $BASE"
run "++ aggiungo funroll-loops"   "-O3 $BASE -funroll-loops"
echo "=== CSV -> $CSV ==="; cat "$CSV"
