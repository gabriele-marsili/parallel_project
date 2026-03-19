// ============================================================================
// plain.cpp — Plain C++ partition mapping kernel (no intrinsics)
// ============================================================================
//
// This file provides the plain (scalar) implementation of the partition
// mapping kernel. It is compiled TWICE:
//   1. With auto-vectorization DISABLED  (-fno-tree-vectorize) → baseline
//   2. With auto-vectorization ENABLED   (-O3 -march=native)   → autovec
//
// The same source, two binaries: this guarantees identical semantics.
// ============================================================================

#include "common.hpp"

// ----------------------------------------------------------------------------
// The hot kernel: maps each key to a partition id.
//
// DESIGN CHOICES FOR AUTO-VECTORIZATION FRIENDLINESS:
//   - Simple loop with unit stride (keys[i] accessed sequentially)
//   - No function calls inside the loop (hash is inline)
//   - No loop-carried dependencies (each iteration is independent)
//   - __restrict__ hints to rule out aliasing
//   - const correctness on input pointer
//   - Known iteration count at loop entry
//
// The compiler (GCC) should be able to auto-vectorize this loop when
// optimization is enabled. We verify with -fopt-info-vec-optimized.
// ----------------------------------------------------------------------------
void partition_map(const spm_key_t* __restrict__ keys,
                   part_t*      __restrict__ part_ids,
                   size_t N,
                   unsigned shift) {
    for (size_t i = 0; i < N; i++) {
        part_ids[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
    }
}

// ============================================================================
// Main: benchmark the plain kernel
// ============================================================================
int main(int argc, char* argv[]) {
    // --- Parse arguments ---
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <N> <P> [seed] [key_space] [reps]" << std::endl;
        std::cerr << "  N         : number of keys (e.g. 100000000)" << std::endl;
        std::cerr << "  P         : number of partitions (power of 2)" << std::endl;
        std::cerr << "  seed      : RNG seed (default: 42)" << std::endl;
        std::cerr << "  key_space : key universe size, 0=full 64-bit (default: 0)" << std::endl;
        std::cerr << "  reps      : number of repetitions (default: 11)" << std::endl;
        return 1;
    }

    const size_t   N         = std::stoull(argv[1]);
    const uint32_t P         = std::stoul(argv[2]);
    const uint64_t seed      = (argc > 3) ? std::stoull(argv[3]) : 42;
    const uint64_t key_space = (argc > 4) ? std::stoull(argv[4]) : 0;
    const int      reps      = (argc > 5) ? std::stoi(argv[5])   : 11;

    // Verify P is a power of 2
    if (P == 0 || (P & (P - 1)) != 0) {
        std::cerr << "ERROR: P must be a power of 2, got " << P << std::endl;
        return 1;
    }
    const unsigned shift = 64 - __builtin_ctz(P); // 64 - log2(P)

    // --- Allocate aligned memory ---
    spm_key_t*  keys     = alloc_aligned<spm_key_t>(N);
    part_t* part_ids = alloc_aligned<part_t>(N);

    // --- Generate keys ---
    KeyGenerator::generate(keys, N, seed, key_space);

    // --- Warmup ---
    partition_map(keys, part_ids, N, shift);

    // --- Benchmark ---
    std::vector<double> times;
    times.reserve(reps);

    for (int r = 0; r < reps; r++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        partition_map(keys, part_ids, N, shift);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        times.push_back(ms);
    }

    // --- Compute and report checksum ---
    uint64_t cksum = compute_checksum(part_ids, N);

    auto result = benchmark(times, N);

#ifdef AUTOVEC_ENABLED
    print_result("plain (autovec)", result);
#else
    print_result("plain (no-vec)", result);
#endif

    std::cout << "  checksum = 0x" << std::hex << cksum << std::dec << std::endl;
    std::cout << "  P=" << P << " shift=" << shift << " seed=" << seed
              << " key_space=" << key_space << std::endl;

    // --- Element-wise verification for small N ---
    if (N <= 32) {
        std::cout << "\n  Element-wise output:" << std::endl;
        for (size_t i = 0; i < N; i++) {
            std::cout << "    keys[" << i << "] = " << keys[i]
                      << "  ->  part_id = " << part_ids[i] << std::endl;
        }
    }

    // --- Distribution analysis ---
    if (P <= 1024) {
        std::vector<uint64_t> counts(P, 0);
        for (size_t i = 0; i < N; i++) counts[part_ids[i]]++;
        uint64_t min_c = *std::min_element(counts.begin(), counts.end());
        uint64_t max_c = *std::max_element(counts.begin(), counts.end());
        double expected = static_cast<double>(N) / P;
        std::cout << "  Distribution: min=" << min_c << " max=" << max_c
                  << " expected=" << std::fixed << std::setprecision(1) << expected
                  << " ratio_max/exp=" << std::setprecision(4) << (max_c / expected)
                  << std::endl;
    }

    std::free(keys);
    std::free(part_ids);
    return 0;
}
