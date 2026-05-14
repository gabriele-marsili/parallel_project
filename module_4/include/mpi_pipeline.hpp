#ifndef MPI_PIPELINE_HPP
#define MPI_PIPELINE_HPP

// Distributed partitioned hash join — one-pass MPI pipeline.
//
// The same algorithmic skeleton of M3 (histogram + scatter + build/probe) but
// with two extra stages that exist only in the distributed setting:
// a global Alltoallv that brings every partition to its owner rank, and a
// local re-shuffle on the receive side that recovers the per-partition layout
// expected by the M3 build/probe kernel.
//
// Partition assignment to ranks (GUIDE.md §3, §5):
//   pid  = hash_key(key, shift32)        in [0, P)
//   dest = pid % R                       owner rank
//   lp   = pid / R                       local partition index on the owner
//          (in [0, P_per_rank) with P_per_rank = P / R)
// To keep the per-rank send buffer contiguous in destination order, records
// are scattered locally in dest-major order:
//   pid' = dest * P_per_rank + lp        used as the local histogram/scatter
//                                        index, in [0, P)
// The hash function itself is unchanged: pid' is just a permutation of pid.
//
// When compiled with -fopenmp, the local-join loop is parallelised by partition.

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <vector>

#include <mpi.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"
#include "join_phases.hpp"
#include "mpi_common.hpp"

