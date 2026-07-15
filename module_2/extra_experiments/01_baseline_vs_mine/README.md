# Esp. 1: baseline di partenza vs la versione consegnata

**Obiettivo.** Isolare il contributo di ogni modifica rispetto alla **baseline di partenza**
`hashjoin_seq_baseline.cpp` (il riferimento fornito: partizionamento `k & (P-1)` +
`std::unordered_map`) verso la **versione consegnata** `hashjoin_seq.cpp` (hash di Fibonacci +
`FlatCountMap`).

**Come.** Un solo binario (`seq_ablation.cpp`) esegue le 4 combinazioni `{mod,fib} x {umap,flat}`
sullo stesso input, con lo stesso timing per fase (template a compile time sui due assi, così
non c'è branch nel loop). Il delta fra due righe = effetto di UNA modifica.

```bash
salloc --partition=normal --nodes=1 --cpus-per-task=32 --exclusive --time=00:10:00
bash run_ablation.sh          # sul nodo Ivy Bridge
python3 plot_ablation.py      # in locale
```

## Risultati (node02 Ivy Bridge, NR=10M, NS=20M, P=128, max_key=1M)

| variante | hash | map | hist ms | scatter ms | join ms | total ms |
|---|---|---|---|---|---|---|
| V0 baseline di partenza | mod | umap | 39 | 407 | 732 | 1177 |
| V1 | fib | umap | 52 | 405 | 765 | 1221 |
| V2 | mod | flat | 39 | 407 | 714 | 1160 |
| **V3 (consegnata)** | **fib** | **flat** | 52 | 405 | **286** | **743** |

End-to-end V0 -> V3 = **1.58x**. Density sweep (fib fisso): flat vs umap sul join va da 2.5x
(max_key=100) a 3.5x (max_key=10M).

## Lettura (come difenderlo)

1. **Le due modifiche sono una coppia obbligata.** FlatCountMap da sola (V2) migliora il join
   di appena il 2%; solo con la hash fib (V3) crolla a 286 ms (2.5x su V2). Motivo: la slot
   function `key & mask` (identity) richiede bit bassi liberi; `mod` li usa come partition id
   (costanti dentro una partizione) -> collisioni; `fib` usa i bit alti, lascia i bassi liberi.
2. Il commento in `join_phases.hpp` ("avoids correlation with the Fibonacci hash") è provato qui.
3. Histogram e scatter sono identici fra le varianti: le modifiche toccano solo hash e join.
