// ============================================================================
// avx2.cpp — AVX2 intrinsics partition mapping kernel
// ============================================================================
//
// STRATEGY FOR AVX2 VECTORIZATION OF 64-BIT MULTIPLY-SHIFT HASHING:
//
// The hash function is:  part_id = (HASH_A * key) >> shift
//
// AVX2 does NOT have a native 64-bit integer multiply instruction.
// _mm256_mul_epu32 multiplies the LOW 32 bits of each 64-bit lane,
// producing a full 64-bit result per lane (4 lanes in 256-bit register).
//
// To compute the full 64-bit product (a * k) we decompose it:
//   Let a = a_lo + a_hi * 2^32   (but a is constant, known at compile time)
//   Let k = k_lo + k_hi * 2^32
//
//   a * k = a_lo * k_lo                          (full 64-bit from mul_epu32)
//         + (a_lo * k_hi) << 32                   (only low 32 bits matter after >> shift)
//         + (a_hi * k_lo) << 32
//         + (a_hi * k_hi) << 64                   (overflows 64 bits, discard)
//
// Since we only need the HIGH bits of the product (we shift right by `shift`
// where shift >= 32 typically for reasonable P), we can often simplify.
// However, for correctness with small P (shift close to 64), we compute
// the full low 64 bits of the product.
//
// We process 4 keys per AVX2 iteration (4 x uint64_t = 256 bits).
//
// ============================================================================

#include "common.hpp"
#include <immintrin.h>

// ----------------------------------------------------------------------------
// AVX2 helper: full 64-bit multiply of 4 lanes using 32-bit primitives
// Returns low 64 bits of (a * k) for each of the 4 lanes.
// ----------------------------------------------------------------------------
static inline __m256i mul64_avx2(__m256i a, __m256i k) {
    // a_lo * k_lo  (full 64-bit results from low 32-bit halves)
    __m256i lo_lo = _mm256_mul_epu32(a, k);

    // Shift a and k right by 32 to get high halves in low positions
    __m256i a_hi = _mm256_srli_epi64(a, 32);
    __m256i k_hi = _mm256_srli_epi64(k, 32);

    // Cross products (only low 32 bits of these 64-bit results contribute
    // after the << 32 shift, so we mask or just shift)
    __m256i a_lo_k_hi = _mm256_mul_epu32(a, k_hi);  // a_lo * k_hi
    __m256i a_hi_k_lo = _mm256_mul_epu32(a_hi, k);  // a_hi * k_lo

    // Sum cross terms and shift left by 32
    __m256i cross = _mm256_add_epi64(a_lo_k_hi, a_hi_k_lo);
    cross = _mm256_slli_epi64(cross, 32);

    // Final result = lo_lo + cross (the a_hi*k_hi << 64 term overflows, ignored)
    return _mm256_add_epi64(lo_lo, cross);
}

