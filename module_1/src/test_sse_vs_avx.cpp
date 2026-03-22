/*
 * test_sse_vs_avx.cpp — Confronto: scalare puro vs SSE 128-bit vs AVX2 256-bit
 * 
 * Su Zen 1 (EPYC 7301) le operazioni 256-bit sono decomposte in 2x128-bit micro-ops.
 * GCC lo sa e auto-vettorizza con xmm (128-bit), non ymm.
 * Questo test verifica se SSE intrinsics manuali battono AVX2 intrinsics.
 */
#include "common.hpp"
#include <immintrin.h>

// Scalare puro (no vectorization)
#pragma GCC push_options
#pragma GCC optimize ("no-tree-vectorize")
static void kernel_scalar(const spm_key_t* __restrict__ keys,
                          part_t* __restrict__ out,
                          size_t N, unsigned shift) {
    for (size_t i = 0; i < N; i++)
        out[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
}
#pragma GCC pop_options

// SSE 128-bit intrinsics: processa 2 chiavi per iterazione
// Stessa decomposizione mul64 ma a 128-bit (nativa su Zen 1)
static void kernel_sse128(const spm_key_t* __restrict__ keys,
                          part_t* __restrict__ out,
                          size_t N, unsigned shift) {
    const __m128i va = _mm_set1_epi64x(static_cast<int64_t>(HASH_A));
    const __m128i va_hi = _mm_srli_epi64(va, 32);
    const __m128i vshift = _mm_cvtsi64_si128(shift);
    const size_t simd_end = N - (N % 2);
    
    for (size_t i = 0; i < simd_end; i += 2) {
        __m128i vk = _mm_load_si128(reinterpret_cast<const __m128i*>(&keys[i]));
        
        // 3x mul per emulare mul64
        __m128i lo_lo = _mm_mul_epu32(va, vk);
        __m128i k_hi = _mm_srli_epi64(vk, 32);
        __m128i cross1 = _mm_mul_epu32(va, k_hi);
        __m128i cross2 = _mm_mul_epu32(va_hi, vk);
        __m128i cross = _mm_add_epi64(cross1, cross2);
        cross = _mm_slli_epi64(cross, 32);
        __m128i prod = _mm_add_epi64(lo_lo, cross);
        
        __m128i h = _mm_srl_epi64(prod, vshift);
        // pack: shuffle per estrarre i 32 bit bassi di ogni lane 64-bit
        // [h0_lo, h0_hi, h1_lo, h1_hi] -> [h0_lo, h1_lo, ?, ?]
        __m128i packed = _mm_shuffle_epi32(h, _MM_SHUFFLE(2, 0, 2, 0));
        // scrivi solo 2 uint32 = 8 byte
        _mm_storel_epi64(reinterpret_cast<__m128i*>(&out[i]), packed);
    }
    for (size_t i = simd_end; i < N; i++)
        out[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
}

// AVX2 256-bit intrinsics: processa 4 chiavi per iterazione
static void kernel_avx256(const spm_key_t* __restrict__ keys,
                          part_t* __restrict__ out,
                          size_t N, unsigned shift) {
    const __m256i va = _mm256_set1_epi64x(static_cast<int64_t>(HASH_A));
    const __m256i va_hi = _mm256_srli_epi64(va, 32);
    const __m256i perm = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
    const size_t simd_end = N - (N % 4);
    
    for (size_t i = 0; i < simd_end; i += 4) {
        __m256i vk = _mm256_load_si256(reinterpret_cast<const __m256i*>(&keys[i]));
        
        __m256i lo_lo = _mm256_mul_epu32(va, vk);
        __m256i k_hi = _mm256_srli_epi64(vk, 32);
        __m256i cross1 = _mm256_mul_epu32(va, k_hi);
        __m256i cross2 = _mm256_mul_epu32(va_hi, vk);
        __m256i cross = _mm256_add_epi64(cross1, cross2);
        cross = _mm256_slli_epi64(cross, 32);
        __m256i prod = _mm256_add_epi64(lo_lo, cross);
        
        __m256i h = _mm256_srli_epi64(prod, shift);
        __m256i packed = _mm256_permutevar8x32_epi32(h, perm);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(&out[i]),
                         _mm256_castsi256_si128(packed));
    }
    for (size_t i = simd_end; i < N; i++)
        out[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
}

int main(int argc, char* argv[]) {
    const size_t N = (argc > 1) ? std::stoull(argv[1]) : 100000000;
    const uint32_t P = (argc > 2) ? std::stoul(argv[2]) : 256;
    const int reps = (argc > 3) ? std::stoi(argv[3]) : 11;
    const unsigned shift = 64 - __builtin_ctz(P);

    spm_key_t* keys = alloc_aligned<spm_key_t>(N);
    part_t* out = alloc_aligned<part_t>(N);
    KeyGenerator::generate(keys, N, 42, 0);

    // Verifica correttezza
    part_t* ref = alloc_aligned<part_t>(N);
    kernel_scalar(keys, ref, N, shift);
    uint64_t cksum_ref = compute_checksum(ref, N);
    
    kernel_sse128(keys, out, N, shift);
    uint64_t cksum_sse = compute_checksum(out, N);
    
    kernel_avx256(keys, out, N, shift);
    uint64_t cksum_avx = compute_checksum(out, N);
    
    std::cout << "Correttezza SSE128: " << (cksum_ref == cksum_sse ? "OK" : "FAIL") << "\n";
    std::cout << "Correttezza AVX256: " << (cksum_ref == cksum_avx ? "OK" : "FAIL") << "\n";
    std::cout << "N=" << N << "  P=" << P << "  shift=" << shift << "\n\n";

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
        std::cout << "    BW=" << std::fixed << std::setprecision(1) << bw << " GB/s\n";
    };

    run("Scalare (IMUL, no-vec)", [&]() { kernel_scalar(keys, out, N, shift); });
    run("SSE 128-bit (2 chiavi)", [&]() { kernel_sse128(keys, out, N, shift); });
    run("AVX2 256-bit (4 chiavi)", [&]() { kernel_avx256(keys, out, N, shift); });

    std::free(keys);
    std::free(out);
    std::free(ref);
}
