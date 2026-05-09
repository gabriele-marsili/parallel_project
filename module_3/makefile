# Pick g++-15 if installed (Homebrew on macOS), otherwise plain g++.
# Command-line override (e.g. `make CXX=g++-13`) always wins.
CXX      = $(shell command -v g++-15 >/dev/null 2>&1 && echo g++-15 || echo g++)
CXXSTD   = -std=c++20
CXXFLAGS = -O3 -Wall -Wextra $(CXXSTD) -Iinclude $(EXTRA_CXXFLAGS)
OMPFLAGS = -fopenmp
ZIPNAME  = ../Modulo3_MarsiliGabriele.zip

all: hashjoin_seq hashjoin_omp

seq: hashjoin_seq

omp: hashjoin_omp

hashjoin_seq: src/hashjoin_seq.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) $< -o $@

hashjoin_omp: src/hashjoin_OpenMP.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) $(OMPFLAGS) $< -o $@

hashjoin_omp_runtime: src/hashjoin_OpenMP.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) $(OMPFLAGS) -DRUNTIME_SCHEDULE $< -o $@

cluster_runtime: src/hashjoin_OpenMP.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) $(OMPFLAGS) -DRUNTIME_SCHEDULE -march=native $< -o hashjoin_omp_runtime

# Cluster build: enable native ISA optimisations.
# Usage: make cluster  (after loading the gcc module on spmcluster)
#        make cluster CXX=g++
cluster: src/hashjoin_seq.cpp src/hashjoin_OpenMP.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) -march=native src/hashjoin_seq.cpp -o hashjoin_seq
	$(CXX) $(CXXFLAGS) $(OMPFLAGS) -march=native src/hashjoin_OpenMP.cpp -o hashjoin_omp

tar:
	$(MAKE) clean
	zip -r $(ZIPNAME) src/ include/ makefile scripts/ README.md
	@[ -d results/cluster ] && zip -r $(ZIPNAME) results/cluster/ --exclude='*.png' || true
	@[ -d report ] && zip -r $(ZIPNAME) report/ \
	    --exclude='report/*.aux' --exclude='report/*.log' \
	    --exclude='report/*.out' --exclude='report/*.toc' \
	    --exclude='report/*.tex' --exclude='report/*.png' \
	    --exclude='report/*.fls' --exclude='report/*.fdb_latexmk' \
	    --exclude='report/*.synctex.gz' || true
	@[ -d tests ] && zip -r $(ZIPNAME) tests/ || true
	@echo "Package ready: $(ZIPNAME)"

clean:
	rm -f hashjoin_seq hashjoin_omp hashjoin_omp_runtime

.PHONY: all seq omp cluster cluster_runtime hashjoin_omp_runtime tar clean