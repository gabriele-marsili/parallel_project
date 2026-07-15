// pingpong_bench.cpp — ping-pong a due rank per stimare i parametri del
// modello di Hockney (alpha = latenza di startup, beta = 1/banda) usato nel
// report M4 per il weak scaling. Il report usa il modello senza averne
// misurato i coefficienti sulla rete del cluster: qui si misurano.
//
// Round-trip su taglie crescenti fra rank 0 e rank (R-1); il piazzamento
// (stesso nodo o nodi diversi) lo decide lo script di lancio. Il tempo
// one-way t(m) = rtt/2 viene poi fittato con t = alpha + beta*m nel plot.
//   -label <s>  etichetta (intra | inter) ricopiata nel CSV

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <mpi.h>

static bool read_str(int argc, char **argv, const char *name, std::string &out) {
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], name)) { out = argv[i + 1]; return true; }
    return false;
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, R = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &R);

    std::string label = "unknown";
    read_str(argc, argv, "-label", label);

    const int peer_lo = 0, peer_hi = R - 1;
    const bool is_lo = (rank == peer_lo), is_hi = (rank == peer_hi);

    if (rank == 0)
        std::printf("label,bytes,iters,t_oneway_us,bw_MBps\n");

    // 8 B .. 32 MiB, potenze di 2
    for (std::size_t bytes = 8; bytes <= (32u << 20); bytes <<= 1) {
        std::vector<char> buf(bytes, 1);
        // meno iterazioni per i messaggi grandi
        const int iters = bytes <= (64u << 10) ? 200 : (bytes <= (1u << 20) ? 50 : 10);

        if (is_lo || is_hi) {
            // warm-up
            for (int w = 0; w < 5; ++w) {
                if (is_lo) {
                    MPI_Send(buf.data(), (int)bytes, MPI_CHAR, peer_hi, 0, MPI_COMM_WORLD);
                    MPI_Recv(buf.data(), (int)bytes, MPI_CHAR, peer_hi, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                } else {
                    MPI_Recv(buf.data(), (int)bytes, MPI_CHAR, peer_lo, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    MPI_Send(buf.data(), (int)bytes, MPI_CHAR, peer_lo, 0, MPI_COMM_WORLD);
                }
            }
            const double t0 = MPI_Wtime();
            for (int it = 0; it < iters; ++it) {
                if (is_lo) {
                    MPI_Send(buf.data(), (int)bytes, MPI_CHAR, peer_hi, 0, MPI_COMM_WORLD);
                    MPI_Recv(buf.data(), (int)bytes, MPI_CHAR, peer_hi, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                } else {
                    MPI_Recv(buf.data(), (int)bytes, MPI_CHAR, peer_lo, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    MPI_Send(buf.data(), (int)bytes, MPI_CHAR, peer_lo, 0, MPI_COMM_WORLD);
                }
            }
            const double dt = MPI_Wtime() - t0;
            if (is_lo) {
                const double oneway_us = dt / (2.0 * iters) * 1e6;
                const double bw = (double)bytes / (dt / (2.0 * iters)) / 1e6;
                std::printf("%s,%zu,%d,%.3f,%.1f\n", label.c_str(), bytes, iters, oneway_us, bw);
                std::fflush(stdout);
            }
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    MPI_Finalize();
    return 0;
}
