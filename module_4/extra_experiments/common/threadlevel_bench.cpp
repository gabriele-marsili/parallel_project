// threadlevel_bench.cpp — driver ibrido MPI+OpenMP con il livello di thread
// support richiesto a MPI_Init_thread parametrico. Il report M4 sceglie
// MPI_THREAD_FUNNELED e argomenta che MULTIPLE aggiungerebbe locking interno
// senza beneficio per questo codice: qui la stessa pipeline (riusa
// include/mpi_pipeline.hpp consegnato, non modificato) gira con i due livelli
// e la differenza, se esiste, viene misurata invece che affermata.
//   -threadlevel funneled|multiple   (default funneled)
//   -t threads   -reps K
// Una riga CSV per rep su stdout (rank 0), con il livello richiesto,
// quello fornito dalla libreria e le fasi rank-max.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
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

static const char *level_name(int lv) {
    switch (lv) {
        case MPI_THREAD_SINGLE:     return "single";
        case MPI_THREAD_FUNNELED:   return "funneled";
        case MPI_THREAD_SERIALIZED: return "serialized";
        case MPI_THREAD_MULTIPLE:   return "multiple";
        default:                    return "unknown";
    }
}

int main(int argc, char **argv) {
    // il livello va deciso prima di MPI_Init_thread: parsing manuale anticipato
    std::string tl = "funneled";
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "-threadlevel")) tl = argv[i + 1];
    const int requested = (tl == "multiple") ? MPI_THREAD_MULTIPLE : MPI_THREAD_FUNNELED;

    int provided = 0;
    MPI_Init_thread(&argc, &argv, requested, &provided);

    int rank = 0, nranks = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);

    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0, hot = 0,
                  threads = 1, reps = 1;
    double rho = 0.0;
    if (!read_arg_u64(argc, argv, "-nr", nr) ||
        !read_arg_u64(argc, argv, "-ns", ns) ||
        !read_arg_u64(argc, argv, "-seed", seed) ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p", p)) {
        if (rank == 0)
            std::fprintf(stderr, "usage: %s -nr N -ns N -seed S -max-key K -p P"
                                 " [-t T] [-skew RHO -hot H]"
                                 " [-threadlevel funneled|multiple] [-reps K]\n", argv[0]);
        MPI_Finalize(); return 1;
    }
    read_arg_double(argc, argv, "-skew", rho);
    read_arg_u64(argc, argv, "-hot", hot);
    read_arg_u64(argc, argv, "-t", threads);
    read_arg_u64(argc, argv, "-reps", reps);
    const bool skewed = (rho > 0.0 && hot > 0);

    omp_set_num_threads(static_cast<int>(threads));

    const std::uint32_t P = static_cast<std::uint32_t>(p);
    const std::uint32_t R = static_cast<std::uint32_t>(nranks);
    if (!is_power_of_two(P) || P < R || (P % R) != 0) {
        if (rank == 0) std::fprintf(stderr, "Error: bad P/R combination\n");
        MPI_Finalize(); return 1;
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
            seed ^ S_SEED_OFFSET, max_key,
            P, rho, static_cast<std::uint32_t>(hot), s_first);
    } else {
        R_local = generate_relation_slice(r_last - r_first, seed, max_key, r_first);
        S_local = generate_relation_slice(s_last - s_first, seed ^ S_SEED_OFFSET,
                                          max_key, s_first);
    }

    if (rank == 0)
        std::printf("threadlevel,provided,workload,ranks,threads,P,NR,NS,rep,"
                    "hist_local_ms,scatter_local_ms,comm_sizes_ms,comm_payload_ms,"
                    "hist_post_ms,scatter_post_ms,join_ms,reduce_ms,total_ms,"
                    "wall_s,join_count,checksum1,checksum2\n");

    for (std::uint64_t rep = 0; rep < reps; ++rep) {
        PhaseTimingMPI timing{};
        MPI_Barrier(MPI_COMM_WORLD);
        const double t_start = MPI_Wtime();
        const JoinResult result = mpi_pipeline::run(R_local, S_local, P, R,
                                                    MPI_COMM_WORLD, timing);
        MPI_Barrier(MPI_COMM_WORLD);
        const double wall = MPI_Wtime() - t_start;

        double loc[9] = {timing.histogram_local, timing.scatter_local,
                         timing.comm_sizes, timing.comm_payload,
                         timing.histogram_post, timing.scatter_post,
                         timing.join_local, timing.reduce_final, timing.total};
        double mx[9] = {0};
        MPI_Reduce(loc, mx, 9, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

        if (rank == 0) {
            std::printf("%s,%s,%s,%d,%llu,%u,%llu,%llu,%llu,"
                        "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                        "%.6f,%llu,%llu,%llu\n",
                        tl.c_str(), level_name(provided),
                        skewed ? "skew" : "uniform",
                        nranks, (unsigned long long)threads, P,
                        (unsigned long long)nr, (unsigned long long)ns,
                        (unsigned long long)rep,
                        mx[0]*1e3, mx[1]*1e3, mx[2]*1e3, mx[3]*1e3,
                        mx[4]*1e3, mx[5]*1e3, mx[6]*1e3, mx[7]*1e3, mx[8]*1e3,
                        wall,
                        (unsigned long long)result.join_count,
                        (unsigned long long)result.checksum1,
                        (unsigned long long)result.checksum2);
            std::fflush(stdout);
        }
    }

    MPI_Finalize();
    return 0;
}
