// sync_bench.cpp — barrier vs thread-pool-con-queue, sulla STESSA pipeline hash join.
//
// Motiva la scelta del report ("A single team of k threads reused across phases via
// std::barrier; a thread pool with a task queue would add unnecessary indirection").
// Qui le DUE strategie girano nello stesso processo, sugli stessi dati, con la stessa
// distribuzione del lavoro: cambia SOLO il primitivo di sincronizzazione fra le fasi.
//
//   -sync barrier : std::barrier(C++20) con completion function (= codice consegnato)
//   -sync pool    : k worker persistenti + coda di task protetta da mutex/condition_variable;
//                   il main sottomette k task per fase e aspetta il completamento.
//
// Le dipendenze dati impongono comunque una barriera piena a OGNI confine di fase:
//   histogram -> (prefix sum) -> scatter -> histogram S -> scatter S -> join.
// Nessuna fase puo iniziare prima che TUTTI abbiano finito la precedente. Percio' la
// coda di task non compra nessun overlap: paga solo lock/wakeup in piu'.
//
// -microsync R : modalita' che misura il COSTO PURO di un confine di fase (R round di
//                fasi vuote), per isolare i ns/sync dei due primitivi.
//
// Build: g++ -O3 -std=c++20 -march=native -pthread -Wall -I../../include sync_bench.cpp -o sync_bench
// Out (CSV pipeline):  sync,threads,NR,NS,P,total_ms,join_count
// Out (CSV microsync): sync,threads,rounds,ns_per_phase_sync

#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <queue>
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
static std::string args(int c, char** v, const char* k, const char* d) {
    for (int i = 1; i + 1 < c; ++i) if (!std::strcmp(v[i], k)) return v[i+1];
    return d;
}

// ────────────────────────────────────────────────────────────────────────
// Thread pool con coda di task (mutex + condition_variable).
// Il main sottomette N task (submit) e poi aspetta che tutti finiscano (wait_all).
// ────────────────────────────────────────────────────────────────────────
class ThreadPool {
public:
    explicit ThreadPool(int n) : nthreads_(n) {
        for (int i = 0; i < n; ++i)
            workers_.emplace_back([this] { worker_loop(); });
    }
    ~ThreadPool() {
        { std::unique_lock<std::mutex> lk(m_); stop_ = true; } cv_.notify_all();
        for (auto& w : workers_) w.join();
    }
    // Sottomette una tornata di 'count' task identici parametrici su t=0..count-1.
    void submit_round(int count, const std::function<void(int)>& fn) {
        {
            std::unique_lock<std::mutex> lk(m_);
            fn_ = &fn;
            remaining_ = count;
            next_id_ = 0;
            total_ = count;
        }
        cv_.notify_all();
        // attende il completamento della tornata
        std::unique_lock<std::mutex> lk(done_m_);
        done_cv_.wait(lk, [this] { return remaining_.load() == 0; });
    }
private:
    void worker_loop() {
        for (;;) {
            const std::function<void(int)>* fn = nullptr;
            int id = -1;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [this] { return stop_ || (fn_ && next_id_ < total_); });
                if (stop_ && !(fn_ && next_id_ < total_)) return;
                id = next_id_++;
                fn = fn_;
            }
            (*fn)(id);
            if (remaining_.fetch_sub(1) == 1) {
                std::unique_lock<std::mutex> lk(done_m_);
                done_cv_.notify_one();
            }
        }
    }
    int nthreads_;
    std::vector<std::thread> workers_;
    std::mutex m_, done_m_;
    std::condition_variable cv_, done_cv_;
    const std::function<void(int)>* fn_ = nullptr;
    int next_id_ = 0, total_ = 0;
    std::atomic<int> remaining_{0};
    bool stop_ = false;
};

