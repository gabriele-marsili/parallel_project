// serial_fraction.cpp — MISURA la frazione seriale letterale della pipeline.
//
// Amdahl: S(p) = 1/(f + (1-f)/p). La domanda del report è: quanto vale f? Il report la
// STIMA fittando la curva di speedup (f ~ 0.078). Ma qual è la frazione seriale VERA del
// codice, cioè la parte che gira su UN solo thread a ogni p? È la barrier completion:
// merge dei k istogrammi locali (O(P*k)) + prefix sum (O(P)) + calcolo offset (O(P*k)),
// eseguita dal thread delegato fra le fasi. Questo binario replica fedelmente la pipeline
// consegnata (hashjoin_parallel.cpp) e cronometra SEPARATAMENTE quella parte seriale.
//
// Output: threads, total_ms, serial_ms, serial_frac (= serial/total).
// Confrontando serial_frac (misurato) con f del fit (0.078) si vede che il codice davvero
// seriale è < 0.1%: la f del fit NON è codice seriale, è saturazione di banda incassata dal
// modello a un parametro. (vedi COMPANION_M2 §4.1)
//
// Build: g++ -O3 -std=c++20 -march=native -pthread -Wall -I../../include serial_fraction.cpp -o serial_fraction

#include <barrier>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"
#include "join_phases.hpp"

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}
static std::uint64_t argu(int c, char** v, const char* k, std::uint64_t d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return std::strtoull(v[i+1], nullptr, 10);
    return d;
}

int main(int argc, char** argv) {
    const std::size_t NR = argu(argc, argv, "-nr", 10000000);
    const std::size_t NS = argu(argc, argv, "-ns", 20000000);
    const std::uint64_t seed = argu(argc, argv, "-seed", 42);
    const std::uint64_t max_key = argu(argc, argv, "-max-key", 1000000);
    const std::uint32_t P = static_cast<std::uint32_t>(argu(argc, argv, "-p", 128));
    const int nt = static_cast<int>(argu(argc, argv, "-t", 16));
    const int reps = static_cast<int>(argu(argc, argv, "-reps", 5));
    const unsigned shift = compute_shift(P);

    const auto R = generate_relation(NR, seed, max_key);
    const auto S = generate_relation(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key);

    double best_total = 1e18, serial_at_best = 0;
    for (int rep = 0; rep < reps; ++rep) {
        std::vector<Record> outR(NR), outS(NS);
        std::vector<std::vector<std::size_t>> lhR(nt, std::vector<std::size_t>(P,0)), lhS(nt, std::vector<std::size_t>(P,0));
        std::vector<std::size_t> ghR(P,0), ghS(P,0), gbR(P,0), gbS(P,0);
        std::vector<std::vector<std::size_t>> offR(nt, std::vector<std::size_t>(P)), offS(nt, std::vector<std::size_t>(P));
        struct alignas(64) PR { JoinResult r{}; };
        std::vector<PR> res(nt);

        double serial_ms = 0.0;   // accumulatore del tempo della barrier completion (seriale)

        // merge + prefix + offset, IDENTICO alla completion function consegnata, cronometrato
        auto seq_merge = [&](bool isR){
            auto s0 = Clock::now();
            auto& gh = isR?ghR:ghS; auto& gb = isR?gbR:gbS; auto& lh = isR?lhR:lhS; auto& off = isR?offR:offS;
            for (std::uint32_t p=0;p<P;++p){ std::size_t s=0; for(int t=0;t<nt;++t) s+=lh[t][p]; gh[p]=s; }
            exclusive_prefix_sum_inplace(gh, gb);
            for (std::uint32_t p=0;p<P;++p){ std::size_t o=gb[p]; for(int t=0;t<nt;++t){ off[t][p]=o; o+=lh[t][p]; } }
            serial_ms += ms(s0, Clock::now());
        };

        int phase = 0;
        auto on_barrier = [&]() noexcept {
            if (phase==0) seq_merge(true);
            else if (phase==2) seq_merge(false);
            ++phase;
        };
        std::barrier sync(nt, on_barrier);

        auto t0 = Clock::now();
        std::vector<std::thread> th; th.reserve(nt);
        for (int t=0;t<nt;++t) th.emplace_back([&,t]{
            const std::size_t rb=(NR*t)/nt, re=(NR*(t+1))/nt, sb=(NS*t)/nt, se=(NS*(t+1))/nt;
            { auto& lh=lhR[t]; for(std::size_t i=rb;i<re;++i) ++lh[hash_key(R[i].key,shift)]; } sync.arrive_and_wait();
            { auto cur=offR[t]; for(std::size_t i=rb;i<re;++i){ auto p=hash_key(R[i].key,shift); outR[cur[p]++]=R[i]; } } sync.arrive_and_wait();
            { auto& lh=lhS[t]; for(std::size_t i=sb;i<se;++i) ++lh[hash_key(S[i].key,shift)]; } sync.arrive_and_wait();
            { auto cur=offS[t]; for(std::size_t i=sb;i<se;++i){ auto p=hash_key(S[i].key,shift); outS[cur[p]++]=S[i]; } } sync.arrive_and_wait();
            { JoinResult loc{}; for(std::uint32_t pid=(std::uint32_t)t;pid<P;pid+=(std::uint32_t)nt){
                std::size_t r0=gbR[pid], r1=r0+ghR[pid], s0=gbS[pid], s1=s0+ghS[pid];
                if(r0==r1||s0==s1) continue;
                FlatCountMap c(r1-r0); for(std::size_t i=r0;i<r1;++i) c.increment(outR[i].key);
                for(std::size_t i=s0;i<s1;++i){ auto k=outS[i].key; auto m=c.count(k); if(m){ loc.join_count+=m; loc.checksum1+=splitmix64(k)*m; loc.checksum2+=splitmix64(k^0x9e3779b97f4a7c15ULL)*m; } } }
              res[t].r=loc; } sync.arrive_and_wait();
        });
        for(auto& x:th) x.join();
        double total = ms(t0, Clock::now());
        if (total < best_total) { best_total = total; serial_at_best = serial_ms; }
    }
    std::printf("%d,%.4f,%.5f,%.6f\n", nt, best_total, serial_at_best, serial_at_best/best_total);
    return 0;
}
