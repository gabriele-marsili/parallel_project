// loadfactor_bench.cpp — perché il load factor conta (la patologia del linear probing).
//
// Il grafico dell'anatomia (distinct=20k) mostra x1/x2/x4 quasi identici: a quel riempimento
// la tabella è sparsa, il probe è ~1. La differenza esplode SOLO vicino al 100%. Qui misuro
// direttamente il probe al variare del load factor alpha = (chiavi distinte)/(slot tabella),
// dimensionando la tabella per un alpha bersaglio. Le chiavi sono ben mescolate (splitmix64),
// così isolo l'effetto del load factor dalla questione dei bit (Esp.1).
//
// Teoria (Knuth, linear probing, ricerca con successo): probe medio ~ 0.5*(1 + 1/(1-alpha)).
//   alpha=0.5 -> 1.5    alpha=0.9 -> 5.5    alpha=0.98 -> 25.5
//
// Build: g++ -O3 -std=c++20 -march=native -Wall loadfactor_bench.cpp -o loadfactor_bench
// Out (CSV): alpha_target,alpha_actual,n_distinct,table_slots,probe_ns_per_key

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using Clock = std::chrono::steady_clock;
static inline std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
static std::uint64_t argu(int c, char** v, const char* k, std::uint64_t d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return std::strtoull(v[i+1], nullptr, 10);
    return d;
}
static double argf(int c, char** v, const char* k, double d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return std::strtod(v[i+1], nullptr);
    return d;
}

int main(int argc, char** argv) {
    // Tabella FISSA (potenza di 2) e D = alpha * nslots -> load factor esatto, fino a ~0.98.
    // tablelog=17 -> 131072 slot (~2 MB), sta in L3: isola il load factor dagli effetti di cache.
    const int tablelog = static_cast<int>(argu(argc, argv, "-tablelog", 17));
    const double alpha = argf(argc, argv, "-alpha", 0.5);
    const int reps = static_cast<int>(argu(argc, argv, "-reps", 7));

    const std::size_t nslots = (std::size_t)1 << tablelog;
    const std::uint64_t D = (std::uint64_t)(alpha * (double)nslots); // chiavi distinte
    const std::uint32_t mask = (std::uint32_t)(nslots - 1);
    struct Slot { std::uint64_t key = ~0ULL; std::uint32_t cnt = 0; std::uint32_t _p = 0; };

    // chiavi distinte ben mescolate
    std::vector<std::uint64_t> keys(D);
    for (std::uint64_t i = 0; i < D; ++i) keys[i] = splitmix64(i) & 0x3FFFFFFF; // < 2^30 (come le vere)

    double best = 1e18; std::uint64_t sink = 0;
    for (int r = 0; r < reps; ++r) {
        std::vector<Slot> slots(nslots);
        // build
        for (std::uint64_t k : keys) {
            std::uint32_t h = (std::uint32_t)k & mask;
            while (slots[h].key != ~0ULL && slots[h].key != k) h = (h + 1) & mask;
            slots[h].key = k; ++slots[h].cnt;
        }
        // probe (tutte presenti = ricerca con successo)
        auto t0 = Clock::now();
        std::uint64_t acc = 0;
        for (std::uint64_t k : keys) {
            std::uint32_t h = (std::uint32_t)k & mask;
            while (slots[h].key != ~0ULL && slots[h].key != k) h = (h + 1) & mask;
            acc += slots[h].cnt;
        }
        auto t1 = Clock::now();
        double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / D;
        if (ns < best) best = ns;
        sink += acc;
    }
    if (sink == 0xDEADBEEF) std::fprintf(stderr, "%llu", (unsigned long long)sink);
    double a_actual = (double)D / (double)nslots;
    std::printf("%.3f,%.4f,%llu,%zu,%.3f\n", alpha, a_actual, (unsigned long long)D, nslots, best);
    return 0;
}
