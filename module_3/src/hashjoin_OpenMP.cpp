#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <omp.h>
#include "utilities_fns.hpp"
#include "generator.hpp"
#include "common_structs.hpp"
#include "join_phases.hpp"
#include "verifier.hpp"

// Histogram + scatter as worksharing for-loops, schedule(static).
static void compute_phases(
    std::uint32_t P,
    const std::vector<Record> &relation,
    unsigned shift,
    PartitionedRelation &part,
    double &t_hist,
    double &t_scatter)
{
    const std::size_t N = relation.size();

    std::vector<std::size_t> hist(P, 0);
    std::vector<std::size_t> offsets(P, 0);
    std::vector<std::vector<std::size_t>> local_hists;
    std::vector<std::vector<std::size_t>> cursors;

    double h_start = 0.0, h_end = 0.0;
    double s_start = 0.0, s_end = 0.0;

    #pragma omp parallel \
        default(none) \
        shared(P, N, relation, shift, part, \
               local_hists, cursors, hist, offsets, \
               h_start, h_end, s_start, s_end)
    {
        #pragma omp single
        {
            const int T = omp_get_num_threads();
            local_hists.resize(T);
            cursors.resize(T);
        }

        // First-touch each thread's row from the thread that will use it,
        // so the page lands on the local NUMA node.
        const int tid = omp_get_thread_num();
        local_hists[tid].assign(P, 0);
        cursors[tid].resize(P);

        #pragma omp barrier

        #pragma omp single
        h_start = omp_get_wtime();

        #pragma omp for schedule(static)
        for (std::size_t i = 0; i < N; ++i)
            ++local_hists[tid][hash_key(relation[i].key, shift)];

        #pragma omp single
        {
            h_end = omp_get_wtime();

            const int T = omp_get_num_threads();
            for (int t = 0; t < T; ++t)
                for (std::uint32_t b = 0; b < P; ++b)
                    hist[b] += local_hists[t][b];

            exclusive_prefix_sum_inplace(hist, offsets);
            part.begin = offsets;
            part.end.resize(P);
            for (std::uint32_t i = 0; i < P; ++i)
                part.end[i] = offsets[i] + hist[i];

            // schedule(static) is identical between histogram and scatter,
            // so the per-thread counts in local_hists give an exact starting
            // offset for thread t inside partition pid.
            for (std::uint32_t pid = 0; pid < P; ++pid) {
                std::size_t running = offsets[pid];
                for (int t = 0; t < T; ++t) {
                    cursors[t][pid] = running;
                    running += local_hists[t][pid];
                }
            }

            s_start = omp_get_wtime();
        }

        // Lock-free scatter: each thread only writes through its own
        // cursors. The destination slot is random with respect to the
        // input order, so the hardware prefetcher does not cover it;
        // the explicit prefetch on the next iteration's slot does.
        constexpr std::size_t PF_DIST = 12;
        #pragma omp for schedule(static)
        for (std::size_t i = 0; i < N; ++i) {
            if (i + PF_DIST < N) {
                const std::uint32_t pid_next =
                    hash_key(relation[i + PF_DIST].key, shift);
                __builtin_prefetch(&part.data[cursors[tid][pid_next]], 1, 0);
            }
            const std::uint32_t pid = hash_key(relation[i].key, shift);
            part.data[cursors[tid][pid]++] = relation[i];
        }

        #pragma omp single
        s_end = omp_get_wtime();
    }

    t_hist    = h_end - h_start;
    t_scatter = s_end - s_start;
}

