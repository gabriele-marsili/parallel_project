/*
 * avx2.cpp => Partition mapping con intrinsics AVX2
 *
 * AVX2 non ha _mm256_mullo_epi64 (disponibile solo da AVX-512).
 * L'unica primitiva per moltiplicazione intera è _mm256_mul_epu32
 * che moltiplica i 32 bit bassi di ogni lane a 64 bit.
 *
 * Il prodotto completo A*k mod 2^64 si decompone in:
 *   A*k = A_lo*k_lo + (A_lo*k_hi + A_hi*k_lo) << 32
 * (il termine A_hi*k_hi << 64 trabocca e viene scartato)
 *
 * Servono 3x vpmuludq + shift + add per emulare una mul64.
 * A_hi è pre-calcolato fuori dal loop per evitare shift ripetuti.
 *
 * -> 4 chiavi per iterazione (4 × 64 bit = 256 bit)
 * -> partition id (32 bit) estratti con vpermd e scritti come 4 uint32 contigui
 */

#include "common.hpp"
#include <immintrin.h>

/**
 * Kernel fn principale
 */
void partition_map_avx2(const spm_key_t *__restrict__ keys,
                        part_t *__restrict__ part_ids,
                        size_t N,
                        unsigned shift)
{
    // costante hash nelle 4 lane a 64 bit
    const __m256i va = _mm256_set1_epi64x(static_cast<int64_t>(HASH_A));
    // pre-calcola A_hi (32 bit alti) — evita uno shift per iterazione
    const __m256i va_hi = _mm256_srli_epi64(va, 32);
    const size_t simd_end = N - (N % 4);

    // indici per estrarre i 32 bit bassi da ogni lane 64-bit
    const __m256i perm_idx = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);

    for (size_t i = 0; i < simd_end; i += 4)
    {
        // carica 4 chiavi (aligned load, 32B alignment garantito da alloc_aligned)
        __m256i vk = _mm256_load_si256(reinterpret_cast<const __m256i *>(&keys[i]));

        // emulazione di A*k a 64 bit con 3 moltiplicazioni a 32 bit:
        __m256i lo_lo = _mm256_mul_epu32(va, vk);      // A_lo * k_lo → 64 bit
        __m256i k_hi  = _mm256_srli_epi64(vk, 32);     // estrai k_hi
        __m256i cross1 = _mm256_mul_epu32(va, k_hi);    // A_lo * k_hi
        __m256i cross2 = _mm256_mul_epu32(va_hi, vk);   // A_hi * k_lo
        // somma i cross terms e shifta << 32
        __m256i cross = _mm256_add_epi64(cross1, cross2);
        cross = _mm256_slli_epi64(cross, 32);
        __m256i prod = _mm256_add_epi64(lo_lo, cross);

        // shift a destra per ottenere il partition id
        __m256i vhash = _mm256_srli_epi64(prod, shift);

        // pack 4×64 → 4×32: estrai i 32 bit bassi di ogni lane via permutazione
        __m256i packed = _mm256_permutevar8x32_epi32(vhash, perm_idx);
        // scrivi i 4 partition id (128 bit bassi)
        _mm_storeu_si128(reinterpret_cast<__m128i *>(&part_ids[i]),
                         _mm256_castsi256_si128(packed));
    }

    // coda scalare per gli ultimi N%4 elementi
    for (size_t i = simd_end; i < N; i++)
        part_ids[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
}

// kernel scalare di riferimento — compilato senza auto-vettorizzazione
// (#pragma optimize) per confronto corretto: senza questo, GCC lo
// vettorizzerebbe con SSE xmm (stessa decomposizione mul32) e lo speedup
// risulterebbe confrontato contro una versione già semi-vettorizzata.
#pragma GCC push_options
#pragma GCC optimize ("no-tree-vectorize")
static void partition_map_scalar(const spm_key_t *__restrict__ keys,
                                 part_t *__restrict__ part_ids,
                                 size_t N,
                                 unsigned shift)
{
    for (size_t i = 0; i < N; i++)
        part_ids[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
}
#pragma GCC pop_options

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <N> <P> [seed] [key_space] [reps]\n";
        return 1;
    }

    const size_t N = std::stoull(argv[1]);
    const uint32_t P = std::stoul(argv[2]);
    const uint64_t seed = (argc > 3) ? std::stoull(argv[3]) : 42;
    const uint64_t key_space = (argc > 4) ? std::stoull(argv[4]) : 0;
    const int reps = (argc > 5) ? std::stoi(argv[5]) : 11;

    if (P == 0 || (P & (P - 1)) != 0)
    {
        std::cerr << "P deve essere potenza di 2 (ricevuto " << P << ")\n";
        return 1;
    }
    const unsigned shift = 64 - __builtin_ctz(P);

    spm_key_t *keys = alloc_aligned<spm_key_t>(N);
    part_t *part_avx2 = alloc_aligned<part_t>(N);
    part_t *part_scalar = alloc_aligned<part_t>(N);

    KeyGenerator::generate(keys, N, seed, key_space);

    // --- verifica correttezza ---
    partition_map_scalar(keys, part_scalar, N, shift);
    partition_map_avx2(keys, part_avx2, N, shift);

    uint64_t cksum_scalar = compute_checksum(part_scalar, N);
    uint64_t cksum_avx2 = compute_checksum(part_avx2, N);

    if (cksum_scalar != cksum_avx2)
    {
        for (size_t i = 0; i < N; i++)
        {
            if (part_scalar[i] != part_avx2[i])
            {
                std::cerr << "MISMATCH i=" << i << " key=" << keys[i]
                          << " scalar=" << part_scalar[i]
                          << " avx2=" << part_avx2[i] << "\n";
                break;
            }
        }
        std::cerr << "AVX2 output diverso dallo scalare!\n";
        std::free(keys);
        std::free(part_avx2);
        std::free(part_scalar);
        return 1;
    }
    std::cout << "Correttezza: OK (checksum=0x"
              << std::hex << cksum_avx2 << std::dec << ")\n";

    if (N <= 32)
    {
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

    for (int r = 0; r < reps; r++)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        partition_map_avx2(keys, part_avx2, N, shift);
        auto t1 = std::chrono::high_resolution_clock::now();
        t_avx2.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    partition_map_scalar(keys, part_scalar, N, shift); // warmup
    for (int r = 0; r < reps; r++)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        partition_map_scalar(keys, part_scalar, N, shift);
        auto t1 = std::chrono::high_resolution_clock::now();
        t_scalar.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    auto res_avx2 = benchmark(t_avx2, N);
    auto res_scalar = benchmark(t_scalar, N);

    print_result("scalar (riferimento)", res_scalar);
    print_result("AVX2 (intrinsics)", res_avx2);
    std::cout << "  Speedup AVX2 vs scalar: " << std::fixed << std::setprecision(2)
              << (res_scalar.median_ms / res_avx2.median_ms) << "x\n";
    std::cout << "  P=" << P << " shift=" << shift
              << " seed=" << seed << " key_space=" << key_space << "\n";

    if (P <= 1024)
    {
        std::vector<uint64_t> counts(P, 0);
        for (size_t i = 0; i < N; i++)
            counts[part_avx2[i]]++;
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
