//Parallel implementation with C++ threads

/* Parallelization strategy:

• Histogram: Data-parallel MAP with thread-local histograms + sequential merge.
Block distribution
-> Each thread processes N/k contiguous records -> good spatial locality.

• Prefix sum: Sequential — O(P), negligible fraction of total time.
(T∞ = O(P), not worth parallelizing)

• Scatter: Lock-free parallel scatter with pre-computed per-thread offsets.
Leverages local histograms to compute non-overlapping write regions.
No synchronization needed (each thread writes to its own region).
Cache-unfriendly (random writes) -> inherently memory-bound.

• Join local: Cyclic distribution —> thread t joins partitions t, t+k, t+2k, ...
Zero scheduling overhead (no atomics, no sorting).
For uniform hash functions the partition sizes are nearly equal,
so cyclic is almost optimal static load balance with minimal variance.
Padded per-thread results avoid false sharing.

• Thread reuse: Threads are spawned once and reused across phases via std::barrier (C++20).
Eliminates spawn/join overhead between phases (reducing O(p) overhead
in the extended Amdahl's model with linear overhead term).

-> Compile: g++ -O3 -std=c++20 -Wall -Wextra -march=native -pthread -Iinclude src/hashjoin_parallel.cpp -o hashjoin_par
-> Run: ./hashjoin_par -nr 10000000 -ns 20000000 -seed 42 -max-key 100000 -p 128 -t 8
*/

#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>
#include <numeric>

#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"
#include "join_phases.hpp"
#include "utilities_fns.hpp"
#include "verifier.hpp"

// Thread count heuristic:
// Avoids oversubscription and prevents launching threads for tiny workloads.
// min_items_per_thread: below this, a single thread is more efficient than
// paying the thread coordination overhead.
static int compute_thread_count(std::size_t workload_items,
                                int max_threads,
                                std::size_t min_items_per_thread = 8192) {
    if (max_threads <= 1) return 1;
    int useful = static_cast<int>(workload_items / min_items_per_thread);
    if (useful < 1) useful = 1;
    return std::min(useful, max_threads);
}

