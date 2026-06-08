MPICXX   ?= mpicxx
CXX      ?= g++
CXXSTD    = -std=c++20
CXXFLAGS  = -O3 -Wall -Wextra -Wpedantic $(CXXSTD) -Iinclude $(EXTRA_CXXFLAGS)
OMPFLAGS  = -fopenmp
ZIPNAME   = ../Modulo4_MarsiliGabriele.zip

all: hashjoin_mpi hashjoin_mpi_omp hashjoin_seq

mpi: hashjoin_mpi

hybrid: hashjoin_mpi_omp

# M3 sequential reference, vendored here so the deliverable is self-contained.
# No MPI, so it builds with the plain C++ compiler, headers resolve via include/.
seq: hashjoin_seq

hashjoin_mpi: src/hashjoin_mpi.cpp include/*.hpp
	$(MPICXX) $(CXXFLAGS) $< -o $@

hashjoin_mpi_omp: src/hashjoin_mpi_omp.cpp include/*.hpp
	$(MPICXX) $(CXXFLAGS) $(OMPFLAGS) $< -o $@

hashjoin_seq: src/hashjoin_seq.cpp include/*.hpp
	$(CXX) $(CXXFLAGS) $< -o $@

# Cluster build: target the spmcluster compute nodes (Ivy Bridge, no AVX2).
# Building with -march=native on the login node (Haswell) emits AVX2 ops
# that fault on the compute nodes, so pin the ISA baseline explicitly.
CLUSTER_ARCH ?= -march=ivybridge -mtune=ivybridge
cluster: src/hashjoin_mpi.cpp src/hashjoin_mpi_omp.cpp src/hashjoin_seq.cpp include/*.hpp
	$(MPICXX) $(CXXFLAGS) $(CLUSTER_ARCH) src/hashjoin_mpi.cpp -o hashjoin_mpi
	$(MPICXX) $(CXXFLAGS) $(OMPFLAGS) $(CLUSTER_ARCH) src/hashjoin_mpi_omp.cpp -o hashjoin_mpi_omp
	$(CXX) $(CXXFLAGS) $(CLUSTER_ARCH) src/hashjoin_seq.cpp -o hashjoin_seq

# Alias kept for the run scripts: the baseline now builds from the local source.
seq_baseline: hashjoin_seq

tar:
	$(MAKE) clean
	rm -f $(ZIPNAME)
	zip -r $(ZIPNAME) src/ include/ Makefile scripts/ README.md
	@[ -d results/cluster ] && zip -r $(ZIPNAME) results/cluster/ --exclude='*.png' || true
	@[ -d report ] && zip -r $(ZIPNAME) report/ \
	    --exclude='report/*.aux' --exclude='report/*.log' \
	    --exclude='report/*.out' --exclude='report/*.toc' \
	    --exclude='report/*.fls' --exclude='report/*.fdb_latexmk' \
	    --exclude='report/*.synctex.gz' --exclude='report/*.png' || true
	@echo "Package ready: $(ZIPNAME)"

clean:
	rm -f hashjoin_mpi hashjoin_mpi_omp hashjoin_seq

.PHONY: all mpi hybrid seq cluster seq_baseline tar clean
