// alltoallv_bench.cpp — microbenchmark del solo MPI_Alltoallv, isolato dalla
// pipeline. Serve all'esperimento 01: la fase comm_payload del report M4
// anti-scala e a 128 rank è erratica; qui la si riproduce con buffer
// sintetici, così gli sweep (rank count, volume, algoritmo forzato via MCA)
// costano millisecondi e non minuti.
//
// Ogni rank invia -recs-per-rank record uint64 in totale, divisi in parti
// uguali fra gli R rank (stessa forma dello scambio uniforme della pipeline).
// Una rep di warm-up non misurata, poi -reps rep misurate: barrier, timer,
// Alltoallv, riduzione MAX fra i rank. Rank 0 stampa una riga CSV per rep.
//   -label <s>  etichetta libera (es. l'algoritmo MCA forzato) ricopiata nel CSV

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <mpi.h>

static bool read_u64(int argc, char **argv, const char *name, std::uint64_t &out) {
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], name)) { out = std::strtoull(argv[i + 1], nullptr, 10); return true; }
    return false;
}
static bool read_str(int argc, char **argv, const char *name, std::string &out) {
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], name)) { out = argv[i + 1]; return true; }
    return false;
}

static inline std::uint64_t splitmix64_next(std::uint64_t &state) {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, R = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &R);

    std::uint64_t recs = 0, reps = 5;
    std::string label = "default";
    if (!read_u64(argc, argv, "-recs-per-rank", recs)) {
        if (rank == 0) std::fprintf(stderr, "usage: %s -recs-per-rank N [-reps K] [-label s]\n", argv[0]);
        MPI_Finalize();
        return 1;
    }
    read_u64(argc, argv, "-reps", reps);
    read_str(argc, argv, "-label", label);

    const std::uint64_t base = recs / R, extra = recs % R;
    std::vector<int> send_counts(R), send_displs(R), recv_counts(R), recv_displs(R);
    std::uint64_t tot_send = 0;
    for (int r = 0; r < R; ++r) {
        send_counts[r] = static_cast<int>(base + (static_cast<std::uint64_t>(r) < extra ? 1 : 0));
        send_displs[r] = static_cast<int>(tot_send);
        tot_send += send_counts[r];
    }
    MPI_Alltoall(send_counts.data(), 1, MPI_INT, recv_counts.data(), 1, MPI_INT, MPI_COMM_WORLD);
    std::uint64_t tot_recv = 0;
    for (int r = 0; r < R; ++r) { recv_displs[r] = static_cast<int>(tot_recv); tot_recv += recv_counts[r]; }

    std::vector<std::uint64_t> send_buf(tot_send), recv_buf(tot_recv);
    std::uint64_t st = 42 + rank;
    for (auto &v : send_buf) v = splitmix64_next(st);

    if (rank == 0)
        std::printf("label,ranks,recs_per_rank,bytes_per_rank,rep,t_max_s\n");

    for (std::uint64_t rep = 0; rep <= reps; ++rep) {   // rep 0 = warm-up
        MPI_Barrier(MPI_COMM_WORLD);
        const double t0 = MPI_Wtime();
        MPI_Alltoallv(send_buf.data(), send_counts.data(), send_displs.data(), MPI_UINT64_T,
                      recv_buf.data(), recv_counts.data(), recv_displs.data(), MPI_UINT64_T,
                      MPI_COMM_WORLD);
        const double dt = MPI_Wtime() - t0;
        double dt_max = 0.0;
        MPI_Reduce(&dt, &dt_max, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        if (rank == 0 && rep > 0) {
            std::printf("%s,%d,%llu,%llu,%llu,%.6f\n",
                        label.c_str(), R,
                        (unsigned long long)recs,
                        (unsigned long long)(recs * 8ULL),
                        (unsigned long long)rep, dt_max);
            std::fflush(stdout);
        }
    }

    MPI_Finalize();
    return 0;
}
