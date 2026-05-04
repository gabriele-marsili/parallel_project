CXX      = g++-15
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
	@for d in results report tests; do \
	    [ -d "$$d" ] && zip -r $(ZIPNAME) $$d/ \
	        --exclude='*.aux' --exclude='*.log' --exclude='*.out' \
	        --exclude='*.toc' --exclude='*.png' || true; \
	done
	@echo "Package ready: $(ZIPNAME)"

clean:
	rm -f hashjoin_seq hashjoin_omp hashjoin_omp_runtime

.PHONY: all seq omp cluster cluster_runtime hashjoin_omp_runtime tar clean