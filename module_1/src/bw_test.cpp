/*
 * bw_test.cpp — Micro-benchmark per isolare il bottleneck.
 *
 * Confronta:
 *   1. Copia pura (read 8B + write 4B) — ceiling della BW
 *   2. Scalare: IMUL + shift
 *   3. AVX2: decomposizione mul64
 *   4. AVX2-light: una sola _mm256_mul_epu32 (32 bit bassi)
 *
 * Se tutti hanno throughput simile → memory-bound confermato.
 * Se la copia è molto più veloce → overhead compute evitabile.
 */

#include "common.hpp"
#include <cstring>

#ifdef __AVX2__
#include <immintrin.h>
#endif

// ---------- 1. Copia pura: leggi uint64, scrivi uint32 (troncato) ----------
static void kernel_copy(const spm_key_t* __restrict__ keys,
                        part_t* __restrict__ out,
                        size_t N) {
    for (size_t i = 0; i < N; i++)
        out[i] = static_cast<part_t>(keys[i]);  // solo troncamento, zero hash
}

// ---------- 2. Scalare: mul + shift ----------
static void kernel_scalar(const spm_key_t* __restrict__ keys,
                          part_t* __restrict__ out,
                          size_t N, unsigned shift) {
    for (size_t i = 0; i < N; i++)
        out[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
}

#ifdef __AVX2__
// ---------- 3. AVX2 full mul64 (decomposizione 3x mul32) ----------
static inline __m256i mul64_avx2(__m256i a, __m256i k) {
    __m256i lo_lo = _mm256_mul_epu32(a, k);
    __m256i a_hi  = _mm256_srli_epi64(a, 32);
    __m256i k_hi  = _mm256_srli_epi64(k, 32);
    __m256i cross1 = _mm256_mul_epu32(a, k_hi);
    __m256i cross2 = _mm256_mul_epu32(a_hi, k);
    __m256i cross  = _mm256_add_epi64(cross1, cross2);
    cross = _mm256_slli_epi64(cross, 32);
    return _mm256_add_epi64(lo_lo, cross);
}

static void kernel_avx2_full(const spm_key_t* __restrict__ keys,
                             part_t* __restrict__ out,
                             size_t N, unsigned shift) {
    const __m256i va = _mm256_set1_epi64x(static_cast<int64_t>(HASH_A));
    const __m256i perm = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
    const size_t simd_end = N - (N % 4);

    for (size_t i = 0; i < simd_end; i += 4) {
        __m256i vk   = _mm256_load_si256(reinterpret_cast<const __m256i*>(&keys[i]));
        __m256i prod = mul64_avx2(va, vk);
        __m256i h    = _mm256_srli_epi64(prod, shift);
        __m256i p    = _mm256_permutevar8x32_epi32(h, perm);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(&out[i]),
                         _mm256_castsi256_si128(p));
    }
    for (size_t i = simd_end; i < N; i++)
        out[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
}

// ---------- 4. AVX2 "light": solo lo32 * A_lo → 64 bit → shift ----------
// Una sola _mm256_mul_epu32 — test per capire se il compute fa differenza
static void kernel_avx2_light(const spm_key_t* __restrict__ keys,
                              part_t* __restrict__ out,
                              size_t N, unsigned shift) {
    // A_lo nei 32 bit bassi di ogni lane 64-bit
    const __m256i va_lo = _mm256_set1_epi64x(static_cast<int64_t>(HASH_A & 0xFFFFFFFF));
    const __m256i perm  = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
    const size_t simd_end = N - (N % 4);

    for (size_t i = 0; i < simd_end; i += 4) {
        __m256i vk   = _mm256_load_si256(reinterpret_cast<const __m256i*>(&keys[i]));
        // Solo 1 mul: moltiplica i 32 bit bassi di A per i 32 bit bassi di k
        __m256i prod = _mm256_mul_epu32(va_lo, vk);
        __m256i h    = _mm256_srli_epi64(prod, shift);
        __m256i p    = _mm256_permutevar8x32_epi32(h, perm);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(&out[i]),
                         _mm256_castsi256_si128(p));
    }
    for (size_t i = simd_end; i < N; i++)
        out[i] = static_cast<part_t>(((HASH_A & 0xFFFFFFFF) * (keys[i] & 0xFFFFFFFF)) >> shift);
}
#endif // __AVX2__

int main(int argc, char* argv[]) {
    const size_t N = (argc > 1) ? std::stoull(argv[1]) : 100000000;
    const uint32_t P = (argc > 2) ? std::stoul(argv[2]) : 256;
    const int reps = (argc > 3) ? std::stoi(argv[3]) : 11;
    const unsigned shift = 64 - __builtin_ctz(P);

    spm_key_t* keys = alloc_aligned<spm_key_t>(N);
    part_t* out = alloc_aligned<part_t>(N);
    KeyGenerator::generate(keys, N, 42, 0);

    auto run = [&](const char* label, auto fn) {
        fn(); // warmup
        std::vector<double> times;
        times.reserve(reps);
        for (int r = 0; r < reps; r++) {
            auto t0 = std::chrono::high_resolution_clock::now();
            fn();
            auto t1 = std::chrono::high_resolution_clock::now();
            times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
        auto res = benchmark(times, N);
        double bw = (double(N) * 12) / (res.median_ms / 1e3) / 1e9;
        print_result(label, res);
        std::cout << "    BW effettiva: " << std::fixed << std::setprecision(1)
                  << bw << " GB/s\n";
    };

    std::cout << "N=" << N << "  P=" << P << "  shift=" << shift
              << "  reps=" << reps << "\n\n";

    run("Copia pura (no hash)", [&]() { kernel_copy(keys, out, N); });
    run("Scalare (IMUL+shift)", [&]() { kernel_scalar(keys, out, N, shift); });

#ifdef __AVX2__
    run("AVX2 full (3x mul32)", [&]() { kernel_avx2_full(keys, out, N, shift); });
    run("AVX2 light (1x mul32)", [&]() { kernel_avx2_light(keys, out, N, shift); });
#endif

    std::free(keys);
    std::free(out);
    return 0;
}
