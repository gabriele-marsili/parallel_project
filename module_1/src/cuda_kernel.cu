/*
 * cuda_kernel.cu -- Kernel CUDA per partition mapping
 *
 * Ogni thread mappa una chiave alla sua partizione
 * Tempi riportati separatamente: H->D transfer, kernel, D->H transfer
 * Correttezza verificata contro il riferimento scalare su CPU
 *
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include <iostream>
#include <iomanip>

#include "common.hpp"

#define CUDA_CHECK(call) do {                                                 \
    cudaError_t err = (call);                                                 \
    if (err != cudaSuccess) {                                                 \
        fprintf(stderr, "CUDA error %s:%d: %s\n",                            \
                __FILE__, __LINE__, cudaGetErrorString(err));                  \
        exit(1);                                                              \
    }                                                                         \
} while (0)

__global__ //=> fn lanciata da CPU ma eseguita in GPU 
void partition_map_cuda(const uint64_t* __restrict__ keys,
                        uint32_t*       __restrict__ part_ids,
                        size_t N, uint32_t hash_a32, unsigned shift) {
    size_t idx = static_cast<size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (idx < N) { //bound check
        uint32_t k_lo = static_cast<uint32_t>(keys[idx]);
        uint32_t k_hi = static_cast<uint32_t>(keys[idx] >> 32);
        part_ids[idx] = ((k_lo ^ k_hi) * hash_a32) >> shift;
    }
}

static void partition_map_cpu(const spm_key_t* keys, part_t* out,
                              size_t N, unsigned shift) {
    for (size_t i = 0; i < N; i++)
        out[i] = hash_key(keys[i], shift);
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
        std::cerr << "P deve essere potenza di 2\n";
        return 1;
    }
    const unsigned shift = compute_shift(P);

    // host: pinned memory per trasferimenti veloci
    spm_key_t* h_keys;
    part_t*    h_part_gpu;
    CUDA_CHECK(cudaMallocHost(&h_keys,     N * sizeof(spm_key_t)));
    CUDA_CHECK(cudaMallocHost(&h_part_gpu, N * sizeof(part_t)));
    part_t* h_part_cpu = static_cast<part_t*>(std::malloc(N * sizeof(part_t)));

    KeyGenerator::generate(h_keys, N, seed, key_space);

    // riferimento CPU
    partition_map_cpu(h_keys, h_part_cpu, N, shift);
    uint64_t cksum_cpu = compute_checksum(h_part_cpu, N);

    // device
    uint64_t* d_keys;
    uint32_t* d_part;
    CUDA_CHECK(cudaMalloc(&d_keys, N * sizeof(uint64_t)));
    CUDA_CHECK(cudaMalloc(&d_part, N * sizeof(uint32_t)));

    const int tpb = 256;
    const int nblocks = static_cast<int>((N + tpb - 1) / tpb);

    cudaEvent_t e0, e1, e2, e3;
    CUDA_CHECK(cudaEventCreate(&e0));
    CUDA_CHECK(cudaEventCreate(&e1));
    CUDA_CHECK(cudaEventCreate(&e2));
    CUDA_CHECK(cudaEventCreate(&e3));

    // warmup
    CUDA_CHECK(cudaMemcpy(d_keys, h_keys, N * sizeof(uint64_t), cudaMemcpyHostToDevice));
    partition_map_cuda<<<nblocks, tpb>>>(d_keys, d_part, N, HASH_A32, shift);
    CUDA_CHECK(cudaDeviceSynchronize());

    std::vector<double> t_h2d, t_kern, t_d2h, t_tot;
    for (int r = 0; r < reps; r++) {
        CUDA_CHECK(cudaEventRecord(e0));
        CUDA_CHECK(cudaMemcpy(d_keys, h_keys, N * sizeof(uint64_t), cudaMemcpyHostToDevice));
        CUDA_CHECK(cudaEventRecord(e1));

        partition_map_cuda<<<nblocks, tpb>>>(d_keys, d_part, N, HASH_A32, shift);
        CUDA_CHECK(cudaEventRecord(e2));

        CUDA_CHECK(cudaMemcpy(h_part_gpu, d_part, N * sizeof(uint32_t), cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaEventRecord(e3));
        CUDA_CHECK(cudaEventSynchronize(e3));

        float h2d, kern, d2h;
        CUDA_CHECK(cudaEventElapsedTime(&h2d,  e0, e1));
        CUDA_CHECK(cudaEventElapsedTime(&kern, e1, e2));
        CUDA_CHECK(cudaEventElapsedTime(&d2h,  e2, e3));

        t_h2d.push_back(h2d);
        t_kern.push_back(kern);
        t_d2h.push_back(d2h);
        t_tot.push_back(h2d + kern + d2h);
    }

    // correttezza
    uint64_t cksum_gpu = compute_checksum(h_part_gpu, N);
    bool ok = (cksum_cpu == cksum_gpu);
    if (!ok) {
        for (size_t i = 0; i < N; i++) {
            if (h_part_cpu[i] != h_part_gpu[i]) {
                std::cerr << "MISMATCH i=" << i
                          << " cpu=" << h_part_cpu[i]
                          << " gpu=" << h_part_gpu[i] << "\n";
                break;
            }
        }
        std::cerr << "Output CUDA diverso dalla CPU!\n";
    } else {
        std::cout << "Correttezza: OK (checksum=0x"
                  << std::hex << cksum_gpu << std::dec << ")\n";
    }

    if (N <= 32) {
        for (size_t i = 0; i < N; i++)
            std::cout << "  keys[" << i << "]=" << h_keys[i]
                      << " cpu=" << h_part_cpu[i]
                      << " gpu=" << h_part_gpu[i]
                      << (h_part_cpu[i] == h_part_gpu[i] ? " OK" : " FAIL") << "\n";
    }

    // risultati
    auto med = [](std::vector<double>& v) {
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Timing CUDA (mediana su " << reps << " ripetizioni):\n";
    std::cout << "  H->D  : " << med(t_h2d)  << " ms\n";
    std::cout << "  Kernel: " << med(t_kern) << " ms\n";
    std::cout << "  D->H  : " << med(t_d2h)  << " ms\n";
    std::cout << "  Totale: " << med(t_tot)  << " ms\n";
    std::cout << "  Throughput (solo kernel): " << std::setprecision(1)
              << (double(N) / 1e6) / (med(t_kern) / 1e3) << " Mkeys/s\n";
    std::cout << "  Throughput (end-to-end):  "
              << (double(N) / 1e6) / (med(t_tot) / 1e3) << " Mkeys/s\n";
    std::cout << "  P=" << P << " shift=" << shift << " N=" << N << "\n";

    CUDA_CHECK(cudaFree(d_keys));
    CUDA_CHECK(cudaFree(d_part));
    CUDA_CHECK(cudaFreeHost(h_keys));
    CUDA_CHECK(cudaFreeHost(h_part_gpu));
    std::free(h_part_cpu);
    CUDA_CHECK(cudaEventDestroy(e0));
    CUDA_CHECK(cudaEventDestroy(e1));
    CUDA_CHECK(cudaEventDestroy(e2));
    CUDA_CHECK(cudaEventDestroy(e3));

    return ok ? 0 : 1;
}
