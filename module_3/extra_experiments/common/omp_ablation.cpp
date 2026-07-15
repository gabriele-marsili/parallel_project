// omp_ablation.cpp — copia strumentata di src/hashjoin_OpenMP.cpp per gli
// esperimenti extra del Modulo 3. Il codice consegnato NON viene toccato:
// questo binario replica la stessa pipeline ma espone come parametri a
// runtime le scelte che nel codice consegnato sono fisse, così ogni
// ablation avviene a parità di binario e di flag di compilazione.
//
// Manopole aggiuntive rispetto al binario consegnato:
//   -sched  <static|static1|dynamic1|dynamic4|dynamic16|guided1|guided16>
//           schedule del join in modalità loop (default dynamic1, come consegnato)
//   -order  <lpt|natural|random>   ordine di sottomissione dei task (default lpt)
//   -hotmul <k>    soglia hot = k * peso medio; 0 = split disattivato (default 8)
//   -pf-scatter <d>  distanza prefetch nello scatter; 0 = off (default 12)
//   -pf-probe   <d>  distanza prefetch nel probe;   0 = off (default 8)
//   -firsttouch <par|seq>  first-touch dei buffer di partizione (default par)
//   -tchunks <n>   numero di task per fase in modalità task (default = T)
//   -reps <r>      ripetizioni; una riga CSV per rep su stdout (default 1)
//
// Compilando con -DNO_NOWAIT il nowait sul single del join task viene rimosso
// (ablation dell'esperimento 02).

#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <random>
#include <string>
#include <omp.h>
#include "utilities_fns.hpp"
#include "generator.hpp"
#include "common_structs.hpp"
#include "join_phases.hpp"
#include "verifier.hpp"

// ---------------------------------------------------------------------------
// Probe con distanza di prefetch parametrica (il codice consegnato usa
// PF_DIST=8 fisso in join_phases.hpp).
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

static inline JoinResult join_one_partition_pf(const PartitionedRelation& Rpart,
                                               const PartitionedRelation& Spart,
                                               std::uint32_t pid,
                                               std::size_t pf) {
    const std::size_t r_begin = Rpart.begin[pid];
    const std::size_t r_end   = Rpart.end[pid];
    const std::size_t s_begin = Spart.begin[pid];
    const std::size_t s_end   = Spart.end[pid];
    if (r_begin == r_end || s_begin == s_end) return JoinResult{};
    FlatCountMap countR = build_table(Rpart, pid);
    return probe_chunk_pf(countR, Spart, s_begin, s_end, pf);
}

