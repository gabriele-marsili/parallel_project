# Esp. 9 — confronto M2 vs M3 a parità di parametri

**Obiettivo.** Il confronto del report (`tab:m2`, sez. 5.7) non è a parità. M2 gira con
max_key=1M e prende il best of 5 (`module_2/scripts/bench_slurm.sh:24,35-48`), M3 con
max_key=5M e media di 3, e lo speedup di M2 è calcolato sulla baseline di M3 (0.802).
Qui: stessi parametri, stessa statistica, entrambe le baseline misurate a ogni max_key.

**Validazione.** A parità di max_key il `join_count` è identico fra M2 e M3 (199999829 a
1M, 40006682 a 5M): i due moduli calcolano la stessa cosa, il confronto è sui tempi di
implementazioni equivalenti.

## Risultati (node01, job 707274, 5 rep, media; best fra parentesi)

Tempi in secondi, NR=10M, NS=20M, P=128, uniform.

| | max_key=1M | | | max_key=5M | | |
|---|---|---|---|---|---|---|
| T | m2_par | m3_loop | gap | m2_par | m3_loop | gap |
| 1 | 0.7940 | 0.4969 | 1.60x | 0.8677 | 0.5556 | 1.56x |
| 2 | 0.5470 | 0.2663 | 2.05x | 0.5806 | 0.2955 | 1.96x |
| 4 | 0.3035 | 0.1508 | 2.01x | 0.3181 | 0.1642 | 1.94x |
| 8 | 0.1762 | 0.0905 | 1.95x | 0.1835 | 0.1117 | 1.64x |
| 16 | 0.1050 | 0.0592 | **1.78x** | 0.1238 | 0.0692 | **1.79x** |
| 32 | 0.0900 | 0.0679 | **1.33x** | 0.1164 | 0.0848 | **1.37x** |

Baseline sequenziali (media di 5): a 1M m2_seq 0.8311 e m3_seq 0.7971; a 5M m2_seq 0.9214
e m3_seq 0.8538.

## Cosa cambia rispetto al report

1. **Il vantaggio di M3 è più grande di quanto dichiarato.** A T=16 il report riporta
   1.46x (0.102 contro 0.070); a parità di carico è **1.79x** a 5M e 1.78x a 1M. Il
   confronto misto sottostimava M3 di circa il 22%, perché metteva M2 sul carico più
   facile (max_key=1M, load factor del join 0.030) e M3 su quello più duro (5M, alpha
   0.129). Il gap vero è stabile intorno a 1.8x su entrambi i carichi, ed è la misura
   corretta da citare.
2. **A T=32 non convergono.** Il report conclude che "the two implementations meet at
   T=32" (0.086 contro 0.089, 1.03x) e ne deduce che a saturazione di banda il modello di
   sincronizzazione diventa secondario. **Quella convergenza è un artefatto del confronto
   misto**: confronta M2 a 1M (0.089) con M3 a 5M (0.086). A parità, M3 resta avanti di
   1.33x a 1M e 1.37x a 5M. La conclusione qualitativa (la banda domina a T=32) resta
   plausibile per altre vie, ma non è sostenuta da quel numero: va ritirata o rimotivata.
3. **Non esiste una "shared sequential baseline".** I due moduli hanno due sequenziali
   diversi: a 5M m2_seq fa 0.9214 e m3_seq 0.8538, cioè l'8% di differenza. Il report ne
   usa uno solo (0.802) per gli speedup di entrambi. Con la sua baseline M2 a T=16 fa
   7.49, non 7.86.
4. **L'effetto best-of-5 è piccolo.** Fra media e best passa circa l'1% (m2_par a T=16,
   1M: 0.1050 contro 0.1036), non "qualche punto percentuale" come stimato prima di
   misurare. Delle tre asimmetrie, questa è la meno rilevante: quella che conta è max_key.

## Cosa non è affermabile

- Il breakdown per fase non è nel confronto: M2 non emette i tempi di fase su stderr, quindi
  a parità di carico si confrontano i totali. Il perché del gap (prefetch sullo scatter,
  modello di sincronizzazione sull'histogram) resta quello documentato nel report, ma non è
  stato rimisurato a parità di max_key.
- La baseline 0.802 del report non è riprodotta: qui m3_seq a 5M fa 0.8538 (media di 5)
  contro 0.802 dichiarato (media di 3). Il 6% di scarto non è spiegato e non è indagato;
  non tocca il confronto M2 vs M3, che è fra tempi paralleli misurati nello stesso job.

## Esecuzione

```bash
sbatch --job-name=m3_09 sbatch_one.sh 09_fair_m2
```
