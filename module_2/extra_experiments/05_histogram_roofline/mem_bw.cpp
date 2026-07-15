// mem_bw.cpp — tetto di banda su Ivy Bridge e verifica che l'Histogram sia memory-bound.
//
// Il report dice: "Histogram ... dominant cost is a sequential scan of N x 8 B ...
// memory-bandwidth-bound (I ~ 0.125 FLOP/byte), so it scales poorly".
//
// Intensita' operazionale dell'histogram:
//   per record: 1 load da 8 B (la chiave) + poche op intere (hash + ++hist[pid]).
//   contando 1 "operazione utile" per record e 8 byte letti  ->  I = 1/8 = 0.125 op/byte.
//   (l'incremento di hist[pid] colpisce un array di P elementi, residente in cache: non
//    aggiunge traffico DRAM significativo -> il traffico e' dominato dagli 8 B/record letti.)
//
// Questo binario misura il TETTO reale del nodo e il kernel histogram effettivo:
//   -kernel read  : somma di N uint64 (solo letture) -> tetto di lettura
//   -kernel copy  : out64[i] = in64[i]               -> STREAM-like (RFO incluso)
//   -kernel histo : ++hist[hash_key(in[i])]          -> il kernel di fase 0 vero
// a -t thread (1 = single core; 16 = full node fisico). Riporta GB/s e op/s.
//
// Build: g++ -O3 -std=c++20 -march=native -pthread -Wall -I../../include mem_bw.cpp -o mem_bw
// Out (CSV): kernel,threads,N,bytes,seconds,GB_s,Gop_s

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "common.hpp"   // hash_key, compute_shift

using Clock = std::chrono::steady_clock;

static std::uint64_t argu(int c, char** v, const char* k, std::uint64_t d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return std::strtoull(v[i+1], nullptr, 10);
    return d;
}
static std::string args(int c, char** v, const char* k, const char* d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return v[i+1];
    return d;
}

int main(int argc, char** argv) {
    const std::string kernel = args(argc, argv, "-kernel", "read");
    const std::size_t N = argu(argc, argv, "-n", 200000000); // 200M u64 = 1.6 GB (fuori cache)
    const int nt = static_cast<int>(argu(argc, argv, "-t", 1));
    const std::uint32_t P = static_cast<std::uint32_t>(argu(argc, argv, "-p", 128));
    const int reps = static_cast<int>(argu(argc, argv, "-reps", 5));
    const unsigned shift = compute_shift(P);

    std::vector<std::uint64_t> in(N);
    for (std::size_t i = 0; i < N; ++i) in[i] = i * 2654435761ULL + 12345ULL;
    std::vector<std::uint64_t> out;
    if (kernel == "copy") out.assign(N, 0);

    double best = 1e18;
    std::uint64_t sink = 0;
    for (int r = 0; r < reps; ++r) {
        auto t0 = Clock::now();
        std::vector<std::thread> th; th.reserve(nt);
        std::vector<std::uint64_t> partial(nt, 0);
        for (int t = 0; t < nt; ++t) {
            th.emplace_back([&, t]{
                const std::size_t a = (N * t) / nt, b = (N * (t + 1)) / nt;
                if (kernel == "read") {
                    std::uint64_t s = 0;
                    for (std::size_t i = a; i < b; ++i) s += in[i];
                    partial[t] = s;
                } else if (kernel == "copy") {
                    for (std::size_t i = a; i < b; ++i) out[i] = in[i];
                } else { // histo
                    std::vector<std::uint32_t> h(P, 0);
                    for (std::size_t i = a; i < b; ++i) ++h[hash_key(in[i], shift)];
                    std::uint64_t s = 0; for (auto v : h) s += v; partial[t] = s;
                }
            });
        }
        for (auto& x : th) x.join();
        auto t1 = Clock::now();
        std::uint64_t acc = 0; for (auto v : partial) acc += v; sink += acc;
        double sec = std::chrono::duration<double>(t1 - t0).count();
        if (sec < best) best = sec;
    }
    if (sink == 0xDEADBEEFULL) std::fprintf(stderr, "%llu", (unsigned long long)sink); // anti-DCE

    // byte contati: read/histo leggono N*8; copy legge N*8 e scrive N*8 (16 B/elem, RFO a parte)
    double bytes = (kernel == "copy") ? (double)N * 16.0 : (double)N * 8.0;
    double gbs = bytes / best / 1e9;
    double gops = (double)N / best / 1e9;
    std::printf("%s,%d,%zu,%.0f,%.4f,%.2f,%.3f\n",
                kernel.c_str(), nt, N, bytes, best, gbs, gops);
    return 0;
}
