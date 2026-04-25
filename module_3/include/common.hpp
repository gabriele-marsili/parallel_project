#ifndef COMMON_HPP
#define COMMON_HPP

#include <cstdint>

// Hash function from Module 1 (XOR-fold + Fibonacci multiply-shift, 32-bit).
// The 64-bit key is folded into 32 bits via XOR of its two halves,
// then multiplied by the Fibonacci constant and right-shifted.
// Returns a partition id in [0, P) with P a power of two.
// shift32 = 32 - log2(P), precomputed by the caller via compute_shift().

using spm_key_t = uint64_t; // 64-bit key
using part_t    = uint32_t; // partition id (32-bit)

static constexpr uint32_t HASH_A32 = 0x9E3779B9u; // floor(2^32 / phi)

inline part_t hash_key(spm_key_t key, unsigned shift32) {
    const uint32_t k_lo = static_cast<uint32_t>(key);
    const uint32_t k_hi = static_cast<uint32_t>(key >> 32);
    return static_cast<uint32_t>(((k_lo ^ k_hi) * HASH_A32) >> shift32);
}

inline unsigned compute_shift(uint32_t P) {
    return 32u - static_cast<unsigned>(__builtin_ctz(P));
}

#endif // COMMON_HPP
