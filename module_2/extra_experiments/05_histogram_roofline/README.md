# Esp. 5: histogram memory-bound e roofline (I = 0.125)

**Obiettivo.** Capire e verificare il claim del report: l'Histogram è memory-bandwidth-bound,
con intensità operazionale I ~ 0.125 FLOP/byte, e quindi scala male.

**Derivazione di I = 0.125.** Per record l'histogram legge 8 B (la chiave) e fa ~1 operazione
utile (hash + `++hist[pid]`). L'incremento colpisce un array di P elementi residente in cache,
quindi non aggiunge traffico DRAM. Traffico ~ 8 B/record, lavoro ~ 1 op/record ->
**I = 1/8 = 0.125 op/byte**. Sul roofline cade nella regione memory-bound (tetto = banda x I).

**Come.** `mem_bw.cpp` misura la banda del nodo con tre kernel a 1..16 thread: `read` (somma
di N uint64, tetto di lettura), `copy` (STREAM-like), `histo` (il kernel di fase 0 vero).

```bash
salloc --partition=normal --cpus-per-task=32 --exclusive --time=00:10:00
bash run_roofline.sh
python3 plot_roofline.py
```

## Risultati (node02, N=200M uint64 = 1.6 GB, fuori cache)

| kernel | 1 core | picco (thread) |
|---|---|---|
| read  | 13.2 GB/s | 41.1 GB/s (16) |
| copy  | 10.1 GB/s | 26.6 GB/s (8) |
| histo | 4.7 GB/s  | 40.3 GB/s (16) |

## Lettura (come difenderlo)

1. A 1 core l'histogram (4.7 GB/s) è compute/latency-bound (catena load-modifica-store su
   `hist[pid]` + hash). A 16 core raggiunge 40 GB/s = il tetto della read pura -> genuinamente
   memory-bound a scala.
2. **Numeri che tornano col report:** histogram_R = 17 ms per 10M chiavi = 80 MB / 0.017 s =
   4.7 GB/s, identico al mio single-core. E l'histogram scala solo 2-3x a p=32: coerente con
   I=0.125 (satura la banda con pochi core, poi non guadagna più; la banda è condivisa).
