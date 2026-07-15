/*
 * avx2_hash64.cpp -- counterfactual: hash 32-bit (vpmulld) vs 64-bit (3x vpmuludq).
 *
 * Il report sostiene che una hash a 64 bit renderebbe l'AVX2 piu' lento dello scalare,
 * perche' AVX2 non ha una moltiplicazione 64x64 nativa: serve la decomposizione
 * "schoolbook" con 3 vpmuludq (32x32->64) + shift + add per ottenere i 64 bit bassi
 * del prodotto, e per giunta si processano solo 4 chiavi/registro invece di 8.
 *
 * Qui misuro davvero, su node09, quattro kernel a N=100M, P=256:
 *   scalar32 : ((k_lo^k_hi)*A32) >> (32-log2P)      (hash del report, riferimento)
 *   avx2_32  : idem in intrinsics, 1 vpmulld, 8 chiavi/iter
 *   scalar64 : (k*A64) >> (64-log2P)                 (multiplicative a 64 bit)
 *   avx2_64  : idem in intrinsics, 3 vpmuludq, 4 chiavi/iter
 * scalar32/scalar64 sono compilati senza auto-vettorizzazione (confronto equo).
 *
 * build (su node09):  g++ -std=c++20 -O3 -march=native -mavx2 -mfma -I ../../include \
 *                         avx2_hash64.cpp -o avx2_hash64
 */
#include "common.hpp"
#include <immintrin.h>

static constexpr uint64_t A64 = 0x9E3779B97F4A7C15ull; // floor(2^64/phi)

// ---- scalari (no autovec, riferimento equo) ----
#pragma GCC push_options
#pragma GCC optimize("no-tree-vectorize")
static void scalar32(const spm_key_t* __restrict__ k, part_t* __restrict__ o,
                     size_t N, unsigned sh32){
    for(size_t i=0;i<N;i++){ uint32_t lo=(uint32_t)k[i], hi=(uint32_t)(k[i]>>32);
        o[i]=(part_t)(((lo^hi)*HASH_A32)>>sh32); }
}
static void scalar64(const spm_key_t* __restrict__ k, part_t* __restrict__ o,
                     size_t N, unsigned sh64){
    for(size_t i=0;i<N;i++) o[i]=(part_t)((k[i]*A64)>>sh64);
}
#pragma GCC pop_options

// ---- AVX2 32-bit: 1 vpmulld, 8 chiavi/iter (kernel del report) ----
static void avx2_32(const spm_key_t* __restrict__ keys, part_t* __restrict__ out,
                    size_t N, unsigned sh32){
    const __m256i va = _mm256_set1_epi32((int32_t)HASH_A32);
    const __m256i shuf = _mm256_setr_epi32(0,2,4,6,1,3,5,7);
    size_t end=N-(N%8);
    for(size_t i=0;i<end;i+=8){
        __m256i v0=_mm256_load_si256((const __m256i*)&keys[i]);
        __m256i v1=_mm256_load_si256((const __m256i*)&keys[i+4]);
        __m256i x0=_mm256_xor_si256(v0,_mm256_srli_epi64(v0,32));
        __m256i x1=_mm256_xor_si256(v1,_mm256_srli_epi64(v1,32));
        __m256i p0=_mm256_permutevar8x32_epi32(x0,shuf);
        __m256i p1=_mm256_permutevar8x32_epi32(x1,shuf);
        __m256i m =_mm256_permute2x128_si256(p0,p1,0x20);
        __m256i h =_mm256_srli_epi32(_mm256_mullo_epi32(m,va),sh32);
        _mm256_storeu_si256((__m256i*)&out[i],h);
    }
    for(size_t i=end;i<N;i++){ uint32_t lo=(uint32_t)keys[i],hi=(uint32_t)(keys[i]>>32);
        out[i]=(part_t)(((lo^hi)*HASH_A32)>>sh32); }
}

