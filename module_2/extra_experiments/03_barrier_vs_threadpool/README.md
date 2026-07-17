# Esp. 3: barriera vs thread pool con coda

**Obiettivo.** Motivare `std::barrier` invece di un thread pool + task queue, e mostrare quali
dipendenze dati impongono la barriera.

**Come.** `sync_bench.cpp` implementa la STESSA pipeline a 5 fasi due volte (barrier e pool),
nello stesso binario, sugli stessi dati e con la stessa distribuzione del lavoro: cambia solo
il primitivo di sincronizzazione. Modalità `-microsync R` misura il costo puro di un confine
di fase (R round di fasi vuote).

```bash
salloc --partition=normal --cpus-per-task=32 --exclusive --time=00:10:00
bash run_sync.sh
python3 plot_sync.py
```

## Risultati (node01, NR=10M, P=128)

| thread | microsync barrier | microsync pool | pipeline barrier | pipeline pool |
|---|---|---|---|---|
| 1  | 37 ns    | 7369 ns  | 1097.6 ms | 1097.8 ms |
| 8  | 7453 ns  | 9337 ns  | 201.3 ms  | 196.0 ms  |
| 32 | 32407 ns | 36977 ns | 87.0 ms   | 86.5 ms   |

La coda è **sempre** più cara sul sync (198x a 1 thread, 1.1x a 32); end-to-end le due pareggiano.

Riprodotto (job SLURM 703899, node01 esclusivo, `OMP_PROC_BIND=close OMP_PLACES=cores`): i
valori coincidono con la prima esecuzione entro il rumore. Il divario e2e barrier-vs-pool resta
sotto il ±7% su tutto il range (con il pool anzi un filo più veloce a diversi `T`, dentro il
rumore), quindi non c'è una direzione stabile: le due strategie pareggiano davvero.
`join_count = 199999829` identico su barrier e pool a ogni `T`: i due percorsi calcolano lo
stesso risultato (la coda non salta lavoro).

## Lettura (come difenderlo)

1. Le dipendenze fra fasi (histogram -> prefix sum -> scatter -> join) impongono una barriera
   piena a ogni confine: nessuna fase parte prima che tutte abbiano finito la precedente.
2. Struttura bulk-synchronous: la coda di task NON compra overlap (nessuna fase può
   sovrapporsi), paga solo lock/wakeup in più. La barriera modella direttamente questo schema.
3. End-to-end pareggiano perché il lavoro (ms) domina il sync (µs): la barriera si sceglie per
   semplicità e minor overhead, non perché il pool renda lento.
