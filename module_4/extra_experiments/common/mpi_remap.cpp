// mpi_remap.cpp — pipeline pura MPI autonoma (copia strumentata di
// mpi_pipeline.hpp + driver, il codice consegnato NON viene toccato) con la
// mappa partizione->rank parametrica. Serve agli esperimenti 04 (imbalance
// sotto skew e remapping consapevole dei pesi) e alla verifica della
// metodologia di timing (barrier prima delle collettive).
//
// Manopole:
//   -remap mod|greedy   mod:    dest(p) = p % R (come consegnato)
//                       greedy: istogramma GLOBALE dei pesi (Allreduce su P
//                               contatori) poi assegnazione greedy LPT: le
//                               partizioni in ordine di peso decrescente,
//                               ognuna al rank attualmente più scarico.
//   -barrier 1|0        con 0 salta gli MPI_Barrier davanti alle collettive:
//                       misura quanto skew di arrivo assorbirebbe la
//                       collettiva senza l'allineamento.
//   -reps K             una riga CSV per rep su stdout (rank 0).
//
// Oltre alle fasi, il CSV riporta recv_max e recv_mean (record ricevuti per
// rank, max e media): il numero che quantifica l'imbalance strutturale.
// Le fasi locali sono sequenziali come nel binario pure-MPI consegnato.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include <mpi.h>

#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"
#include "join_phases.hpp"
#include "utilities_fns.hpp"
#include "verifier.hpp"
#include "mpi_common.hpp"

struct Plan {
    // perm[pid] = indice dest-major nel buffer di invio (blocchi contigui per rank)
    std::vector<std::uint32_t> perm;
    std::vector<std::uint32_t> dest_of;     // pid -> rank proprietario
    std::vector<std::uint32_t> lp_of;       // pid -> indice locale sul proprietario
    std::vector<std::uint32_t> rank_count;  // partizioni per rank
    std::vector<std::uint32_t> rank_off;    // prefix sum di rank_count
    double plan_seconds = 0.0;              // costo del piano (Allreduce+greedy)
};

// mod: dest = pid % R, lp = pid / R (identica al consegnato)
static Plan plan_mod(std::uint32_t P, std::uint32_t R) {
    Plan pl;
    const std::uint32_t Ppr = P / R;
    pl.perm.resize(P); pl.dest_of.resize(P); pl.lp_of.resize(P);
    pl.rank_count.assign(R, Ppr);
    pl.rank_off.resize(R);
    for (std::uint32_t r = 0; r < R; ++r) pl.rank_off[r] = r * Ppr;
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        const std::uint32_t dest = pid % R, lp = pid / R;
        pl.dest_of[pid] = dest; pl.lp_of[pid] = lp;
        pl.perm[pid] = dest * Ppr + lp;
    }
    return pl;
}

// greedy: pesi globali (R+S) per partizione via Allreduce, poi LPT sui rank.
// Deterministico: tutti i rank calcolano lo stesso piano dagli stessi pesi.
static Plan plan_greedy(const std::vector<std::size_t> &hist_pid_R,
                        const std::vector<std::size_t> &hist_pid_S,
                        std::uint32_t P, std::uint32_t R, MPI_Comm comm) {
    Plan pl;
    const double t0 = MPI_Wtime();

    std::vector<std::uint64_t> local(P), global(P);
    for (std::uint32_t pid = 0; pid < P; ++pid)
        local[pid] = static_cast<std::uint64_t>(hist_pid_R[pid]) + hist_pid_S[pid];
    MPI_Allreduce(local.data(), global.data(), static_cast<int>(P),
                  MPI_UINT64_T, MPI_SUM, comm);

    std::vector<std::uint32_t> order(P);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) {
        if (global[a] != global[b]) return global[a] > global[b];
        return a < b; // tie-break deterministico
    });

    std::vector<std::uint64_t> load(R, 0);
    pl.dest_of.resize(P); pl.lp_of.resize(P);
    pl.rank_count.assign(R, 0);
    for (std::uint32_t idx = 0; idx < P; ++idx) {
        const std::uint32_t pid = order[idx];
        std::uint32_t best = 0;
        for (std::uint32_t r = 1; r < R; ++r)
            if (load[r] < load[best]) best = r;
        pl.dest_of[pid] = best;
        pl.lp_of[pid]   = pl.rank_count[best]++;
        load[best] += global[pid];
    }
    pl.rank_off.resize(R);
    std::uint32_t run = 0;
    for (std::uint32_t r = 0; r < R; ++r) { pl.rank_off[r] = run; run += pl.rank_count[r]; }
    pl.perm.resize(P);
    for (std::uint32_t pid = 0; pid < P; ++pid)
        pl.perm[pid] = pl.rank_off[pl.dest_of[pid]] + pl.lp_of[pid];

    pl.plan_seconds = MPI_Wtime() - t0;
    return pl;
}