// Histogram + scatter as T explicit tasks per phase, chunk [N*t/T, N*(t+1)/T).
// local_hists[t] and cursors[t] are indexed by the captured task index t, not
// by omp_get_thread_num(), so counts and offsets stay consistent regardless of
// which thread picks up each task.
static void compute_phases_tasks(
    std::uint32_t P,
    const std::vector<Record> &relation,
    unsigned shift,
    PartitionedRelation &part,
    double &t_hist,
    double &t_scatter)
{
    const std::size_t N = relation.size();
    const int T = omp_get_max_threads();

    std::vector<std::size_t> hist(P, 0);
    std::vector<std::size_t> offsets(P, 0);
    std::vector<std::vector<std::size_t>> local_hists(T, std::vector<std::size_t>(P, 0));
    std::vector<std::vector<std::size_t>> cursors(T, std::vector<std::size_t>(P, 0));

    double h_start = 0.0, h_end = 0.0;
    double s_start = 0.0, s_end = 0.0;

    #pragma omp parallel \
        default(none) \
        shared(P, N, T, relation, shift, part, \
               local_hists, cursors, hist, offsets, \
               h_start, h_end, s_start, s_end)
    {
        #pragma omp single
        {
            h_start = omp_get_wtime();

            #pragma omp taskgroup
            {
                for (int t = 0; t < T; ++t) {
                    const std::size_t lo = (N *  t     ) / T;
                    const std::size_t hi = (N * (t + 1)) / T;
                    #pragma omp task default(none) firstprivate(t, lo, hi) \
                        shared(relation, shift, local_hists)
                    {
                        for (std::size_t i = lo; i < hi; ++i)
                            ++local_hists[t][hash_key(relation[i].key, shift)];
                    }
                }
            }

            h_end = omp_get_wtime();

            for (int t = 0; t < T; ++t)
                for (std::uint32_t b = 0; b < P; ++b)
                    hist[b] += local_hists[t][b];

            exclusive_prefix_sum_inplace(hist, offsets);
            part.begin = offsets;
            part.end.resize(P);
            for (std::uint32_t i = 0; i < P; ++i)
                part.end[i] = offsets[i] + hist[i];

            for (std::uint32_t pid = 0; pid < P; ++pid) {
                std::size_t running = offsets[pid];
                for (int t = 0; t < T; ++t) {
                    cursors[t][pid] = running;
                    running += local_hists[t][pid];
                }
            }

            s_start = omp_get_wtime();

            #pragma omp taskgroup
            {
                for (int t = 0; t < T; ++t) {
                    const std::size_t lo = (N *  t     ) / T;
                    const std::size_t hi = (N * (t + 1)) / T;
                    #pragma omp task default(none) firstprivate(t, lo, hi) \
                        shared(relation, shift, part, cursors)
                    {
                        constexpr std::size_t PF_DIST = 12;
                        for (std::size_t i = lo; i < hi; ++i) {
                            if (i + PF_DIST < hi) {
                                const std::uint32_t pid_next =
                                    hash_key(relation[i + PF_DIST].key, shift);
                                __builtin_prefetch(&part.data[cursors[t][pid_next]], 1, 0);
                            }
                            const std::uint32_t pid = hash_key(relation[i].key, shift);
                            part.data[cursors[t][pid]++] = relation[i];
                        }
                    }
                }
            }

            s_end = omp_get_wtime();
        }
    }

    t_hist    = h_end - h_start;
    t_scatter = s_end - s_start;
}

JoinResult run_loop(
    const std::vector<Record> &R,
    const std::vector<Record> &S,
    std::uint32_t P,
    PhaseTiming &timing,
    PartitionedRelation &Rpart,
    PartitionedRelation &Spart)
{
    const unsigned shift = compute_shift(P);

    compute_phases(P, R, shift, Rpart, timing.histogram_R, timing.scatter_R);
    compute_phases(P, S, shift, Spart, timing.histogram_S, timing.scatter_S);

    std::uint64_t join_count = 0, checksum1 = 0, checksum2 = 0;
    double j_start, j_end;

    j_start = omp_get_wtime();
    // dynamic,1: per-partition cost is unknown ahead of time and varies
    // by two orders of magnitude under skew, so static would saddle one
    // thread with all the heavy partitions.
#ifdef RUNTIME_SCHEDULE
    #pragma omp parallel for schedule(runtime) \
        default(none) shared(P, Rpart, Spart) \
        reduction(+: join_count, checksum1, checksum2)
#else
    #pragma omp parallel for schedule(dynamic,1) \
        default(none) shared(P, Rpart, Spart) \
        reduction(+: join_count, checksum1, checksum2)
#endif
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        const JoinResult local = join_one_partition(Rpart, Spart, pid);
        join_count += local.join_count;
        checksum1  += local.checksum1;
        checksum2  += local.checksum2;
    }
    j_end = omp_get_wtime();
    timing.join_local = j_end - j_start;

    timing.total = timing.histogram_R + timing.scatter_R
                 + timing.histogram_S + timing.scatter_S
                 + timing.join_local;

    return JoinResult{join_count, checksum1, checksum2};
}