/*Full parallel pipeline — single thread-team, barrier-synchronized:
All phases run on the SAME set of threads, synchronized by barriers.
This avoids the cost of spawning/joining threads between phases.

(extended) Amdahl with overhead:
S(p) ≤ 1 / [f + O(p) + (1-f)/p]
where O(p) includes thread creation overhead. 
reusing threads => reduce O(p) from ~6 spawn/join cycles to 1.

out_R and out_S must be pre-allocated to size NR and NS respectively before the timed region in main()
*/
static JoinResult partitioned_hash_join_parallel(const std::vector<Record>& R,
                                                  const std::vector<Record>& S,
                                                  std::uint32_t P, int nthreads,
                                                  PhaseTiming& timing,
                                                  std::vector<Record>& out_R,
                                                  std::vector<Record>& out_S) {
    using Clock = std::chrono::steady_clock;
    const unsigned shift = compute_shift(P);
    const std::size_t NR = R.size();
    const std::size_t NS = S.size();

    // Determine actual thread count based on workload
    const int nt = compute_thread_count(std::min(NR, NS), nthreads);

    // ── Shared state across phases ──
    // Thread-local histograms for R and S
    std::vector<std::vector<std::size_t>> local_hists_R(nt, std::vector<std::size_t>(P, 0));
    std::vector<std::vector<std::size_t>> local_hists_S(nt, std::vector<std::size_t>(P, 0));

    // Global histograms, offsets, output arrays
    std::vector<std::size_t> global_hist_R(P, 0), global_hist_S(P, 0);
    std::vector<std::size_t> global_begin_R(P, 0), global_begin_S(P, 0);
    // out_R and out_S are pre-allocated by the caller —> no allocation here.

    // Per-thread scatter offsets
    std::vector<std::vector<std::size_t>> offsets_R(nt, std::vector<std::size_t>(P));
    std::vector<std::vector<std::size_t>> offsets_S(nt, std::vector<std::size_t>(P));

    // Join results — padded to avoid false sharing 
    struct alignas(64) PaddedResult { JoinResult result{}; };
    std::vector<PaddedResult> thr_results(nt);

    // Phase timing (written by thread 0 only)
    Clock::time_point phase_t0, phase_t1;

    // ── Barrier with completion function ──
    // The completion function runs on a single thread between phases,
    // used for the sequential prefix-sum and offset computation.
    int phase = 0;
    auto on_barrier = [&]() noexcept {
        phase_t1 = Clock::now();
        double elapsed = std::chrono::duration<double>(phase_t1 - phase_t0).count();

        switch (phase) {
            case 0: { // After histogram R -> compute prefix sum + scatter offsets for R
                timing.histogram_R = elapsed;
                // Merge local histograms -> global
                for (int t = 0; t < nt; ++t)
                    for (std::uint32_t pid = 0; pid < P; ++pid)
                        global_hist_R[pid] += local_hists_R[t][pid];
                // Prefix sum
                global_begin_R = exclusive_prefix_sum(global_hist_R);
                // Per-thread offsets for lock-free scatter
                for (std::uint32_t pid = 0; pid < P; ++pid) {
                    std::size_t off = global_begin_R[pid];
                    for (int t = 0; t < nt; ++t) {
                        offsets_R[t][pid] = off;
                        off += local_hists_R[t][pid];
                    }
                }
                break;
            }
            case 1: { // After scatter R -> record time
                timing.scatter_R = elapsed;
                break;
            }
            case 2: { // After histogram S -> compute prefix sum + scatter offsets for S
                timing.histogram_S = elapsed;
                for (int t = 0; t < nt; ++t)
                    for (std::uint32_t pid = 0; pid < P; ++pid)
                        global_hist_S[pid] += local_hists_S[t][pid];
                global_begin_S = exclusive_prefix_sum(global_hist_S);
                for (std::uint32_t pid = 0; pid < P; ++pid) {
                    std::size_t off = global_begin_S[pid];
                    for (int t = 0; t < nt; ++t) {
                        offsets_S[t][pid] = off;
                        off += local_hists_S[t][pid];
                    }
                }
                break;
            }
            case 3: { // After scatter S -> record time
                timing.scatter_S = elapsed;
                break;
            }
            case 4: { // After join -> record time
                timing.join_local = elapsed;
                break;
            }
        }
        ++phase;
        phase_t0 = Clock::now();
    };

    std::barrier sync(nt, on_barrier);

    // ── Launch thread team ──
    phase_t0 = Clock::now();

    std::vector<std::thread> threads;
    threads.reserve(nt);
    for (int t = 0; t < nt; ++t) {
        threads.emplace_back([&, t]() {
            // Block ranges for this thread
            const std::size_t r_start = (NR * t) / nt;
            const std::size_t r_end   = (NR * (t + 1)) / nt;
            const std::size_t s_start = (NS * t) / nt;
            const std::size_t s_end   = (NS * (t + 1)) / nt;

            // ── Phase 0: Histogram R ──
            {
                auto& lh = local_hists_R[t];
                for (std::size_t i = r_start; i < r_end; ++i)
                    ++lh[hash_key(R[i].key, shift)];
            }
            sync.arrive_and_wait();

            // ── Phase 1: Scatter R (lock-free) ──
            {
                auto cursor = offsets_R[t]; // local copy on stack
                for (std::size_t i = r_start; i < r_end; ++i) {
                    const std::uint32_t pid = hash_key(R[i].key, shift);
                    out_R[cursor[pid]++] = R[i];
                }
            }
            sync.arrive_and_wait();

            // ── Phase 2: Histogram S ──
            {
                auto& lh = local_hists_S[t];
                for (std::size_t i = s_start; i < s_end; ++i)
                    ++lh[hash_key(S[i].key, shift)];
            }
            sync.arrive_and_wait();

            // ── Phase 3: Scatter S (lock-free) ──
            {
                auto cursor = offsets_S[t];
                for (std::size_t i = s_start; i < s_end; ++i) {
                    const std::uint32_t pid = hash_key(S[i].key, shift);
                    out_S[cursor[pid]++] = S[i];
                }
            }
            sync.arrive_and_wait();

            // ── Phase 4: Local join (cyclic distribution) ──
            // Thread t owns partitions t, t+nt, t+2*nt, ...
            // Zero scheduling overhead: no atomics, no sorting.
            // For uniform hash functions partition sizes are nearly equal,
            // so cyclic is approximately equal to optimal static load balance.
            {
                JoinResult local{};
                for (std::uint32_t pid = static_cast<std::uint32_t>(t); pid < P;
                    pid += static_cast<std::uint32_t>(nt)) {
                    const std::size_t rb = global_begin_R[pid];
                    const std::size_t re = rb + global_hist_R[pid];
                    const std::size_t sb = global_begin_S[pid];
                    const std::size_t se = sb + global_hist_S[pid];
                    if (rb == re || sb == se) continue;

                    FlatCountMap countR(re - rb);
                    for (std::size_t i = rb; i < re; ++i)
                        countR.increment(out_R[i].key);

                    for (std::size_t i = sb; i < se; ++i) {
                        const std::uint64_t key = out_S[i].key;
                        const std::uint32_t m   = countR.count(key);
                        if (m) {
                            local.join_count += m;
                            local.checksum1  += splitmix64(key) * m;
                            local.checksum2  += splitmix64(key ^ 0x9e3779b97f4a7c15ULL) * m;
                        }
                    }
                }
                thr_results[t].result = local;
            }
            sync.arrive_and_wait();
        });
    }
    for (auto& th : threads) th.join();

    // ── Reduce results ──
    JoinResult total{};
    for (int t = 0; t < nt; ++t) {
        total.join_count += thr_results[t].result.join_count;
        total.checksum1  += thr_results[t].result.checksum1;
        total.checksum2  += thr_results[t].result.checksum2;
    }

    timing.total = timing.histogram_R + timing.scatter_R
                 + timing.histogram_S + timing.scatter_S
                 + timing.join_local;
    return total;
}

