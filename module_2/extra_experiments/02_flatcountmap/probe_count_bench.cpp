// probe_count_bench.cpp — il modello di Knuth sbaglia i probe o sbaglia il costo per probe?
//
// loadfactor_bench.cpp misura i NANOSECONDI per probe e il risultato NON e' proporzionale ai
// probe attesi da Knuth (ns/probe attesi varia da ~1.7 a ~7.0). Ci sono due spiegazioni possibili
// e vanno separate, altrimenti resta un "boh":
//   (a) la combinatoria di Knuth e' sbagliata qui -> il numero di probe non e' quello previsto;
//   (b) la combinatoria e' giusta ma i probe non costano tutti uguale -> il modello conta i
//       probe, non i cicli.
//
// Questo bench decide fra le due CONTANDO gli slot visitati, invece di cronometrarli. Replica
// esattamente il build+probe di loadfactor_bench.cpp (stesso splitmix64, stessa tabella fissa
// 2^17, stesse alpha, stesse chiavi), quindi i conteggi sono direttamente confrontabili con
// results/loadfactor.csv riga per riga.
//
// NOTA sulla riproducibilita': qui non si misura tempo. E' aritmetica intera deterministica,
// senza thread e senza clock, quindi l'output e' IDENTICO su qualunque macchina (Mac o nodo del
// cluster) e a qualunque livello di ottimizzazione. Se rilanciandolo ottieni numeri diversi, e'
// un bug, non rumore di misura. I NANOSECONDI, invece, restano specifici di node02.
//
// Riporta anche le cache line DISTINTE toccate dal probe: la tabella e' allocata 2 MB, ma con
// poche chiavi ne tocchi solo una frazione, e quel footprint effettivo cresce con alpha. Serve a
// spiegare perche' il tempo cresce gia' a alpha basso, dove i probe sono ancora ~1.
//
// Build: g++ -O2 -std=c++20 -Wall probe_count_bench.cpp -o probe_count_bench
// Out (CSV): alpha,n_distinct,probe_medio_contato,probe_medio_knuth,linee_cache_distinte,kb_toccati

#include <cstdint>
#include <cstdio>
#include <vector>

static inline std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

int main() {
    const int tablelog = 17; // 131072 slot * 16 B = 2 MB, come loadfactor_bench
    const std::size_t nslots = (std::size_t)1 << tablelog;
    const std::uint32_t mask = (std::uint32_t)(nslots - 1);
    struct Slot { std::uint64_t key = ~0ULL; std::uint32_t cnt = 0; std::uint32_t _p = 0; };
    static_assert(sizeof(Slot) == 16, "lo Slot deve restare 16 B: 4 per cache line");

    const double alphas[] = {0.10, 0.25, 0.40, 0.50, 0.60, 0.70, 0.80, 0.90, 0.95, 0.98};

    std::printf("alpha,n_distinct,probe_medio_contato,probe_medio_knuth,linee_cache_distinte,kb_toccati\n");
    for (double alpha : alphas) {
        const std::uint64_t D = (std::uint64_t)(alpha * (double)nslots);

        std::vector<std::uint64_t> keys(D);
        for (std::uint64_t i = 0; i < D; ++i) keys[i] = splitmix64(i) & 0x3FFFFFFF; // < 2^30

        // build — identico a loadfactor_bench / FlatCountMap::increment
        std::vector<Slot> slots(nslots);
        for (std::uint64_t k : keys) {
            std::uint32_t h = (std::uint32_t)k & mask;
            while (slots[h].key != ~0ULL && slots[h].key != k) h = (h + 1) & mask;
            slots[h].key = k;
            ++slots[h].cnt;
        }

        // probe — identico a FlatCountMap::count, ma contando gli slot visitati (>= 1)
        // e marcando le cache line distinte toccate (4 slot da 16 B per linea da 64 B).
        std::vector<std::uint8_t> line_touched(nslots / 4, 0);
        std::uint64_t total_probes = 0;
        for (std::uint64_t k : keys) {
            std::uint32_t h = (std::uint32_t)k & mask;
            std::uint64_t steps = 1;
            line_touched[h / 4] = 1;
            while (slots[h].key != ~0ULL && slots[h].key != k) {
                h = (h + 1) & mask;
                ++steps;
                line_touched[h / 4] = 1;
            }
            total_probes += steps;
        }

        std::uint64_t lines = 0;
        for (std::uint8_t v : line_touched) lines += v;

        // Knuth, linear probing, ricerca CON successo (il probe cerca solo chiavi presenti).
        const double knuth = 0.5 * (1.0 + 1.0 / (1.0 - alpha));

        std::printf("%.2f,%llu,%.3f,%.3f,%llu,%.0f\n", alpha, (unsigned long long)D,
                    (double)total_probes / (double)D, knuth,
                    (unsigned long long)lines, lines * 64.0 / 1024.0);
    }
    return 0;
}
