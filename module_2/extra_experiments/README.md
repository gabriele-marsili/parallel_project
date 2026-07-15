# Modulo 2, materiale extra per l'orale

Materiale **aggiuntivo** (non tocca report né codice consegnati) per preparare l'orale del
Modulo 2. Entry point: **`COMPANION_M2.pdf`** (walkthrough del report + risposte al todo + 5
esperimenti con grafici + deep dive analitici + cheat-sheet).

Tutti gli esperimenti girano su un **compute node Ivy Bridge** (`node02`: Xeon E5-2640 v2,
2x8 core, 16 fisici / 32 HT, L2 256 KB/core, L3 20 MB/socket), lo stesso tipo di nodo del
report M2 (partizione `normal`, NON node09 che serviva al Modulo 1).

## I sei esperimenti

| cartella | domanda | finding chiave |
|---|---|---|
| `01_baseline_vs_mine/` | contributo di ogni modifica vs baseline di partenza | hash + FlatCountMap sono una coppia obbligata (1.58x end-to-end) |
| `02_flatcountmap/` | cos'è/come è fatta FlatCountMap, load factor, L3, padding | probe 4x più veloce di unordered_map; false sharing fino a 2.2x |
| `03_barrier_vs_threadpool/` | perché barriera e non thread pool + queue | coda fino a 200x più cara sul sync; end-to-end pari |
| `04_join_load_balance/` | perché cyclic; cos'è LPT | uniforme: 4 strategie pari entro il rumore (cyclic a costo zero); skew: block collassa |
| `05_histogram_roofline/` | perché histogram scala male (I=0.125) | a 16 core legge a 40 GB/s = tetto banda -> memory-bound; ~11 istr/record (perf) |
| `06_amdahl/` | come si stima f, perché è apparente | f fittata 7.8% vs seriale letterale misurato 0.1% (la f = banda, non codice) |

Nota: `05_histogram_roofline/run_perf_oi.sh` usa `perf` (disponibile solo sul login node, non
sui compute) per misurare le istruzioni/record; il resto gira sul compute node.

## Riprodurre tutto

```bash
# sul cluster (i sorgenti sono in ~/module_2)
rsync -az extra_experiments/ spmcluster:~/module_2/extra_experiments/
ssh spmcluster 'cd ~/module_2 && sbatch extra_experiments/sbatch_all.sh'   # ~11 min su un nodo

# in locale, a job finito
bash extra_experiments/build_companion.sh   # rigenera COMPANION_M2.pdf
```

Ogni cartella `0N_*/` ha `README.md` (metodo, comando, tabella, come difenderlo), il sorgente
`.cpp`, `run_*.sh`, `plot_*.py`, `results/` e `plots/`.

## Note di onestà (nel companion, da dire all'orale)

1. La frazione seriale f ~ 0.078 di Amdahl è **apparente** (fittata): il codice davvero seriale
   è < 0.1%; f ingloba la saturazione di banda. Ecco perché il picco misurato resta sotto S_inf.
2. La FlatCountMap rende **solo** con la hash di Fibonacci (Esp. 1).
3. Il padding a 64 B è difensivo (accumulo thread-local, una scrittura finale); il campo `_p`
   dello Slot è ridondante con l'allineamento naturale.
