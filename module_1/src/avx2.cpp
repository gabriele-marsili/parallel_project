/*
 * avx2.cpp -- Partition mapping con intrinsics AVX2.
 *
 * Il problema principale: AVX2 non ha una moltiplicazione 64x64 bit nativa.
 * _mm256_mul_epu32 moltiplica solo i 32 bit bassi di ogni lane a 64 bit.
 *
 * Per ottenere il prodotto completo (A * k) mod 2^64 decomponiamo:
 *   A*k = A_lo*k_lo + (A_lo*k_hi + A_hi*k_lo) << 32
 * Il termine A_hi*k_hi << 64 va in overflow e si ignora.
 *
 * Processiamo 4 chiavi per iterazione (4 x 64bit = 256bit).
 * I partition id (32 bit) vengono impacchettati con permutevar8x32
 * e scritti come 4 uint32_t contigui.
 */

#include "common.hpp"
#include <immintrin.h>

// Moltiplicazione 64-bit su 4 lane usando primitive a 32 bit.
static inline __m256i mul64_avx2(__m256i a, __m256i k) {
    __m256i lo_lo = _mm256_mul_epu32(a, k);          // a_lo * k_lo (full 64-bit)

    __m256i a_hi = _mm256_srli_epi64(a, 32);
    __m256i k_hi = _mm256_srli_epi64(k, 32);

    __m256i a_lo_k_hi = _mm256_mul_epu32(a, k_hi);   // cross term 1
    __m256i a_hi_k_lo = _mm256_mul_epu32(a_hi, k);   // cross term 2

    __m256i cross = _mm256_add_epi64(a_lo_k_hi, a_hi_k_lo);
    cross = _mm256_slli_epi64(cross, 32);

    return _mm256_add_epi64(lo_lo, cross);
}

void partition_map_avx2(const spm_key_t* __restrict__ keys,
                        part_t*          __restrict__ part_ids,
                        size_t N,
                        unsigned shift) {

    const __m256i va = _mm256_set1_epi64x(static_cast<int64_t>(HASH_A));
    const size_t  simd_end = N - (N % 4);

    // indici per raccogliere i 32-bit bassi da ogni lane 64-bit
    const __m256i perm_idx = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);

    for (size_t i = 0; i < simd_end; i += 4) {
        __m256i vk    = _mm256_load_si256(reinterpret_cast<const __m256i*>(&keys[i]));
        __m256i prod  = mul64_avx2(va, vk);
        __m256i vhash = _mm256_srli_epi64(prod, shift);

        // pack 4x64 -> 4x32: i partition id stanno nei 32 bit bassi di ogni lane
        __m256i packed = _mm256_permutevar8x32_epi32(vhash, perm_idx);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(&part_ids[i]),
                         _mm256_castsi256_si128(packed));
    }

    // coda scalare per gli elementi rimanenti (N % 4)
    for (size_t i = simd_end; i < N; i++)
        part_ids[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
}

// Riferimento scalare per il confronto di correttezza
static void partition_map_scalar(const spm_key_t* __restrict__ keys,
                                 part_t*          __restrict__ part_ids,
                                 size_t N,
                                 unsigned shift) {
    for (size_t i = 0; i < N; i++)
        part_ids[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <N> <P> [seed] [key_space] [reps]\n";
        return 1;
    }

    const size_t   N         = std::stoull(argv[1]);
    const uint32_t P         = std::stoul(argv[2]);
    const uint64_t seed      = (argc > 3) ? std::stoull(argv[3]) : 42;
    const uint64_t key_space = (argc > 4) ? std::stoull(argv[4]) : 0;
    const int      reps      = (argc > 5) ? std::stoi(argv[5])   : 11;

    if (P == 0 || (P & (P - 1)) != 0) {
        std::cerr << "P deve essere potenza di 2 (ricevuto " << P << ")\n";
        return 1;
    }
    const unsigned shift = 64 - __builtin_ctz(P);

    spm_key_t* keys        = alloc_aligned<spm_key_t>(N);
    part_t*    part_avx2   = alloc_aligned<part_t>(N);
    part_t*    part_scalar = alloc_aligned<part_t>(N);

    KeyGenerator::generate(keys, N, seed, key_space);

    // --- verifica correttezza ---
    partition_map_scalar(keys, part_scalar, N, shift);
    partition_map_avx2(keys, part_avx2, N, shift);

    uint64_t cksum_scalar = compute_checksum(part_scalar, N);
    uint64_t cksum_avx2   = compute_checksum(part_avx2, N);

    if (cksum_scalar != cksum_avx2) {
        for (size_t i = 0; i < N; i++) {
            if (part_scalar[i] != part_avx2[i]) {
                std::cerr << "MISMATCH i=" << i << " key=" << keys[i]
                          << " scalar=" << part_scalar[i]
                          << " avx2=" << part_avx2[i] << "\n";
                break;
            }
        }
        std::cerr << "AVX2 output diverso dallo scalare!\n";
        std::free(keys); std::free(part_avx2); std::free(part_scalar);
        return 1;
    }
    std::cout << "Correttezza: OK (checksum=0x"
              << std::hex << cksum_avx2 << std::dec << ")\n";

    if (N <= 32) {
        for (size_t i = 0; i < N; i++)
            std::cout << "  keys[" << i << "]=" << keys[i]
                      << "  scalar=" << part_scalar[i]
                      << "  avx2=" << part_avx2[i]
                      << (part_scalar[i] == part_avx2[i] ? "  OK" : "  FAIL") << "\n";
    }

    // --- benchmark ---
    partition_map_avx2(keys, part_avx2, N, shift); // warmup

    std::vector<double> t_avx2, t_scalar;
    t_avx2.reserve(reps);
    t_scalar.reserve(reps);

    for (int r = 0; r < reps; r++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        partition_map_avx2(keys, part_avx2, N, shift);
        auto t1 = std::chrono::high_resolution_clock::now();
        t_avx2.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    partition_map_scalar(keys, part_scalar, N, shift); // warmup
    for (int r = 0; r < reps; r++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        partition_map_scalar(keys, part_scalar, N, shift);
        auto t1 = std::chrono::high_resolution_clock::now();
        t_scalar.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    auto res_avx2   = benchmark(t_avx2, N);
    auto res_scalar = benchmark(t_scalar, N);

    print_result("scalar (riferimento)", res_scalar);
    print_result("AVX2 (intrinsics)", res_avx2);
    std::cout << "  Speedup AVX2 vs scalar: " << std::fixed << std::setprecision(2)
              << (res_scalar.median_ms / res_avx2.median_ms) << "x\n";
    std::cout << "  P=" << P << " shift=" << shift
              << " seed=" << seed << " key_space=" << key_space << "\n";

    if (P <= 1024) {
        std::vector<uint64_t> counts(P, 0);
        for (size_t i = 0; i < N; i++) counts[part_avx2[i]]++;
        uint64_t mn = *std::min_element(counts.begin(), counts.end());
        uint64_t mx = *std::max_element(counts.begin(), counts.end());
        double exp = static_cast<double>(N) / P;
        std::cout << "  Distribuzione: min=" << mn << " max=" << mx
                  << " atteso=" << std::fixed << std::setprecision(1) << exp
                  << " max/atteso=" << std::setprecision(4) << (mx / exp) << "\n";
    }

    std::free(keys);
    std::free(part_avx2);
    std::free(part_scalar);
    return 0;
}
