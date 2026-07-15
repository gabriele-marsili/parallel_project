#!/bin/bash
# Compila i binari degli esperimenti extra M4 sul LOGIN node (come il flusso
# consegnato: -march=ivybridge perché i compute node non hanno l'AVX2 del
# login node). I binari finiscono in bin/.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p bin
INC=../include
ARCH="-march=ivybridge"
FLAGS="-O3 -std=c++20 -Wall -Wextra $ARCH"

echo "[build] alltoallv_bench"
mpicxx $FLAGS common/alltoallv_bench.cpp -o bin/alltoallv_bench
echo "[build] pingpong_bench"
mpicxx $FLAGS common/pingpong_bench.cpp -o bin/pingpong_bench
echo "[build] mpi_remap"
mpicxx $FLAGS -I"$INC" common/mpi_remap.cpp -o bin/mpi_remap
echo "[build] threadlevel_bench"
mpicxx $FLAGS -fopenmp -I"$INC" common/threadlevel_bench.cpp -o bin/threadlevel_bench
echo "[ok] binari in bin/"
