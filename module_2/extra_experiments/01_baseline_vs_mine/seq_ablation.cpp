// seq_ablation.cpp — isola il contributo di OGNI modifica rispetto alla baseline di partenza.
//
// La baseline (src/hashjoin_seq_baseline.cpp) fa due scelte "semplici":
//   (H) partition mapping   : key & (P-1)   ("mod naive", solo bit bassi)
//   (M) build/probe locale  : std::unordered_map<uint64,uint32>   (separate chaining)
//
// La versione consegnata (src/hashjoin_seq.cpp) le sostituisce con:
//   (H) hash Fibonacci multiply-shift a 32 bit (Modulo 1)
//   (M) FlatCountMap (open addressing, linear probing, slot 16 B)
//
// Questo binario esegue le 4 combinazioni {mod,fib} x {umap,flat} sullo STESSO input,
// con lo STESSO timing per-fase, cosi il delta fra due righe = effetto di UNA modifica.
//   V0 = mod  + umap  = baseline di partenza
//   V1 = fib  + umap  = solo hash cambiata
//   V2 = mod  + flat  = solo struttura cambiata
//   V3 = fib  + flat  = la versione consegnata
//
// Build:  g++ -O3 -std=c++20 -march=native -Wall -I../../include seq_ablation.cpp -o seq_ablation
// Run:    ./seq_ablation -nr 10000000 -ns 20000000 -seed 42 -max-key 1000000 -p 128 -hash fib -map flat

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.hpp"           // hash_key, compute_shift
#include "common_structs.hpp"   // Record, JoinResult
#include "generator.hpp"        // generate_relation, splitmix64
#include "join_phases.hpp"      // FlatCountMap, exclusive_prefix_sum