// ---- AVX2 64-bit: 3 vpmuludq (decomposizione), 4 chiavi/iter ----
static void avx2_64(const spm_key_t* __restrict__ keys, part_t* __restrict__ out,
                    size_t N, unsigned sh64){
    const __m256i vAlo=_mm256_set1_epi64x((int64_t)(A64 & 0xFFFFFFFFull));
    const __m256i vAhi=_mm256_set1_epi64x((int64_t)(A64 >> 32));
    const __m256i shuf=_mm256_setr_epi32(0,2,4,6,0,2,4,6);
    size_t end=N-(N%4);
    for(size_t i=0;i<end;i+=4){
        __m256i vk =_mm256_load_si256((const __m256i*)&keys[i]);  // 4 chiavi uint64
        __m256i khi=_mm256_srli_epi64(vk,32);
        __m256i ll =_mm256_mul_epu32(vk ,vAlo);   // k_lo*A_lo   (vpmuludq 1)
        __m256i c1 =_mm256_mul_epu32(khi,vAlo);   // k_hi*A_lo   (vpmuludq 2)
        __m256i c2 =_mm256_mul_epu32(vk ,vAhi);   // k_lo*A_hi   (vpmuludq 3)
        __m256i mid=_mm256_add_epi64(c1,c2);
        __m256i prod=_mm256_add_epi64(ll,_mm256_slli_epi64(mid,32)); // 64 bit bassi
        __m256i h  =_mm256_srli_epi64(prod,sh64);
        __m256i pk =_mm256_permutevar8x32_epi32(h,shuf);            // 4 uint32 in low128
        _mm_storeu_si128((__m128i*)&out[i],_mm256_castsi256_si128(pk));
    }
    for(size_t i=end;i<N;i++) out[i]=(part_t)((keys[i]*A64)>>sh64);
}

template<class F>
static BenchResult run(F f, const spm_key_t* k, part_t* o, size_t N, unsigned sh, int reps){
    f(k,o,N,sh); // warmup
    std::vector<double> t; t.reserve(reps);
    for(int r=0;r<reps;r++){ auto a=std::chrono::high_resolution_clock::now();
        f(k,o,N,sh); auto b=std::chrono::high_resolution_clock::now();
        t.push_back(std::chrono::duration<double,std::milli>(b-a).count()); }
    return benchmark(t,N);
}

int main(int argc,char**argv){
    size_t N=(argc>1)?std::stoull(argv[1]):100000000ull;
    uint32_t P=(argc>2)?(uint32_t)std::stoul(argv[2]):256u;
    int reps=(argc>3)?std::stoi(argv[3]):11;
    unsigned sh32=compute_shift(P);          // 32-log2P
    unsigned sh64=64u-(32u-sh32);            // 64-log2P

    spm_key_t* k=alloc_aligned<spm_key_t>(N);
    part_t* o1=alloc_aligned<part_t>(N); part_t* o2=alloc_aligned<part_t>(N);
    KeyGenerator::generate(k,N,42,0);

    // correttezza: avx2 == scalare per ciascuna larghezza
    scalar32(k,o1,N,sh32); avx2_32(k,o2,N,sh32);
    bool ok32=(compute_checksum(o1,N)==compute_checksum(o2,N));
    scalar64(k,o1,N,sh64); avx2_64(k,o2,N,sh64);
    bool ok64=(compute_checksum(o1,N)==compute_checksum(o2,N));
    printf("Correttezza: avx2_32==scalar32 %s | avx2_64==scalar64 %s\n",
           ok32?"OK":"FAIL", ok64?"OK":"FAIL");
    if(!ok32||!ok64){ fprintf(stderr,"mismatch\n"); return 1; }

    auto s32=run(scalar32,k,o1,N,sh32,reps);
    auto a32=run(avx2_32 ,k,o2,N,sh32,reps);
    auto s64=run(scalar64,k,o1,N,sh64,reps);
    auto a64=run(avx2_64 ,k,o2,N,sh64,reps);

    auto line=[&](const char* nm, const BenchResult& r){
        printf("%-10s  median=%8.3f ms  throughput=%8.1f Mkeys/s  BW=%5.2f GB/s\n",
               nm, r.median_ms, r.throughput_Mkeys_per_s, r.throughput_Mkeys_per_s*12/1e3); };
    line("scalar32",s32); line("avx2_32",a32);
    line("scalar64",s64); line("avx2_64",a64);
    printf("speedup avx2_32/scalar32 = %.2fx | avx2_64/scalar64 = %.2fx\n",
           s32.median_ms/a32.median_ms, s64.median_ms/a64.median_ms);
    printf("CSV,%zu,%u,%.1f,%.1f,%.1f,%.1f\n", N, P,
           s32.throughput_Mkeys_per_s,a32.throughput_Mkeys_per_s,
           s64.throughput_Mkeys_per_s,a64.throughput_Mkeys_per_s);
    free(k); free(o1); free(o2); return 0;
}
