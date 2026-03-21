# ============================================================================
# Makefile for SPM Module 1 — Partitioned Hash Join: Partition Mapping Kernel
# ============================================================================
#
# Targets:
#   all          — build baseline, autovec, avx2 (and cuda if nvcc available)
#   baseline     — plain C++ with auto-vectorization DISABLED
#   autovec      — plain C++ with auto-vectorization ENABLED
#   avx2         — AVX2 intrinsics version
#   cuda         — CUDA version (optional, requires nvcc)
#   clean        — remove all binaries and reports
#
# Build on spmcluster node09:
#   make all            (builds CPU targets)
#   make cuda           (builds CUDA target, requires nvcc in PATH)
#
# Vectorization reports are saved to results/ directory.
# ============================================================================

CXX       := g++
NVCC      := nvcc

# Common flags
CXXFLAGS_COMMON := -std=c++20 -Wall -Wextra -I include

# Optimization flags (node09 is AMD Zen-based, supports AVX2)
OPT_FLAGS := -O3 -march=native -mavx2 -mfma

# Flags for baseline (NO auto-vectorization)
CXXFLAGS_BASELINE := $(CXXFLAGS_COMMON) $(OPT_FLAGS) -fno-tree-vectorize

# Flags for auto-vectorization version (with report)
CXXFLAGS_AUTOVEC := $(CXXFLAGS_COMMON) $(OPT_FLAGS) -ftree-vectorize \
                    -DAUTOVEC_ENABLED \
                    -fopt-info-vec-optimized=results/vec_report_optimized.txt \
                    -fopt-info-vec-missed=results/vec_report_missed.txt

# Flags for AVX2 intrinsics version
CXXFLAGS_AVX2 := $(CXXFLAGS_COMMON) $(OPT_FLAGS)

# CUDA flags
NVCCFLAGS := -std=c++17 -O3 -I include \
             -gencode arch=compute_80,code=sm_80

# Directories
SRCDIR  := src
BINDIR  := bin
RESDIR  := results

# ============================================================================
# Targets
# ============================================================================

.PHONY: all clean baseline autovec avx2 cuda dirs datasets

all: dirs baseline autovec avx2 datasets

dirs:
	@mkdir -p $(BINDIR) $(RESDIR)

baseline: dirs
	$(CXX) $(CXXFLAGS_BASELINE) $(SRCDIR)/plain.cpp -o $(BINDIR)/plain_baseline

autovec: dirs
	$(CXX) $(CXXFLAGS_AUTOVEC) $(SRCDIR)/plain.cpp -o $(BINDIR)/plain_autovec

avx2: dirs
	$(CXX) $(CXXFLAGS_AVX2) $(SRCDIR)/avx2.cpp -o $(BINDIR)/avx2

datasets: dirs
	$(CXX) $(CXXFLAGS_COMMON) -O2 $(SRCDIR)/datasets/dataset_creator.cpp -o $(BINDIR)/dataset_creator

cuda: dirs
	$(NVCC) $(NVCCFLAGS) $(SRCDIR)/cuda_kernel.cu -o $(BINDIR)/cuda_kernel

clean:
	rm -rf $(BINDIR) $(RESDIR)/*.txt

cleandata:
	rm -rf data/*.bin

# ============================================================================
# Convenience: run quick correctness test (small N)
# ============================================================================
.PHONY: test_correctness test_bench

test_correctness: all
	@echo "=== Correctness test (N=16, P=4) ==="
	@echo "--- Baseline ---"
	$(BINDIR)/plain_baseline 16 4
	@echo ""
	@echo "--- Autovec ---"
	$(BINDIR)/plain_autovec 16 4
	@echo ""
	@echo "--- AVX2 ---"
	$(BINDIR)/avx2 16 4

test_bench: all
	@echo "=== Benchmark (N=100M, P=256) ==="
	@echo "--- Baseline ---"
	$(BINDIR)/plain_baseline 100000000 256
	@echo ""
	@echo "--- Autovec ---"
	$(BINDIR)/plain_autovec 100000000 256
	@echo ""
	@echo "--- AVX2 ---"
	$(BINDIR)/avx2 100000000 256

# Generate default datasets (idempotent — skips existing)
generate_data: datasets
	$(BINDIR)/dataset_creator

# List existing datasets
list_data: datasets
	$(BINDIR)/dataset_creator --list