using Clock = std::chrono::steady_clock;
static double secs(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

// ---- mapping selezionabile a compile time (nessun branch nel loop hot) ----
enum class Hash { Mod, Fib };
enum class Map  { Umap, Flat };

template <Hash H>
static inline std::uint32_t part_id(std::uint64_t key, unsigned shift, std::uint32_t maskP) {
    if constexpr (H == Hash::Fib) return hash_key(key, shift);
    else                          return static_cast<std::uint32_t>(key) & maskP; // key & (P-1)
}

// ---- una fase histogram+scatter parametrizzata sulla hash ----
template <Hash H>
static void partition_relation(const std::vector<Record>& rel, std::uint32_t P, unsigned shift,
                               std::uint32_t maskP,
                               std::vector<Record>& out,
                               std::vector<std::size_t>& begin,
                               std::vector<std::size_t>& end,
                               double& t_hist, double& t_scatter) {
    std::vector<std::size_t> hist(P, 0);
    auto t0 = Clock::now();
    for (const auto& rec : rel) ++hist[part_id<H>(rec.key, shift, maskP)];
    begin = exclusive_prefix_sum(hist);
    auto t1 = Clock::now();
    t_hist = secs(t0, t1);

    std::vector<std::size_t> next = begin;
    t0 = Clock::now();
    for (const auto& rec : rel) {
        const std::uint32_t pid = part_id<H>(rec.key, shift, maskP);
        out[next[pid]++] = rec;
    }
    t1 = Clock::now();
    t_scatter = secs(t0, t1);

    end.resize(P);
    for (std::uint32_t p = 0; p < P; ++p) end[p] = begin[p] + hist[p];
}

// ---- join di una partizione, parametrizzato sulla struttura dati ----
template <Map M>
static JoinResult join_partition(const std::vector<Record>& outR, std::size_t rb, std::size_t re,
                                 const std::vector<Record>& outS, std::size_t sb, std::size_t se) {
    JoinResult r{};
    if (rb == re || sb == se) return r;
    if constexpr (M == Map::Flat) {
        FlatCountMap cntR(re - rb);
        for (std::size_t i = rb; i < re; ++i) cntR.increment(outR[i].key);
        for (std::size_t i = sb; i < se; ++i) {
            const std::uint64_t k = outS[i].key;
            const std::uint32_t m = cntR.count(k);
            if (m) { r.join_count += m;
                     r.checksum1 += splitmix64(k) * m;
                     r.checksum2 += splitmix64(k ^ 0x9e3779b97f4a7c15ULL) * m; }
        }
    } else {
        std::unordered_map<std::uint64_t, std::uint32_t> cntR;
        cntR.reserve((re - rb) * 2);
        for (std::size_t i = rb; i < re; ++i) ++cntR[outR[i].key];
        for (std::size_t i = sb; i < se; ++i) {
            const std::uint64_t k = outS[i].key;
            auto it = cntR.find(k);
            if (it != cntR.end()) { const std::uint32_t m = it->second;
                     r.join_count += m;
                     r.checksum1 += splitmix64(k) * m;
                     r.checksum2 += splitmix64(k ^ 0x9e3779b97f4a7c15ULL) * m; }
        }
    }
    return r;
}

template <Hash H, Map M>
static JoinResult run_pipeline(const std::vector<Record>& R, const std::vector<Record>& S,
                               std::uint32_t P, double& t_hist, double& t_scatter,
                               double& t_join, double& t_total) {
    const unsigned shift = compute_shift(P);
    const std::uint32_t maskP = P - 1u;

    std::vector<Record> outR(R.size()), outS(S.size());
    std::vector<std::size_t> beginR, endR, beginS, endS;
    double hR, sR, hS, sS;

    auto t0 = Clock::now();
    partition_relation<H>(R, P, shift, maskP, outR, beginR, endR, hR, sR);
    partition_relation<H>(S, P, shift, maskP, outS, beginS, endS, hS, sS);

    JoinResult total{};
    auto tj0 = Clock::now();
    for (std::uint32_t p = 0; p < P; ++p) {
        const JoinResult loc = join_partition<M>(outR, beginR[p], endR[p], outS, beginS[p], endS[p]);
        total.join_count += loc.join_count;
        total.checksum1  += loc.checksum1;
        total.checksum2  += loc.checksum2;
    }
    auto tj1 = Clock::now();

    t_hist    = hR + hS;
    t_scatter = sR + sS;
    t_join    = secs(tj0, tj1);
    t_total   = secs(t0, tj1);
    return total;
}

static std::uint64_t argu(int argc, char** argv, const char* k, std::uint64_t d) {
    for (int i = 1; i + 1 < argc; ++i) if (!std::strcmp(argv[i], k)) return std::strtoull(argv[i+1], nullptr, 10);
    return d;
}
static std::string args(int argc, char** argv, const char* k, const char* d) {
    for (int i = 1; i + 1 < argc; ++i) if (!std::strcmp(argv[i], k)) return argv[i+1];
    return d;
}

int main(int argc, char** argv) {
    const std::size_t NR = argu(argc, argv, "-nr", 10000000);
    const std::size_t NS = argu(argc, argv, "-ns", 20000000);
    const std::uint64_t seed = argu(argc, argv, "-seed", 42);
    const std::uint64_t max_key = argu(argc, argv, "-max-key", 1000000);
    const std::uint32_t P = static_cast<std::uint32_t>(argu(argc, argv, "-p", 128));
    const std::string hs = args(argc, argv, "-hash", "fib");
    const std::string ms = args(argc, argv, "-map", "flat");
    const int reps = static_cast<int>(argu(argc, argv, "-reps", 5));

    const auto R = generate_relation(NR, seed, max_key);
    const auto S = generate_relation(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key);

    double bh=1e9, bs=1e9, bj=1e9, bt=1e9;
    JoinResult res{};
    for (int r = 0; r < reps; ++r) {
        double h,s,j,t;
        if      (hs=="fib" && ms=="flat") res = run_pipeline<Hash::Fib, Map::Flat>(R,S,P,h,s,j,t);
        else if (hs=="fib" && ms=="umap") res = run_pipeline<Hash::Fib, Map::Umap>(R,S,P,h,s,j,t);
        else if (hs=="mod" && ms=="flat") res = run_pipeline<Hash::Mod, Map::Flat>(R,S,P,h,s,j,t);
        else                              res = run_pipeline<Hash::Mod, Map::Umap>(R,S,P,h,s,j,t);
        if (t < bt) { bt=t; bh=h; bs=s; bj=j; }
    }

    // CSV-friendly su stdout:  hash,map,NR,NS,P,max_key,hist_ms,scatter_ms,join_ms,total_ms,join_count
    std::printf("%s,%s,%zu,%zu,%u,%llu,%.3f,%.3f,%.3f,%.3f,%llu\n",
                hs.c_str(), ms.c_str(), NR, NS, P, (unsigned long long)max_key,
                bh*1e3, bs*1e3, bj*1e3, bt*1e3, (unsigned long long)res.join_count);
    return 0;
}
