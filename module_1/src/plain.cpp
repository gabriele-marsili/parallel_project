/*
 * plain.cpp -- Kernel scalare di partition mapping.
 *
 * Compilato due volte dallo stesso sorgente:
 *   - baseline: con -fno-tree-vectorize
 *   - autovec:  con -O3 -march=native (vettorizzazione abilitata)
 *
 * Il loop è scritto in modo da favorire l'auto-vectorization di GCC:
 *   - stride unitario, nessuna dipendenza tra iterazioni
 *   - nessuna function call nel body (HASH_A*key >> shift è tutto inline)
 *   - __restrict__ per escludere aliasing
 *   - conteggio iterazioni noto all'ingresso del loop
 */

#include "common.hpp"

/**
 * Kernel fn:
 * const spm_key_t* __restrict__ keys =  puntatore arr di input
 * part_t* __restrict__ part_ids = puntatore ad arr di output
 *
 */
void partition_map(const spm_key_t *__restrict__ keys,
                   part_t *__restrict__ part_ids,
                   size_t N,
                   unsigned shift)
{
    for (size_t i = 0; i < N; i++)
    {
        // HASH_A * keys[i] -> moltiplicazione a 64 bit (IMUL => ris troncato a 64 bit)
        //  >>shift (logico a dx) => estrae bit più sinificativi (singola ix)
        //  static_cast<part_t> => tronca da 64 a 32 bit
        part_ids[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
    }

    /*OSS: condizioni (soddisfatte) del compilatore per poter trasformare in istruzioni AVX2:
        - # iterazioni nota = N
        - iterazioni indipendenti tra loro
        - accesso sequenziale sia a part_ids che a keys
        - no function call né aliasing (-> uso di __restrict__)
        - nessun branch condizionale
    */
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <N> <P> [seed] [key_space] [reps]\n";
        std::cerr << "  N         : numero di chiavi\n";
        std::cerr << "  P         : partizioni (potenza di 2)\n";
        std::cerr << "  seed      : seed RNG (default: 42)\n";
        std::cerr << "  key_space : universo chiavi, 0 = full 64-bit (default: 0)\n";
        std::cerr << "  reps      : ripetizioni benchmark (default: 11)\n";
        return 1;
    }

    const size_t N = std::stoull(argv[1]);
    const uint32_t P = std::stoul(argv[2]);
    const uint64_t seed = (argc > 3) ? std::stoull(argv[3]) : 42;
    const uint64_t key_space = (argc > 4) ? std::stoull(argv[4]) : 0;
    const int reps = (argc > 5) ? std::stoi(argv[5]) : 11;

    // validazione P (P potenza di 2 => ha un solo 1 in binario e P-1 ha tutti 1 come bit sotto al bit=1 in P => AND == 0 tra P e P-1)
    if (P == 0 || (P & (P - 1)) != 0)
    {
        std::cerr << "P deve essere potenza di 2 (ricevuto " << P << ")\n";
        return 1;
    }
    //calcolo shift tramite Count Trailing Zeros
    const unsigned shift = 64 - __builtin_ctz(P);

    //allocazione e generazione:
    spm_key_t *keys = alloc_aligned<spm_key_t>(N);
    part_t *part_ids = alloc_aligned<part_t>(N);
    KeyGenerator::generate(keys, N, seed, key_space);

    //warmup => riempie cache e TLB (altrimenti prima misurazione sistematicamente più lenta)
    partition_map(keys, part_ids, N, shift);

    //loop di benchmark e misurazione del tempo (solo del kernel):
    std::vector<double> times;
    times.reserve(reps);
    for (int r = 0; r < reps; r++)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        partition_map(keys, part_ids, N, shift);
        auto t1 = std::chrono::high_resolution_clock::now();
        times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    uint64_t cksum = compute_checksum(part_ids, N);
    auto result = benchmark(times, N);

#ifdef AUTOVEC_ENABLED
    print_result("plain (autovec)", result);
#else
    print_result("plain (no-vec)", result);
#endif

    std::cout << "  checksum=0x" << std::hex << cksum << std::dec << "\n";
    std::cout << "  P=" << P << " shift=" << shift
              << " seed=" << seed << " key_space=" << key_space << "\n";

    // stampa element-wise per N piccolo (verifica manuale)
    if (N <= 32)
    {
        std::cout << "\n  Output dettagliato:\n";
        for (size_t i = 0; i < N; i++)
            std::cout << "    keys[" << i << "]=" << keys[i]
                      << " -> " << part_ids[i] << "\n";
    }

    // distribuzione tra partizioni
    if (P <= 1024)
    {
        std::vector<uint64_t> counts(P, 0);
        for (size_t i = 0; i < N; i++)
            counts[part_ids[i]]++;
        
        //Conta quanti elementi finiscono in ogni partizione (min ≈ max ≈ N/P per una buona hash fn)
        uint64_t mn = *std::min_element(counts.begin(), counts.end());
        uint64_t mx = *std::max_element(counts.begin(), counts.end());
        double exp = static_cast<double>(N) / P;
        std::cout << "  Distribuzione: min=" << mn << " max=" << mx
                  << " atteso=" << std::fixed << std::setprecision(1) << exp
                  << " max/atteso=" << std::setprecision(4) << (mx / exp) << "\n";
    }

    //deallocazione (allocazione fatta con std::aligned_alloc):
    std::free(keys);
    std::free(part_ids);
    return 0;
}
