# Esp. 2: anatomia della FlatCountMap

**Obiettivo.** Motivare la FlatCountMap in dettaglio: open addressing vs unordered_map, load
factor, residenza in L3, e il padding a 64 B contro il false sharing.

**Come.** `flatmap_bench.cpp` isola il build+probe di una partizione a thread singolo
(templato sul moltiplicatore di sizing per variare il load factor). `false_sharing.cpp` fa
scrivere k thread su un array condiviso `volatile` (il volatile impedisce la register
promotion), packed (8 B) vs padded (64 B).

```bash
salloc --partition=normal --cpus-per-task=32 --exclusive --time=00:10:00
bash run_flatmap.sh
python3 plot_flatmap.py
```

## Risultati (node02, single core dove non indicato)

- **impl** (distinct=20k): probe umap 18.8 ns vs FlatCountMap 4.7 ns = **4x**. I 3 load factor
  (x1/x2/x4) sono quasi identici QUI (4.9/4.7/4.6 ns) perché la tabella è sparsa (duplicati ->
  alpha basso). Il load factor conta solo vicino al 100%.
- **loadfactor** (`loadfactor_bench.cpp`): probe vs alpha -> piatto fino a 50% (10 ns), poi
  esplode (22 ns a 90%, 43 ns a 98%, forma di Knuth). Il sizing x2 (`n < r_count*2`) garantisce
  alpha <= 50% nel CASO PEGGIORE (tutte chiavi distinte) -> resta nella zona piatta; x1 rischia.
- **cache**: probe FlatCountMap 3.8 ns (64 KB, L2) -> 5.8 (16 MB, L3) -> 14.4 ns (1 GB, DRAM).
  Il divario con unordered_map si allarga fuori da L3 (flat 14 vs umap 77 ns): fuori cache ogni
  accesso e' un miss a ~100 ns; umap fa pointer chasing (miss casuali, piu' per lookup), flat fa
  linear probing (slot contigui, prefetchabili, ~un miss per lookup). Motiva "fittare in L3".
- **false sharing** (stesso lavoro): packed 123 ms (1t) -> 349 ms (32t); padded piatto ~130 ms.
  Fino a **2.2x** di penalità a 16-32 thread.

## Lettura (come difenderlo)

1. Open addressing contiguo (16 B/slot, 4 per cache line) batte separate chaining: niente
   allocazioni per-nodo, niente pointer chasing.
2. **Onestà 1:** nel join consegnato l'accumulo è thread-local con una sola scrittura finale,
   quindi il padding è difensivo (il false sharing è reale ma non è la hot loop).
3. **Onestà 2:** il campo `_p` dello Slot è ridondante con l'allineamento naturale (lo Slot
   sarebbe 16 B comunque); rende solo esplicito l'intento.