// Stato condiviso della pipeline (uguale per entrambe le strategie).
struct PipeState {
    const std::vector<Record>&R, &S;
    std::uint32_t P; int nt; unsigned shift;
    std::size_t NR, NS;
    std::vector<std::vector<std::size_t>> lhR, lhS;
    std::vector<std::size_t> ghR, ghS, gbR, gbS;
    std::vector<std::vector<std::size_t>> offR, offS;
    std::vector<Record>&outR, &outS;
    struct alignas(64) PR { JoinResult r{}; };
    std::vector<PR> res;
    PipeState(const std::vector<Record>&R_, const std::vector<Record>&S_, std::uint32_t P_, int nt_,
              std::vector<Record>&oR, std::vector<Record>&oS)
        : R(R_), S(S_), P(P_), nt(nt_), shift(compute_shift(P_)), NR(R_.size()), NS(S_.size()),
          lhR(nt_, std::vector<std::size_t>(P_,0)), lhS(nt_, std::vector<std::size_t>(P_,0)),
          ghR(P_,0), ghS(P_,0), gbR(P_,0), gbS(P_,0),
          offR(nt_, std::vector<std::size_t>(P_)), offS(nt_, std::vector<std::size_t>(P_)),
          outR(oR), outS(oS), res(nt_) {}
    inline std::size_t rb(int t) const { return (NR * t) / nt; }
    inline std::size_t re(int t) const { return (NR * (t+1)) / nt; }
    inline std::size_t sb(int t) const { return (NS * t) / nt; }
    inline std::size_t se(int t) const { return (NS * (t+1)) / nt; }
};

// I 5 kernel di fase (identici nei due mondi).
static void k_hist_R(PipeState& st, int t) {
    auto& lh = st.lhR[t]; for (std::size_t i = st.rb(t); i < st.re(t); ++i) ++lh[hash_key(st.R[i].key, st.shift)];
}
static void k_scatter_R(PipeState& st, int t) {
    auto cur = st.offR[t]; for (std::size_t i = st.rb(t); i < st.re(t); ++i) { auto p = hash_key(st.R[i].key, st.shift); st.outR[cur[p]++] = st.R[i]; }
}
static void k_hist_S(PipeState& st, int t) {
    auto& lh = st.lhS[t]; for (std::size_t i = st.sb(t); i < st.se(t); ++i) ++lh[hash_key(st.S[i].key, st.shift)];
}
static void k_scatter_S(PipeState& st, int t) {
    auto cur = st.offS[t]; for (std::size_t i = st.sb(t); i < st.se(t); ++i) { auto p = hash_key(st.S[i].key, st.shift); st.outS[cur[p]++] = st.S[i]; }
}
static void k_join(PipeState& st, int t) {
    JoinResult loc{};
    for (std::uint32_t pid = (std::uint32_t)t; pid < st.P; pid += (std::uint32_t)st.nt) {
        std::size_t rb = st.gbR[pid], re = rb + st.ghR[pid];
        std::size_t sb = st.gbS[pid], se = sb + st.ghS[pid];
        if (rb == re || sb == se) continue;
        FlatCountMap cntR(re - rb);
        for (std::size_t i = rb; i < re; ++i) cntR.increment(st.outR[i].key);
        for (std::size_t i = sb; i < se; ++i) { auto k = st.outS[i].key; auto m = cntR.count(k);
            if (m) { loc.join_count += m; loc.checksum1 += splitmix64(k)*m; loc.checksum2 += splitmix64(k^0x9e3779b97f4a7c15ULL)*m; } }
    }
    st.res[t].r = loc;
}
// merge + prefix + offset (parte sequenziale fra le fasi, uguale nei due mondi).
static void seq_merge(PipeState& st, bool isR) {
    auto& gh = isR ? st.ghR : st.ghS; auto& gb = isR ? st.gbR : st.gbS;
    auto& lh = isR ? st.lhR : st.lhS; auto& off = isR ? st.offR : st.offS;
    std::fill(gh.begin(), gh.end(), 0);
    for (int t = 0; t < st.nt; ++t) for (std::uint32_t p = 0; p < st.P; ++p) gh[p] += lh[t][p];
    exclusive_prefix_sum_inplace(gh, gb);
    for (std::uint32_t p = 0; p < st.P; ++p) { std::size_t o = gb[p]; for (int t = 0; t < st.nt; ++t) { off[t][p] = o; o += lh[t][p]; } }
}
static JoinResult reduce(PipeState& st) {
    JoinResult tot{}; for (int t = 0; t < st.nt; ++t) { tot.join_count += st.res[t].r.join_count; tot.checksum1 += st.res[t].r.checksum1; tot.checksum2 += st.res[t].r.checksum2; } return tot;
}