namespace mpi_pipeline {

// Permute pid into dest-major order so the local scatter output is naturally
// contiguous per destination rank.
inline std::uint32_t dest_major_pid(std::uint32_t pid,
                                    std::uint32_t R,
                                    std::uint32_t P_per_rank) noexcept {
    const std::uint32_t dest = pid % R;
    const std::uint32_t lp   = pid / R;
    return dest * P_per_rank + lp;
}

// Local histogram of one relation over pid' (dest-major). O(N_local).
inline std::vector<std::size_t> histogram_dest_major(
    const std::vector<Record>& rel,
    std::uint32_t R, std::uint32_t P_per_rank, unsigned shift) {
    std::vector<std::size_t> hist(static_cast<std::size_t>(R) * P_per_rank, 0);
    for (const auto& rec : rel) {
        const std::uint32_t pid_p = dest_major_pid(hash_key(rec.key, shift),
                                                   R, P_per_rank);
        ++hist[pid_p];
    }
    return hist;
}

// Local scatter of one relation into a dest-major buffer. O(N_local).
inline void scatter_dest_major(const std::vector<Record>& rel,
                               std::uint32_t R, std::uint32_t P_per_rank,
                               unsigned shift,
                               const std::vector<std::size_t>& begin,
                               std::vector<std::uint64_t>& out) {
    std::vector<std::size_t> next = begin;
    for (const auto& rec : rel) {
        const std::uint32_t pid_p = dest_major_pid(hash_key(rec.key, shift),
                                                   R, P_per_rank);
        out[next[pid_p]++] = rec.key;
    }
}

#ifdef _OPENMP
// Parallel histogram in dest-major order. Each thread keeps a private
// histogram and only merges at the end, so the hot loop has no atomics
// or critical sections. The per-thread tables are returned so the
// matching parallel scatter can reuse them to derive per-thread offsets.
inline std::vector<std::size_t> histogram_dest_major_par(
    const std::vector<Record>& rel,
    std::uint32_t R, std::uint32_t P_per_rank, unsigned shift,
    std::vector<std::vector<std::size_t>>& per_thread)
{
    const std::size_t P_total = static_cast<std::size_t>(R) * P_per_rank;
    const int T = omp_get_max_threads();
    per_thread.assign(T, std::vector<std::size_t>(P_total, 0));

    #pragma omp parallel
    {
        const int tid = omp_get_thread_num();
        auto& h = per_thread[tid];
        #pragma omp for schedule(static)
        for (std::size_t i = 0; i < rel.size(); ++i) {
            const std::uint32_t pid_p = dest_major_pid(
                hash_key(rel[i].key, shift), R, P_per_rank);
            ++h[pid_p];
        }
    }

    std::vector<std::size_t> hist(P_total, 0);
    for (int t = 0; t < T; ++t) {
        const auto& h = per_thread[t];
        for (std::size_t p = 0; p < P_total; ++p) hist[p] += h[p];
    }
    return hist;
}

// Parallel scatter that reuses the per-thread histograms. Each thread
// gets its own offset table so writes never collide on the same slot
// of the output buffer; static scheduling ensures every thread visits
// the same input range it did during the histogram pass.
inline void scatter_dest_major_par(
    const std::vector<Record>& rel,
    std::uint32_t R, std::uint32_t P_per_rank, unsigned shift,
    const std::vector<std::size_t>& begin,
    const std::vector<std::vector<std::size_t>>& per_thread,
    std::vector<std::uint64_t>& out)
{
    const std::size_t P_total = static_cast<std::size_t>(R) * P_per_rank;
    const int T = static_cast<int>(per_thread.size());

    std::vector<std::vector<std::size_t>> offsets(
        T, std::vector<std::size_t>(P_total));
    for (std::size_t p = 0; p < P_total; ++p) {
        std::size_t cur = begin[p];
        for (int t = 0; t < T; ++t) {
            offsets[t][p] = cur;
            cur += per_thread[t][p];
        }
    }

    #pragma omp parallel
    {
        const int tid = omp_get_thread_num();
        auto off = offsets[tid]; // private copy
        #pragma omp for schedule(static)
        for (std::size_t i = 0; i < rel.size(); ++i) {
            const std::uint32_t pid_p = dest_major_pid(
                hash_key(rel[i].key, shift), R, P_per_rank);
            out[off[pid_p]++] = rel[i].key;
        }
    }
}

// Parallel histogram on the receive-side buffer, indexed by lp = pid/R.
// Same pattern as the dest-major variant but over a flat uint64 buffer
// and a much smaller histogram (P_per_rank entries instead of P).
inline std::vector<std::size_t> histogram_post_par(
    const std::vector<std::uint64_t>& recv,
    std::uint32_t R, std::uint32_t P_per_rank, unsigned shift,
    std::vector<std::vector<std::size_t>>& per_thread)
{
    const int T = omp_get_max_threads();
    per_thread.assign(T, std::vector<std::size_t>(P_per_rank, 0));

    #pragma omp parallel
    {
        const int tid = omp_get_thread_num();
        auto& h = per_thread[tid];
        #pragma omp for schedule(static)
        for (std::size_t i = 0; i < recv.size(); ++i) {
            const std::uint32_t lp = hash_key(recv[i], shift) / R;
            ++h[lp];
        }
    }

    std::vector<std::size_t> hist(P_per_rank, 0);
    for (int t = 0; t < T; ++t)
        for (std::uint32_t lp = 0; lp < P_per_rank; ++lp)
            hist[lp] += per_thread[t][lp];
    return hist;
}

// Parallel scatter_post into the per-partition layout consumed by the
// M3 build/probe kernel. Same offsets-per-thread trick as scatter_local.
inline void scatter_post_par(
    const std::vector<std::uint64_t>& recv,
    std::uint32_t R, std::uint32_t P_per_rank, unsigned shift,
    const std::vector<std::size_t>& begin,
    const std::vector<std::vector<std::size_t>>& per_thread,
    PartitionedRelation& part)
{
    const int T = static_cast<int>(per_thread.size());
    std::vector<std::vector<std::size_t>> offsets(
        T, std::vector<std::size_t>(P_per_rank));
    for (std::uint32_t lp = 0; lp < P_per_rank; ++lp) {
        std::size_t cur = begin[lp];
        for (int t = 0; t < T; ++t) {
            offsets[t][lp] = cur;
            cur += per_thread[t][lp];
        }
    }

    #pragma omp parallel
    {
        const int tid = omp_get_thread_num();
        auto off = offsets[tid];
        #pragma omp for schedule(static)
        for (std::size_t i = 0; i < recv.size(); ++i) {
            const std::uint32_t lp = hash_key(recv[i], shift) / R;
            part.data[off[lp]++].key = recv[i];
        }
    }
}
#endif // _OPENMP

// Sum of P_per_rank consecutive histogram slots per destination rank.
inline std::vector<int> send_counts_per_dest(const std::vector<std::size_t>& hist,
                                             std::uint32_t R,
                                             std::uint32_t P_per_rank) {
    std::vector<int> counts(R, 0);
    for (std::uint32_t r = 0; r < R; ++r) {
        std::size_t s = 0;
        const std::size_t base = static_cast<std::size_t>(r) * P_per_rank;
        for (std::uint32_t lp = 0; lp < P_per_rank; ++lp) s += hist[base + lp];
        counts[r] = static_cast<int>(s);
    }
    return counts;
}

inline std::vector<int> exclusive_prefix_sum_int(const std::vector<int>& v) {
    std::vector<int> out(v.size(), 0);
    int run = 0;
    for (std::size_t i = 0; i < v.size(); ++i) { out[i] = run; run += v[i]; }
    return out;
}

// Wrap Alltoallv so the count/displ vectors are explicit. The records are
// 8-byte unsigned ints on the wire (mpi_record_type()).
inline void exchange_alltoallv(const std::vector<std::uint64_t>& send_buf,
                               const std::vector<int>& send_counts,
                               const std::vector<int>& send_displs,
                               std::vector<std::uint64_t>& recv_buf,
                               const std::vector<int>& recv_counts,
                               const std::vector<int>& recv_displs,
                               MPI_Comm comm) {
    MPI_Alltoallv(send_buf.data(),  send_counts.data(),  send_displs.data(),
                  mpi_record_type(),
                  recv_buf.data(),  recv_counts.data(),  recv_displs.data(),
                  mpi_record_type(),
                  comm);
}

// Run the full distributed pipeline. Returns the globally-aggregated
// (join_count, checksum1, checksum2). Times are filled in `timing`
// (rank-local — the caller may MPI_Reduce(MAX) them for the report).
inline JoinResult run(const std::vector<Record>& R_local,
                      const std::vector<Record>& S_local,
                      std::uint32_t P, std::uint32_t R,
                      MPI_Comm comm,
                      PhaseTimingMPI& timing) {
    const unsigned shift = compute_shift(P);
    const std::uint32_t P_per_rank = P / R;

    double t0 = 0.0, t1 = 0.0;

    // ---- 1. local histogram (R and S) ------------------------------------
    MPI_Barrier(comm);
    t0 = MPI_Wtime();
#ifdef _OPENMP
    std::vector<std::vector<std::size_t>> pt_hist_R, pt_hist_S;
    auto hist_R = histogram_dest_major_par(R_local, R, P_per_rank, shift, pt_hist_R);
    auto hist_S = histogram_dest_major_par(S_local, R, P_per_rank, shift, pt_hist_S);
#else
    auto hist_R = histogram_dest_major(R_local, R, P_per_rank, shift);
    auto hist_S = histogram_dest_major(S_local, R, P_per_rank, shift);
#endif
    auto begin_R = exclusive_prefix_sum(hist_R);
    auto begin_S = exclusive_prefix_sum(hist_S);
    t1 = MPI_Wtime();
    timing.histogram_local = t1 - t0;

    // ---- 2. local scatter (R and S) into dest-major buffers --------------
    t0 = MPI_Wtime();
    std::vector<std::uint64_t> send_R(R_local.size());
    std::vector<std::uint64_t> send_S(S_local.size());
#ifdef _OPENMP
    scatter_dest_major_par(R_local, R, P_per_rank, shift, begin_R, pt_hist_R, send_R);
    scatter_dest_major_par(S_local, R, P_per_rank, shift, begin_S, pt_hist_S, send_S);
#else
    scatter_dest_major(R_local, R, P_per_rank, shift, begin_R, send_R);
    scatter_dest_major(S_local, R, P_per_rank, shift, begin_S, send_S);
#endif
    t1 = MPI_Wtime();
    timing.scatter_local = t1 - t0;

    // ---- 3. exchange counts (Alltoall, one per relation) -----------------
    MPI_Barrier(comm);
    t0 = MPI_Wtime();
    auto send_counts_R = send_counts_per_dest(hist_R, R, P_per_rank);
    auto send_counts_S = send_counts_per_dest(hist_S, R, P_per_rank);
    auto send_displs_R = exclusive_prefix_sum_int(send_counts_R);
    auto send_displs_S = exclusive_prefix_sum_int(send_counts_S);

    std::vector<int> recv_counts_R(R, 0), recv_counts_S(R, 0);
    MPI_Alltoall(send_counts_R.data(), 1, MPI_INT,
                 recv_counts_R.data(), 1, MPI_INT, comm);
    MPI_Alltoall(send_counts_S.data(), 1, MPI_INT,
                 recv_counts_S.data(), 1, MPI_INT, comm);
    auto recv_displs_R = exclusive_prefix_sum_int(recv_counts_R);
    auto recv_displs_S = exclusive_prefix_sum_int(recv_counts_S);
    t1 = MPI_Wtime();
    timing.comm_sizes = t1 - t0;

    // ---- 4. exchange payload (Alltoallv) ---------------------------------
    std::size_t recv_total_R = 0, recv_total_S = 0;
    for (int c : recv_counts_R) recv_total_R += static_cast<std::size_t>(c);
    for (int c : recv_counts_S) recv_total_S += static_cast<std::size_t>(c);

    std::vector<std::uint64_t> recv_R(recv_total_R);
    std::vector<std::uint64_t> recv_S(recv_total_S);

    MPI_Barrier(comm);
    t0 = MPI_Wtime();
    exchange_alltoallv(send_R, send_counts_R, send_displs_R,
                       recv_R, recv_counts_R, recv_displs_R, comm);
    exchange_alltoallv(send_S, send_counts_S, send_displs_S,
                       recv_S, recv_counts_S, recv_displs_S, comm);
    t1 = MPI_Wtime();
    timing.comm_payload = t1 - t0;

    // Free the send buffers now — recv side allocates ~the same.
    std::vector<std::uint64_t>().swap(send_R);
    std::vector<std::uint64_t>().swap(send_S);

    // ---- 5. histogram_post: count per local_pid on the receive side ------
    // After Alltoallv each record's key still hashes to the same pid; locally
    // we only care about lp = pid / R (in [0, P_per_rank)).
    t0 = MPI_Wtime();
#ifdef _OPENMP
    std::vector<std::vector<std::size_t>> pt_lp_R, pt_lp_S;
    auto hist_lp_R = histogram_post_par(recv_R, R, P_per_rank, shift, pt_lp_R);
    auto hist_lp_S = histogram_post_par(recv_S, R, P_per_rank, shift, pt_lp_S);
#else
    std::vector<std::size_t> hist_lp_R(P_per_rank, 0);
    std::vector<std::size_t> hist_lp_S(P_per_rank, 0);
    for (std::uint64_t k : recv_R) {
        const std::uint32_t lp = hash_key(k, shift) / R;
        ++hist_lp_R[lp];
    }
    for (std::uint64_t k : recv_S) {
        const std::uint32_t lp = hash_key(k, shift) / R;
        ++hist_lp_S[lp];
    }
#endif
    auto begin_lp_R = exclusive_prefix_sum(hist_lp_R);
    auto begin_lp_S = exclusive_prefix_sum(hist_lp_S);
    t1 = MPI_Wtime();
    timing.histogram_post = t1 - t0;

    // ---- 6. scatter_post: rearrange recv buffers into per-partition order
    t0 = MPI_Wtime();
    PartitionedRelation Rpart, Spart;
    Rpart.data.resize(recv_total_R);
    Spart.data.resize(recv_total_S);
#ifdef _OPENMP
    scatter_post_par(recv_R, R, P_per_rank, shift, begin_lp_R, pt_lp_R, Rpart);
    scatter_post_par(recv_S, R, P_per_rank, shift, begin_lp_S, pt_lp_S, Spart);
#else
    {
        std::vector<std::size_t> next = begin_lp_R;
        for (std::uint64_t k : recv_R) {
            const std::uint32_t lp = hash_key(k, shift) / R;
            Rpart.data[next[lp]++].key = k;
        }
    }
    {
        std::vector<std::size_t> next = begin_lp_S;
        for (std::uint64_t k : recv_S) {
            const std::uint32_t lp = hash_key(k, shift) / R;
            Spart.data[next[lp]++].key = k;
        }
    }
#endif
    Rpart.begin = begin_lp_R;
    Spart.begin = begin_lp_S;
    Rpart.end.resize(P_per_rank);
    Spart.end.resize(P_per_rank);
    for (std::uint32_t lp = 0; lp < P_per_rank; ++lp) {
        Rpart.end[lp] = begin_lp_R[lp] + hist_lp_R[lp];
        Spart.end[lp] = begin_lp_S[lp] + hist_lp_S[lp];
    }
    t1 = MPI_Wtime();
    timing.scatter_post = t1 - t0;

    // Free the unordered recv buffers.
    std::vector<std::uint64_t>().swap(recv_R);
    std::vector<std::uint64_t>().swap(recv_S);

    // ---- 7. local join over P_per_rank partitions ------------------------
    t0 = MPI_Wtime();
    JoinResult local{};
    std::uint64_t acc_count = 0, acc_ck1 = 0, acc_ck2 = 0;
#ifdef _OPENMP
    // Hybrid: parallelise the per-partition build+probe. Each partition is
    // an independent build/probe pair (no shared state) so a parallel for
    // with reduction is the natural mapping. dynamic schedule for skewed
    // workloads — same rationale as M3 lez. 14.
    #pragma omp parallel for schedule(dynamic) \
            reduction(+:acc_count) reduction(+:acc_ck1) reduction(+:acc_ck2)
    for (std::int64_t lp = 0; lp < static_cast<std::int64_t>(P_per_rank); ++lp) {
        const JoinResult one = join_one_partition(Rpart, Spart,
                                                   static_cast<std::uint32_t>(lp));
        acc_count += one.join_count;
        acc_ck1   += one.checksum1;
        acc_ck2   += one.checksum2;
    }
#else
    for (std::uint32_t lp = 0; lp < P_per_rank; ++lp) {
        const JoinResult one = join_one_partition(Rpart, Spart, lp);
        acc_count += one.join_count;
        acc_ck1   += one.checksum1;
        acc_ck2   += one.checksum2;
    }
#endif
    local.join_count = acc_count;
    local.checksum1  = acc_ck1;
    local.checksum2  = acc_ck2;
    t1 = MPI_Wtime();
    timing.join_local = t1 - t0;

    // ---- 8. global reduction of the three aggregates ---------------------
    MPI_Barrier(comm);
    t0 = MPI_Wtime();
    std::uint64_t in[3]  = { local.join_count, local.checksum1, local.checksum2 };
    std::uint64_t out[3] = { 0, 0, 0 };
    MPI_Allreduce(in, out, 3, MPI_UINT64_T, MPI_SUM, comm);
    t1 = MPI_Wtime();
    timing.reduce_final = t1 - t0;

    timing.total = timing.histogram_local + timing.scatter_local
                 + timing.comm_sizes      + timing.comm_payload
                 + timing.histogram_post  + timing.scatter_post
                 + timing.join_local      + timing.reduce_final;

    JoinResult global{};
    global.join_count = out[0];
    global.checksum1  = out[1];
    global.checksum2  = out[2];
    return global;
}

} // namespace mpi_pipeline

#endif // MPI_PIPELINE_HPP
