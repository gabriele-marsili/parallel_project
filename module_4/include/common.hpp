#ifndef COMMON_HPP
#define COMMON_HPP

#include <cstdint>

/* hash function from module 1 (XOR-fold + Fibonacci multiply-shift, 32-bit):
   the 64-bit key is folded into 32 bits by XOR-ing its two halves, multiplied
   by the Fibonacci constant and right-shifted to a partition id in [0, P).
   P is a power of two and shift32 = 32 - log2(P) is precomputed by the caller */

using spm_key_t = uint64_t; // 64-bit key
using part_t    = uint32_t; // partition id (32-bit)

static constexpr uint32_t HASH_A32 = 0x9E3779B9u; // floor(2^32 / phi)

/* fixed offset that derives S's seed from R's, so the two relations draw
   different but deterministic key streams (must match across seq/mpi/omp) */
static constexpr uint64_t S_SEED_OFFSET = 0xdeadebdecdeedef1ULL;

inline part_t hash_key(spm_key_t key, unsigned shift32) {
    const uint32_t k_lo = static_cast<uint32_t>(key);
    const uint32_t k_hi = static_cast<uint32_t>(key >> 32);
    return static_cast<uint32_t>(((k_lo ^ k_hi) * HASH_A32) >> shift32);
}

inline unsigned compute_shift(uint32_t P) {
    return 32u - static_cast<unsigned>(__builtin_ctz(P));
}

#endif // COMMON_HPP