// ── Pipeline con thread pool + queue ──
static JoinResult pipeline_pool(PipeState& st) {
    ThreadPool pool(st.nt);
    pool.submit_round(st.nt, [&](int t){ k_hist_R(st, t); });
    seq_merge(st, true);
    pool.submit_round(st.nt, [&](int t){ k_scatter_R(st, t); });
    pool.submit_round(st.nt, [&](int t){ k_hist_S(st, t); });
    seq_merge(st, false);
    pool.submit_round(st.nt, [&](int t){ k_scatter_S(st, t); });
    pool.submit_round(st.nt, [&](int t){ k_join(st, t); });
    return reduce(st);
}

// ── Pipeline con std::barrier (come il codice consegnato) ──
static JoinResult pipeline_barrier(PipeState& st) {
    int phase = 0;
    auto on_barrier = [&]() noexcept {
        switch (phase) { case 0: seq_merge(st, true); break; case 2: seq_merge(st, false); break; default: break; }
        ++phase;
    };
    std::barrier sync(st.nt, on_barrier);
    std::vector<std::thread> th; th.reserve(st.nt);
    for (int t = 0; t < st.nt; ++t) {
        th.emplace_back([&, t]{
            k_hist_R(st, t);    sync.arrive_and_wait();
            k_scatter_R(st, t); sync.arrive_and_wait();
            k_hist_S(st, t);    sync.arrive_and_wait();
            k_scatter_S(st, t); sync.arrive_and_wait();
            k_join(st, t);      sync.arrive_and_wait();
        });
    }
    for (auto& x : th) x.join();
    return reduce(st);
}

// ── microsync: costo puro di un confine di fase, R round di fasi vuote ──
static double micro_barrier(int nt, std::uint64_t rounds) {
    std::barrier sync(nt);
    auto t0 = Clock::now();
    std::vector<std::thread> th; th.reserve(nt);
    for (int t = 0; t < nt; ++t) th.emplace_back([&]{ for (std::uint64_t r = 0; r < rounds; ++r) sync.arrive_and_wait(); });
    for (auto& x : th) x.join();
    auto t1 = Clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / rounds;
}
static double micro_pool(int nt, std::uint64_t rounds) {
    ThreadPool pool(nt);
    auto noop = [](int){};
    auto t0 = Clock::now();
    for (std::uint64_t r = 0; r < rounds; ++r) pool.submit_round(nt, noop);
    auto t1 = Clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / rounds;
}

int main(int argc, char** argv) {
    const std::string sync = args(argc, argv, "-sync", "barrier");
    const int nt = static_cast<int>(argu(argc, argv, "-t", 16));
    const std::uint64_t micro = argu(argc, argv, "-microsync", 0);
    const int reps = static_cast<int>(argu(argc, argv, "-reps", 5));

    if (micro > 0) {
        double best = 1e18;
        for (int r = 0; r < reps; ++r) {
            double v = (sync == "pool") ? micro_pool(nt, micro) : micro_barrier(nt, micro);
            if (v < best) best = v;
        }
        std::printf("%s,%d,%llu,%.1f\n", sync.c_str(), nt, (unsigned long long)micro, best);
        return 0;
    }

    const std::size_t NR = argu(argc, argv, "-nr", 10000000);
    const std::size_t NS = argu(argc, argv, "-ns", 20000000);
    const std::uint64_t seed = argu(argc, argv, "-seed", 42);
    const std::uint64_t max_key = argu(argc, argv, "-max-key", 1000000);
    const std::uint32_t P = static_cast<std::uint32_t>(argu(argc, argv, "-p", 128));

    const auto R = generate_relation(NR, seed, max_key);
    const auto S = generate_relation(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key);

    double best = 1e18; std::uint64_t jc = 0;
    for (int r = 0; r < reps; ++r) {
        std::vector<Record> outR(NR), outS(NS);
        PipeState st(R, S, P, nt, outR, outS);
        auto t0 = Clock::now();
        JoinResult res = (sync == "pool") ? pipeline_pool(st) : pipeline_barrier(st);
        auto t1 = Clock::now();
        double m = ms(t0, t1);
        if (m < best) best = m;
        jc = res.join_count;
    }
    std::printf("%s,%d,%zu,%zu,%u,%.3f,%llu\n", sync.c_str(), nt, NR, NS, P, best, (unsigned long long)jc);
    return 0;
}
