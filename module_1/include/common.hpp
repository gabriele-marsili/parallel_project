#ifndef COMMON_HPP
#define COMMON_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cassert>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Tipi base:
 * Viene utilizzato un alias diverso da key_t per evitare il conflitto con POSIX key_t
 * (sys/types.h definisce key_t come int32_t su macOS).
 */
using spm_key_t = uint64_t; //chiave - 64bit 
using part_t    = uint32_t; //id di partizione 

/*
 * Hash function: XOR-fold + Fibonacci multiply-shift a 32 bit.
 *
 * La chiave a 64 bit viene prima "ripiegata" in 32 bit tramite XOR
 * delle due metà (k_lo ^ k_hi), poi moltiplicata per la costante
 * Fibonacci a 32 bit e shiftata a destra.
 *
 * Questa scelta è motivata dalla compatibilità SIMD:
 * - AVX2 ha _mm256_mullo_epi32 nativo (8 mul 32x32→32 in una istruzione)
 * - NON ha _mm256_mullo_epi64 (disponibile solo da AVX-512)
 * - Una mul64 in AVX2 richiede 3x vpmuludq + shift + add → overhead
 *
 * La XOR-fold preserva l'entropia di entrambe le metà della chiave,
 * e la moltiplicazione Fibonacci garantisce buona distribuzione
 * (Knuth, TAOCP Vol. 3: Fibonacci hashing per tabelle).
 *
 * Distribuzione verificata: max/atteso ≤ 1.005 su 100M chiavi.
 */
static constexpr uint32_t HASH_A32 = 0x9E3779B9u; // floor(2^32 / phi)

/* h(k) = ((k_lo ^ k_hi) * A32) >> (32 - log2(P))
 * Restituisce un intero in [0, P) con P potenza di 2.
 * shift32 = 32 - log2(P), pre-calcolato dal chiamante.
 */
inline part_t hash_key(spm_key_t key, unsigned shift32) {
    uint32_t k_lo = static_cast<uint32_t>(key);
    uint32_t k_hi = static_cast<uint32_t>(key >> 32);
    return (uint32_t)(((k_lo ^ k_hi) * HASH_A32) >> shift32);
}

// Calcolo dello shift: per P partizioni (potenza di 2), shift = 32 - log2(P)
inline unsigned compute_shift(uint32_t P) {
    return 32 - __builtin_ctz(P);
}


// Generatore di chiavi deterministico (xoshiro256**).
// Stato inizializzato con SplitMix64 a partire dal seed.
// Se key_space > 0 le chiavi sono ridotte mod key_space (per controllare i duplicati).
class KeyGenerator {
public:
    
    //Riempie un array preallocato con N chiavi pseudo-casuali generate deterministicamente a partire dal seed
    static void generate(spm_key_t* keys, size_t N, uint64_t seed, uint64_t key_space = 0) {
        
        //algo xoshiro256**
        static constexpr uint64_t SPLITMIX_INC = 0x9E3779B97F4A7C15ULL; // golden ratio 64-bit
        uint64_t s[4]; //stato (arr di 4 uint64_t)
        for (int i = 0; i < 4; i++) { //init dello stato -> SplitMix64 per trasformare il seed 
            seed += SPLITMIX_INC; //incremento golden ratio 
            uint64_t z = seed;
            //mixing (sfrutta xor-shift-multiply):
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            z = z ^ (z >> 31);
            s[i] = z;
        }

        for (size_t i = 0; i < N; i++) { //fase di "scrambler"
            const uint64_t result = rotl(s[1] * 5, 7) * 9;
            keys[i] = (key_space > 0) ? (result % key_space) : result; //controllo duplicati (per test hash fn con input ad alta duplicazione)

            //aggiornamento dello stato:
            const uint64_t t = s[1] << 17;
            s[2] ^= s[0]; s[3] ^= s[1];
            s[1] ^= s[2]; s[0] ^= s[3];
            s[2] ^= t;
            s[3] = rotl(s[3], 45);
        }
    }

private:
    //rotazione a sinistra di k bit (bit uscenti "rientrano" a destra)
    static inline uint64_t rotl(uint64_t x, int k) { 
        return (x << k) | (x >> (64 - k));
    }
};


// Allocazione allineata a 32 byte (richiesto da AVX2 _mm256_load_*).
// Usa posix_memalign perché std::aligned_alloc non è disponibile su macOS.
inline void* aligned_alloc_wrapper(size_t alignment, size_t size) {
    // posix_memalign richiede che size sia multiplo di alignment -> arrotondiamo per eccesso.
    // Es con alignment=32:
    //   size=100 -> 100+31=131, 131 & ~31 = 131 & 0x...E0 = 128
    //   size=64  -> 64+31=95,   95  & ~31 = 95  & 0x...E0 = 64  
    // ~(alignment-1) azzera i bit bassi, forzando il multiplo.
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);

    // posix_memalign scrive in ptr l'indirizzo allocato, garantendo che l'indirizzo sia multiplo di alignment
    // => 0 in caso di successo / codice errore altrimenti   
    void* ptr = nullptr;
    int ret = posix_memalign(&ptr, alignment, aligned_size);
    if (ret != 0 || !ptr) {
        std::cerr << "posix_memalign failed (size=" << size << ")\n";
        std::exit(1);
    }
    return ptr;
}

// Wrapper tipizzato: alloca quantity elementi di tipo T, allineati a 32 byte
// Uso: spm_key_t* keys = alloc_aligned<spm_key_t>(N)
template<typename T>
T* alloc_aligned(size_t quantity, size_t alignment = 32) {
    return static_cast<T*>(aligned_alloc_wrapper(alignment, quantity * sizeof(T)));
}


