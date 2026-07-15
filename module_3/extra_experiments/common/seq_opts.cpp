// seq_opts.cpp — baseline sequenziale con le ottimizzazioni di sez. 2.4 del
// report M3 (prefetch software su scatter e probe) attivabili a runtime.
// Serve all'esperimento 06: l'efficienza >1 del loop a T basso nel report
// deriva dal confronto con una baseline che NON ha il prefetch; qui si misura
// quanto della differenza a T=1 sparisce dando alla baseline le stesse
// ottimizzazioni. Copia di src/hashjoin_seq.cpp (recuperato dal cluster),
// con -pf-scatter/-pf-probe (0 = off) e -reps.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"
#include "join_phases.hpp"
#include "utilities_fns.hpp"
#include "verifier.hpp"

static std::vector<std::size_t> compute_histogram(const std::vector<Record>& rel,
                                                   std::uint32_t P, unsigned shift) {
    std::vector<std::size_t> hist(P, 0);
    for (const auto& rec : rel)
        ++hist[hash_key(rec.key, shift)];
    return hist;
}

// Scatter con prefetch software parametrico sulla destinazione random
// (stessa ottimizzazione del binario OpenMP consegnato, PF_DIST=12).
template <class OutVec>
static void scatter_partitioned_pf(const std::vector<Record>& rel,
                                   unsigned shift,
                                   const std::vector<std::size_t>& begin,
                                   OutVec& out,
                                   std::size_t pf) {
    const std::size_t N = rel.size();
    std::vector<std::size_t> next = begin;
    for (std::size_t i = 0; i < N; ++i) {
        if (pf && i + pf < N) {
            const std::uint32_t pid_next = hash_key(rel[i + pf].key, shift);
            __builtin_prefetch(&out[next[pid_next]], 1, 0);
        }
        const std::uint32_t pid = hash_key(rel[i].key, shift);
        out[next[pid]++] = rel[i];
    }
}

// Probe con prefetch parametrico (il probe_chunk del header ha PF_DIST=8 fisso).
static inline JoinResult probe_chunk_pf(const FlatCountMap& tbl,
                                        const PartitionedRelation& Spart,
                                        std::size_t s_lo, std::size_t s_hi,
                                        std::size_t pf) {
    JoinResult result{};
    for (std::size_t i = s_lo; i < s_hi; ++i) {
        if (pf && i + pf < s_hi) {
            const std::uint64_t k_next = Spart.data[i + pf].key;
            __builtin_prefetch(&tbl.slots[tbl.slot_of(k_next)], 0, 0);
        }
        const std::uint64_t key = Spart.data[i].key;
        const std::uint32_t m   = tbl.count(key);
        if (m) {
            result.join_count += m;
            result.checksum1  += splitmix64(key) * m;
            result.checksum2  += splitmix64(key ^ 0x9e3779b97f4a7c15ULL) * m;
        }
    }
    return result;
}

static JoinResult join_pipeline(const std::vector<Record>& R,
                                const std::vector<Record>& S,
                                std::uint32_t P,
                                std::size_t pf_scatter, std::size_t pf_probe,
                                PhaseTiming& timing,
                                PartitionedRelation& Rpart,
                                PartitionedRelation& Spart) {
    using Clock = std::chrono::steady_clock;
    const unsigned shift = compute_shift(P);
    auto t0 = Clock::now(), t1 = t0;

    t0 = Clock::now();
    const auto hist_R  = compute_histogram(R, P, shift);
    const auto begin_R = exclusive_prefix_sum(hist_R);
    t1 = Clock::now();
    timing.histogram_R = std::chrono::duration<double>(t1 - t0).count();

    t0 = Clock::now();
    scatter_partitioned_pf(R, shift, begin_R, Rpart.data, pf_scatter);
    t1 = Clock::now();
    timing.scatter_R = std::chrono::duration<double>(t1 - t0).count();

    Rpart.begin = begin_R;
    Rpart.end.resize(P);
    for (std::uint32_t pid = 0; pid < P; ++pid) Rpart.end[pid] = begin_R[pid] + hist_R[pid];

    t0 = Clock::now();
    const auto hist_S  = compute_histogram(S, P, shift);
    const auto begin_S = exclusive_prefix_sum(hist_S);
    t1 = Clock::now();
    timing.histogram_S = std::chrono::duration<double>(t1 - t0).count();

    t0 = Clock::now();
    scatter_partitioned_pf(S, shift, begin_S, Spart.data, pf_scatter);
    t1 = Clock::now();
    timing.scatter_S = std::chrono::duration<double>(t1 - t0).count();

    Spart.begin = begin_S;
    Spart.end.resize(P);
    for (std::uint32_t pid = 0; pid < P; ++pid) Spart.end[pid] = begin_S[pid] + hist_S[pid];

    JoinResult total{};
    t0 = Clock::now();
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        const std::size_t r_begin = Rpart.begin[pid];
        const std::size_t r_end   = Rpart.end[pid];
        const std::size_t s_begin = Spart.begin[pid];
        const std::size_t s_end   = Spart.end[pid];
        if (r_begin == r_end || s_begin == s_end) continue;
        FlatCountMap countR = build_table(Rpart, pid);
        const JoinResult local = probe_chunk_pf(countR, Spart, s_begin, s_end, pf_probe);
        total.join_count += local.join_count;
        total.checksum1  += local.checksum1;
        total.checksum2  += local.checksum2;
    }
    t1 = Clock::now();
    timing.join_local = std::chrono::duration<double>(t1 - t0).count();

    timing.total = timing.histogram_R + timing.scatter_R
                 + timing.histogram_S + timing.scatter_S
                 + timing.join_local;
    return total;
}