struct Timing {
    double histogram_local = 0, plan = 0, scatter_local = 0, comm_sizes = 0,
           comm_payload = 0, histogram_post = 0, scatter_post = 0,
           join_local = 0, reduce_final = 0, total = 0;
};

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank = 0, nranks = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);
    MPI_Comm comm = MPI_COMM_WORLD;

    std::uint64_t nr = 0, ns = 0, seed = 0, max_key = 0, p = 0, hot = 0,
                  reps = 1, barrier = 1;
    double rho = 0.0;
    std::string remap = "mod";

    if (!read_arg_u64(argc, argv, "-nr", nr) ||
        !read_arg_u64(argc, argv, "-ns", ns) ||
        !read_arg_u64(argc, argv, "-seed", seed) ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p", p)) {
        if (rank == 0)
            std::fprintf(stderr, "usage: %s -nr N -ns N -seed S -max-key K -p P"
                                 " [-skew RHO -hot H] [-remap mod|greedy]"
                                 " [-barrier 0|1] [-reps K]\n", argv[0]);
        MPI_Finalize(); return 1;
    }
    read_arg_double(argc, argv, "-skew", rho);
    read_arg_u64(argc, argv, "-hot", hot);
    read_arg_u64(argc, argv, "-reps", reps);
    read_arg_u64(argc, argv, "-barrier", barrier);
    read_arg_str(argc, argv, "-remap", remap);
    const bool skewed = (rho > 0.0 && hot > 0);
    const bool use_barrier = (barrier != 0);

    const std::uint32_t P = static_cast<std::uint32_t>(p);
    const std::uint32_t R = static_cast<std::uint32_t>(nranks);
    if (!is_power_of_two(P) || P < R || (P % R) != 0) {
        if (rank == 0) std::fprintf(stderr, "Error: bad P/R combination\n");
        MPI_Finalize(); return 1;
    }
    if (remap != "mod" && remap != "greedy") {
        if (rank == 0) std::fprintf(stderr, "Error: bad -remap\n");
        MPI_Finalize(); return 1;
    }

    const unsigned shift = compute_shift(P);

    std::size_t r_first = 0, r_last = 0, s_first = 0, s_last = 0;
    block_partition(static_cast<std::size_t>(nr), nranks, rank, r_first, r_last);
    block_partition(static_cast<std::size_t>(ns), nranks, rank, s_first, s_last);

    std::vector<Record> R_local, S_local;
    if (skewed) {
        R_local = generate_skewed_relation_slice(
            static_cast<std::size_t>(nr), r_last - r_first, seed, max_key,
            P, rho, static_cast<std::uint32_t>(hot), r_first);
        S_local = generate_skewed_relation_slice(
            static_cast<std::size_t>(ns), s_last - s_first,
            seed ^ S_SEED_OFFSET, max_key,
            P, rho, static_cast<std::uint32_t>(hot), s_first);
    } else {
        R_local = generate_relation_slice(r_last - r_first, seed, max_key, r_first);
        S_local = generate_relation_slice(s_last - s_first, seed ^ S_SEED_OFFSET,
                                          max_key, s_first);
    }

    if (rank == 0)
        std::printf("remap,workload,barrier,ranks,P,NR,NS,rep,"
                    "hist_local_ms,plan_ms,scatter_local_ms,comm_sizes_ms,"
                    "comm_payload_ms,hist_post_ms,scatter_post_ms,join_ms,"
                    "reduce_ms,total_ms,recv_max,recv_mean,"
                    "join_count,checksum1,checksum2\n");

    for (std::uint64_t rep = 0; rep < reps; ++rep) {
        Timing tm{};
        double t0 = 0, t1 = 0;

        // 1) istogramma locale per pid (ordine naturale, indipendente dal piano)
        if (use_barrier) MPI_Barrier(comm);
        t0 = MPI_Wtime();
        std::vector<std::size_t> hist_pid_R(P, 0), hist_pid_S(P, 0);
        for (const auto &rec : R_local) ++hist_pid_R[hash_key(rec.key, shift)];
        for (const auto &rec : S_local) ++hist_pid_S[hash_key(rec.key, shift)];
        t1 = MPI_Wtime();
        tm.histogram_local = t1 - t0;

        // 2) piano di assegnazione partizione->rank
        Plan pl = (remap == "mod")
                      ? plan_mod(P, R)
                      : plan_greedy(hist_pid_R, hist_pid_S, P, R, comm);
        tm.plan = pl.plan_seconds;

        // istogrammi in ordine perm (dest-major) per offsets e send counts
        std::vector<std::size_t> hist_R(P, 0), hist_S(P, 0);
        for (std::uint32_t pid = 0; pid < P; ++pid) {
            hist_R[pl.perm[pid]] = hist_pid_R[pid];
            hist_S[pl.perm[pid]] = hist_pid_S[pid];
        }
        auto begin_R = exclusive_prefix_sum(hist_R);
        auto begin_S = exclusive_prefix_sum(hist_S);

        // 3) scatter locale in ordine dest-major
        t0 = MPI_Wtime();
        std::vector<std::uint64_t> send_R(R_local.size()), send_S(S_local.size());
        {
            std::vector<std::size_t> next = begin_R;
            for (const auto &rec : R_local)
                send_R[next[pl.perm[hash_key(rec.key, shift)]]++] = rec.key;
        }
        {
            std::vector<std::size_t> next = begin_S;
            for (const auto &rec : S_local)
                send_S[next[pl.perm[hash_key(rec.key, shift)]]++] = rec.key;
        }
        t1 = MPI_Wtime();
        tm.scatter_local = t1 - t0;

        // 4) scambio dei contatori
        if (use_barrier) MPI_Barrier(comm);
        t0 = MPI_Wtime();
        std::vector<int> send_counts_R(R, 0), send_counts_S(R, 0);
        for (std::uint32_t r = 0; r < R; ++r) {
            std::size_t sR = 0, sS = 0;
            for (std::uint32_t lp = 0; lp < pl.rank_count[r]; ++lp) {
                sR += hist_R[pl.rank_off[r] + lp];
                sS += hist_S[pl.rank_off[r] + lp];
            }
            send_counts_R[r] = static_cast<int>(sR);
            send_counts_S[r] = static_cast<int>(sS);
        }
        auto psum = [](const std::vector<int> &v) {
            std::vector<int> out(v.size(), 0); int run = 0;
            for (std::size_t i = 0; i < v.size(); ++i) { out[i] = run; run += v[i]; }
            return out;
        };
        auto send_displs_R = psum(send_counts_R);
        auto send_displs_S = psum(send_counts_S);
        std::vector<int> recv_counts_R(R, 0), recv_counts_S(R, 0);
        MPI_Alltoall(send_counts_R.data(), 1, MPI_INT, recv_counts_R.data(), 1, MPI_INT, comm);
        MPI_Alltoall(send_counts_S.data(), 1, MPI_INT, recv_counts_S.data(), 1, MPI_INT, comm);
        auto recv_displs_R = psum(recv_counts_R);
        auto recv_displs_S = psum(recv_counts_S);
        t1 = MPI_Wtime();
        tm.comm_sizes = t1 - t0;

        std::size_t recv_total_R = 0, recv_total_S = 0;
        for (int c : recv_counts_R) recv_total_R += static_cast<std::size_t>(c);
        for (int c : recv_counts_S) recv_total_S += static_cast<std::size_t>(c);

        std::vector<std::uint64_t> recv_R(recv_total_R), recv_S(recv_total_S);

        // 5) scambio del payload
        if (use_barrier) MPI_Barrier(comm);
        t0 = MPI_Wtime();
        MPI_Alltoallv(send_R.data(), send_counts_R.data(), send_displs_R.data(), MPI_UINT64_T,
                      recv_R.data(), recv_counts_R.data(), recv_displs_R.data(), MPI_UINT64_T, comm);
        MPI_Alltoallv(send_S.data(), send_counts_S.data(), send_displs_S.data(), MPI_UINT64_T,
                      recv_S.data(), recv_counts_S.data(), recv_displs_S.data(), MPI_UINT64_T, comm);
        t1 = MPI_Wtime();
        tm.comm_payload = t1 - t0;

        std::vector<std::uint64_t>().swap(send_R);
        std::vector<std::uint64_t>().swap(send_S);

        // volume ricevuto per rank (record R+S): max e media fra i rank
        const std::uint64_t recv_here = recv_total_R + recv_total_S;
        std::uint64_t recv_max = 0, recv_sum = 0;
        MPI_Reduce(&recv_here, &recv_max, 1, MPI_UINT64_T, MPI_MAX, 0, comm);
        MPI_Reduce(&recv_here, &recv_sum, 1, MPI_UINT64_T, MPI_SUM, 0, comm);

        // 6) istogramma post per lp
        const std::uint32_t P_local = pl.rank_count[rank];
        t0 = MPI_Wtime();
        std::vector<std::size_t> hist_lp_R(P_local, 0), hist_lp_S(P_local, 0);
        for (std::uint64_t k : recv_R) ++hist_lp_R[pl.lp_of[hash_key(k, shift)]];
        for (std::uint64_t k : recv_S) ++hist_lp_S[pl.lp_of[hash_key(k, shift)]];
        auto begin_lp_R = exclusive_prefix_sum(hist_lp_R);
        auto begin_lp_S = exclusive_prefix_sum(hist_lp_S);
        t1 = MPI_Wtime();
        tm.histogram_post = t1 - t0;

        // 7) scatter post nel layout per-partizione
        t0 = MPI_Wtime();
        PartitionedRelation Rpart, Spart;
        Rpart.data.resize(recv_total_R);
        Spart.data.resize(recv_total_S);
        {
            std::vector<std::size_t> next = begin_lp_R;
            for (std::uint64_t k : recv_R)
                Rpart.data[next[pl.lp_of[hash_key(k, shift)]]++].key = k;
        }
        {
            std::vector<std::size_t> next = begin_lp_S;
            for (std::uint64_t k : recv_S)
                Spart.data[next[pl.lp_of[hash_key(k, shift)]]++].key = k;
        }
        Rpart.begin = begin_lp_R; Spart.begin = begin_lp_S;
        Rpart.end.resize(P_local); Spart.end.resize(P_local);
        for (std::uint32_t lp = 0; lp < P_local; ++lp) {
            Rpart.end[lp] = begin_lp_R[lp] + hist_lp_R[lp];
            Spart.end[lp] = begin_lp_S[lp] + hist_lp_S[lp];
        }
        t1 = MPI_Wtime();
        tm.scatter_post = t1 - t0;

        std::vector<std::uint64_t>().swap(recv_R);
        std::vector<std::uint64_t>().swap(recv_S);

        // 8) join locale
        t0 = MPI_Wtime();
        JoinResult local{};
        for (std::uint32_t lp = 0; lp < P_local; ++lp) {
            const JoinResult one = join_one_partition(Rpart, Spart, lp);
            local.join_count += one.join_count;
            local.checksum1  += one.checksum1;
            local.checksum2  += one.checksum2;
        }
        t1 = MPI_Wtime();
        tm.join_local = t1 - t0;

        // 9) riduzione finale
        if (use_barrier) MPI_Barrier(comm);
        t0 = MPI_Wtime();
        std::uint64_t in[3] = {local.join_count, local.checksum1, local.checksum2};
        std::uint64_t out[3] = {0, 0, 0};
        MPI_Allreduce(in, out, 3, MPI_UINT64_T, MPI_SUM, comm);
        t1 = MPI_Wtime();
        tm.reduce_final = t1 - t0;

        tm.total = tm.histogram_local + tm.plan + tm.scatter_local + tm.comm_sizes
                 + tm.comm_payload + tm.histogram_post + tm.scatter_post
                 + tm.join_local + tm.reduce_final;

        // riduzione MAX delle fasi (il breakdown vede il rank più lento)
        double loc[10] = {tm.histogram_local, tm.plan, tm.scatter_local,
                          tm.comm_sizes, tm.comm_payload, tm.histogram_post,
                          tm.scatter_post, tm.join_local, tm.reduce_final, tm.total};
        double mx[10] = {0};
        MPI_Reduce(loc, mx, 10, MPI_DOUBLE, MPI_MAX, 0, comm);

        if (rank == 0) {
            std::printf("%s,%s,%llu,%d,%u,%llu,%llu,%llu,"
                        "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                        "%llu,%.1f,%llu,%llu,%llu\n",
                        remap.c_str(), skewed ? "skew" : "uniform",
                        (unsigned long long)barrier, nranks, P,
                        (unsigned long long)nr, (unsigned long long)ns,
                        (unsigned long long)rep,
                        mx[0]*1e3, mx[1]*1e3, mx[2]*1e3, mx[3]*1e3, mx[4]*1e3,
                        mx[5]*1e3, mx[6]*1e3, mx[7]*1e3, mx[8]*1e3, mx[9]*1e3,
                        (unsigned long long)recv_max,
                        (double)recv_sum / nranks,
                        (unsigned long long)out[0],
                        (unsigned long long)out[1],
                        (unsigned long long)out[2]);
            std::fflush(stdout);
        }
    }

    MPI_Finalize();
    return 0;
}