// ---------------------------------------------------------------------------
// Histogram + scatter worksharing (variante loop), prefetch parametrico.
static void compute_phases(
    std::uint32_t P,
    const std::vector<Record> &relation,
    unsigned shift,
    PartitionedRelation &part,
    std::size_t pf_scatter,
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
        shared(P, N, relation, shift, part, pf_scatter, \
               local_hists, cursors, hist, offsets, \
               h_start, h_end, s_start, s_end)
    {
        #pragma omp single
        {
            const int T = omp_get_num_threads();
            local_hists.resize(T);
            cursors.resize(T);
        }

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

            for (std::uint32_t pid = 0; pid < P; ++pid) {
                std::size_t running = offsets[pid];
                for (int t = 0; t < T; ++t) {
                    cursors[t][pid] = running;
                    running += local_hists[t][pid];
                }
            }

            s_start = omp_get_wtime();
        }

        #pragma omp for schedule(static)
        for (std::size_t i = 0; i < N; ++i) {
            if (pf_scatter && i + pf_scatter < N) {
                const std::uint32_t pid_next =
                    hash_key(relation[i + pf_scatter].key, shift);
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

// ---------------------------------------------------------------------------
// Histogram + scatter a task espliciti (variante task), numero di task e
// prefetch parametrici. Nel consegnato il numero di task per fase è T.
static void compute_phases_tasks(
    std::uint32_t P,
    const std::vector<Record> &relation,
    unsigned shift,
    PartitionedRelation &part,
    int n_tasks,
    std::size_t pf_scatter,
    double &t_hist,
    double &t_scatter)
{
    const std::size_t N = relation.size();
    const int NT = n_tasks;

    std::vector<std::size_t> hist(P, 0);
    std::vector<std::size_t> offsets(P, 0);
    std::vector<std::vector<std::size_t>> local_hists(NT, std::vector<std::size_t>(P, 0));
    std::vector<std::vector<std::size_t>> cursors(NT, std::vector<std::size_t>(P, 0));

    double h_start = 0.0, h_end = 0.0;
    double s_start = 0.0, s_end = 0.0;

    #pragma omp parallel \
        default(none) \
        shared(P, N, NT, relation, shift, part, pf_scatter, \
               local_hists, cursors, hist, offsets, \
               h_start, h_end, s_start, s_end)
    {
        #pragma omp single
        {
            h_start = omp_get_wtime();

            #pragma omp taskgroup
            {
                for (int t = 0; t < NT; ++t) {
                    const std::size_t lo = (N * (std::size_t)t      ) / NT;
                    const std::size_t hi = (N * (std::size_t)(t + 1)) / NT;
                    #pragma omp task default(none) firstprivate(t, lo, hi) \
                        shared(relation, shift, local_hists)
                    {
                        for (std::size_t i = lo; i < hi; ++i)
                            ++local_hists[t][hash_key(relation[i].key, shift)];
                    }
                }
            }

            h_end = omp_get_wtime();

            for (int t = 0; t < NT; ++t)
                for (std::uint32_t b = 0; b < P; ++b)
                    hist[b] += local_hists[t][b];

            exclusive_prefix_sum_inplace(hist, offsets);
            part.begin = offsets;
            part.end.resize(P);
            for (std::uint32_t i = 0; i < P; ++i)
                part.end[i] = offsets[i] + hist[i];

            for (std::uint32_t pid = 0; pid < P; ++pid) {
                std::size_t running = offsets[pid];
                for (int t = 0; t < NT; ++t) {
                    cursors[t][pid] = running;
                    running += local_hists[t][pid];
                }
            }

            s_start = omp_get_wtime();

            #pragma omp taskgroup
            {
                for (int t = 0; t < NT; ++t) {
                    const std::size_t lo = (N * (std::size_t)t      ) / NT;
                    const std::size_t hi = (N * (std::size_t)(t + 1)) / NT;
                    #pragma omp task default(none) firstprivate(t, lo, hi) \
                        shared(relation, shift, part, cursors, pf_scatter)
                    {
                        for (std::size_t i = lo; i < hi; ++i) {
                            if (pf_scatter && i + pf_scatter < hi) {
                                const std::uint32_t pid_next =
                                    hash_key(relation[i + pf_scatter].key, shift);
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

// ---------------------------------------------------------------------------
struct Knobs {
    // schedule del join loop
    omp_sched_t sched_kind = omp_sched_dynamic;
    int         sched_chunk = 1;
    std::string sched_name = "dynamic1";
    // variante task
    std::string order = "lpt";      // lpt | natural | random
    std::uint64_t hotmul = 8;       // 0 = split off
    int tchunks = 0;                // 0 = usa T
    // prefetch
    std::size_t pf_scatter = 12;
    std::size_t pf_probe   = 8;
    // NUMA
    std::string firsttouch = "par"; // par | seq
};

static JoinResult run_loop(
    const std::vector<Record> &R,
    const std::vector<Record> &S,
    std::uint32_t P,
    const Knobs &kn,
    PhaseTiming &timing,
    PartitionedRelation &Rpart,
    PartitionedRelation &Spart)
{
    const unsigned shift = compute_shift(P);

    compute_phases(P, R, shift, Rpart, kn.pf_scatter, timing.histogram_R, timing.scatter_R);
    compute_phases(P, S, shift, Spart, kn.pf_scatter, timing.histogram_S, timing.scatter_S);

    std::uint64_t join_count = 0, checksum1 = 0, checksum2 = 0;
    const std::size_t pf_probe = kn.pf_probe;

    const double j_start = omp_get_wtime();
    #pragma omp parallel for schedule(runtime) \
        default(none) shared(P, Rpart, Spart, pf_probe) \
        reduction(+: join_count, checksum1, checksum2)
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        const JoinResult local = join_one_partition_pf(Rpart, Spart, pid, pf_probe);
        join_count += local.join_count;
        checksum1  += local.checksum1;
        checksum2  += local.checksum2;
    }
    timing.join_local = omp_get_wtime() - j_start;

    timing.total = timing.histogram_R + timing.scatter_R
                 + timing.histogram_S + timing.scatter_S
                 + timing.join_local;

    return JoinResult{join_count, checksum1, checksum2};
}

struct alignas(64) PaddedResult { JoinResult r{}; };
static_assert(sizeof(PaddedResult) == 64,
              "PaddedResult must be exactly one cache line");

static JoinResult run_task(
    const std::vector<Record> &R,
    const std::vector<Record> &S,
    std::uint32_t P,
    const Knobs &kn,
    PhaseTiming &timing,
    PartitionedRelation &Rpart,
    PartitionedRelation &Spart)
{
    const unsigned shift = compute_shift(P);
    const int T = omp_get_max_threads();
    const int NT = (kn.tchunks > 0) ? kn.tchunks : T;

    compute_phases_tasks(P, R, shift, Rpart, NT, kn.pf_scatter, timing.histogram_R, timing.scatter_R);
    compute_phases_tasks(P, S, shift, Spart, NT, kn.pf_scatter, timing.histogram_S, timing.scatter_S);

    std::vector<std::size_t> weights(P);
    std::size_t total_work = 0;
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        weights[pid] = (Rpart.end[pid] - Rpart.begin[pid])
                     + (Spart.end[pid] - Spart.begin[pid]);
        total_work += weights[pid];
    }
    std::vector<std::uint32_t> order(P);
    std::iota(order.begin(), order.end(), 0);
    if (kn.order == "lpt") {
        std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
            return weights[a] > weights[b];
        });
    } else if (kn.order == "random") {
        std::mt19937_64 rng(1234);
        std::shuffle(order.begin(), order.end(), rng);
    } // natural: 0..P-1 così com'è

    const std::size_t avg_weight = (P > 0) ? (total_work / P) : 0;
    // hotmul=0 disattiva lo split: soglia irraggiungibile.
    const std::size_t hot_threshold =
        (kn.hotmul > 0) ? kn.hotmul * avg_weight : ~std::size_t(0);

    auto split_count_for = [&](std::size_t w) -> int {
        if (w <= hot_threshold || T <= 1) return 1;
        const std::size_t k_raw = (avg_weight > 0) ? (w / avg_weight) : 1;
        const int k = static_cast<int>(std::min<std::size_t>(k_raw, T));
        return std::max(2, k);
    };

    const std::size_t pf_probe = kn.pf_probe;
    std::vector<PaddedResult> thr_results(T);
    const double j_start = omp_get_wtime();

    #pragma omp parallel \
        default(none) \
        shared(P, order, weights, hot_threshold, Rpart, Spart, thr_results, pf_probe) \
        firstprivate(split_count_for)
    {
#ifdef NO_NOWAIT
        #pragma omp single
#else
        #pragma omp single nowait
#endif
        {
            for (std::uint32_t idx = 0; idx < P; ++idx) {
                const std::uint32_t pid = order[idx];
                const std::size_t   w   = weights[pid];

                if (w <= hot_threshold) {
                    #pragma omp task default(none) firstprivate(pid) \
                        shared(Rpart, Spart, thr_results, pf_probe)
                    {
                        const int tid = omp_get_thread_num();
                        const JoinResult local = join_one_partition_pf(Rpart, Spart, pid, pf_probe);
                        thr_results[tid].r.join_count += local.join_count;
                        thr_results[tid].r.checksum1  += local.checksum1;
                        thr_results[tid].r.checksum2  += local.checksum2;
                    }
                } else {
                    const int K = split_count_for(w);
                    #pragma omp task default(none) firstprivate(pid, K) \
                        shared(Rpart, Spart, thr_results, pf_probe)
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
                                    shared(tbl, Spart, thr_results, pf_probe)
                                {
                                    const int tid = omp_get_thread_num();
                                    const JoinResult local = probe_chunk_pf(tbl, Spart, s_lo, s_hi, pf_probe);
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

// ---------------------------------------------------------------------------
int main(int argc, char **argv)
{
    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0;
    std::string mode = "loop";
    double skew = 0.0;
    std::uint64_t hot = 4;
    std::uint64_t threads = omp_get_max_threads();
    std::uint64_t reps = 1;

    Knobs kn;

    if (!read_arg_u64(argc, argv, "-nr", nr) ||
        !read_arg_u64(argc, argv, "-ns", ns) ||
        !read_arg_u64(argc, argv, "-seed", seed) ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p", p))
    {
        std::cerr << "usage: " << argv[0]
                  << " -nr N -ns N -seed S -max-key K -p P [-mode loop|task]"
                     " [-t T] [-skew RHO] [-hot H] [-reps R]"
                     " [-sched NAME] [-order lpt|natural|random] [-hotmul K]"
                     " [-pf-scatter D] [-pf-probe D] [-firsttouch par|seq]"
                     " [-tchunks N]\n";
        return 1;
    }

    read_arg_str(argc, argv, "-mode", mode);
    read_arg_double(argc, argv, "-skew", skew);
    read_arg_u64(argc, argv, "-hot", hot);
    read_arg_u64(argc, argv, "-t", threads);
    read_arg_u64(argc, argv, "-reps", reps);

    std::string sched_name = "dynamic1";
    read_arg_str(argc, argv, "-sched", sched_name);
    read_arg_str(argc, argv, "-order", kn.order);
    read_arg_u64(argc, argv, "-hotmul", kn.hotmul);
    std::uint64_t pf_s = 12, pf_p = 8, tch = 0;
    read_arg_u64(argc, argv, "-pf-scatter", pf_s);
    read_arg_u64(argc, argv, "-pf-probe", pf_p);
    read_arg_u64(argc, argv, "-tchunks", tch);
    read_arg_str(argc, argv, "-firsttouch", kn.firsttouch);
    kn.pf_scatter = pf_s;
    kn.pf_probe   = pf_p;
    kn.tchunks    = static_cast<int>(tch);
    kn.sched_name = sched_name;

    if      (sched_name == "static")    { kn.sched_kind = omp_sched_static;  kn.sched_chunk = 0; }
    else if (sched_name == "static1")   { kn.sched_kind = omp_sched_static;  kn.sched_chunk = 1; }
    else if (sched_name == "dynamic1")  { kn.sched_kind = omp_sched_dynamic; kn.sched_chunk = 1; }
    else if (sched_name == "dynamic4")  { kn.sched_kind = omp_sched_dynamic; kn.sched_chunk = 4; }
    else if (sched_name == "dynamic16") { kn.sched_kind = omp_sched_dynamic; kn.sched_chunk = 16; }
    else if (sched_name == "guided1")   { kn.sched_kind = omp_sched_guided;  kn.sched_chunk = 1; }
    else if (sched_name == "guided16")  { kn.sched_kind = omp_sched_guided;  kn.sched_chunk = 16; }
    else { std::cerr << "Error: unknown -sched '" << sched_name << "'\n"; return 1; }

    const std::uint32_t P = static_cast<std::uint32_t>(p);
    if (!is_power_of_two(P)) { std::cerr << "Error: P must be a power of two.\n"; return 1; }
    if (mode != "loop" && mode != "task") { std::cerr << "Error: bad -mode\n"; return 1; }
    if (kn.order != "lpt" && kn.order != "natural" && kn.order != "random") {
        std::cerr << "Error: bad -order\n"; return 1;
    }
    if (kn.firsttouch != "par" && kn.firsttouch != "seq") {
        std::cerr << "Error: bad -firsttouch\n"; return 1;
    }

    omp_set_num_threads(static_cast<int>(threads));
    omp_set_schedule(kn.sched_kind, kn.sched_chunk);

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

    // Header CSV su stdout (una riga per rep).
    std::cout << "mode,workload,sched,order,hotmul,pf_scatter,pf_probe,firsttouch,tchunks,"
                 "threads,P,NR,NS,rep,hist_r_ms,scatter_r_ms,hist_s_ms,scatter_s_ms,"
                 "join_ms,total_ms,join_count,checksum1,checksum2\n";

    for (std::uint64_t rep = 0; rep < reps; ++rep) {
        // Buffer di partizione freschi a ogni rep: il first-touch (e quindi il
        // placement NUMA) viene rifatto da zero, come in una invocazione a sé.
        PartitionedRelation Rpart, Spart;
        Rpart.data.resize(NR);
        Spart.data.resize(NS);

        if (kn.firsttouch == "par") {
            #pragma omp parallel for schedule(static)
            for (std::size_t i = 0; i < NR; ++i) Rpart.data[i].key = 0;
            #pragma omp parallel for schedule(static)
            for (std::size_t i = 0; i < NS; ++i) Spart.data[i].key = 0;
        } else {
            // first-touch sequenziale: tutte le pagine finiscono sul nodo NUMA
            // del thread master (ablation dell'esperimento 05).
            for (std::size_t i = 0; i < NR; ++i) Rpart.data[i].key = 0;
            for (std::size_t i = 0; i < NS; ++i) Spart.data[i].key = 0;
        }

        PhaseTiming timing{};
        JoinResult result{};

        if (mode == "loop")
            result = run_loop(R, S, P, kn, timing, Rpart, Spart);
        else
            result = run_task(R, S, P, kn, timing, Rpart, Spart);

        std::cout << mode << ',' << workload << ',' << kn.sched_name << ','
                  << kn.order << ',' << kn.hotmul << ',' << kn.pf_scatter << ','
                  << kn.pf_probe << ',' << kn.firsttouch << ',' << kn.tchunks << ','
                  << threads << ',' << P << ',' << NR << ',' << NS << ','
                  << rep << ','
                  << std::fixed << std::setprecision(3)
                  << timing.histogram_R * 1e3 << ',' << timing.scatter_R * 1e3 << ','
                  << timing.histogram_S * 1e3 << ',' << timing.scatter_S * 1e3 << ','
                  << timing.join_local * 1e3 << ',' << timing.total * 1e3 << ','
                  << result.join_count << ',' << result.checksum1 << ','
                  << result.checksum2 << '\n';
        std::cout.flush();

        // Verifica naive sui casi piccoli (fuori dalla regione misurata).
        if (NR <= 500 && NS <= 500) {
            const JoinResult naive = naive_join_verifier(R, S);
            const bool ok = (naive.join_count == result.join_count &&
                             naive.checksum1  == result.checksum1  &&
                             naive.checksum2  == result.checksum2);
            std::cerr << "naive_verify=" << (ok ? "PASS" : "FAIL") << "\n";
        }
    }

    return 0;
}
