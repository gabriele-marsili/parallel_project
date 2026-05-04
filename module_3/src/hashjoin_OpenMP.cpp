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

// Parallel partitioning: histogram -> prefix sum -> scatter.
//
// Variables that are shared across threads (local_hists, cursors, hist, offsets)
// must be declared BEFORE the parallel region so OpenMP treats them as shared.
// The function owns its own #pragma omp parallel region so the caller does not
// need to worry about the parallelism level.
//
// t_hist and t_scatter are set to wall-clock seconds for each sub-phase.
static void compute_phases(
    std::uint32_t P,
    const std::vector<Record> &relation,
    unsigned shift,
    PartitionedRelation &part,
    double &t_hist,
    double &t_scatter)
{
    const std::size_t N = relation.size();

    // Declared here (before parallel) -> shared among all threads.
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
        // ── Init: size the outer vectors once T is known; each thread
        //    first-touches its own row to keep memory local on NUMA. ──────────
        #pragma omp single
        {
            const int T = omp_get_num_threads();
            local_hists.resize(T);
            cursors.resize(T);
        }
        // implicit barrier

        const int tid = omp_get_thread_num();
        local_hists[tid].assign(P, 0);
        cursors[tid].resize(P);

        #pragma omp barrier  // all rows initialised before any for

        // ── Phase 1: per-thread histogram ────────────────────────────────────
        #pragma omp single
        h_start = omp_get_wtime();
        // implicit barrier: all threads start histogram at the same time

        #pragma omp for schedule(static)
        for (std::size_t i = 0; i < N; ++i)
            ++local_hists[tid][hash_key(relation[i].key, shift)];
        // implicit barrier: all threads done with histogram before single reads
        // local_hists — removing nowait here was the key correctness fix.

        // ── Phase 2: merge histograms + prefix sum + cursor pre-computation ──
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

            // cursors[t][pid] = start offset for thread t within partition pid.
            // schedule(static) assigns the same input chunks in histogram and
            // scatter, so the per-thread counts from local_hists are exact.
            for (std::uint32_t pid = 0; pid < P; ++pid) {
                std::size_t running = offsets[pid];
                for (int t = 0; t < T; ++t) {
                    cursors[t][pid] = running;
                    running += local_hists[t][pid];
                }
            }

            s_start = omp_get_wtime();
        }
        // implicit barrier after single

        // ── Phase 3: lock-free scatter (each thread writes its own slots) ────
        #pragma omp for schedule(static)
        for (std::size_t i = 0; i < N; ++i) {
            const std::uint32_t pid = hash_key(relation[i].key, shift);
            part.data[cursors[tid][pid]++] = relation[i];
        }
        // implicit barrier

        #pragma omp single
        s_end = omp_get_wtime();
    }

    t_hist    = h_end - h_start;
    t_scatter = s_end - s_start;
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop-level version
// ─────────────────────────────────────────────────────────────────────────────

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
    // schedule(dynamic,1): each thread takes one partition at a time.
    // On skewed input this is crucial — static would give one thread all
    // the large partitions.
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

// ─────────────────────────────────────────────────────────────────────────────
// Task-based version – configuration (A): loop-level partitioning + task join
// ─────────────────────────────────────────────────────────────────────────────

// Per-thread accumulator padded to one cache line to eliminate false sharing.
// alignas(64) forces the size to be a multiple of 64 bytes, so consecutive
// elements in the vector sit on different cache lines.
struct alignas(64) PaddedResult { JoinResult r{}; };
static_assert(sizeof(PaddedResult) == 64,
              "PaddedResult must be exactly one cache line (24 bytes + 40 pad)");

JoinResult run_task(
    const std::vector<Record> &R,
    const std::vector<Record> &S,
    std::uint32_t P,
    PhaseTiming &timing,
    PartitionedRelation &Rpart,
    PartitionedRelation &Spart)
{
    const unsigned shift = compute_shift(P);

    // Phases 1–3 identical to loop-level (partitioning is always loop-level).
    compute_phases(P, R, shift, Rpart, timing.histogram_R, timing.scatter_R);
    compute_phases(P, S, shift, Spart, timing.histogram_S, timing.scatter_S);

    // LPT ordering: generate tasks for the heaviest partitions first to
    // minimise the makespan (longest-processing-time heuristic).
    // Proxy: #records in R-partition + #records in S-partition.
    std::vector<std::uint32_t> order(P);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        const std::size_t wa = (Rpart.end[a] - Rpart.begin[a])
                             + (Spart.end[a] - Spart.begin[a]);
        const std::size_t wb = (Rpart.end[b] - Rpart.begin[b])
                             + (Spart.end[b] - Spart.begin[b]);
        return wa > wb;
    });

    // Per-thread accumulators (size = max threads after omp_set_num_threads).
    const int T = omp_get_max_threads();
    std::vector<PaddedResult> thr_results(T);

    const double j_start = omp_get_wtime();

    #pragma omp parallel \
        default(none) shared(P, order, Rpart, Spart, thr_results)
    {
        // single+nowait: one thread generates all P tasks; the other T-1
        // threads start picking tasks from the queue immediately rather than
        // waiting at the implicit barrier of a plain `single`.
        #pragma omp single nowait
        {
            for (std::uint32_t idx = 0; idx < P; ++idx) {
                const std::uint32_t pid = order[idx];
                // firstprivate(pid): captures the loop variable value at task
                // creation time — mandatory when tasks outlive the iteration.
                #pragma omp task default(none) firstprivate(pid) \
                    shared(Rpart, Spart, thr_results)
                {
                    // Tasks are tied by default: the thread executing this
                    // body is stable, so omp_get_thread_num() is safe.
                    const int tid = omp_get_thread_num();
                    const JoinResult local = join_one_partition(Rpart, Spart, pid);
                    thr_results[tid].r.join_count += local.join_count;
                    thr_results[tid].r.checksum1  += local.checksum1;
                    thr_results[tid].r.checksum2  += local.checksum2;
                }
            }
        }
        // Implicit barrier at end of parallel region: all tasks complete
        // before any thread exits. thr_results is fully populated here.
    }

    timing.join_local = omp_get_wtime() - j_start;

    // Sequential reduction (T is small, overhead is negligible).
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

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

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

    // First-touch NUMA: zero-init dei buffer di scatter dal thread che li scriverà
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
