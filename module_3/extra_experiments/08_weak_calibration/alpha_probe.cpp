// Esp. 8 — anatomia della FlatCountMap sotto le tre calibrazioni del weak scaling.
//
// Thread singolo, nessuna misura di scaling: qui interessa solo come cambia la
// struttura dati di UNA partizione al variare di (NR, max_key, P). Riporta il
// load factor EFFETTIVO (slot occupati / slot allocati, contati, non stimati) e
// il costo per chiave di build e probe.
//
// Generatore e hash sono quelli del binario di modulo, quindi le partizioni
// analizzate sono le stesse che vedrebbe il join.
//
// Uso:
//   ./alpha_probe -nr N -ns N -max-key K -p P [-seed S] [-npart K] [-reps R]

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <vector>

#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"
#include "join_phases.hpp"
#include "utilities_fns.hpp"

int main(int argc, char **argv) {
    std::uint64_t nr = 0, ns = 0, seed = 42, max_key = 0, p = 0;
    std::uint64_t npart = 8, reps = 3;

    if (!read_arg_u64(argc, argv, "-nr", nr) ||
        !read_arg_u64(argc, argv, "-ns", ns) ||
        !read_arg_u64(argc, argv, "-max-key", max_key) ||
        !read_arg_u64(argc, argv, "-p", p)) {
        std::fprintf(stderr,
                     "usage: %s -nr N -ns N -max-key K -p P "
                     "[-seed S] [-npart K] [-reps R]\n",
                     argv[0]);
        return 1;
    }
    read_arg_u64(argc, argv, "-seed", seed);
    read_arg_u64(argc, argv, "-npart", npart);
    read_arg_u64(argc, argv, "-reps", reps);
    if (npart > p) npart = p;

    const unsigned shift = compute_shift(static_cast<std::uint32_t>(p));

    const std::vector<Record> R = generate_relation(nr, seed, max_key);
    const std::vector<Record> S =
        generate_relation(ns, seed ^ 0xdeadebdecdeedef1ULL, max_key);

    // Estrae le prime npart partizioni. Il resto della relazione non serve:
    // le partizioni sono equivalenti sotto carico uniforme.
    std::vector<std::vector<std::uint64_t>> Rp(npart), Sp(npart);
    for (const Record &r : R) {
        const part_t pid = hash_key(r.key, shift);
        if (pid < npart) Rp[pid].push_back(r.key);
    }
    for (const Record &s : S) {
        const part_t pid = hash_key(s.key, shift);
        if (pid < npart) Sp[pid].push_back(s.key);
    }

    std::cout << "max_key,P,NR,NS,npart,rep,r_count,slots,table_kb,distinct,"
                 "alpha,build_ns_per_key,probe_ns_per_key,join_count\n";

    for (std::uint64_t rep = 0; rep < reps; ++rep) {
        double build_s = 0.0, probe_s = 0.0;
        std::uint64_t keys_built = 0, keys_probed = 0;
        std::uint64_t occupied = 0, slots_tot = 0, used = 0;
        JoinResult acc{};

        for (std::uint64_t i = 0; i < npart; ++i) {
            const std::vector<std::uint64_t> &rk = Rp[i];
            const std::vector<std::uint64_t> &sk = Sp[i];
            if (rk.empty() || sk.empty()) continue;

            const auto t0 = std::chrono::steady_clock::now();
            FlatCountMap tbl(rk.size());
            for (const std::uint64_t k : rk) tbl.increment(k);
            const auto t1 = std::chrono::steady_clock::now();

            // Probe fedele a probe_chunk(): stesso prefetch, stessa aggregazione.
            constexpr std::size_t PF_DIST = 8;
            JoinResult res{};
            const auto t2 = std::chrono::steady_clock::now();
            for (std::size_t j = 0; j < sk.size(); ++j) {
                if (j + PF_DIST < sk.size())
                    __builtin_prefetch(&tbl.slots[tbl.slot_of(sk[j + PF_DIST])], 0, 0);
                const std::uint32_t m = tbl.count(sk[j]);
                if (m) {
                    res.join_count += m;
                    res.checksum1 += splitmix64(sk[j]) * m;
                }
            }
            const auto t3 = std::chrono::steady_clock::now();

            build_s += std::chrono::duration<double>(t1 - t0).count();
            probe_s += std::chrono::duration<double>(t3 - t2).count();
            keys_built += rk.size();
            keys_probed += sk.size();
            slots_tot += tbl.slots.size();

            // Fuori dai timer: conta gli slot davvero occupati.
            for (const FlatCountMap::Slot &s : tbl.slots)
                if (s.key != ~0ULL) ++occupied;

            acc.join_count += res.join_count;
            acc.checksum1 += res.checksum1;
            ++used;
        }

        if (used == 0) {
            std::fprintf(stderr, "nessuna partizione popolata (P=%llu)\n",
                         (unsigned long long)p);
            return 1;
        }

        const double alpha = double(occupied) / double(slots_tot);
        const std::uint64_t slots_avg = slots_tot / used;

        std::cout << max_key << ',' << p << ',' << nr << ',' << ns << ',' << used
                  << ',' << rep << ',' << (keys_built / used) << ',' << slots_avg
                  << ',' << (slots_avg * sizeof(FlatCountMap::Slot) / 1024) << ','
                  << (occupied / used) << ',' << std::fixed << std::setprecision(4)
                  << alpha << ',' << std::setprecision(3)
                  << (build_s * 1e9 / double(keys_built)) << ','
                  << (probe_s * 1e9 / double(keys_probed)) << ',' << acc.join_count
                  << '\n';
        std::cout.flush();
    }

    return 0;
}