// One cache line per slot rules out false sharing on the per-thread reduction.
struct alignas(64) PaddedResult { JoinResult r{}; };
static_assert(sizeof(PaddedResult) == 64,
              "PaddedResult must be exactly one cache line");

JoinResult run_task(
    const std::vector<Record> &R,
    const std::vector<Record> &S,
    std::uint32_t P,
    PhaseTiming &timing,
    PartitionedRelation &Rpart,
    PartitionedRelation &Spart)
{
    const unsigned shift = compute_shift(P);

    compute_phases_tasks(P, R, shift, Rpart, timing.histogram_R, timing.scatter_R);
    compute_phases_tasks(P, S, shift, Spart, timing.histogram_S, timing.scatter_S);

    // LPT: submit partitions in decreasing order of |R_p| + |S_p|, which is
    // a linear cost proxy for the per-partition build+probe.
    std::vector<std::size_t> weights(P);
    std::size_t total_work = 0;
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        weights[pid] = (Rpart.end[pid] - Rpart.begin[pid])
                     + (Spart.end[pid] - Spart.begin[pid]);
        total_work += weights[pid];
    }
    std::vector<std::uint32_t> order(P);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        return weights[a] > weights[b];
    });

    // A partition above 8x the average weight is split internally, so the
    // efficiency <= H/T bound from the four hot partitions is broken.
    const int T = omp_get_max_threads();
    const std::size_t avg_weight = (P > 0) ? (total_work / P) : 0;
    const std::size_t hot_threshold = 8 * avg_weight;

    // K sub-tasks per hot partition, each roughly the size of an average
    // partition. T=1 falls back to no split.
    auto split_count_for = [&](std::size_t w) -> int {
        if (w <= hot_threshold || T <= 1) return 1;
        const std::size_t k_raw = (avg_weight > 0) ? (w / avg_weight) : 1;
        const int k = static_cast<int>(std::min<std::size_t>(k_raw, T));
        return std::max(2, k);
    };

    std::vector<PaddedResult> thr_results(T);
    const double j_start = omp_get_wtime();

    #pragma omp parallel \
        default(none) \
        shared(P, order, weights, hot_threshold, Rpart, Spart, thr_results) \
        firstprivate(split_count_for)
    {
        #pragma omp single nowait
        {
            for (std::uint32_t idx = 0; idx < P; ++idx) {
                const std::uint32_t pid = order[idx];
                const std::size_t   w   = weights[pid];

                if (w <= hot_threshold) {
                    #pragma omp task default(none) firstprivate(pid) \
                        shared(Rpart, Spart, thr_results)
                    {
                        const int tid = omp_get_thread_num();
                        const JoinResult local = join_one_partition(Rpart, Spart, pid);
                        thr_results[tid].r.join_count += local.join_count;
                        thr_results[tid].r.checksum1  += local.checksum1;
                        thr_results[tid].r.checksum2  += local.checksum2;
                    }
                } else {
                    // Hot partition: build the table once, then fan out the
                    // probe over K sub-tasks inside a taskgroup so the table
                    // outlives all readers.
                    const int K = split_count_for(w);
                    #pragma omp task default(none) firstprivate(pid, K) \
                        shared(Rpart, Spart, thr_results)
                    {
                        FlatCountMap tbl = build_table(Rpart, pid);
                        const std::size_t s_begin = Spart.begin[pid];
                        const std::size_t s_end   = Spart.end[pid];
                        const std::size_t s_n     = s_end - s_begin;
                        const std::size_t chunk   = (s_n + K - 1) / K;

                        #pragma omp taskgroup
                        {
                            for (int k = 0; k < K; ++k) {
                                const std::size_t s_lo = s_begin + k * chunk;
                                const std::size_t s_hi = std::min(s_end, s_lo + chunk);
                                if (s_lo >= s_hi) continue;
                                #pragma omp task default(none) \
                                    firstprivate(s_lo, s_hi) \
                                    shared(tbl, Spart, thr_results)
                                {
                                    const int tid = omp_get_thread_num();
                                    const JoinResult local = probe_chunk(tbl, Spart, s_lo, s_hi);
                                    thr_results[tid].r.join_count += local.join_count;
                                    thr_results[tid].r.checksum1  += local.checksum1;
                                    thr_results[tid].r.checksum2  += local.checksum2;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    timing.join_local = omp_get_wtime() - j_start;

    const double acc_start = omp_get_wtime();
    JoinResult result{};
    for (int t = 0; t < T; ++t) {
        result.join_count += thr_results[t].r.join_count;
        result.checksum1  += thr_results[t].r.checksum1;
        result.checksum2  += thr_results[t].r.checksum2;
    }
    timing.accumulation = omp_get_wtime() - acc_start;

    timing.total = timing.histogram_R + timing.scatter_R
                 + timing.histogram_S + timing.scatter_S
                 + timing.join_local + timing.accumulation;

    return result;
}

int main(int argc, char **argv)
{
    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0;
    std::string mode = "loop";
    double skew = 0.0;
    std::uint64_t hot = 4;
    std::uint64_t threads = omp_get_max_threads();

    if (!read_arg_u64(argc, argv, "-nr", nr) ||
        !read_arg_u64(argc, argv, "-ns", ns) ||
        !read_arg_u64(argc, argv, "-seed", seed) ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p", p))
    {
        usage_omp(argv[0]);
        return 1;
    }

    read_arg_str(argc, argv, "-mode", mode);
    read_arg_double(argc, argv, "-skew", skew);
    read_arg_u64(argc, argv, "-hot", hot);
    read_arg_u64(argc, argv, "-t", threads);

    const std::uint32_t P = static_cast<std::uint32_t>(p);
    if (!is_power_of_two(P)) {
        std::cerr << "Error: P must be a power of two.\n";
        return 1;
    }
    if (mode != "loop" && mode != "task") {
        std::cerr << "Error: unknown mode '" << mode << "'. Use 'loop' or 'task'.\n";
        return 1;
    }

    omp_set_num_threads(static_cast<int>(threads));

    const std::size_t NR = static_cast<std::size_t>(nr);
    const std::size_t NS = static_cast<std::size_t>(ns);

    std::vector<Record> R, S;
    if (skew > 0.0) {
        R = generate_skewed_relation(NR, seed,
                                     max_key, P, skew,
                                     static_cast<std::uint32_t>(hot));
        S = generate_skewed_relation(NS, seed ^ 0xdeadebdecdeedef1ULL,
                                     max_key, P, skew,
                                     static_cast<std::uint32_t>(hot));
    } else {
        R = generate_relation(NR, seed, max_key);
        S = generate_relation(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key);
    }

    PartitionedRelation Rpart, Spart;
    Rpart.data.resize(NR);
    Spart.data.resize(NS);

    // First-touch the partition buffers in parallel so each page lands on
    // the NUMA node of the thread that will later scatter into it. The
    // PartitionedRelation::data allocator skips construction, so resize()
    // above leaves the pages untouched and this loop is the real first
    // write that fixes the placement.
    #pragma omp parallel for schedule(static)
    for (std::size_t i = 0; i < NR; ++i) Rpart.data[i].key = 0;
    #pragma omp parallel for schedule(static)
    for (std::size_t i = 0; i < NS; ++i) Spart.data[i].key = 0;

    PhaseTiming timing{};
    JoinResult result{};

    const double t_start = omp_get_wtime();

    if (mode == "loop")
        result = run_loop(R, S, P, timing, Rpart, Spart);
    else
        result = run_task(R, S, P, timing, Rpart, Spart);

    const double t_end = omp_get_wtime();

    std::cout << "NR=" << NR << " NS=" << NS << " P=" << P
              << " seed=" << seed << " max_key=" << max_key
              << " threads=" << threads << " mode=" << mode
              << " skew=" << skew << " hot=" << hot << "\n";
    std::cout << "join_count=" << result.join_count << "\n";
    std::cout << "checksum1="  << result.checksum1  << "\n";
    std::cout << "checksum2="  << result.checksum2  << "\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "time_sec=" << (t_end - t_start) << "\n";

    timing.print();

    if (NR <= 500 && NS <= 500) {
        const JoinResult naive = naive_join_verifier(R, S);
        const bool ok = (naive.join_count == result.join_count &&
                         naive.checksum1  == result.checksum1  &&
                         naive.checksum2  == result.checksum2);
        std::cout << "naive_verify=" << (ok ? "PASS" : "FAIL") << "\n";
        if (!ok) {
            std::cerr << "MISMATCH: naive_count=" << naive.join_count
                      << " omp_count=" << result.join_count << "\n";
        }
    }

    return 0;
}
