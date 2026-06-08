/* pure MPI driver: CLI parsing, input generation and result reporting
(join pipeline itself in mpi_pipeline.hpp) */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <vector>

#include <mpi.h>

#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"
#include "utilities_fns.hpp"
#include "verifier.hpp"
#include "mpi_common.hpp"
#include "mpi_pipeline.hpp"

static void usage_mpi(const char *prog)
{
    std::fprintf(stderr,
                 "Usage:\n"
                 "  mpirun -n R %s -nr NR -ns NS -seed SEED -max-key K -p P "
                 "[-skew RHO -hot H]\n",
                 prog);
}

// per-phase max-reduction onto rank 0 so the breakdown sees the slowest rank (actual critical path)
static PhaseTimingMPI reduce_max_timing(const PhaseTimingMPI &local, MPI_Comm comm)
{
    const double in[9] = {
        local.histogram_local, local.scatter_local,
        local.comm_sizes, local.comm_payload,
        local.histogram_post, local.scatter_post,
        local.join_local, local.reduce_final,
        local.total};
    double out[9] = {0};
    MPI_Reduce(in, out, 9, MPI_DOUBLE, MPI_MAX, 0, comm);

    // timing (unpack the reduced maxima back into the struct)
    PhaseTimingMPI t{};
    t.histogram_local = out[0];
    t.scatter_local = out[1];
    t.comm_sizes = out[2];
    t.comm_payload = out[3];
    t.histogram_post = out[4];
    t.scatter_post = out[5];
    t.join_local = out[6];
    t.reduce_final = out[7];
    t.total = out[8];
    return t;
}

int main(int argc, char **argv)
{
    // pure MPI: one thread per rank
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_SINGLE, &provided);

    int rank = 0, nranks = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);

    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0, hot = 0;
    double rho = 0.0;
    if (!read_arg_u64(argc, argv, "-nr", nr) ||
        !read_arg_u64(argc, argv, "-ns", ns) ||
        !read_arg_u64(argc, argv, "-seed", seed) ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p", p))
    {
        if (rank == 0)
            usage_mpi(argv[0]);
        MPI_Finalize();
        return 1;
    }
    read_arg_double(argc, argv, "-skew", rho);
    read_arg_u64(argc, argv, "-hot", hot);
    const bool skewed = (rho > 0.0 && hot > 0);

    const std::uint32_t P = static_cast<std::uint32_t>(p);
    const std::uint32_t R = static_cast<std::uint32_t>(nranks);
    
    //checks:
    if (!is_power_of_two(P) || P < R || (P % R) != 0)
    {
        if (rank == 0)
        {
            std::fprintf(stderr,
                         "Error: P (%u) must be a power of two, >= nranks (%u), "
                         "and a multiple of nranks.\n",
                         P, R);
        }
        MPI_Finalize();
        return 1;
    }

    std::size_t r_first = 0, r_last = 0, s_first = 0, s_last = 0;
    block_partition(static_cast<std::size_t>(nr), nranks, rank, r_first, r_last);
    block_partition(static_cast<std::size_t>(ns), nranks, rank, s_first, s_last);

    /*
    • uniform: each rank generates only its slice via splitmix64 skip-ahead, so
    the concatenated output matches the sequential generator byte-for-byte.

    • skewed: the initial hot-key selection does a variable number of PRNG draws
    (rejection sampling), so the per-record skip-ahead offset is unknown and
    each rank regenerates the full relation and slices it, outside timing */
    std::vector<Record> R_local, S_local;
    if (skewed)
    {
        R_local = generate_skewed_relation_slice(
            static_cast<std::size_t>(nr), r_last - r_first, seed, max_key,
            P, rho, static_cast<std::uint32_t>(hot), r_first);
        
        S_local = generate_skewed_relation_slice(
            static_cast<std::size_t>(ns), s_last - s_first,
            seed ^ S_SEED_OFFSET, max_key,
            P, rho, static_cast<std::uint32_t>(hot), s_first);
    }
    else //uniform 
    {
        R_local = generate_relation_slice(
            r_last - r_first, seed, max_key, r_first);
        
        S_local = generate_relation_slice(
            s_last - s_first, seed ^ S_SEED_OFFSET, max_key, s_first);
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

    if (rank == 0)
    {
        std::cout << "NR=" << nr << " NS=" << ns << " P=" << P
                  << " seed=" << seed << " max_key=" << max_key
                  << " ranks=" << nranks
                  << " workload=" << (skewed ? "skewed" : "uniform")
                  << " rho=" << rho << " hot=" << hot << "\n";
        std::cout << "join_count=" << result.join_count << "\n";
        std::cout << "checksum1=" << result.checksum1 << "\n";
        std::cout << "checksum2=" << result.checksum2 << "\n";
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "time_sec=" << wall << "\n";
        tmax.print(/*rank=*/-1);
    }

        
    /*naive verifier check for tiny inputs in O(N^2) (uniform only)
    -> the skewed case is handled by hashjoin_seq
     */
    if (!skewed && nr <= 500 && ns <= 500 && rank == 0)
    {
        const auto R_full = generate_relation(static_cast<std::size_t>(nr),
                                              seed, max_key);
        const auto S_full = generate_relation(static_cast<std::size_t>(ns),
                                              seed ^ S_SEED_OFFSET,
                                              max_key);
        const JoinResult naive = naive_join_verifier(R_full, S_full);
        const bool ok = (naive.join_count == result.join_count &&
                         naive.checksum1 == result.checksum1 &&
                         naive.checksum2 == result.checksum2);
        std::cout << "naive_verify=" << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok)
        {
            std::cerr << "MISMATCH: naive_count=" << naive.join_count
                      << " mpi_count=" << result.join_count << "\n";
        }
    }

    MPI_Finalize();
    return 0;
}