int main(int argc, char** argv) {
    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0;
    double skew = 0.0;
    std::uint64_t hot = 4, reps = 1, pf_s = 0, pf_p = 0;

    if (!read_arg_u64(argc, argv, "-nr", nr) ||
        !read_arg_u64(argc, argv, "-ns", ns) ||
        !read_arg_u64(argc, argv, "-seed", seed) ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p", p)) {
        std::cerr << "usage: " << argv[0]
                  << " -nr N -ns N -seed S -max-key K -p P"
                     " [-skew RHO] [-hot H] [-reps R] [-pf-scatter D] [-pf-probe D]\n";
        return 1;
    }
    read_arg_double(argc, argv, "-skew", skew);
    read_arg_u64(argc, argv, "-hot", hot);
    read_arg_u64(argc, argv, "-reps", reps);
    read_arg_u64(argc, argv, "-pf-scatter", pf_s);
    read_arg_u64(argc, argv, "-pf-probe", pf_p);

    const std::uint32_t P = static_cast<std::uint32_t>(p);
    if (!is_power_of_two(P)) { std::cerr << "Error: P must be a power of two.\n"; return 1; }

    const std::size_t NR = static_cast<std::size_t>(nr);
    const std::size_t NS = static_cast<std::size_t>(ns);

    std::vector<Record> R, S;
    if (skew > 0.0) {
        R = generate_skewed_relation(NR, seed, max_key, P, skew,
                                     static_cast<std::uint32_t>(hot));
        S = generate_skewed_relation(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key, P, skew,
                                     static_cast<std::uint32_t>(hot));
    } else {
        R = generate_relation(NR, seed, max_key);
        S = generate_relation(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key);
    }

    const std::string workload = (skew > 0.0) ? "skew" : "uniform";

    std::cout << "impl,workload,pf_scatter,pf_probe,P,NR,NS,rep,"
                 "hist_r_ms,scatter_r_ms,hist_s_ms,scatter_s_ms,join_ms,total_ms,"
                 "join_count,checksum1,checksum2\n";

    for (std::uint64_t rep = 0; rep < reps; ++rep) {
        PartitionedRelation Rpart, Spart;
        Rpart.data.resize(NR);
        Spart.data.resize(NS);

        PhaseTiming timing{};
        const JoinResult result =
            join_pipeline(R, S, P, pf_s, pf_p, timing, Rpart, Spart);

        std::cout << "seq," << workload << ',' << pf_s << ',' << pf_p << ','
                  << P << ',' << NR << ',' << NS << ',' << rep << ','
                  << std::fixed << std::setprecision(3)
                  << timing.histogram_R * 1e3 << ',' << timing.scatter_R * 1e3 << ','
                  << timing.histogram_S * 1e3 << ',' << timing.scatter_S * 1e3 << ','
                  << timing.join_local * 1e3 << ',' << timing.total * 1e3 << ','
                  << result.join_count << ',' << result.checksum1 << ','
                  << result.checksum2 << '\n';
        std::cout.flush();
    }

    return 0;
}
