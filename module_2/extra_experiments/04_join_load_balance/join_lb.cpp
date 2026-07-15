// join_lb.cpp — distribuzione del carico nella fase Join Local: cyclic vs block vs dynamic vs LPT.
//
// Motiva la scelta del report ("Partitions are distributed cyclically") e spiega LPT.
// Partiziona R e S con la hash del progetto (fib), poi esegue SOLO la fase join con 4
// strategie di assegnamento delle P partizioni ai k thread, misurando:
//   - join_wall_ms  : tempo di parete della fase (max sui thread)
//   - imbalance     : max(busy_t) / mean(busy_t)  (1.0 = perfettamente bilanciato)
//
// Strategie:
//   cyclic  : t possiede t, t+k, t+2k, ...           (progetto — zero overhead)
//   block   : t possiede [t*P/k, (t+1)*P/k)          (contiguo)
//   dynamic : atomic<uint32> next; ognuno pesca next++ (work-stealing, granularita' 1)
//   lpt     : Longest-Processing-Time — ordina le partizioni per costo desc e assegna
//             greedy al thread meno carico (bilanciamento statico ottimo, O(P log P + P k))
//
// Carico:
//   uniforme  : -skew 0                 -> partizioni ~uguali (fib su chiavi uniformi)
//   skew      : -skew RHO -hot H        -> con prob RHO la chiave e' una di H "hot"
//               -> poche partizioni molto piu' grandi (stressa il load balancing)
//
// Build: g++ -O3 -std=c++20 -march=native -pthread -Wall -I../../include join_lb.cpp -o join_lb
// Out (CSV): strategy,skew,hot,threads,P,join_wall_ms,imbalance,max_part_frac,join_count

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"
#include "join_phases.hpp"

using Clock = std::chrono::steady_clock;

static std::uint64_t argu(int c, char** v, const char* k, std::uint64_t d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return std::strtoull(v[i+1], nullptr, 10);
    return d;
}
static double argf(int c, char** v, const char* k, double d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return std::strtod(v[i+1], nullptr);
    return d;
}
static std::string args(int c, char** v, const char* k, const char* d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return v[i+1];
    return d;
}

// Generatore con skew opzionale: frazione RHO delle chiavi presa da H valori "hot".
static std::vector<Record> gen_skew(std::size_t n, std::uint64_t seed, std::uint64_t max_key,
                                    double rho, int hot, const std::vector<std::uint64_t>& hotvals) {
    std::vector<Record> out(n);
    std::uint64_t st = seed;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t r = splitmix64_next(st);
        if (rho > 0.0 && hot > 0) {
            // usa i bit alti per la moneta, i bassi per la scelta -> indipendenti
            double coin = (double)(r >> 40) / (double)(1ULL << 24);
            if (coin < rho) { out[i].key = hotvals[(r >> 8) % hot]; continue; }
        }
        out[i].key = (max_key == 0) ? 0ULL : (r % max_key);
    }
    return out;
}

static void partition(const std::vector<Record>& rel, std::uint32_t P, unsigned shift,
                      std::vector<Record>& out, std::vector<std::size_t>& begin,
                      std::vector<std::size_t>& hist) {
    hist.assign(P, 0);
    for (const auto& rec : rel) ++hist[hash_key(rec.key, shift)];
    begin.assign(P, 0);
    std::size_t run = 0;
    for (std::uint32_t p = 0; p < P; ++p) { begin[p] = run; run += hist[p]; }
    std::vector<std::size_t> next = begin;
    for (const auto& rec : rel) { auto p = hash_key(rec.key, shift); out[next[p]++] = rec; }
}

// join di una lista di partizioni, restituisce risultato locale
static JoinResult join_parts(const std::vector<std::uint32_t>& parts,
                             const std::vector<Record>& outR, const std::vector<std::size_t>& gbR, const std::vector<std::size_t>& ghR,
                             const std::vector<Record>& outS, const std::vector<std::size_t>& gbS, const std::vector<std::size_t>& ghS) {
    JoinResult loc{};
    for (std::uint32_t pid : parts) {
        std::size_t rb = gbR[pid], re = rb + ghR[pid];
        std::size_t sb = gbS[pid], se = sb + ghS[pid];
        if (rb == re || sb == se) continue;
        FlatCountMap cntR(re - rb);
        for (std::size_t i = rb; i < re; ++i) cntR.increment(outR[i].key);
        for (std::size_t i = sb; i < se; ++i) { auto k = outS[i].key; auto m = cntR.count(k);
            if (m) { loc.join_count += m; loc.checksum1 += splitmix64(k)*m; loc.checksum2 += splitmix64(k^0x9e3779b97f4a7c15ULL)*m; } }
    }
    return loc;
}