// ============================================================
// Main
// ============================================================
int main(int argc, char** argv) {
    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0, t = 0;

    if (!read_arg_u64(argc, argv, "-nr", nr) ||
        !read_arg_u64(argc, argv, "-ns", ns) ||
        !read_arg_u64(argc, argv, "-seed", seed) ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p", p)) {
        usage_par(argv[0]);
        return 1;
    }

    if (!read_arg_u64(argc, argv, "-t", t) || t == 0)
        t = std::thread::hardware_concurrency();
    if (t == 0) t = 4;

    const std::uint32_t P = static_cast<std::uint32_t>(p);
    if (!is_power_of_two(P)) { std::cerr << "Error: P must be a power of two.\n"; return 1; }

    const std::size_t NR = static_cast<std::size_t>(nr);
    const std::size_t NS = static_cast<std::size_t>(ns);
    const int nthreads = static_cast<int>(t);

    const auto R = generate_relation(NR, seed, max_key);
    const auto S = generate_relation(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key);

    // Pre-allocate output buffers outside the timed region (~240 MB for NR=10M).
    // The timed region then measures only the algorithm phases, not heap setup.
    std::vector<Record> out_R(NR), out_S(NS);

    PhaseTiming timing{};
    const auto t0 = std::chrono::steady_clock::now();
    const JoinResult result = partitioned_hash_join_parallel(R, S, P, nthreads, timing, out_R, out_S);
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "NR=" << NR << " NS=" << NS << " P=" << P
              << " seed=" << seed << " max_key=" << max_key
              << " threads=" << nthreads << "\n";
    std::cout << "join_count=" << result.join_count << "\n";
    std::cout << "checksum1="  << result.checksum1  << "\n";
    std::cout << "checksum2="  << result.checksum2  << "\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "time_sec=" << sec << "\n";

    timing.print();

    if (NR <= 500 && NS <= 500) {
        const JoinResult naive = naive_join_verifier(R, S);
        bool ok = (naive.join_count == result.join_count &&
                   naive.checksum1  == result.checksum1 &&
                   naive.checksum2  == result.checksum2);
        std::cout << "naive_verify=" << (ok ? "PASS" : "FAIL") << "\n";
    }

    return 0;
}
