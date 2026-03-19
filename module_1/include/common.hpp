#ifndef COMMON_HPP
#define COMMON_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <random>
#include <chrono>
#include <numeric>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstring>

// ============================================================================
// Type aliases
// ============================================================================
using spm_key_t   = uint64_t;    // 64-bit unsigned key
using part_t  = uint32_t;    // partition identifier (fits in 32 bits easily)

// ============================================================================
// Hash function: Multiply-Shift (universal hashing)
// ============================================================================
//
// WHY THIS HASH FUNCTION?
//
// From Ferragina's "Pearls of Algorithm Engineering" (Ch.8, §8.3.1):
//   The multiply-add-shift scheme is a universal hash class that avoids
//   expensive modulo-by-prime operations, using only multiplications and
//   bit shifts on power-of-two table sizes.
//
//   h_{a}(k) = (a * k) >> (w - l)
//   where w = 64 (word size), l = log2(P), a is an odd 64-bit constant.
//
// This is ideal for SIMD vectorization because:
//   1. Only a multiply + right-shift per key (no division/modulo).
//   2. When P is a power of two, the shift amount is a compile-time constant.
//   3. The 64-bit multiply maps naturally to AVX2 _mm256_mul_epu32 pairs
//      or can be done with full 64-bit multiply on scalar path.
//   4. Provides provably good distribution (universal hashing guarantee).
//
// The constant A is chosen as a large odd number with good bit-mixing
// properties. We use a value derived from the golden ratio * 2^64, which
// is a well-known Fibonacci hashing constant (Knuth, TAOCP Vol.3).
//
// Fibonacci hashing constant: floor(2^64 / phi) where phi = (1+sqrt(5))/2
// This is odd by construction and has excellent bit-spreading properties.
//
static constexpr spm_key_t HASH_A = 0x9E3779B97F4A7C15ULL; // Fibonacci/golden-ratio constant

// Compute partition id for a single key.
// P MUST be a power of two. The shift amount is (64 - log2(P)).
inline part_t hash_key(spm_key_t key, unsigned shift) {
    return static_cast<part_t>((HASH_A * key) >> shift);
}

// ============================================================================
// Deterministic key generation from a seed (reproducible).
// Uses a fast xoshiro256** PRNG for high throughput.
// ============================================================================
class KeyGenerator {
public:
    // Generate N keys deterministically from seed into a pre-allocated buffer.
    // key_space: if > 0, keys are reduced modulo key_space to control duplicates.
    static void generate(spm_key_t* keys, size_t N, uint64_t seed, uint64_t key_space = 0) {
        // SplitMix64 to seed the state
        uint64_t s[4];
        for (int i = 0; i < 4; i++) {
            seed += 0x9E3779B97F4A7C15ULL;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            z = z ^ (z >> 31);
            s[i] = z;
        }
        // xoshiro256** generation
        for (size_t i = 0; i < N; i++) {
            const uint64_t result = rotl(s[1] * 5, 7) * 9;
            keys[i] = (key_space > 0) ? (result % key_space) : result;

            const uint64_t t = s[1] << 17;
            s[2] ^= s[0];
            s[3] ^= s[1];
            s[1] ^= s[2];
            s[0] ^= s[3];
            s[2] ^= t;
            s[3] = rotl(s[3], 45);
        }
    }

private:
    static inline uint64_t rotl(uint64_t x, int k) {
        return (x << k) | (x >> (64 - k));
    }
};

// ============================================================================
// Aligned memory allocation helpers (32-byte alignment for AVX2)
// ============================================================================
inline void* aligned_alloc_wrapper(size_t alignment, size_t size) {
    // Round up size to multiple of alignment (required by some aligned_alloc implementations)
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
    void* ptr = std::aligned_alloc(alignment, aligned_size);
    if (!ptr) {
        std::cerr << "ERROR: aligned_alloc failed for size=" << size << std::endl;
        std::exit(1);
    }
    return ptr;
}

template<typename T>
T* alloc_aligned(size_t count, size_t alignment = 32) {
    return static_cast<T*>(aligned_alloc_wrapper(alignment, count * sizeof(T)));
}

// ============================================================================
// Verification: checksum over the output array.
// Uses a simple but effective hash-based checksum to detect mismatches
// without printing the full array.
// ============================================================================
inline uint64_t compute_checksum(const part_t* part_ids, size_t N) {
    uint64_t h = 0xCBF29CE484222325ULL; // FNV-1a offset basis
    for (size_t i = 0; i < N; i++) {
        h ^= static_cast<uint64_t>(part_ids[i]);
        h *= 0x100000001B3ULL; // FNV-1a prime
    }
    return h;
}

// ============================================================================
// Timing utilities
// ============================================================================
struct BenchResult {
    double median_ms;
    double stddev_ms;
    double throughput_Mkeys_per_s;
    size_t N;
};

inline BenchResult benchmark(const std::vector<double>& times_ms, size_t N) {
    BenchResult r;
    r.N = N;
    auto sorted = times_ms;
    std::sort(sorted.begin(), sorted.end());
    size_t mid = sorted.size() / 2;
    r.median_ms = (sorted.size() % 2 == 0)
                  ? (sorted[mid - 1] + sorted[mid]) / 2.0
                  : sorted[mid];
    double mean = 0;
    for (auto t : sorted) mean += t;
    mean /= sorted.size();
    double var = 0;
    for (auto t : sorted) var += (t - mean) * (t - mean);
    r.stddev_ms = std::sqrt(var / sorted.size());
    r.throughput_Mkeys_per_s = (static_cast<double>(N) / 1e6) / (r.median_ms / 1e3);
    return r;
}

inline void print_result(const std::string& label, const BenchResult& r) {
    std::cout << std::left << std::setw(30) << label
              << "  N=" << std::setw(12) << r.N
              << "  median=" << std::fixed << std::setprecision(3) << std::setw(10) << r.median_ms << " ms"
              << "  stddev=" << std::setprecision(3) << std::setw(8) << r.stddev_ms << " ms"
              << "  throughput=" << std::setprecision(1) << r.throughput_Mkeys_per_s << " Mkeys/s"
              << std::endl;
}

#endif // COMMON_HPP
