// false_sharing.cpp — dimostra a cosa serve `alignas(64)` sugli accumulatori per-thread.
//
// Nel join parallelo ogni thread accumula il proprio JoinResult; a fine fase il main
// riduce. Il codice consegnato pad-da le slot a 64 B (`struct alignas(64) PaddedResult`)
// per evitare il false sharing: se i k accumulatori stanno nella STESSA cache line, ogni
// scrittura di un thread invalida la copia degli altri -> ping-pong di coerenza sul bus.
//
// Qui k thread scrivono ripetutamente sul proprio slot in un array condiviso `volatile`
// (il volatile impedisce al compilatore di tenere lo slot in un registro: forza la
// scrittura in memoria a ogni iterazione, come farebbe un accumulatore realmente
// aggiornato in memoria a ogni passo):
//   -mode packed : slot adiacenti (stride 8 B, 8 per cache line) -> false sharing
//   -mode padded : slot distanziati 64 B (uno per cache line)    -> niente false sharing
// Stesso numero di scritture in entrambi i casi; cambia solo il layout.
//
// Build: g++ -O3 -std=c++20 -march=native -pthread -Wall false_sharing.cpp -o false_sharing
// Out (CSV): mode,threads,iters,time_ms,checksum

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

static std::uint64_t argu(int c, char** v, const char* k, std::uint64_t d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return std::strtoull(v[i+1], nullptr, 10);
    return d;
}
static std::string args(int c, char** v, const char* k, const char* d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return v[i+1];
    return d;
}

// stride in numero di uint64: 1 = 8 B (packed), 8 = 64 B (una cache line, padded)
static double run(int nt, std::uint64_t iters, int stride_u64, std::uint64_t& checksum) {
    // array condiviso, volatile: ogni scrittura raggiunge la memoria (niente register promotion)
    std::vector<std::uint64_t> buf((std::size_t)nt * stride_u64 + 8, 0);
    volatile std::uint64_t* arr = buf.data();
    auto t0 = Clock::now();
    std::vector<std::thread> th; th.reserve(nt);
    for (int t = 0; t < nt; ++t) {
        th.emplace_back([&, t]() {
            const std::size_t idx = (std::size_t)t * stride_u64;
            std::uint64_t x = (std::uint64_t)t + 1;
            for (std::uint64_t i = 0; i < iters; ++i) {
                arr[idx] = arr[idx] + x;            // read-modify-write su memoria condivisa
                x = x * 6364136223846793005ULL + 1442695040888963407ULL; // LCG anti-CSE
            }
        });
    }
    for (auto& x : th) x.join();
    auto t1 = Clock::now();
    checksum = 0;
    for (int t = 0; t < nt; ++t) checksum += arr[(std::size_t)t * stride_u64];
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main(int argc, char** argv) {
    const std::string mode = args(argc, argv, "-mode", "padded");
    const int nt = static_cast<int>(argu(argc, argv, "-t", 16));
    const std::uint64_t iters = argu(argc, argv, "-iters", 200000000ULL);
    const int reps = static_cast<int>(argu(argc, argv, "-reps", 5));
    const int stride = (mode == "packed") ? 1 : 8;  // 8 B vs 64 B

    double best = 1e18; std::uint64_t cs = 0;
    for (int r = 0; r < reps; ++r) {
        std::uint64_t c;
        double t = run(nt, iters, stride, c);
        if (t < best) best = t;
        cs = c;
    }
    std::printf("%s,%d,%llu,%.3f,%llu\n", mode.c_str(), nt,
                (unsigned long long)iters, best, (unsigned long long)cs);
    return 0;
}
