/* sequential reference for the partitioned hash join (from module 3)*/

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

// histogram, O(N)
static std::vector<std::size_t> compute_histogram(const std::vector<Record> &rel,
                                                  std::uint32_t P, unsigned shift)
{
    std::vector<std::size_t> hist(P, 0);
    for (const auto &rec : rel)
        ++hist[hash_key(rec.key, shift)];
    return hist;
}

/* scatter, O(N), writes into a pre-allocated output buffer.
templated on the output allocator so it works with both std::allocator and the
default_init_allocator used by PartitionedRelation::data */
template <class OutVec>
static void scatter_partitioned(const std::vector<Record> &rel,
                                unsigned shift,
                                const std::vector<std::size_t> &begin,
                                OutVec &out)
{
    std::vector<std::size_t> next = begin;
    for (const auto &rec : rel)
    {
        const std::uint32_t pid = hash_key(rec.key, shift);
        out[next[pid]++] = rec;
    }
}

/* sequential join pipeline
-> takes pre-allocated PartitionedRelation buffers so the scatter measurement excludes heap allocation */
static JoinResult partitioned_hash_join_sequential(const std::vector<Record> &R,
                                                   const std::vector<Record> &S,
                                                   std::uint32_t P,
                                                   PhaseTiming &timing,
                                                   PartitionedRelation &Rpart,
                                                   PartitionedRelation &Spart)
{
    using Clock = std::chrono::steady_clock;
    const unsigned shift = compute_shift(P);
    auto t0 = Clock::now(), t1 = t0;

    // partition R
    t0 = Clock::now();
    const auto hist_R = compute_histogram(R, P, shift);
    const auto begin_R = exclusive_prefix_sum(hist_R);
    t1 = Clock::now();
    timing.histogram_R = std::chrono::duration<double>(t1 - t0).count();

    t0 = Clock::now();
    scatter_partitioned(R, shift, begin_R, Rpart.data); // into the pre-allocated buffer
    t1 = Clock::now();
    timing.scatter_R = std::chrono::duration<double>(t1 - t0).count();

    Rpart.begin = begin_R;
    Rpart.end.resize(P);
    for (std::uint32_t pid = 0; pid < P; ++pid)
        Rpart.end[pid] = begin_R[pid] + hist_R[pid];

    // partition S
    t0 = Clock::now();
    const auto hist_S = compute_histogram(S, P, shift);
    const auto begin_S = exclusive_prefix_sum(hist_S);
    t1 = Clock::now();
    timing.histogram_S = std::chrono::duration<double>(t1 - t0).count();

    t0 = Clock::now();
    scatter_partitioned(S, shift, begin_S, Spart.data); // into the pre-allocated buffer
    t1 = Clock::now();
    timing.scatter_S = std::chrono::duration<double>(t1 - t0).count();

    Spart.begin = begin_S;
    Spart.end.resize(P);
    for (std::uint32_t pid = 0; pid < P; ++pid)
        Spart.end[pid] = begin_S[pid] + hist_S[pid];

    // local join + accumulation
    JoinResult total{};
    t0 = Clock::now();
    for (std::uint32_t pid = 0; pid < P; ++pid)
    {
        const JoinResult local = join_one_partition(Rpart, Spart, pid);
        total.join_count += local.join_count;
        total.checksum1 += local.checksum1;
        total.checksum2 += local.checksum2;
    }
    t1 = Clock::now();
    timing.join_local = std::chrono::duration<double>(t1 - t0).count();

    timing.total = timing.histogram_R + timing.scatter_R + timing.histogram_S + timing.scatter_S + timing.join_local;
    return total;
}

int main(int argc, char **argv)
{
    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0;
    double skew = 0.0;
    std::uint64_t hot = 4;

    if (!read_arg_u64(argc, argv, "-nr", nr) ||
        !read_arg_u64(argc, argv, "-ns", ns) ||
        !read_arg_u64(argc, argv, "-seed", seed) ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p", p))
    {
        usage_seq(argv[0]);
        return 1;
    }
    read_arg_double(argc, argv, "-skew", skew);
    read_arg_u64(argc, argv, "-hot", hot);

    const std::uint32_t P = static_cast<std::uint32_t>(p);
    if (!is_power_of_two(P))
    {
        std::cerr << "Error: P must be a power of two.\n";
        return 1;
    }

    const std::size_t NR = static_cast<std::size_t>(nr);
    const std::size_t NS = static_cast<std::size_t>(ns);

    std::vector<Record> R, S;
    if (skew > 0.0)
    {
        R = generate_skewed_relation(NR, seed, max_key, P, skew,
                                     static_cast<std::uint32_t>(hot));
        S = generate_skewed_relation(NS, seed ^ S_SEED_OFFSET, max_key, P, skew,
                                     static_cast<std::uint32_t>(hot));
    }
    else
    {
        R = generate_relation(NR, seed, max_key);
        S = generate_relation(NS, seed ^ S_SEED_OFFSET, max_key);
    }

    /* pre-allocate the output buffers outside the timed region so scatter
    measures only data movement, not heap allocation*/
    PartitionedRelation Rpart, Spart;
    Rpart.data.resize(NR);
    Spart.data.resize(NS);

    PhaseTiming timing{};
    const auto t0 = std::chrono::steady_clock::now();
    const JoinResult result = partitioned_hash_join_sequential(R, S, P, timing, Rpart, Spart);
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();

    // machine-parseable output
    std::cout << "NR=" << NR << " NS=" << NS << " P=" << P
              << " seed=" << seed << " max_key=" << max_key
              << " threads=1 skew=" << skew << " hot=" << hot << "\n";
    std::cout << "join_count=" << result.join_count << "\n";
    std::cout << "checksum1=" << result.checksum1 << "\n";
    std::cout << "checksum2=" << result.checksum2 << "\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "time_sec=" << sec << "\n";

    // phase breakdown on stderr
    timing.print();

    // naive verification for tiny inputs
    if (NR <= 500 && NS <= 500)
    {
        const JoinResult naive = naive_join_verifier(R, S);
        bool ok = (naive.join_count == result.join_count &&
                   naive.checksum1 == result.checksum1 &&
                   naive.checksum2 == result.checksum2);
        std::cout << "naive_verify=" << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok)
        {
            std::cerr << "MISMATCH: naive_count=" << naive.join_count
                      << " seq_count=" << result.join_count << "\n";
        }
    }

    return 0;
}
