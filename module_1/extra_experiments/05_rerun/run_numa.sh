#!/bin/bash
# Sensibilita' dell'end-to-end CUDA al dominio NUMA dell'host, su node09.
# Esegue il kernel CUDA legando host+memoria a ogni dominio NUMA con numactl e
# registra la topologia, cosi' che i numeri del grafico abbiano un artefatto.
# Scrive in extra_experiments/05_rerun/results/, senza toccare i CSV consegnati.
set -e
cd "$(dirname "$0")/../.."     # -> module_1
OUT="extra_experiments/05_rerun/results"; mkdir -p "$OUT"
RAW="$OUT/numa_node09.txt"
CSV="$OUT/cuda_numa_measured.csv"
N=100000000; P=256; SEED=42; REPS=11

echo "NODE=$(hostname)  DATE=$(date)" | tee "$RAW"
export PATH=/usr/local/cuda-12.3/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda-12.3/lib64:$LD_LIBRARY_PATH

echo "=== Topologia: nvidia-smi topo -m ===" | tee -a "$RAW"
nvidia-smi topo -m 2>&1 | tee -a "$RAW"
echo "=== Topologia: numactl --hardware ===" | tee -a "$RAW"
numactl --hardware 2>&1 | tee -a "$RAW"
echo "=== GPU ===" | tee -a "$RAW"
nvidia-smi --query-gpu=name,memory.total --format=csv,noheader | tee -a "$RAW"

nvcc -std=c++17 -O3 -I include -gencode arch=compute_80,code=sm_80 \
     src/cuda_kernel.cu -o bin/cuda_kernel

NODES=$(numactl --hardware | awk '/^available:/{print $2}')
echo "=== Sweep NUMA (N=$N P=$P, $NODES domini) ===" | tee -a "$RAW"
echo "numa_node,h2d_ms,kernel_ms,d2h_ms,total_ms,e2e_Mkeys_s,kernel_Mkeys_s" > "$CSV"

set +e   # una run che fallisce non deve abortire lo sweep
for n in $(seq 0 $((NODES-1))); do
  echo "--- numactl --cpunodebind=$n --membind=$n ---" | tee -a "$RAW"
  o=$(numactl --cpunodebind=$n --membind=$n bin/cuda_kernel $N $P $SEED 0 $REPS 2>&1)
  rc=$?
  echo "$o" | tee -a "$RAW"
  if [ $rc -ne 0 ]; then
    echo "[FALLITA: exit=$rc dominio $n]" | tee -a "$RAW"
    echo "$n,FAIL,FAIL,FAIL,FAIL,FAIL,FAIL" >> "$CSV"
    continue
  fi
  # estrae i campi dal formato "  H->D  : 63.271 ms" ecc.
  g() { echo "$o" | grep -m1 -- "$1" | sed -E 's/[^0-9.]*([0-9]+\.[0-9]+).*/\1/'; }
  h2d=$(g 'H->D'); ker=$(g 'Kernel:'); d2h=$(g 'D->H'); tot=$(g 'Totale')
  e2e=$(echo "$o" | grep -m1 'end-to-end' | sed -E 's/[^0-9.]*([0-9]+\.[0-9]+).*/\1/')
  kmk=$(echo "$o" | grep -m1 'solo kernel' | sed -E 's/[^0-9.]*([0-9]+\.[0-9]+).*/\1/')
  echo "$n,$h2d,$ker,$d2h,$tot,$e2e,$kmk" >> "$CSV"
done

echo "=== DONE ===" | tee -a "$RAW"
cat "$CSV"
