// flatmap_bench.cpp — anatomia della FlatCountMap (single thread, solo build+probe).
//
// Isola la fase "join locale di UNA partizione" e la misura al variare di:
//   -impl umap | flat1 | flat2 | flat4
//        umap  = std::unordered_map<uint64,uint32> (la baseline di partenza)
//        flatX = open addressing, tabella dimensionata a next_pow2(distinct * X)
//                flat2 (X=2, load factor <= 50%) e' quella del progetto.
//   -distinct D : numero di chiavi distinte in R (controlla la DIMENSIONE della tabella,
//                 quindi se sta in L2/L3 o spilla in DRAM).
//
// Due domande a cui risponde:
//   (a) FlatCountMap vs unordered_map, a parita di lavoro (quanto vale open addressing).
//   (b) load factor: X=1 (troppe collisioni) vs X=2 (progetto) vs X=4 (spreco di cache).
//   (c) residenza in cache: al crescere di D la tabella supera L2 poi L3 -> ns/probe sale.
//
// Build: g++ -O3 -std=c++20 -march=native -Wall -I../../include flatmap_bench.cpp -o flatmap_bench
// Out (CSV): impl,distinct,nR,nS,table_bytes,build_ns_per_key,probe_ns_per_key,join_count

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "generator.hpp"   // splitmix64_next (RNG) e Record

using Clock = std::chrono::steady_clock;
static double ns(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::nano>(b - a).count();
}

// FlatCountMap con moltiplicatore di sizing come parametro di template (X = load-factor target).
template <unsigned MUL>
struct FlatMap {
    struct Slot { std::uint64_t key = ~0ULL; std::uint32_t cnt = 0; std::uint32_t _p = 0; };
    std::vector<Slot> slots;
    std::uint32_t mask;
    explicit FlatMap(std::size_t r_count) {
        std::size_t n = 1;
        while (n < r_count * MUL) n <<= 1;
        if (n < 2) n = 2;
        slots.assign(n, Slot{});
        mask = static_cast<std::uint32_t>(n - 1);
    }
    std::size_t bytes() const { return slots.size() * sizeof(Slot); }
    inline std::uint32_t slot_of(std::uint64_t k) const { return static_cast<std::uint32_t>(k) & mask; }
    inline void increment(std::uint64_t k) {
        std::uint32_t h = slot_of(k);
        while (slots[h].key != ~0ULL && slots[h].key != k) h = (h + 1) & mask;
        if (slots[h].key == ~0ULL) slots[h].key = k;
        ++slots[h].cnt;
    }
    inline std::uint32_t count(std::uint64_t k) const {
        std::uint32_t h = slot_of(k);
        while (slots[h].key != ~0ULL && slots[h].key != k) h = (h + 1) & mask;
        return (slots[h].key == k) ? slots[h].cnt : 0u;
    }
};

static std::uint64_t argu(int c, char** v, const char* k, std::uint64_t d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return std::strtoull(v[i+1], nullptr, 10);
    return d;
}
static std::string args(int c, char** v, const char* k, const char* d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return v[i+1];
    return d;
}

template <class MAP>
static void bench_flat(const std::vector<Record>& R, const std::vector<Record>& S,
                       int reps, double& build_ns, double& probe_ns, std::size_t& tbytes,
                       std::uint64_t& jc) {
    double bb = 1e18, bp = 1e18; std::uint64_t last = 0; std::size_t tb = 0;
    for (int r = 0; r < reps; ++r) {
        MAP m(R.size());
        tb = m.bytes();
        auto t0 = Clock::now();
        for (const auto& rec : R) m.increment(rec.key);
        auto t1 = Clock::now();
        std::uint64_t jj = 0;
        for (const auto& rec : S) jj += m.count(rec.key);
        auto t2 = Clock::now();
        double b = ns(t0, t1) / R.size();
        double p = ns(t1, t2) / S.size();
        if (b < bb) bb = b;
        if (p < bp) bp = p;
        last = jj;
    }
    build_ns = bb; probe_ns = bp; tbytes = tb; jc = last;
}

static void bench_umap(const std::vector<Record>& R, const std::vector<Record>& S,
                       int reps, double& build_ns, double& probe_ns, std::size_t& tbytes,
                       std::uint64_t& jc) {
    double bb = 1e18, bp = 1e18; std::uint64_t last = 0;
    for (int r = 0; r < reps; ++r) {
        std::unordered_map<std::uint64_t, std::uint32_t> m;
        m.reserve(R.size() * 2);
        auto t0 = Clock::now();
        for (const auto& rec : R) ++m[rec.key];
        auto t1 = Clock::now();
        std::uint64_t jj = 0;
        for (const auto& rec : S) { auto it = m.find(rec.key); if (it != m.end()) jj += it->second; }
        auto t2 = Clock::now();
        double b = ns(t0, t1) / R.size();
        double p = ns(t1, t2) / S.size();
        if (b < bb) bb = b;
        if (p < bp) bp = p;
        last = jj;
    }
    // stima "table_bytes" per unordered_map: nodi + bucket (indicativa, non layout esatto)
    tbytes = R.size() * (sizeof(void*) + 16) + R.size() * sizeof(void*);
    build_ns = bb; probe_ns = bp; jc = last;
}

int main(int argc, char** argv) {
    const std::string impl = args(argc, argv, "-impl", "flat2");
    const std::uint64_t distinct = argu(argc, argv, "-distinct", 20000);
    const std::size_t nR = argu(argc, argv, "-nr", 40000);
    const std::size_t nS = argu(argc, argv, "-ns", 80000);
    const std::uint64_t seed = argu(argc, argv, "-seed", 42);
    const int reps = static_cast<int>(argu(argc, argv, "-reps", 7));

    // R e S di UNA partizione: chiavi in [0, distinct) uniformi (come dentro una partizione fib).
    const auto R = generate_relation(nR, seed, distinct);
    const auto S = generate_relation(nS, seed ^ 0xdeadebdecdeedef1ULL, distinct);

    double build_ns = 0, probe_ns = 0; std::size_t tbytes = 0; std::uint64_t jc = 0;
    if      (impl == "umap")  bench_umap(R, S, reps, build_ns, probe_ns, tbytes, jc);
    else if (impl == "flat1") bench_flat<FlatMap<1>>(R, S, reps, build_ns, probe_ns, tbytes, jc);
    else if (impl == "flat2") bench_flat<FlatMap<2>>(R, S, reps, build_ns, probe_ns, tbytes, jc);
    else if (impl == "flat4") bench_flat<FlatMap<4>>(R, S, reps, build_ns, probe_ns, tbytes, jc);
    else { std::fprintf(stderr, "unknown -impl %s\n", impl.c_str()); return 1; }

    std::printf("%s,%llu,%zu,%zu,%zu,%.3f,%.3f,%llu\n",
                impl.c_str(), (unsigned long long)distinct, nR, nS, tbytes,
                build_ns, probe_ns, (unsigned long long)jc);
    return 0;
}
