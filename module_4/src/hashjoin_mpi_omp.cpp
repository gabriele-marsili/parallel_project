// Module 4 — Distributed partitioned hash join (MPI + OpenMP hybrid).
// Same pipeline as hashjoin_mpi.cpp; the only difference is that this
// translation unit is compiled with -fopenmp, so mpi_pipeline::run uses the
// OpenMP-parallel local join loop guarded by `#ifdef _OPENMP`.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

#include <mpi.h>
#include <omp.h>

#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"
#include "utilities_fns.hpp"
#include "verifier.hpp"
#include "mpi_common.hpp"
#include "mpi_pipeline.hpp"

static void usage_hybrid(const char* prog) {
    std::fprintf(stderr,
        "Usage:\n"
        "  mpirun -n R %s -nr NR -ns NS -seed SEED -max-key K -p P [-t THREADS]\n",
        prog);
}

static PhaseTimingMPI reduce_max_timing(const PhaseTimingMPI& local, MPI_Comm comm) {
    const double in[9] = {
        local.histogram_local, local.scatter_local,
        local.comm_sizes,      local.comm_payload,
        local.histogram_post,  local.scatter_post,
        local.join_local,      local.reduce_final,
        local.total
    };
    double out[9] = {0};
    MPI_Reduce(in, out, 9, MPI_DOUBLE, MPI_MAX, 0, comm);
    PhaseTimingMPI t{};
    t.histogram_local = out[0]; t.scatter_local = out[1];
    t.comm_sizes      = out[2]; t.comm_payload  = out[3];
    t.histogram_post  = out[4]; t.scatter_post  = out[5];
    t.join_local      = out[6]; t.reduce_final  = out[7];
    t.total           = out[8];
    return t;
}

int main(int argc, char** argv) {
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);

    int rank = 0, nranks = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);

    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0, t = 0, hot = 0;
    double rho = 0.0;
    if (!read_arg_u64(argc, argv, "-nr", nr) ||
        !read_arg_u64(argc, argv, "-ns", ns) ||
        !read_arg_u64(argc, argv, "-seed", seed) ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p", p)) {
        if (rank == 0) usage_hybrid(argv[0]);
        MPI_Finalize();
        return 1;
    }
    if (read_arg_u64(argc, argv, "-t", t) && t > 0) {
        omp_set_num_threads(static_cast<int>(t));
    }
    read_arg_double(argc, argv, "-skew", rho);
    read_arg_u64(argc, argv, "-hot", hot);
    const bool skewed = (rho > 0.0 && hot > 0);

    const std::uint32_t P = static_cast<std::uint32_t>(p);
    const std::uint32_t R = static_cast<std::uint32_t>(nranks);
    if (!is_power_of_two(P) || P < R || (P % R) != 0) {
        if (rank == 0) {
            std::fprintf(stderr,
                "Error: P (%u) must be a power of two, >= nranks (%u), "
                "and a multiple of nranks.\n", P, R);
        }
        MPI_Finalize();
        return 1;
    }

    std::size_t r_first = 0, r_last = 0, s_first = 0, s_last = 0;
    block_partition(static_cast<std::size_t>(nr), nranks, rank, r_first, r_last);
    block_partition(static_cast<std::size_t>(ns), nranks, rank, s_first, s_last);

    std::vector<Record> R_local, S_local;
    if (skewed) {
        R_local = generate_skewed_relation_slice(
            static_cast<std::size_t>(nr), r_last - r_first, seed, max_key,
            P, rho, static_cast<std::uint32_t>(hot), r_first);
        S_local = generate_skewed_relation_slice(
            static_cast<std::size_t>(ns), s_last - s_first,
            seed ^ 0xdeadebdecdeedef1ULL, max_key,
            P, rho, static_cast<std::uint32_t>(hot), s_first);
    } else {
        R_local = generate_relation_slice(
            r_last - r_first, seed, max_key, r_first);
        S_local = generate_relation_slice(
            s_last - s_first, seed ^ 0xdeadebdecdeedef1ULL, max_key, s_first);
    }

    PhaseTimingMPI timing{};
    MPI_Barrier(MPI_COMM_WORLD);
    const double t_start = MPI_Wtime();
    const JoinResult result = mpi_pipeline::run(R_local, S_local, P, R,
                                                MPI_COMM_WORLD, timing);
    MPI_Barrier(MPI_COMM_WORLD);
    const double t_end = MPI_Wtime();
    const double wall = t_end - t_start;

    const PhaseTimingMPI tmax = reduce_max_timing(timing, MPI_COMM_WORLD);

    if (rank == 0) {
        std::cout << "NR=" << nr << " NS=" << ns << " P=" << P
                  << " seed=" << seed << " max_key=" << max_key
                  << " ranks=" << nranks
                  << " threads=" << omp_get_max_threads()
                  << " workload=" << (skewed ? "skewed" : "uniform")
                  << " rho=" << rho << " hot=" << hot << "\n";
        std::cout << "join_count=" << result.join_count << "\n";
        std::cout << "checksum1="  << result.checksum1  << "\n";
        std::cout << "checksum2="  << result.checksum2  << "\n";
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "time_sec=" << wall << "\n";
        tmax.print(/*rank=*/-1);
    }

    if (nr <= 500 && ns <= 500 && rank == 0) {
        const auto R_full = generate_relation(static_cast<std::size_t>(nr),
                                              seed, max_key);
        const auto S_full = generate_relation(static_cast<std::size_t>(ns),
                                              seed ^ 0xdeadebdecdeedef1ULL,
                                              max_key);
        const JoinResult naive = naive_join_verifier(R_full, S_full);
        const bool ok = (naive.join_count == result.join_count &&
                         naive.checksum1  == result.checksum1 &&
                         naive.checksum2  == result.checksum2);
        std::cout << "naive_verify=" << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok) {
            std::cerr << "MISMATCH: naive_count=" << naive.join_count
                      << " hybrid_count=" << result.join_count << "\n";
        }
    }

    MPI_Finalize();
    return 0;
}
