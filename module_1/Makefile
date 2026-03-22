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

# Detect architecture: AVX2 flags solo su x86_64
ARCH := $(shell uname -m)
ifeq ($(ARCH),x86_64)
    OPT_FLAGS := -O3 -march=native -mavx2 -mfma
    VEC_REPORT := -fopt-info-vec-optimized=results/vec_report_optimized.txt \
                  -fopt-info-vec-missed=results/vec_report_missed.txt
else
    OPT_FLAGS := -O3
    VEC_REPORT :=
endif

# Flags for baseline (NO auto-vectorization)
CXXFLAGS_BASELINE := $(CXXFLAGS_COMMON) $(OPT_FLAGS) -fno-tree-vectorize

# Flags for auto-vectorization version (with report)
CXXFLAGS_AUTOVEC := $(CXXFLAGS_COMMON) $(OPT_FLAGS) -ftree-vectorize \
                    -DAUTOVEC_ENABLED $(VEC_REPORT)

# Flags for AVX2 intrinsics version (solo su x86_64)
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

# Su x86_64 (cluster): compila tutto incluso AVX2
# Su arm64 (macOS): compila solo baseline, autovec, datasets
ifeq ($(ARCH),x86_64)
all: dirs baseline autovec avx2 datasets
else
all: dirs baseline autovec datasets
	@echo ""
	@echo "NOTA: avx2 e cuda richiedono il cluster (x86_64 + GPU NVIDIA)"
endif

dirs:
	@mkdir -p $(BINDIR) $(RESDIR)

baseline: dirs
	$(CXX) $(CXXFLAGS_BASELINE) $(SRCDIR)/plain.cpp -o $(BINDIR)/plain_baseline

autovec: dirs
	$(CXX) $(CXXFLAGS_AUTOVEC) $(SRCDIR)/plain.cpp -o $(BINDIR)/plain_autovec

avx2: dirs
ifeq ($(ARCH),x86_64)
	$(CXX) $(CXXFLAGS_AVX2) $(SRCDIR)/avx2.cpp -o $(BINDIR)/avx2
else
	@echo "SKIP avx2: richiede x86_64 (sei su $(ARCH))"
endif

datasets: dirs
	$(CXX) $(CXXFLAGS_COMMON) -O2 $(SRCDIR)/dataset_creator.cpp -o $(BINDIR)/dataset_creator

cuda: dirs
	@if command -v $(NVCC) > /dev/null 2>&1; then \
		$(NVCC) $(NVCCFLAGS) $(SRCDIR)/cuda_kernel.cu -o $(BINDIR)/cuda_kernel; \
	else \
		echo "SKIP cuda: nvcc non trovato (serve il cluster con GPU NVIDIA)"; \
	fi

clean:
	rm -rf $(BINDIR) $(RESDIR)/*.txt

cleandata:
	rm -rf data/*.bin

cleanplots:
	rm -rf $(RESDIR)/plots $(RESDIR)/*.csv

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

# ============================================================================
# Analysis: parse results and generate plots
# ============================================================================
.PHONY: parse plots analysis

parse:
	python3 analysis/parse_results.py

plots: parse
	python3 analysis/plot_results.py

plots-pdf: parse
	python3 analysis/plot_results.py --format pdf

analysis: plots
	@echo "Grafici generati in $(RESDIR)/plots/"
