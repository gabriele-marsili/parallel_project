# Modulo di analisi (`analysis/`)

Pipeline di analisi dei risultati: converte l'output testuale dei benchmark in CSV strutturato, poi genera grafici per il report.

---

## Struttura

```
analysis/
├── parse_results.py       # output testuale -> CSV
├── plot_results.py        # CSV -> grafici PNG/PDF
└── generate_test_data.sh  # benchmark locali per testare la pipeline
```

Output prodotto in:
```
results/
├── cpu_results.csv        # dati CPU parsati
├── cuda_results.csv       # dati CUDA parsati
└── plots/
    ├── 01_throughput_vs_N.png
    ├── 02_throughput_vs_P.png
    ├── 03_speedup_vs_N.png
    ├── ...
    └── 10_summary_table.png
```

---

## Workflow

Il flusso è in 3 passi:

```
benchmark binari  ->  parse_results.py  ->  plot_results.py
  (testo)              (CSV)               (grafici)
```

### 1. Eseguire i benchmark (sul cluster)
```bash
sbatch scripts/run_bench.sh    # produce results/bench_*.out
sbatch scripts/run_cuda.sh     # produce results/cuda_*.out
```

### 2. Parsare i risultati
```bash
python3 analysis/parse_results.py
```
Cerca automaticamente in `results/` i file `.txt` e `.out`, li parsa con regex, e produce `cpu_results.csv` e `cuda_results.csv`.

### 3. Generare i grafici
```bash
python3 analysis/plot_results.py              # tutti i grafici
python3 analysis/plot_results.py --no-cuda    # solo CPU
python3 analysis/plot_results.py --format pdf # output PDF per LaTeX
```

### Test locale (senza cluster)
```bash
bash analysis/generate_test_data.sh    # esegue baseline + autovec
python3 analysis/parse_results.py
python3 analysis/plot_results.py --no-cuda
```
Su Apple Silicon produce grafici solo con baseline e autovec (no AVX2, no CUDA).

---

## Dipendenze Python

```bash
pip3 install pandas matplotlib
```
Nessun'altra dipendenza. Il backend matplotlib è `Agg` (non serve GUI), funziona anche via SSH.

---

## Lista dei grafici

| # | Nome | Cosa mostra | Perché è utile |
|---|------|-------------|----------------|
| 01 | `throughput_vs_N` | Throughput al variare di N, per implementazione | Mostra la **scalabilità**: come varia la performance con la dimensione dell'input. Per N grande il throughput cala -> memory bandwidth bound |
| 02 | `throughput_vs_P` | Throughput al variare di P | Verifica che il numero di partizioni non influenzi significativamente le performance (lo shift è un'operazione a costo costante) |
| 03 | `speedup_vs_N` | Speedup di autovec/AVX2 rispetto al baseline | Il grafico più importante per il report: quantifica il **guadagno** dalla vettorizzazione. Lo speedup cala per N grande -> bottleneck sulla bandwidth |
| 04 | `speedup_vs_P` | Speedup al variare delle partizioni | Verifica che lo speedup sia stabile indipendentemente da P |
| 05 | `time_vs_N` | Tempo mediano con barre di errore (stddev) | Mostra i tempi assoluti e la **variabilità** delle misurazioni |
| 06 | `distribution_quality` | Rapporto max/atteso nella distribuzione hash | Verifica la **qualità della hash**: valori vicini a 1.0 = distribuzione uniforme |
| 07 | `keyspace_sensitivity` | Throughput al variare del key_space | Mostra se i duplicati influenzano le performance (non dovrebbero, dato che la hash è compute-only senza branch) |
| 08 | `cuda_breakdown` | Stacked bar: H->D / kernel / D->H | Il grafico chiave per CUDA: mostra che il **transfer domina** il tempo totale |
| 09 | `cuda_vs_cpu` | Throughput GPU vs migliore CPU | Confronto diretto. Il kernel CUDA batte la CPU, ma end-to-end potrebbe perdere per i trasferimenti |
| 10 | `summary_table` | Tabella riepilogativa come immagine | Riassunto numerico includibile direttamente nel report |

---

## Come usare i grafici nel report

I grafici sono generati a 150 DPI, dimensione 10×6 pollici — ottimali per inclusione in LaTeX:

```latex
\begin{figure}[h]
    \centering
    \includegraphics[width=0.85\textwidth]{../results/plots/03_speedup_vs_N.png}
    \caption{Speedup di autovec e AVX2 rispetto al baseline, al variare di N.}
    \label{fig:speedup_n}
\end{figure}
```

Per output PDF (vettoriale, qualità migliore per la stampa):
```bash
python3 analysis/plot_results.py --format pdf
```

---

## Estendere con nuovi grafici

Per aggiungere un grafico:

1. Creare una funzione `plot_mio_grafico(df, outdir, fmt)` in `plot_results.py`
2. Usare `save_fig(fig, outdir, 'NN_nome', fmt)` per salvare
3. Chiamarla dal `main()` nella sezione appropriata (CPU o CUDA)

I dizionari `COLORS`, `MARKERS`, `LABELS` definiscono lo stile per ogni implementazione — usarli per coerenza visiva tra i grafici.