int main(int argc, char** argv) {
    const std::string strat = args(argc, argv, "-strategy", "cyclic");
    const std::size_t NR = argu(argc, argv, "-nr", 10000000);
    const std::size_t NS = argu(argc, argv, "-ns", 20000000);
    const std::uint64_t seed = argu(argc, argv, "-seed", 42);
    const std::uint64_t max_key = argu(argc, argv, "-max-key", 1000000);
    const std::uint32_t P = static_cast<std::uint32_t>(argu(argc, argv, "-p", 128));
    const int nt = static_cast<int>(argu(argc, argv, "-t", 16));
    const double rho = argf(argc, argv, "-skew", 0.0);
    const int hot = static_cast<int>(argu(argc, argv, "-hot", 4));
    const int reps = static_cast<int>(argu(argc, argv, "-reps", 5));

    const unsigned shift = compute_shift(P);
    // valori hot: scelti cosi da cadere in partizioni distinte
    std::vector<std::uint64_t> hotvals;
    { std::uint64_t s = 0xABCDEF; std::vector<bool> seen(P, false);
      while ((int)hotvals.size() < hot) { std::uint64_t v = splitmix64_next(s) % (max_key ? max_key : (1ULL<<30));
        auto p = hash_key(v, shift); if (!seen[p]) { seen[p] = true; hotvals.push_back(v); } } }

    const auto R = gen_skew(NR, seed, max_key, rho, hot, hotvals);
    const auto S = gen_skew(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key, rho, hot, hotvals);

    std::vector<Record> outR(NR), outS(NS);
    std::vector<std::size_t> gbR, ghR, gbS, ghS;
    partition(R, P, shift, outR, gbR, ghR);
    partition(S, P, shift, outS, gbS, ghS);

    // costo per partizione (per LPT e per la frazione max)
    std::vector<std::size_t> cost(P);
    std::size_t tot_cost = 0, max_cost = 0;
    for (std::uint32_t p = 0; p < P; ++p) { cost[p] = ghR[p] + ghS[p]; tot_cost += cost[p]; max_cost = std::max(max_cost, cost[p]); }

    // costruisci le liste di partizioni per thread secondo la strategia
    auto build_lists = [&](void) -> std::vector<std::vector<std::uint32_t>> {
        std::vector<std::vector<std::uint32_t>> lst(nt);
        if (strat == "cyclic") {
            for (std::uint32_t p = 0; p < P; ++p) lst[p % nt].push_back(p);
        } else if (strat == "block") {
            for (std::uint32_t p = 0; p < P; ++p) { int t = (int)((std::size_t)p * nt / P); if (t >= nt) t = nt-1; lst[t].push_back(p); }
        } else if (strat == "lpt") {
            std::vector<std::uint32_t> order(P);
            std::iota(order.begin(), order.end(), 0u);
            std::sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b){ return cost[a] > cost[b]; });
            std::vector<std::size_t> load(nt, 0);
            for (std::uint32_t p : order) { int t = (int)(std::min_element(load.begin(), load.end()) - load.begin()); lst[t].push_back(p); load[t] += cost[p]; }
        }
        return lst; // "dynamic" gestita a parte
    };

    // Raccolgo TUTTE le ripetizioni (non solo il best): servono mean e deviazione standard
    // per dimostrare che le differenze fra strategie sono dentro il rumore di misura.
    std::vector<double> walls; walls.reserve(reps);
    double imb_sum = 0; double sched_ms = 0; std::uint64_t jc = 0;
    for (int rep = 0; rep < reps; ++rep) {
        std::vector<double> busy(nt, 0.0);
        std::vector<JoinResult> part_res(nt);

        // lists deve vivere fino al join() dei thread (che sta FUORI dall'if/else)
        std::vector<std::vector<std::uint32_t>> lists;
        std::atomic<std::uint32_t> nextp{0};

        // Costo dello SCHEDULING (fuori dal lavoro utile): per LPT è l'ordinamento O(P log P),
        // per cyclic/block è banale, per dynamic è 0 a monte (la contesa atomica è nel loop).
        auto s0 = Clock::now();
        if (strat != "dynamic") lists = build_lists();
        sched_ms += std::chrono::duration<double,std::milli>(Clock::now()-s0).count();

        auto wall0 = Clock::now();
        std::vector<std::thread> th; th.reserve(nt);
        if (strat == "dynamic") {
            for (int t = 0; t < nt; ++t) th.emplace_back([&, t]{
                auto b0 = Clock::now(); JoinResult loc{};
                for (;;) { std::uint32_t p = nextp.fetch_add(1); if (p >= P) break;
                    std::vector<std::uint32_t> one{p};
                    JoinResult r = join_parts(one, outR, gbR, ghR, outS, gbS, ghS);
                    loc.join_count += r.join_count; loc.checksum1 += r.checksum1; loc.checksum2 += r.checksum2; }
                part_res[t] = loc; busy[t] = std::chrono::duration<double,std::milli>(Clock::now()-b0).count();
            });
        } else {
            for (int t = 0; t < nt; ++t) th.emplace_back([&, t]{
                auto b0 = Clock::now();
                part_res[t] = join_parts(lists[t], outR, gbR, ghR, outS, gbS, ghS);
                busy[t] = std::chrono::duration<double,std::milli>(Clock::now()-b0).count();
            });
        }
        for (auto& x : th) x.join();
        double wall = std::chrono::duration<double,std::milli>(Clock::now()-wall0).count();

        double mx = *std::max_element(busy.begin(), busy.end());
        double mean = std::accumulate(busy.begin(), busy.end(), 0.0) / nt;
        imb_sum += (mean > 0) ? mx / mean : 1.0;
        JoinResult tot{}; for (auto& r : part_res) { tot.join_count += r.join_count; tot.checksum1 += r.checksum1; tot.checksum2 += r.checksum2; }
        walls.push_back(wall);
        jc = tot.join_count;
    }

    double wmean = std::accumulate(walls.begin(), walls.end(), 0.0) / walls.size();
    double var = 0; for (double w : walls) var += (w - wmean) * (w - wmean);
    double wstd = std::sqrt(var / walls.size());
    double imb = imb_sum / reps;
    sched_ms /= reps;
    double max_frac = (double)max_cost * nt / (double)tot_cost; // >1 => una partizione da sola supera la quota media di un thread
    // strategy,skew,hot,threads,P,wall_mean_ms,wall_std_ms,imbalance,sched_ms,max_part_frac,join_count
    std::printf("%s,%.2f,%d,%d,%u,%.3f,%.3f,%.3f,%.4f,%.3f,%llu\n",
                strat.c_str(), rho, hot, nt, P, wmean, wstd, imb, sched_ms, max_frac, (unsigned long long)jc);
    return 0;
}
