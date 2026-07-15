// hash_quality.cpp -- Qualita' di distribuzione delle hash di partizionamento.
// Confronta la Fibonacci multiply-shift 32-bit del report contro alternative
// (mod naive, low-bits del prodotto, no-fold, Fibonacci 64-bit) su piu'
// distribuzioni di chiavi (uniformi + strutturate/avversarie).
// Aritmetica pura e deterministica: i numeri sono hardware-independent
// (identici su Mac arm64 e su node09 x86_64) -> lo si esegue comunque su node09
// per coerenza metodologica con gli altri test del modulo.
//
// build:  c++ -std=c++20 -O3 hash_quality.cpp -o hash_quality
// run:    ./hash_quality [N] [P] [summary.csv] [occupancy.csv]
// Stampa una tabella leggibile e, se dati i path, scrive due CSV:
//   summary.csv    : dist,hash,N,P,max_exp,min,cov,gini,chi2_dof,entropy_norm,empty
//   occupancy.csv  : dist,hash,partition,count   (per gli istogrammi di occupazione)

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <vector>
#include <cmath>
#include <string>
#include <functional>
#include <algorithm>

static constexpr uint32_t A32 = 0x9E3779B9u;            // floor(2^32 / phi)
static constexpr uint64_t A64 = 0x9E3779B97F4A7C15ull;  // floor(2^64 / phi)

static inline unsigned log2u(uint32_t P){ unsigned l=0; while((1u<<l)<P) l++; return l; }

// -------- hash candidates: k -> [0,P) --------
static inline uint32_t h_fib32(uint64_t k, unsigned s32, uint32_t /*mask*/){
    uint32_t lo=(uint32_t)k, hi=(uint32_t)(k>>32);
    return (uint32_t)(((lo^hi)*A32) >> s32);              // report: high bits
}
static inline uint32_t h_fib32_lowbits(uint64_t k, unsigned /*s32*/, uint32_t mask){
    uint32_t lo=(uint32_t)k, hi=(uint32_t)(k>>32);
    return (uint32_t)(((lo^hi)*A32) & mask);              // low bits of product
}
static inline uint32_t h_mod(uint64_t k, unsigned /*s32*/, uint32_t mask){
    return (uint32_t)(k & mask);                          // naive k % P (P pow2)
}
static inline uint32_t h_fib64(uint64_t k, unsigned s32, uint32_t /*mask*/){
    unsigned s64 = 64u - (32u - s32);                     // 64 - log2P
    return (uint32_t)((k*A64) >> s64);                    // 64-bit multiplicative
}
static inline uint32_t h_mult32_nofold(uint64_t k, unsigned s32, uint32_t /*mask*/){
    uint32_t lo=(uint32_t)k;                              // ignora i 32 bit alti
    return (uint32_t)((lo*A32) >> s32);
}

struct Hash { const char* name; std::function<uint32_t(uint64_t,unsigned,uint32_t)> f; };

// -------- key generators (streaming, una chiave alla volta) --------
struct Xoshiro {                                          // identico a common.hpp
    uint64_t s[4];
    static uint64_t rotl(uint64_t x,int k){ return (x<<k)|(x>>(64-k)); }
    void seed(uint64_t sd){
        for(int i=0;i<4;i++){ sd+=0x9E3779B97F4A7C15ull; uint64_t z=sd;
            z=(z^(z>>30))*0xBF58476D1CE4E5B9ull; z=(z^(z>>27))*0x94D049BB133111EBull;
            z=z^(z>>31); s[i]=z; }
    }
    uint64_t next(){ uint64_t r=rotl(s[1]*5,7)*9; uint64_t t=s[1]<<17;
        s[2]^=s[0]; s[3]^=s[1]; s[1]^=s[2]; s[0]^=s[3]; s[2]^=t; s[3]=rotl(s[3],45);
        return r; }
};

struct Dist { const char* name; const char* desc; std::function<uint64_t(size_t,Xoshiro&)> f; };

// -------- metrics --------
struct Metrics { uint64_t mn,mx; double max_exp,cov,gini,chi2_dof,entropy_norm; uint32_t empty; };

static Metrics metrics(const std::vector<uint64_t>& c, double N){
    uint32_t P=(uint32_t)c.size(); double exp=N/P;
    uint64_t mn=~0ull,mx=0; uint32_t empty=0;
    double mean=N/P, var=0, chi2=0, H=0;
    for(uint64_t v:c){ mn=std::min(mn,v); mx=std::max(mx,v); if(v==0)empty++;
        double d=(double)v-mean; var+=d*d; chi2+=(d*d)/exp;
        if(v){ double p=(double)v/N; H-=p*std::log2(p);} }
    var/=P; double sd=std::sqrt(var);
    std::vector<uint64_t> s=c; std::sort(s.begin(),s.end());
    double cum=0,gsum=0,tot=0; for(uint64_t v:s) tot+=v;
    for(size_t i=0;i<s.size();i++){ cum+=s[i]; gsum+=cum; }
    double gini = tot>0 ? (P+1.0)/P - 2.0*gsum/(P*tot) : 0.0;
    return { mn,mx, (double)mx/exp, sd/mean, gini, chi2/(P-1), H/std::log2((double)P), empty };
}