// ----------------------------------------------------------------------------
// AVX2 partition mapping kernel
// Processes 4 keys at a time.
// ----------------------------------------------------------------------------
void partition_map_avx2(const spm_key_t* __restrict__ keys,
                        part_t*      __restrict__ part_ids,
                        size_t N,
                        unsigned shift) {

    const __m256i va = _mm256_set1_epi64x(static_cast<int64_t>(HASH_A));
    const size_t  simd_end = N - (N % 4);  // Process groups of 4

    for (size_t i = 0; i < simd_end; i += 4) {
        // Load 4 keys (4 x 64-bit = 256 bits), use unaligned for safety
        // (though our allocation is 32-byte aligned)
        __m256i vk = _mm256_load_si256(reinterpret_cast<const __m256i*>(&keys[i]));

        // Compute hash: (HASH_A * key) >> shift
        __m256i prod = mul64_avx2(va, vk);
        __m256i vhash = _mm256_srli_epi64(prod, shift);

        // Extract the 4 partition ids (they are in the low 32 bits of each 64-bit lane)
        // We need to pack them into 4 consecutive uint32_t values.
        //
        // vhash lanes: [ h0 (64b) | h1 (64b) | h2 (64b) | h3 (64b) ]
        // We want:     [ h0 (32b) | h1 (32b) | h2 (32b) | h3 (32b) ]
        //
        // Use shuffle to gather low 32 bits from each 64-bit lane.
        // _mm256_shuffle_epi32 with mask 0b_11_11_10_00 = 0xF8 picks
        // elements {0,2} from each 128-bit half => positions 0,2,4,6
        //
        // Simpler approach: extract and store individually, or use
        // _mm256_permutevar8x32_epi32 to gather.
        __m256i perm_idx = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
        __m256i packed = _mm256_permutevar8x32_epi32(vhash, perm_idx);

        // Store only the low 128 bits (4 x uint32_t)
        _mm_storeu_si128(reinterpret_cast<__m128i*>(&part_ids[i]),
                         _mm256_castsi256_si128(packed));
    }

    // Scalar tail for remaining elements
    for (size_t i = simd_end; i < N; i++) {
        part_ids[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
    }
}

// ============================================================================
// Reference scalar kernel (same as in plain.cpp, for correctness comparison)
// ============================================================================
void partition_map_scalar(const spm_key_t* __restrict__ keys,
                          part_t*      __restrict__ part_ids,
                          size_t N,
                          unsigned shift) {
    for (size_t i = 0; i < N; i++) {
        part_ids[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
    }
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <N> <P> [seed] [key_space] [reps]" << std::endl;
        return 1;
    }

    const size_t   N         = std::stoull(argv[1]);
    const uint32_t P         = std::stoul(argv[2]);
    const uint64_t seed      = (argc > 3) ? std::stoull(argv[3]) : 42;
    const uint64_t key_space = (argc > 4) ? std::stoull(argv[4]) : 0;
    const int      reps      = (argc > 5) ? std::stoi(argv[5])   : 11;

    if (P == 0 || (P & (P - 1)) != 0) {
        std::cerr << "ERROR: P must be a power of 2, got " << P << std::endl;
        return 1;
    }
    const unsigned shift = 64 - __builtin_ctz(P);

    // --- Allocate ---
    spm_key_t*  keys       = alloc_aligned<spm_key_t>(N);
    part_t* part_avx2  = alloc_aligned<part_t>(N);
    part_t* part_scalar = alloc_aligned<part_t>(N);

    // --- Generate keys ---
    KeyGenerator::generate(keys, N, seed, key_space);

    // --- Correctness check: scalar vs AVX2 ---
    partition_map_scalar(keys, part_scalar, N, shift);
    partition_map_avx2(keys, part_avx2, N, shift);

    uint64_t cksum_scalar = compute_checksum(part_scalar, N);
    uint64_t cksum_avx2   = compute_checksum(part_avx2, N);

    bool correct = (cksum_scalar == cksum_avx2);
    if (!correct) {
        // Find first mismatch
        for (size_t i = 0; i < N; i++) {
            if (part_scalar[i] != part_avx2[i]) {
                std::cerr << "MISMATCH at i=" << i
                          << " key=" << keys[i]
                          << " scalar=" << part_scalar[i]
                          << " avx2=" << part_avx2[i] << std::endl;
                break;
            }
        }
        std::cerr << "ERROR: AVX2 output differs from scalar!" << std::endl;
        std::free(keys); std::free(part_avx2); std::free(part_scalar);
        return 1;
    }
    std::cout << "Correctness: PASS (checksum=0x" << std::hex << cksum_avx2 << std::dec << ")" << std::endl;

    // Element-wise comparison for small N
    if (N <= 32) {
        std::cout << "  Element-wise check:" << std::endl;
        for (size_t i = 0; i < N; i++) {
            std::cout << "    keys[" << i << "]=" << keys[i]
                      << "  scalar=" << part_scalar[i]
                      << "  avx2=" << part_avx2[i]
                      << (part_scalar[i] == part_avx2[i] ? "  OK" : "  FAIL")
                      << std::endl;
        }
    }

    // --- Benchmark AVX2 ---
    partition_map_avx2(keys, part_avx2, N, shift); // warmup

    std::vector<double> times_avx2;
    times_avx2.reserve(reps);
    for (int r = 0; r < reps; r++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        partition_map_avx2(keys, part_avx2, N, shift);
        auto t1 = std::chrono::high_resolution_clock::now();
        times_avx2.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    // --- Benchmark scalar (for speedup calculation) ---
    partition_map_scalar(keys, part_scalar, N, shift); // warmup

    std::vector<double> times_scalar;
    times_scalar.reserve(reps);
    for (int r = 0; r < reps; r++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        partition_map_scalar(keys, part_scalar, N, shift);
        auto t1 = std::chrono::high_resolution_clock::now();
        times_scalar.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    auto res_avx2   = benchmark(times_avx2, N);
    auto res_scalar = benchmark(times_scalar, N);

    print_result("scalar (reference)", res_scalar);
    print_result("AVX2 (intrinsics)", res_avx2);
    std::cout << "  Speedup (AVX2 vs scalar): "
              << std::fixed << std::setprecision(2)
              << (res_scalar.median_ms / res_avx2.median_ms) << "x" << std::endl;
    std::cout << "  P=" << P << " shift=" << shift << " seed=" << seed
              << " key_space=" << key_space << std::endl;

    // --- Distribution analysis ---
    if (P <= 1024) {
        std::vector<uint64_t> counts(P, 0);
        for (size_t i = 0; i < N; i++) counts[part_avx2[i]]++;
        uint64_t min_c = *std::min_element(counts.begin(), counts.end());
        uint64_t max_c = *std::max_element(counts.begin(), counts.end());
        double expected = static_cast<double>(N) / P;
        std::cout << "  Distribution: min=" << min_c << " max=" << max_c
                  << " expected=" << std::fixed << std::setprecision(1) << expected
                  << " ratio_max/exp=" << std::setprecision(4) << (max_c / expected)
                  << std::endl;
    }

    std::free(keys);
    std::free(part_avx2);
    std::free(part_scalar);
    return 0;
}
