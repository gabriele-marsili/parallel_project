// hashjoin_seq.cpp — Sequential reference implementation
//
// Partitioned Hash Join with Duplicates
// Uses the Module-1 hash function (XOR-fold + Fibonacci multiply-shift).
//
// Compile: g++ -O3 -std=c++20 -Wall -Wextra -Iinclude src/hashjoin_seq.cpp -o hashjoin_seq
// Run:     ./hashjoin_seq -nr 5 -ns 8 -seed 13 -max-key 8 -p 4

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

// ============================================================
// Sequential partitioning phases
// ============================================================

// Histogram — O(N)
//
// Intensità operazionale (roofline). Per ogni record il loop fa:
//   - 1 LOAD di 8 byte: la chiave (Record = { uint64_t key }), letta da un array che a
//     N=10M pesa 80 MB >> L3 (20 MB/socket): quindi ogni chiave arriva da DRAM, una volta.
//     -> traffico DRAM = 8 byte / record.
//   - ++hist[pid]: read-modify-write su hist, che ha P elementi (P<=1024 -> <=4 KB): sta in
//     L1, NON genera traffico DRAM. Quindi il denominatore del roofline sono solo gli 8 B.
// Contando 1 "operazione utile" per record (convenzione del report): I = 1/8 = 0.125 op/byte.
// Anche contando l'aritmetica della hash (xor, mul, shift ~= 4-5 op) l'intensità resta ~0.6
// op/byte, ben sotto il ginocchio del roofline -> regime MEMORY-BOUND in ogni caso.
// Misurato (extra_experiments/05_histogram_roofline): ~11 istruzioni/record per l'histogram
// vs ~2.75 per una read pura (perf); e a 16 core l'histogram legge a ~40 GB/s = il tetto
// della read del nodo -> il collo di bottiglia è la banda, non il calcolo (che resta nascosto
// sotto la latenza di memoria). E' per questo che la fase Histogram scala male (2-3x a p=32).
static std::vector<std::size_t> compute_histogram(const std::vector<Record>& rel,
                                                   std::uint32_t P, unsigned shift) {
    std::vector<std::size_t> hist(P, 0);
    for (const auto& rec : rel)
        ++hist[hash_key(rec.key, shift)];
    return hist;
}

// Scatter — O(N), writes into pre-allocated output buffer
static void scatter_partitioned(const std::vector<Record>& rel,
                                unsigned shift,
                                const std::vector<std::size_t>& begin,
                                std::vector<Record>& out) {
    // out must already have rel.size() capacity; no allocation here
    std::vector<std::size_t> next = begin;
    for (const auto& rec : rel) {
        const std::uint32_t pid = hash_key(rec.key, shift);
        out[next[pid]++] = rec;
    }
}

// (partition_relation not used — sequential uses inline phases with timing)

// ============================================================
// Full sequential join pipeline with phase timing
// ============================================================
// Accepts pre-allocated PartitionedRelation buffers (data field sized NR/NS before call).
// Scatter writes directly into Rpart.data / Spart.data — no heap allocation inside the
// timed region. begin/end metadata is filled in here.
static JoinResult partitioned_hash_join_sequential(const std::vector<Record>& R,
                                                    const std::vector<Record>& S,
                                                    std::uint32_t P,
                                                    PhaseTiming& timing,
                                                    PartitionedRelation& Rpart,
                                                    PartitionedRelation& Spart) {
    using Clock = std::chrono::steady_clock;
    const unsigned shift = compute_shift(P);
    auto t0 = Clock::now(), t1 = t0;

    // --- Partition R ---
    t0 = Clock::now();
    const auto hist_R  = compute_histogram(R, P, shift);
    const auto begin_R = exclusive_prefix_sum(hist_R);
    t1 = Clock::now();
    timing.histogram_R = std::chrono::duration<double>(t1 - t0).count();

    t0 = Clock::now();
    scatter_partitioned(R, shift, begin_R, Rpart.data);  // writes into pre-allocated buffer
    t1 = Clock::now();
    timing.scatter_R = std::chrono::duration<double>(t1 - t0).count();

    Rpart.begin = begin_R;
    Rpart.end.resize(P);
    for (std::uint32_t pid = 0; pid < P; ++pid) Rpart.end[pid] = begin_R[pid] + hist_R[pid];

    // --- Partition S ---
    t0 = Clock::now();
    const auto hist_S  = compute_histogram(S, P, shift);
    const auto begin_S = exclusive_prefix_sum(hist_S);
    t1 = Clock::now();
    timing.histogram_S = std::chrono::duration<double>(t1 - t0).count();

    t0 = Clock::now();
    scatter_partitioned(S, shift, begin_S, Spart.data);  // writes into pre-allocated buffer
    t1 = Clock::now();
    timing.scatter_S = std::chrono::duration<double>(t1 - t0).count();

    Spart.begin = begin_S;
    Spart.end.resize(P);
    for (std::uint32_t pid = 0; pid < P; ++pid) Spart.end[pid] = begin_S[pid] + hist_S[pid];

    // --- Local join + accumulation ---
    JoinResult total{};
    t0 = Clock::now();
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        const JoinResult local = join_one_partition(Rpart, Spart, pid);
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

// ============================================================
// Main
// ============================================================
int main(int argc, char** argv) {
    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0;

    if (!read_arg_u64(argc, argv, "-nr", nr) ||
        !read_arg_u64(argc, argv, "-ns", ns) ||
        !read_arg_u64(argc, argv, "-seed", seed) ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p", p)) {
        usage_seq(argv[0]);
        return 1;
    }

    const std::uint32_t P = static_cast<std::uint32_t>(p);
    if (!is_power_of_two(P)) { std::cerr << "Error: P must be a power of two.\n"; return 1; }

    const std::size_t NR = static_cast<std::size_t>(nr);
    const std::size_t NS = static_cast<std::size_t>(ns);

    const auto R = generate_relation(NR, seed, max_key);
    const auto S = generate_relation(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key);

    // Pre-allocate output buffers outside the timed region so that scatter
    // only measures data movement, not heap allocation (~240 MB for NR=10M).
    PartitionedRelation Rpart, Spart;
    Rpart.data.resize(NR);
    Spart.data.resize(NS);

    PhaseTiming timing{};
    const auto t0 = std::chrono::steady_clock::now();
    const JoinResult result = partitioned_hash_join_sequential(R, S, P, timing, Rpart, Spart);
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();

    // Standard output (machine-parseable)
    std::cout << "NR=" << NR << " NS=" << NS << " P=" << P
              << " seed=" << seed << " max_key=" << max_key
              << " threads=1\n";
    std::cout << "join_count=" << result.join_count << "\n";
    std::cout << "checksum1="  << result.checksum1  << "\n";
    std::cout << "checksum2="  << result.checksum2  << "\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "time_sec=" << sec << "\n";

    // Phase breakdown on stderr
    timing.print();

    // Naive verification for tiny inputs
    if (NR <= 500 && NS <= 500) {
        const JoinResult naive = naive_join_verifier(R, S);
        bool ok = (naive.join_count == result.join_count &&
                   naive.checksum1  == result.checksum1 &&
                   naive.checksum2  == result.checksum2);
        std::cout << "naive_verify=" << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok) {
            std::cerr << "MISMATCH: naive_count=" << naive.join_count
                      << " seq_count=" << result.join_count << "\n";
        }
    }

    return 0;
}
