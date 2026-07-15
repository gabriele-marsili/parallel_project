/*
 * avx2.cpp => Partition mapping con intrinsics AVX2
 *
 * Hash function: XOR-fold + Fibonacci multiply-shift a 32 bit
 *   h(k) = ((k_lo ^ k_hi) * A32) >> shift32
 *
 * Questa hash usa operazioni native AVX2:
 *   - _mm256_srli_epi64: estrae k_hi (32 bit alti della chiave)
 *   - _mm256_xor_si256: XOR fold k_lo ^ k_hi
 *   - _mm256_mullo_epi32: Fibonacci multiply 32-bit (NATIVA, 1 istruzione!)
 *   - _mm256_srli_epi32: shift per partition id
 *
 * A differenza di una hash mul64, qui non serve decomporre la
 * moltiplicazione: _mm256_mullo_epi32 mappa direttamente su vpmulld
 * (una singola istruzione AVX2). Questo è il punto chiave per
 * ottenere speedup reale: senza operazioni native il SIMD perde
 * il vantaggio.
 *
 * Layout: 8 chiavi uint64 per iterazione (2 registri ymm in input),
 * i risultati sono 8 uint32 in un singolo registro ymm.
 */

#include "common.hpp"
#include <immintrin.h>

/**
 * Kernel AVX2: processa 8 chiavi per iterazione.
 * Carica 8 chiavi uint64 (2 × 256 bit), le ripiega a 32 bit con XOR,
 * moltiplica con A32 e shifta per ottenere il partition id.
 */
void partition_map_avx2(const spm_key_t *__restrict__ keys, //restric per il compiler : keys e part_ids non si sovrappongono in memoria
                        part_t *__restrict__ part_ids,
                        size_t N,
                        unsigned shift)
{
    const __m256i va32 = _mm256_set1_epi32(static_cast<int32_t>(HASH_A32)); //init: HASH_A32 (costante del moltiplicatore di Fibonacci) replicato 8 volte nel reg. da 256 bit
    // indici per estrarre i 32 bit bassi da ogni lane 64-bit: pos 0,2,4,6
    const __m256i shuf = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7); //inizializza un registro con 8 costanti a 32 bit specificate in ordine regolare -> usato poi per maschera su permutazione
    const size_t simd_end = N - (N % 8); //coda scalare (ultimi simd_end elem.)

    for (size_t i = 0; i < simd_end; i += 8)
    {
        // carica 8 chiavi uint64 (8 byte) in 2 registri ymm da 256 bit (32byte) l'uno (4+4 keys) (_mm256_load_si256 carica 256 bit di dati allineati dalla memoria)
        // -> ovviamente il puntatore a keys deve esser aligned in memoria (a 32 byte -> 256 bit)
        __m256i vk0 = _mm256_load_si256(reinterpret_cast<const __m256i *>(&keys[i]));
        __m256i vk1 = _mm256_load_si256(reinterpret_cast<const __m256i *>(&keys[i + 4]));

        // estrai k_hi (32 bit alti) di ogni chiave shiftando a destra di 32 (porta 32 bit della parte alta nella parte bassa e poi azzera la parte alta)
        // _mm256_srli_epi64 fa shift logico a destra di 32 bit (Shift Right Logical Immediate) in modo indipendente su ciascuno dei 4 elementi a 64 bit (epi64) contenuti nel registro
        __m256i hi0 = _mm256_srli_epi64(vk0, 32);
        __m256i hi1 = _mm256_srli_epi64(vk1, 32);

        // XOR fold: k_lo ^ k_hi (il risultato sta nei 32 bit bassi di ogni lane 64-bit)
        __m256i x0 = _mm256_xor_si256(vk0, hi0);
        __m256i x1 = _mm256_xor_si256(vk1, hi1);

        // pack: estrai i 32 bit bassi di ogni lane 64-bit (4 valori per registro)
        // e combina in un singolo registro con 8 valori a 32 bit
        // _mm256_permutevar8x32_epi32 prende elem utili in idxs 0,2,4,6 e li porta nella parte bassa del registro
        // _mm256_permute2x128_si256 prende corsie da 128 bit da p0 e p1 e le unisce nel registro mixed
        //0x20 in binario è 0010 0000 : 0000 => metà bassa di p0 in parte bassa del ris ; 0010 => parte bassa di p1 in parte alta del ris
        __m256i p0 = _mm256_permutevar8x32_epi32(x0, shuf); // [x0,x1,x2,x3,?,?,?,?]
        __m256i p1 = _mm256_permutevar8x32_epi32(x1, shuf); // [x4,x5,x6,x7,?,?,?,?]
        __m256i mixed = _mm256_permute2x128_si256(p0, p1, 0x20); // [x0..x3, x4..x7]

        // Fibonacci multiply 32-bit: 8 moltiplicazioni in una istruzione (vpmulld) (trattiene solo i 32 bit bassi del prodotto intero)
        __m256i prod = _mm256_mullo_epi32(mixed, va32);

        // shift a destra per ottenere il partition id (i bit più significativi ("a sinistra") sono quelli con la massima entropia. Shiftando a destra di shift, portiamo i bit migliori nella parte bassa del numero per usarli come ID della partizione)
        __m256i h = _mm256_srli_epi32(prod, shift);

        // scrivi 8 partition id (8 × 32 bit = 256 bit del registro h nella mem puntata da &part_ids[i])
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(&part_ids[i]), h); //store un-aligned 
    }

    // coda scalare per gli ultimi N%8 elementi
    for (size_t i = simd_end; i < N; i++)
        part_ids[i] = hash_key(keys[i], shift);
}

// kernel scalare di riferimento — compilato senza auto-vettorizzazione
// per confronto corretto vs AVX2 intrinsics
#pragma GCC push_options
#pragma GCC optimize ("no-tree-vectorize")
static void partition_map_scalar(const spm_key_t *__restrict__ keys,
                                 part_t *__restrict__ part_ids,
                                 size_t N,
                                 unsigned shift)
{
    for (size_t i = 0; i < N; i++)
        part_ids[i] = hash_key(keys[i], shift);
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
    const unsigned shift = compute_shift(P);

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
