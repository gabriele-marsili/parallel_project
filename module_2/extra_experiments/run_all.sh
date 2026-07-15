#!/bin/bash
# Esegue TUTTI gli esperimenti extra del Modulo 2 su UN compute node Ivy Bridge.
# Uso (dentro salloc sul nodo):
#   salloc --partition=normal --nodes=1 --cpus-per-task=32 --exclusive --time=00:40:00
#   bash extra_experiments/run_all.sh
set -euo pipefail
cd "$(dirname "$0")"
echo "########## NODE $(hostname)  $(date) ##########"
g++ --version | head -1

for d in 01_baseline_vs_mine 02_flatcountmap 03_barrier_vs_threadpool 04_join_load_balance 05_histogram_roofline 06_amdahl; do
  echo ""
  echo "==================== $d ===================="
  bash "$d"/run_*.sh
done
echo ""
echo "########## DONE $(hostname) $(date) ##########"
