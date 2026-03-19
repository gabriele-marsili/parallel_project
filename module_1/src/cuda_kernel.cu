// ============================================================================
// cuda_kernel.cu — CUDA partition mapping kernel (OPTIONAL task)
// ============================================================================
//
// STRATEGY:
// Each CUDA thread computes the partition id for one (or more) keys.
// The kernel is embarrassingly parallel: no inter-thread communication.
//
// We separately report:
//   1. Host-to-device transfer time (keys H->D)
//   2. Kernel execution time
//   3. Device-to-host transfer time (part_ids D->H)
//
// Correctness is verified against the CPU scalar reference.
// ============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <iomanip>

// We include common.hpp for key generation and checksum, but the CUDA
// kernel itself uses raw types for device compatibility.
#include "common.hpp"

// ============================================================================
// CUDA kernel
// ============================================================================
__global__ void partition_map_cuda(const uint64_t* __restrict__ keys,
                                   uint32_t*       __restrict__ part_ids,
                                   size_t N,
                                   uint64_t hash_a,
                                   unsigned shift) {
    size_t idx = static_cast<size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (idx < N) {
        part_ids[idx] = static_cast<uint32_t>((hash_a * keys[idx]) >> shift);
    }
}

// ============================================================================
// CUDA error checking macro
// ============================================================================
#define CUDA_CHECK(call)                                                       \
    do {                                                                        \
        cudaError_t err = (call);                                               \
        if (err != cudaSuccess) {                                               \
            fprintf(stderr, "CUDA error at %s:%d: %s\n", __FILE__, __LINE__,   \
                    cudaGetErrorString(err));                                    \
            exit(1);                                                            \
        }                                                                       \
    } while (0)

