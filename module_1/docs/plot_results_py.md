# `analysis/plot_results.py`

Genera tutti i grafici di analisi a partire dai CSV prodotti da `parse_results.py`.

---

## Dipendenze

```python
import pandas as pd       # manipolazione dati tabulari
import matplotlib.pyplot   # grafici
import numpy as np         # calcoli numerici
```

`matplotlib.use('Agg')` seleziona il backend non-interattivo: genera le immagini senza aprire finestre. Indispensabile per eseguire lo script via SSH sul cluster (dove non c'è display).

---

## Configurazione globale

### Stile
```python
plt.rcParams.update({
    'figure.figsize': (10, 6),
    'figure.dpi': 150,
    'axes.grid': True,
    ...
})
```
Imposta dimensioni, risoluzione e griglia per tutti i grafici. 150 DPI × 10×6 pollici = immagini da 1500×900 pixel, buone sia per lo schermo che per il report.

### Colori e marker per implementazione
```python
COLORS = {
    'baseline': '#2196F3',  # blu
    'autovec':  '#4CAF50',  # verde
    'avx2':     '#FF9800',  # arancione
    'cuda':     '#9C27B0',  # viola
}
```
Usare colori e marker coerenti tra tutti i grafici rende il report più leggibile — il lettore associa "arancione = AVX2" e non deve rileggere la legenda ogni volta.

---

## Come funzionano i grafici (pattern comune)

Ogni funzione `plot_*` segue lo stesso schema:

```python
def plot_qualcosa(df, outdir, fmt):
    # 1. filtra i dati rilevanti
    sweep = df[(df['key_space'] == 0)].copy()
    
    # 2. crea la figura
    fig, ax = plt.subplots()
    
    # 3. plotta una serie per ogni implementazione
    for impl in ['baseline', 'autovec', 'avx2']:
        sub = sweep[sweep['impl'] == impl].sort_values('N')
        ax.plot(sub['N'], sub['throughput_Mkeys_s'], ...)
    
    # 4. configura assi, titolo, legenda
    ax.set_xlabel(...)
    ax.legend()
    
    # 5. salva e chiudi
    save_fig(fig, outdir, 'nome_grafico', fmt)
```

---

## Grafici nel dettaglio

### 01 — Throughput vs N
Mostra come il throughput (Mkeys/s) varia con la dimensione dell'input.

**Cosa cercare**: per N piccolo (1M) il throughput è alto perché i dati stanno in cache L2/L3. Per N grande (100M+) i dati non ci stanno più → il throughput cala e si stabilizza sul limite della bandwidth di memoria principale.

**Asse X logaritmico**: perché N varia su ordini di grandezza diversi (1M → 200M). Senza scala log, i punti a 1M e 10M sarebbero schiacciati a sinistra.

### 03 — Speedup vs N
Lo speedup è calcolato come:
```python
speedup = baseline_median_ms / impl_median_ms
```
Un merge sui dati allinea le misurazioni per N uguale.

**Cosa cercare**: lo speedup della vettorizzazione cala per N grande. Questo indica che il bottleneck si sposta dal compute (dove la vettorizzazione aiuta) alla memoria (dove non aiuta — tutti leggono gli stessi dati dalla stessa RAM).

### 05 — Tempo vs N con barre di errore
Usa `ax.errorbar()` con `yerr=stddev_ms` per mostrare la variabilità.

**Scala log-log**: tempo e N crescono entrambi linearmente (doppiando N si doppia il tempo), quindi in scala log-log appaiono come rette. Deviazioni dalla retta indicano effetti di cache.

### 06 — Qualità distribuzione
Il rapporto `max(count) / atteso` dovrebbe essere vicino a 1.0 per una buona hash. La linea rossa tratteggiata segna la distribuzione perfetta.

**Cosa cercare**: per N piccolo il rapporto è più alto (varianza statistica), per N grande converge a ~1.0 (legge dei grandi numeri).

### 07 — Sensibilità al key_space
Asse X categorico (non numerico) perché key_space=0 significa "full 64-bit" e va mostrato come label speciale.

**Cosa cercare**: il throughput dovrebbe essere costante — la hash moltiplica comunque, indipendentemente dai duplicati. Se cambia, potrebbe essere un effetto di cache (chiavi in uno spazio ristretto → meno cache miss).

### 08 — CUDA breakdown (stacked bar)
```python
ax.bar(x, h2d, width, label='H→D transfer')
ax.bar(x, kern, width, bottom=h2d, label='Kernel')
ax.bar(x, d2h, width, bottom=h2d + kern, label='D→H transfer')
```
Le barre sono impilate: il parametro `bottom` di ogni barra parte dalla cima della precedente.

**Cosa cercare**: il kernel (verde) è una fetta minuscola, i trasferimenti (rosso + blu) dominano. Questo è il messaggio chiave: per operazioni semplici, la GPU è limitata dalla PCIe, non dal compute.

### 09 — CUDA vs CPU
Confronta il throughput della migliore CPU (AVX2 se disponibile, altrimenti autovec) con il CUDA kernel-only e end-to-end.

**Cosa cercare**: il kernel CUDA batte la CPU, ma end-to-end potrebbe essere più lento per i trasferimenti.

### 10 — Tabella riepilogativa
Usa `ax.table()` di matplotlib per generare una tabella come immagine — così è includibile nel report come figure senza bisogno di formattazione LaTeX manuale.

I dati sono aggregati per implementazione (mediana se ci sono duplicati dallo sweep P).

---

## Funzioni di utilità

### `format_N(n)`
```python
def format_N(n):
    if n >= 1e9: return f'{n/1e9:.0f}G'
    if n >= 1e6: return f'{n/1e6:.0f}M'
    ...
```
Converte i numeri grandi in etichette leggibili per gli assi (1000000 → "1M").

### `save_fig(fig, outdir, name, fmt)`
Salva con `bbox_inches='tight'` che ritaglia i bordi bianchi inutili (senza, matplotlib lascia margini enormi). `facecolor='white'` assicura sfondo bianco (il default in alcuni temi è trasparente, brutto nel report).

---

## Opzioni da riga di comando

```
--cpu FILE      CSV risultati CPU (default: results/cpu_results.csv)
--cuda FILE     CSV risultati CUDA (default: results/cuda_results.csv)
--no-cuda       Salta i grafici CUDA
--outdir DIR    Directory output (default: results/plots)
--format FMT    png (default), pdf, svg
```

Per il report LaTeX, `--format pdf` produce grafici vettoriali (scalabili senza perdita di qualità, peso minore per la stampa).
