// mem_ceiling.cpp -- tetto di banda single-core MATCHED al pattern del kernel M1.
//
// Il kernel di partition mapping legge un array di uint64 (chiavi, 8 B) e scrive
// un array di uint32 (partition id, 4 B): 12 B/chiave di traffico "utile". Ma una
// store normale su una linea non in cache innesca un write-allocate (RFO), quindi
// il traffico DRAM reale e' ~16 B/chiave. Questo microbench misura, single-core:
//   read64    : sola lettura (8 B/elem)                      -> tetto di lettura
//   copy_tempo: out32[i]=in64[i], store temporali (autovec)  -> pattern del kernel
//   copy_nt   : idem ma con store non-temporali (_mm256_stream) -> niente RFO
// Riporta GB/s con convenzione "utile" (12 B) e "reale con RFO" (16 B) dove serve.
//
// Serve ad ANCORARE il "96% del tetto single-core (~16.4 GB/s)" del report con una
// misura reale su node09, oggi assente nel repo.
//
// build: g++ -std=c++20 -O3 -march=native mem_ceiling.cpp -o mem_ceiling
// run:   ./mem_ceiling [N] [reps]

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>
#include <algorithm>
#include <cmath>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

using clk = std::chrono::steady_clock;

static double median(std::vector<double>& v){
    std::sort(v.begin(), v.end());
    return v[v.size()/2];
}

static void* amalloc(size_t bytes){
    void* p=nullptr;
    if(posix_memalign(&p, 32, bytes)!=0){ perror("posix_memalign"); exit(1);} return p;
}

int main(int argc, char** argv){
    size_t N   = (argc>1)? std::stoull(argv[1]) : 100000000ull;
    int    reps= (argc>2)? std::stoi(argv[2])   : 11;

    uint64_t* in  = (uint64_t*)amalloc(N*sizeof(uint64_t));
    uint32_t* out = (uint32_t*)amalloc(N*sizeof(uint32_t));
    // first-touch + valori
    for(size_t i=0;i<N;i++){ in[i]=i*2654435761ull+12345ull; }
    std::memset(out,0,N*sizeof(uint32_t));

    volatile uint64_t sink=0;
    auto bench=[&](const char* name, auto kernel, double bytes_useful, double bytes_real){
        kernel(); // warmup
        std::vector<double> t; t.reserve(reps);
        for(int r=0;r<reps;r++){
            auto t0=clk::now(); kernel(); auto t1=clk::now();
            t.push_back(std::chrono::duration<double>(t1-t0).count());
        }
        double s=median(t);
        double bw_u = bytes_useful/1e9/s;
        double bw_r = bytes_real  /1e9/s;
        double mks  = (double)N/1e6/s;
        if(bytes_real>0 && std::abs(bytes_real-bytes_useful)>1)
            printf("%-12s  %8.3f ms  %8.2f Mkeys/s   %7.2f GB/s (utile %.0fB)  %7.2f GB/s (RFO %.0fB)\n",
                   name, s*1e3, mks, bw_u, bytes_useful/N, bw_r, bytes_real/N);
        else
            printf("%-12s  %8.3f ms  %8.2f Mkeys/s   %7.2f GB/s (%.0fB/elem)\n",
                   name, s*1e3, mks, bw_u, bytes_useful/N);
    };

    printf("# mem_ceiling  N=%zu  reps=%d  (in=%.2f GB, out=%.2f GB)\n",
           N, reps, N*8.0/1e9, N*4.0/1e9);

    // read-only: somma (il compilatore vettorizza la riduzione)
    bench("read64", [&](){ uint64_t a=0; for(size_t i=0;i<N;i++) a+=in[i]; sink=a; },
          N*8.0, 0.0);

    // copy pattern del kernel, store temporali
    bench("copy_tempo", [&](){ for(size_t i=0;i<N;i++) out[i]=(uint32_t)in[i]; },
          N*12.0, N*16.0);

#if defined(__AVX2__)
    // copy pattern del kernel, store non-temporali (niente RFO sull'output)
    const __m256i shuf = _mm256_setr_epi32(0,2,4,6,1,3,5,7);
    bench("copy_nt", [&](){
        size_t end=N-(N%8);
        for(size_t i=0;i<end;i+=8){
            __m256i v0=_mm256_load_si256((const __m256i*)&in[i]);
            __m256i v1=_mm256_load_si256((const __m256i*)&in[i+4]);
            __m256i p0=_mm256_permutevar8x32_epi32(v0,shuf);
            __m256i p1=_mm256_permutevar8x32_epi32(v1,shuf);
            __m256i m =_mm256_permute2x128_si256(p0,p1,0x20);
            _mm256_stream_si256((__m256i*)&out[i], m);
        }
        for(size_t i=end;i<N;i++) out[i]=(uint32_t)in[i];
        _mm_sfence();
    }, N*12.0, N*12.0);
#endif

    (void)sink;
    free(in); free(out);
    return 0;
}
