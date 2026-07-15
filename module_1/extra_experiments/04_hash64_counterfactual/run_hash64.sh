#!/bin/bash
# Counterfactual 32 vs 64 bit su node09. Build + bench + conteggio istruzioni.
set -e
cd "$(dirname "$0")"
mkdir -p results
echo "NODE=$(hostname)"; g++ --version | head -1
g++ -std=c++20 -O3 -march=native -mavx2 -mfma -I ../../include avx2_hash64.cpp -o avx2_hash64
./avx2_hash64 100000000 256 11 | tee results/hash64_node09.txt

echo; echo "=== istruzioni di moltiplicazione nel binario (objdump) ==="
echo "vpmulld  (mul 32-bit nativa, kernel avx2_32): $(objdump -d avx2_hash64 | grep -c vpmulld)"
echo "vpmuludq (mul 32x32->64,     kernel avx2_64): $(objdump -d avx2_hash64 | grep -c vpmuludq)"
echo "=== estratto del loop avx2_64 (prime vpmuludq) ==="
objdump -d avx2_hash64 | grep -E 'vpmuludq|vpmulld|vpsllq|vpaddq|vpsrlq' | head -20