int main(int argc,char**argv){
    size_t N = (argc>1)? std::stoull(argv[1]) : 100000000ull;
    uint32_t P = (argc>2)? (uint32_t)std::stoul(argv[2]) : 256u;
    const char* sum_path = (argc>3)? argv[3] : nullptr;
    const char* occ_path = (argc>4)? argv[4] : nullptr;
    unsigned lg=log2u(P), s32=32u-lg; uint32_t mask=P-1;

    std::vector<Hash> hashes = {
        {"fib32",         h_fib32},
        {"fib32_lowbits", h_fib32_lowbits},
        {"mod",           h_mod},
        {"fib64",         h_fib64},
        {"mult32_nofold", h_mult32_nofold},
    };
    std::vector<Dist> dists = {
        {"uniform",        "xoshiro256** (chiavi random 64-bit)",
            [](size_t,Xoshiro&x){ return x.next(); }},
        {"sequential",     "k=i (surrogate/row-id contigui)",
            [](size_t i,Xoshiro&){ return (uint64_t)i; }},
        {"strided_pow2",   "k=i*4096 (offset allineati / id con tag nei bit bassi)",
            [](size_t i,Xoshiro&){ return (uint64_t)i*4096ull; }},
        {"low16_zero",     "entropia solo nei bit alti, 16 bit bassi = 0",
            [](size_t,Xoshiro&x){ return (x.next()<<16); }},
        {"high32_only",    "entropia SOLO nei 32 bit alti (32 bit bassi = 0)",
            [](size_t,Xoshiro&x){ return (x.next()<<32); }},
        {"dup_1000",       "universo piccolo: k mod 1000 (molti duplicati, valori contigui)",
            [](size_t,Xoshiro&x){ return x.next()%1000ull; }},
        {"dup_struct",     "1000 valori distinti ma STRUTTURATI: (k mod 1000)*65536",
            [](size_t,Xoshiro&x){ return (x.next()%1000ull)<<16; }},
    };

    FILE* fs = sum_path ? std::fopen(sum_path,"w") : nullptr;
    FILE* fo = occ_path ? std::fopen(occ_path,"w") : nullptr;
    if(fs) std::fprintf(fs,"dist,hash,N,P,max_exp,min,cov,gini,chi2_dof,entropy_norm,empty\n");
    if(fo) std::fprintf(fo,"dist,hash,partition,count\n");

    printf("# N=%zu  P=%u  (log2P=%u, shift32=%u)\n", N, P, lg, s32);
    printf("# metriche: max/exp (1=ideale), CoV (0=ideale), Gini (0=ideale),\n");
    printf("#           chi2/dof (~1=uniforme), H/log2P (1=ideale), empty=part. vuote\n\n");

    for(auto& d : dists){
        printf("== dist: %-13s  %s ==\n", d.name, d.desc);
        printf("  %-16s %10s %10s %8s %8s %12s %8s %7s\n",
               "hash","max/exp","min","CoV","Gini","chi2/dof","H/log2P","empty");
        std::vector<std::vector<uint64_t>> cnt(hashes.size(), std::vector<uint64_t>(P,0));
        Xoshiro xo; xo.seed(42);
        for(size_t i=0;i<N;i++){
            uint64_t k=d.f(i,xo);
            for(size_t h=0;h<hashes.size();h++)
                cnt[h][ hashes[h].f(k,s32,mask) ]++;
        }
        for(size_t h=0;h<hashes.size();h++){
            Metrics m=metrics(cnt[h],(double)N);
            printf("  %-16s %10.4f %10llu %8.4f %8.4f %12.3f %8.5f %7u\n",
                   hashes[h].name, m.max_exp,(unsigned long long)m.mn,
                   m.cov, m.gini, m.chi2_dof, m.entropy_norm, m.empty);
            if(fs) std::fprintf(fs,"%s,%s,%zu,%u,%.6f,%llu,%.6f,%.6f,%.6f,%.6f,%u\n",
                   d.name,hashes[h].name,N,P,m.max_exp,(unsigned long long)m.mn,
                   m.cov,m.gini,m.chi2_dof,m.entropy_norm,m.empty);
            if(fo) for(uint32_t p=0;p<P;p++)
                   std::fprintf(fo,"%s,%s,%u,%llu\n",d.name,hashes[h].name,p,
                                (unsigned long long)cnt[h][p]);
        }
        printf("\n");
    }
    if(fs) std::fclose(fs);
    if(fo) std::fclose(fo);
    return 0;
}