// Checksum FNV-1a sull'array di output per confrontare le implementazioni senza stampare N valori
inline uint64_t compute_checksum(const part_t* part_ids, size_t N) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < N; i++) { //per ogni elemento XOR con l'elemento, poi moltiplica per il prime FNV
        h ^= static_cast<uint64_t>(part_ids[i]);
        h *= 0x100000001B3ULL; //prime FNV => checksum sensibile all'ordine e posizione degli el.
    }
    return h;
}


// Caricamento dataset binari via mmap (zero-copy)
// Formato file (scritto da dataset_creator):
//   [0..7]   magic  0x53504D4B455953AA
//   [8..15]  N
//   [16..23] seed
//   [24..31] key_space
//   [32..39] riservato
//   [40..]   N x uint64_t chiavi

static constexpr uint64_t DATASET_MAGIC = 0x53504D4B455953AAULL;
static constexpr size_t DATASET_HEADER_SIZE = 40;

struct DatasetView {
    const spm_key_t* keys; //punta nel file mappato
    size_t N;
    uint64_t seed;
    uint64_t key_space;
    void* mmap_base; //usato per munmap
    size_t mmap_len;
};

//carica il dataset usando mmap
inline bool dataset_load(const char* path, DatasetView& view) {
    int fd = open(path, O_RDONLY); //apertura in readonly
    if (fd < 0) {
        std::cerr << "cannot open dataset: " << path << "\n";
        return false;
    }
    
    //ottenimento della dim del file:
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return false; }
    size_t file_size = static_cast<size_t>(st.st_size);

    if (file_size < DATASET_HEADER_SIZE) {
        std::cerr << "dataset file too small\n";
        close(fd); return false;
    }

    //mmap mappa il file in mem virtuale => base punta al contenuto del file 
    // -> uso di demand paging da parte del kernel del SO 
    void* base = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED) {
        std::cerr << "mmap failed: " << path << "\n";
        return false;
    }

    //parsing dell'header: 
    const uint64_t* hdr = static_cast<const uint64_t*>(base);
    if (hdr[0] != DATASET_MAGIC) {
        std::cerr << "bad magic in " << path << "\n";
        munmap(base, file_size);
        return false;
    }

    view.N = static_cast<size_t>(hdr[1]);
    view.seed = hdr[2];
    view.key_space = hdr[3];
    view.keys = reinterpret_cast<const spm_key_t*>(static_cast<const char*>(base) + DATASET_HEADER_SIZE); //chiavi partono da offset 40
    view.mmap_base = base;
    view.mmap_len  = file_size;

    //OSS: uso di mmap per: zero copy + lazy loading + shared mem
    
    size_t expected = DATASET_HEADER_SIZE + view.N * sizeof(spm_key_t);
    if (file_size != expected) {
        std::cerr << "size mismatch in " << path
                  << " (expected " << expected << ", got " << file_size << ")\n";
        munmap(base, file_size);
        return false;
    }
    return true;
}

//rilascia la mappatura => puntatori non più validi
inline void dataset_unload(DatasetView& view) {
    if (view.mmap_base && view.mmap_base != MAP_FAILED) {
        munmap(view.mmap_base, view.mmap_len);
        view.mmap_base = nullptr;
        view.keys = nullptr;
    }
}


// Utilities per il benchmarking: mediana, stddev, throughput.

struct BenchResult {
    double median_ms; //tempo mediano
    double stddev_ms; //deviazione standard
    double throughput_Mkeys_per_s; //Mkeys = milioni di chiavi al s 
    size_t N;
};

/**
 *  Ordina i tempi, prende la medianat, calcola media, varianza e throughput 
*/
inline BenchResult benchmark(const std::vector<double>& times_ms, size_t N) {
    BenchResult r{};
    r.N = N;

    auto sorted = times_ms;
    std::sort(sorted.begin(), sorted.end()); //ordina i tempi
    size_t mid = sorted.size() / 2; //el di mezzo
    //calcolo mediana come media tra due elementi di mezzo se size è pari / el di mezzo altrimenti 
    r.median_ms = (sorted.size() % 2 == 0)
                  ? (sorted[mid - 1] + sorted[mid]) / 2.0
                  : sorted[mid];

    //calcolo della media:
    double mean = 0;
    for (auto t : sorted) mean += t;
    mean /= static_cast<double>(sorted.size());

    //calcolo della varianza e deviazione standard:
    double var = 0;
    for (auto t : sorted) var += (t - mean) * (t - mean);
    r.stddev_ms = std::sqrt(var / static_cast<double>(sorted.size()));

    //calcolo del throughput (in Mkeys/s):
    r.throughput_Mkeys_per_s = (static_cast<double>(N) / 1e6) / (r.median_ms / 1e3);
    return r;
}

//utility fn per stampare i risultati
inline void print_result(const std::string& label, const BenchResult& r) {
    std::cout << std::left << std::setw(30) << label
              << "  N=" << std::setw(12) << r.N
              << "  median=" << std::fixed << std::setprecision(3)
              << std::setw(10) << r.median_ms << " ms"
              << "  stddev=" << std::setprecision(3)
              << std::setw(8) << r.stddev_ms << " ms"
              << "  throughput=" << std::setprecision(1)
              << r.throughput_Mkeys_per_s << " Mkeys/s\n";
}

#endif // COMMON_HPP
