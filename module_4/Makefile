MPICXX   ?= mpicxx
CXXSTD    = -std=c++20
CXXFLAGS  = -O3 -Wall -Wextra -Wpedantic $(CXXSTD) -Iinclude -I../module_3/include $(EXTRA_CXXFLAGS)
OMPFLAGS  = -fopenmp
ZIPNAME   = ../Modulo4_MarsiliGabriele.zip

all: hashjoin_mpi hashjoin_mpi_omp

mpi: hashjoin_mpi

hybrid: hashjoin_mpi_omp

hashjoin_mpi: src/hashjoin_mpi.cpp include/*.hpp
	$(MPICXX) $(CXXFLAGS) $< -o $@

hashjoin_mpi_omp: src/hashjoin_mpi_omp.cpp include/*.hpp
	$(MPICXX) $(CXXFLAGS) $(OMPFLAGS) $< -o $@

# Cluster build: target the spmcluster compute nodes (Ivy Bridge, no AVX2).
# Building with -march=native on the login node (Haswell) emits AVX2 ops
# that fault on the compute nodes; pin the ISA baseline explicitly.
CLUSTER_ARCH ?= -march=ivybridge -mtune=ivybridge
cluster: src/hashjoin_mpi.cpp src/hashjoin_mpi_omp.cpp include/*.hpp
	$(MPICXX) $(CXXFLAGS) $(CLUSTER_ARCH) src/hashjoin_mpi.cpp -o hashjoin_mpi
	$(MPICXX) $(CXXFLAGS) $(OMPFLAGS) $(CLUSTER_ARCH) src/hashjoin_mpi_omp.cpp -o hashjoin_mpi_omp

# Re-build the M3 sequential baseline so speedup comparisons stay honest.
seq_baseline:
	$(MAKE) -C ../module_3 hashjoin_seq

tar:
	$(MAKE) clean
	zip -r $(ZIPNAME) src/ include/ Makefile scripts/ README.md GUIDE.md
	@[ -d results/cluster ] && zip -r $(ZIPNAME) results/cluster/ --exclude='*.png' || true
	@[ -d report ] && zip -r $(ZIPNAME) report/ \
	    --exclude='report/*.aux' --exclude='report/*.log' \
	    --exclude='report/*.out' --exclude='report/*.toc' \
	    --exclude='report/*.fls' --exclude='report/*.fdb_latexmk' \
	    --exclude='report/*.synctex.gz' || true
	@[ -d tests ] && zip -r $(ZIPNAME) tests/ || true
	@echo "Package ready: $(ZIPNAME)"

clean:
	rm -f hashjoin_mpi hashjoin_mpi_omp

.PHONY: all mpi hybrid cluster seq_baseline tar clean
