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
 * Tipi base per il progetto.
 * Viene utilizzato un alias diverso da key_t per evitare il conflitto con POSIX key_t
 * (sys/types.h definisce key_t come int32_t su macOS).
 */
using spm_key_t = uint64_t;
using part_t    = uint32_t;

/*
 * Costante hash: Fibonacci hashing.
 *
 * A = floor(2^64 / phi), dove phi = rapporto aureo.
 * E' dispari (necessario per invertibilità mod 2^64) e ha ottime
 * proprietà di bit-mixing grazie all'equidistribuzione di Weyl.
 * Vedi la guida LaTeX §2.1.4 per i dettagli sulla scelta.
 */
static constexpr spm_key_t HASH_A = 0x9E3779B97F4A7C15ULL;

/* h(k) = (A * k) >> (64 - log2(P)).  P deve essere potenza di 2. */
inline part_t hash_key(spm_key_t key, unsigned shift) {
    return static_cast<part_t>((HASH_A * key) >> shift);
}

// --------------------------------------------------------------------------
// Generatore di chiavi deterministico (xoshiro256**).
// Stato inizializzato con SplitMix64 a partire dal seed.
// Se key_space > 0 le chiavi sono ridotte mod key_space (per controllare i duplicati).
// --------------------------------------------------------------------------
class KeyGenerator {
public:
    static void generate(spm_key_t* keys, size_t N, uint64_t seed, uint64_t key_space = 0) {
        uint64_t s[4];
        for (int i = 0; i < 4; i++) {
            seed += 0x9E3779B97F4A7C15ULL;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            z = z ^ (z >> 31);
            s[i] = z;
        }
        for (size_t i = 0; i < N; i++) {
            const uint64_t result = rotl(s[1] * 5, 7) * 9;
            keys[i] = (key_space > 0) ? (result % key_space) : result;

            const uint64_t t = s[1] << 17;
            s[2] ^= s[0]; s[3] ^= s[1];
            s[1] ^= s[2]; s[0] ^= s[3];
            s[2] ^= t;
            s[3] = rotl(s[3], 45);
        }
    }

private:
    static inline uint64_t rotl(uint64_t x, int k) {
        return (x << k) | (x >> (64 - k));
    }
};

// --------------------------------------------------------------------------
// Allocazione allineata a 32 byte (richiesto da AVX2 _mm256_load_*).
// --------------------------------------------------------------------------
inline void* aligned_alloc_wrapper(size_t alignment, size_t size) {
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
    void* ptr = std::aligned_alloc(alignment, aligned_size);
    if (!ptr) {
        std::cerr << "aligned_alloc failed (size=" << size << ")\n";
        std::exit(1);
    }
    return ptr;
}

template<typename T>
T* alloc_aligned(size_t count, size_t alignment = 32) {
    return static_cast<T*>(aligned_alloc_wrapper(alignment, count * sizeof(T)));
}

// --------------------------------------------------------------------------
// Checksum FNV-1a sull'array di output.
// Serve per confrontare le implementazioni senza stampare N valori.
// --------------------------------------------------------------------------
inline uint64_t compute_checksum(const part_t* part_ids, size_t N) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < N; i++) {
        h ^= static_cast<uint64_t>(part_ids[i]);
        h *= 0x100000001B3ULL;
    }
    return h;
}

// --------------------------------------------------------------------------
// Caricamento dataset binari via mmap (zero-copy).
//
// Formato file (scritto da dataset_creator):
//   [0..7]   magic  0x53504D4B455953AA
//   [8..15]  N
//   [16..23] seed
//   [24..31] key_space
//   [32..39] riservato
//   [40..]   N x uint64_t chiavi
// --------------------------------------------------------------------------
static constexpr uint64_t DATASET_MAGIC       = 0x53504D4B455953AAULL;
static constexpr size_t   DATASET_HEADER_SIZE = 40;

struct DatasetView {
    const spm_key_t* keys;
    size_t           N;
    uint64_t         seed;
    uint64_t         key_space;
    void*            mmap_base;
    size_t           mmap_len;
};

inline bool dataset_load(const char* path, DatasetView& view) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        std::cerr << "cannot open dataset: " << path << "\n";
        return false;
    }
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return false; }
    size_t file_size = static_cast<size_t>(st.st_size);

    if (file_size < DATASET_HEADER_SIZE) {
        std::cerr << "dataset file too small\n";
        close(fd); return false;
    }

    void* base = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED) {
        std::cerr << "mmap failed: " << path << "\n";
        return false;
    }

    const uint64_t* hdr = static_cast<const uint64_t*>(base);
    if (hdr[0] != DATASET_MAGIC) {
        std::cerr << "bad magic in " << path << "\n";
        munmap(base, file_size);
        return false;
    }

    view.N         = static_cast<size_t>(hdr[1]);
    view.seed      = hdr[2];
    view.key_space = hdr[3];
    view.keys      = reinterpret_cast<const spm_key_t*>(
                         static_cast<const char*>(base) + DATASET_HEADER_SIZE);
    view.mmap_base = base;
    view.mmap_len  = file_size;

    size_t expected = DATASET_HEADER_SIZE + view.N * sizeof(spm_key_t);
    if (file_size != expected) {
        std::cerr << "size mismatch in " << path
                  << " (expected " << expected << ", got " << file_size << ")\n";
        munmap(base, file_size);
        return false;
    }
    return true;
}

inline void dataset_unload(DatasetView& view) {
    if (view.mmap_base && view.mmap_base != MAP_FAILED) {
        munmap(view.mmap_base, view.mmap_len);
        view.mmap_base = nullptr;
        view.keys = nullptr;
    }
}

// --------------------------------------------------------------------------
// Utilities per il benchmarking: mediana, stddev, throughput.
// --------------------------------------------------------------------------
struct BenchResult {
    double median_ms;
    double stddev_ms;
    double throughput_Mkeys_per_s;
    size_t N;
};

inline BenchResult benchmark(const std::vector<double>& times_ms, size_t N) {
    BenchResult r{};
    r.N = N;

    auto sorted = times_ms;
    std::sort(sorted.begin(), sorted.end());
    size_t mid = sorted.size() / 2;
    r.median_ms = (sorted.size() % 2 == 0)
                  ? (sorted[mid - 1] + sorted[mid]) / 2.0
                  : sorted[mid];

    double mean = 0;
    for (auto t : sorted) mean += t;
    mean /= static_cast<double>(sorted.size());

    double var = 0;
    for (auto t : sorted) var += (t - mean) * (t - mean);
    r.stddev_ms = std::sqrt(var / static_cast<double>(sorted.size()));

    r.throughput_Mkeys_per_s = (static_cast<double>(N) / 1e6) / (r.median_ms / 1e3);
    return r;
}

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
