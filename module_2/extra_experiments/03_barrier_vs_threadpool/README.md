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

## Risultati (node02, NR=10M, P=128)

| thread | microsync barrier | microsync pool | pipeline barrier | pipeline pool |
|---|---|---|---|---|
| 1  | 37 ns    | 7525 ns  | 1096 ms | 1108 ms |
| 8  | 8411 ns  | 8910 ns  | 202 ms  | 195 ms  |
| 32 | 32311 ns | 36386 ns | 89.5 ms | 89.4 ms |

La coda è **sempre** più cara sul sync (fino a 200x a 1 thread); end-to-end le due pareggiano.

## Lettura (come difenderlo)

1. Le dipendenze fra fasi (histogram -> prefix sum -> scatter -> join) impongono una barriera
   piena a ogni confine: nessuna fase parte prima che tutte abbiano finito la precedente.
2. Struttura bulk-synchronous: la coda di task NON compra overlap (nessuna fase può
   sovrapporsi), paga solo lock/wakeup in più. La barriera modella direttamente questo schema.
3. End-to-end pareggiano perché il lavoro (ms) domina il sync (µs): la barriera si sceglie per
   semplicità e minor overhead, non perché il pool renda lento.
