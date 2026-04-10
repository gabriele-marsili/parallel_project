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

//from module 1 (copy-pasted)

//spm_key_t instead of key_t to avoid POSIX conflict (sys/types.h)
using spm_key_t = uint64_t;  //64-bit key
using part_t    = uint32_t;  //partition id (32 bit)

/*--- Hash function ---
XOR-fold + Fibonacci multiply-shift (32-bit).
The 64-bit key is folded into 32 bits via XOR of its two halves,
then multiplied by the Fibonacci constant and right-shifted.
Using 32-bit multiply is essential for SIMD: _mm256_mullo_epi32
is native in AVX2, while a 64-bit multiply would require a
3-instruction vpmuludq decomposition.
Distribution verified: max/expected <= 1.005 on 100M keys, P=256.
*/
static constexpr uint32_t HASH_A32 = 0x9E3779B9u; //floor(2^32 / phi)

//kernel fn that compute: h(k) = ((k_lo ^ k_hi) * A32) >> shift32
//returns an integer in [0, P) with P power of 2.
//shift32 = 32 - log2(P), precomputed by the caller
inline part_t hash_key(spm_key_t key, unsigned shift32) {
    uint32_t k_lo = static_cast<uint32_t>(key);
    uint32_t k_hi = static_cast<uint32_t>(key >> 32);
    return (uint32_t)(((k_lo ^ k_hi) * HASH_A32) >> shift32);
}

inline unsigned compute_shift(uint32_t P) {
    return 32 - __builtin_ctz(P);
}


// --- Key generator (xoshiro256**) ---
//state seeded via SplitMix64 from the user seed
//if key_space > 0, keys are reduced mod key_space to control duplicates
class KeyGenerator {
public:
    static void generate(spm_key_t* keys, size_t N, uint64_t seed, uint64_t key_space = 0) {
        static constexpr uint64_t SPLITMIX_INC = 0x9E3779B97F4A7C15ULL;
        uint64_t s[4];
        for (int i = 0; i < 4; i++) {
            seed += SPLITMIX_INC;
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


// --- Aligned allocation (32-byte, required by AVX2) ---
inline void* aligned_alloc_wrapper(size_t alignment, size_t size) {
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
    void* ptr = nullptr;
    int ret = posix_memalign(&ptr, alignment, aligned_size);
    if (ret != 0 || !ptr) {
        std::cerr << "posix_memalign failed (size=" << size << ")\n";
        std::exit(1);
    }
    return ptr;
}

template<typename T>
T* alloc_aligned(size_t quantity, size_t alignment = 32) {
    return static_cast<T*>(aligned_alloc_wrapper(alignment, quantity * sizeof(T)));
}


// --- Checksum (FNV-1a) for cross-implementation comparison ---
inline uint64_t compute_checksum(const part_t* part_ids, size_t N) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < N; i++) {
        h ^= static_cast<uint64_t>(part_ids[i]);
        h *= 0x100000001B3ULL;
    }
    return h;
}


// --- Dataset loading via mmap ---
// Binary format (written by dataset_creator):
//   [0..7]   magic  0x53504D4B455953AA
//   [8..15]  N
//   [16..23] seed
//   [24..31] key_space
//   [32..39] reserved
//   [40..]   N x uint64_t keys

static constexpr uint64_t DATASET_MAGIC = 0x53504D4B455953AAULL;
static constexpr size_t DATASET_HEADER_SIZE = 40;

struct DatasetView {
    const spm_key_t* keys;
    size_t N;
    uint64_t seed;
    uint64_t key_space;
    void* mmap_base;
    size_t mmap_len;
};

//load the dataset using Memory Mapping
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

    view.N = static_cast<size_t>(hdr[1]);
    view.seed = hdr[2];
    view.key_space = hdr[3];
    view.keys = reinterpret_cast<const spm_key_t*>(
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


// --- Benchmarking utilities ---

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
