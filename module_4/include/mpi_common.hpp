#ifndef MPI_COMMON_HPP
#define MPI_COMMON_HPP

#include <cstdint>
#include <cstdio>
#include <vector>
#include <mpi.h>

#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"

// Distributed phase timing. Mirrors PhaseTiming of M3 but adds the
// communication intervals introduced by the MPI redistribution.
struct PhaseTimingMPI {
    double histogram_local = 0.0; // local histogram of R and S (in dest-major pid')
    double scatter_local   = 0.0; // local scatter into dest-major send buffer
    double comm_sizes      = 0.0; // MPI_Alltoall of send_counts (R + S)
    double comm_payload    = 0.0; // MPI_Alltoallv of records    (R + S)
    double histogram_post  = 0.0; // re-histogram of received buffer by local_pid
    double scatter_post    = 0.0; // local scatter into final per-partition layout
    double join_local      = 0.0; // build + probe over the locally-owned partitions
    double reduce_final    = 0.0; // MPI_Allreduce of (count, ck1, ck2)
    double total           = 0.0; // sum of the intervals above (rank-local)

    void print(int rank) const {
        std::fprintf(stderr,
            "--- Phase Breakdown (rank %d) ---\n"
            "  Histogram_local : %10.3f ms\n"
            "  Scatter_local   : %10.3f ms\n"
            "  Comm_sizes      : %10.3f ms\n"
            "  Comm_payload    : %10.3f ms\n"
            "  Histogram_post  : %10.3f ms\n"
            "  Scatter_post    : %10.3f ms\n"
            "  Join_local      : %10.3f ms\n"
            "  Reduce_final    : %10.3f ms\n"
            "  TOTAL           : %10.3f ms\n",
            rank,
            histogram_local * 1e3,
            scatter_local   * 1e3,
            comm_sizes      * 1e3,
            comm_payload    * 1e3,
            histogram_post  * 1e3,
            scatter_post    * 1e3,
            join_local      * 1e3,
            reduce_final    * 1e3,
            total           * 1e3);
    }
};

// MPI datatype for Record. Record is { uint64_t key } so the wire type is
// just MPI_UINT64_T. Centralised here so the call sites stay readable.
inline MPI_Datatype mpi_record_type() { return MPI_UINT64_T; }

// splitmix64 admits exact skip-ahead: every call advances the internal state
// by GOLDEN. To regenerate a deterministic chunk starting at global offset k
// using the same algorithm as the sequential generator, seed the local state
// with (global_seed + GOLDEN * k) and call splitmix64_next as usual.
constexpr std::uint64_t SPLITMIX_GOLDEN = 0x9e3779b97f4a7c15ULL;

// Generate the local slice [offset, offset + n_local) of a uniform relation
// equivalent to generate_relation(N_global, base_seed, max_key) truncated to
// that range. byte-for-byte identical to the sequential generator.
static inline std::vector<Record> generate_relation_slice(
    std::size_t n_local,
    std::uint64_t base_seed,
    std::uint64_t max_key,
    std::size_t offset)
{
    std::vector<Record> out(n_local);
    std::uint64_t state = base_seed + SPLITMIX_GOLDEN * static_cast<std::uint64_t>(offset);
    for (std::size_t i = 0; i < n_local; ++i) {
        const std::uint64_t r = splitmix64_next(state);
        out[i].key = (max_key == 0) ? 0ULL : (r % max_key);
    }
    return out;
}

// Build the same skewed relation that the M3 reference generator would
// produce for the given parameters and return only the local slice
// [offset, offset + n_local). The full relation is generated on every rank;
// the cost is amortised because generation is outside the measured region
// and the keys are 8 bytes each (e.g. 100M keys = 800 MB, fits in compute-node
// RAM and is dwarfed by the join working set).
static inline std::vector<Record> generate_skewed_relation_slice(
    std::size_t n_global,
    std::size_t n_local,
    std::uint64_t base_seed,
    std::uint64_t max_key,
    std::uint32_t P,
    double rho,
    std::uint32_t hot_count,
    std::size_t offset)
{
    auto full = generate_skewed_relation(n_global, base_seed, max_key,
                                         P, rho, hot_count);
    std::vector<Record> out(n_local);
    for (std::size_t i = 0; i < n_local; ++i) out[i] = full[offset + i];
    return out;
}

// Compute [first, last) of a balanced 1D distribution of N items over P parts.
static inline void block_partition(std::size_t N, int P, int rank,
                                   std::size_t& first, std::size_t& last) {
    const std::size_t q = N / static_cast<std::size_t>(P);
    const std::size_t r = N % static_cast<std::size_t>(P);
    const std::size_t ur = static_cast<std::size_t>(rank);
    first = ur * q + (ur < r ? ur : r);
    last  = first + q + (ur < r ? 1 : 0);
}

#endif // MPI_COMMON_HPP