// ============================================================================
// CPU reference (scalar)
// ============================================================================
void partition_map_cpu(const spm_key_t* keys, part_t* part_ids, size_t N, unsigned shift) {
    for (size_t i = 0; i < N; i++) {
        part_ids[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
    }
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <N> <P> [seed] [key_space] [reps]" << std::endl;
        return 1;
    }

    const size_t   N         = std::stoull(argv[1]);
    const uint32_t P         = std::stoul(argv[2]);
    const uint64_t seed      = (argc > 3) ? std::stoull(argv[3]) : 42;
    const uint64_t key_space = (argc > 4) ? std::stoull(argv[4]) : 0;
    const int      reps      = (argc > 5) ? std::stoi(argv[5])   : 11;

    if (P == 0 || (P & (P - 1)) != 0) {
        std::cerr << "ERROR: P must be a power of 2" << std::endl;
        return 1;
    }
    const unsigned shift = 64 - __builtin_ctz(P);

    // --- Host allocation (pinned for faster transfers) ---
    spm_key_t*  h_keys;
    part_t* h_part_gpu;
    part_t* h_part_cpu;

    CUDA_CHECK(cudaMallocHost(&h_keys,     N * sizeof(spm_key_t)));
    CUDA_CHECK(cudaMallocHost(&h_part_gpu, N * sizeof(part_t)));
    h_part_cpu = static_cast<part_t*>(std::malloc(N * sizeof(part_t)));

    // --- Generate keys ---
    KeyGenerator::generate(h_keys, N, seed, key_space);

    // --- CPU reference ---
    partition_map_cpu(h_keys, h_part_cpu, N, shift);
    uint64_t cksum_cpu = compute_checksum(h_part_cpu, N);

    // --- Device allocation ---
    uint64_t* d_keys;
    uint32_t* d_part_ids;
    CUDA_CHECK(cudaMalloc(&d_keys,     N * sizeof(uint64_t)));
    CUDA_CHECK(cudaMalloc(&d_part_ids, N * sizeof(uint32_t)));

    // --- Kernel launch config ---
    const int threads_per_block = 256;
    const int num_blocks = static_cast<int>((N + threads_per_block - 1) / threads_per_block);

    // --- CUDA events for timing ---
    cudaEvent_t ev_start, ev_h2d, ev_kernel, ev_d2h;
    CUDA_CHECK(cudaEventCreate(&ev_start));
    CUDA_CHECK(cudaEventCreate(&ev_h2d));
    CUDA_CHECK(cudaEventCreate(&ev_kernel));
    CUDA_CHECK(cudaEventCreate(&ev_d2h));

    // --- Warmup ---
    CUDA_CHECK(cudaMemcpy(d_keys, h_keys, N * sizeof(uint64_t), cudaMemcpyHostToDevice));
    partition_map_cuda<<<num_blocks, threads_per_block>>>(d_keys, d_part_ids, N, HASH_A, shift);
    CUDA_CHECK(cudaDeviceSynchronize());

    // --- Benchmark ---
    std::vector<double> t_h2d_ms, t_kernel_ms, t_d2h_ms, t_total_ms;

    for (int r = 0; r < reps; r++) {
        CUDA_CHECK(cudaEventRecord(ev_start));
        CUDA_CHECK(cudaMemcpy(d_keys, h_keys, N * sizeof(uint64_t), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaEventRecord(ev_h2d));

        partition_map_cuda<<<num_blocks, threads_per_block>>>(d_keys, d_part_ids, N, HASH_A, shift);
        CUDA_CHECK(cudaEventRecord(ev_kernel));

        CUDA_CHECK(cudaMemcpy(h_part_gpu, d_part_ids, N * sizeof(uint32_t), cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaEventRecord(ev_d2h));
        CUDA_CHECK(cudaEventSynchronize(ev_d2h));

        float h2d, kern, d2h;
        CUDA_CHECK(cudaEventElapsedTime(&h2d,  ev_start, ev_h2d));
        CUDA_CHECK(cudaEventElapsedTime(&kern, ev_h2d,   ev_kernel));
        CUDA_CHECK(cudaEventElapsedTime(&d2h,  ev_kernel, ev_d2h));

        t_h2d_ms.push_back(h2d);
        t_kernel_ms.push_back(kern);
        t_d2h_ms.push_back(d2h);
        t_total_ms.push_back(h2d + kern + d2h);
    }

    // --- Correctness check ---
    uint64_t cksum_gpu = compute_checksum(h_part_gpu, N);
    bool correct = (cksum_cpu == cksum_gpu);

    if (!correct) {
        for (size_t i = 0; i < N; i++) {
            if (h_part_cpu[i] != h_part_gpu[i]) {
                std::cerr << "MISMATCH at i=" << i << " cpu=" << h_part_cpu[i]
                          << " gpu=" << h_part_gpu[i] << std::endl;
                break;
            }
        }
        std::cerr << "ERROR: CUDA output differs from CPU!" << std::endl;
    } else {
        std::cout << "Correctness: PASS (checksum=0x" << std::hex << cksum_gpu
                  << std::dec << ")" << std::endl;
    }

    if (N <= 32) {
        for (size_t i = 0; i < N; i++) {
            std::cout << "  keys[" << i << "]=" << h_keys[i]
                      << " cpu=" << h_part_cpu[i]
                      << " gpu=" << h_part_gpu[i]
                      << (h_part_cpu[i] == h_part_gpu[i] ? " OK" : " FAIL") << std::endl;
        }
    }

    // --- Report ---
    auto sort_median = [](std::vector<double>& v) -> double {
        std::sort(v.begin(), v.end());
        return v[v.size()/2];
    };

    double med_h2d    = sort_median(t_h2d_ms);
    double med_kernel = sort_median(t_kernel_ms);
    double med_d2h    = sort_median(t_d2h_ms);
    double med_total  = sort_median(t_total_ms);

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "CUDA timing breakdown (median over " << reps << " reps):" << std::endl;
    std::cout << "  H->D transfer : " << med_h2d    << " ms" << std::endl;
    std::cout << "  Kernel         : " << med_kernel << " ms" << std::endl;
    std::cout << "  D->H transfer : " << med_d2h    << " ms" << std::endl;
    std::cout << "  Total          : " << med_total  << " ms" << std::endl;
    std::cout << "  Throughput (kernel only): "
              << std::setprecision(1)
              << (static_cast<double>(N) / 1e6) / (med_kernel / 1e3) << " Mkeys/s" << std::endl;
    std::cout << "  Throughput (end-to-end) : "
              << (static_cast<double>(N) / 1e6) / (med_total / 1e3) << " Mkeys/s" << std::endl;
    std::cout << "  P=" << P << " shift=" << shift << " N=" << N << std::endl;

    // --- Cleanup ---
    CUDA_CHECK(cudaFree(d_keys));
    CUDA_CHECK(cudaFree(d_part_ids));
    CUDA_CHECK(cudaFreeHost(h_keys));
    CUDA_CHECK(cudaFreeHost(h_part_gpu));
    std::free(h_part_cpu);

    CUDA_CHECK(cudaEventDestroy(ev_start));
    CUDA_CHECK(cudaEventDestroy(ev_h2d));
    CUDA_CHECK(cudaEventDestroy(ev_kernel));
    CUDA_CHECK(cudaEventDestroy(ev_d2h));

    return correct ? 0 : 1;
}
