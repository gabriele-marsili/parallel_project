# `analysis/parse_results.py`

Converte l'output testuale dei benchmark (prodotto da `run_bench.sh` e `run_cuda.sh`) in CSV strutturato, leggibile da `plot_results.py` e da qualsiasi foglio di calcolo.

---

## Il problema

L'output dei binari è testo pensato per la lettura umana:
```
plain (no-vec)            N=1000000     median=0.275    ms  stddev=0.048  ms  throughput=3640.2 Mkeys/s
  checksum=0x14802eb66a61103f
  P=256 shift=56 seed=42 key_space=0
  Distribuzione: min=3764 max=4070 atteso=3906.2 max/atteso=1.0419
```

Serve estrarre i numeri e organizzarli in una tabella. Lo facciamo con regex.

---

## Le regex principali

### `RE_CPU_RESULT`
```python
RE_CPU_RESULT = re.compile(
    r'(?P<label>[\w\s()/-]+?)\s+'
    r'N=(?P<N>\d+)\s+'
    r'median=(?P<median>[\d.]+)\s+ms\s+'
    r'stddev=(?P<stddev>[\d.]+)\s+ms\s+'
    r'throughput=(?P<throughput>[\d.]+)\s+Mkeys/s'
)
```

Cattura una riga di risultato CPU. I gruppi nominati (`?P<nome>`) estraggono direttamente i valori. `[\w\s()/-]+?` cattura il label (es. "plain (no-vec)") in modo non-greedy (`+?`) — altrimenti mangerebbe anche "N=".

### `RE_CONTEXT`
```python
RE_CONTEXT = re.compile(
    r'---\s+N=(?P<N>\d+)\s+P=(?P<P>\d+)'
    r'(?:\s+key_space=(?P<key_space>\d+))?\s*---'
)
```

Cattura la riga di contesto `--- N=100000000 P=256 ---`. Il gruppo `key_space` è opzionale (`(?:...)?`) perché non tutti gli esperimenti lo specificano.

### `RE_TAG`
```python
RE_TAG = re.compile(r'^\[(\w+)\]$')
```

Cattura i tag `[baseline]`, `[autovec]`, `[avx2]` che identificano quale implementazione segue.

### `RE_CUDA_PHASE`
```python
RE_CUDA_PHASE = re.compile(
    r'(?P<phase>H->D|Kernel|D->H|Totale)\s*:\s*(?P<time>[\d.]+)\s*ms'
)
```

Cattura le righe di timing CUDA: `H->D  : 32.456 ms`.

---

## Logica di parsing: `parse_cpu_file`

Il parser è una macchina a stati che scorre il file riga per riga:

```
stato iniziale
     │
     ├── regex EXPERIMENT -> salva nome esperimento
     ├── regex CONTEXT    -> salva N, P, key_space correnti
     ├── regex TAG        -> salva l'implementazione corrente
     ├── regex CPU_RESULT -> crea nuova riga con i campi numerici
     ├── regex DISTRIB    -> aggiunge campi distribuzione alla riga pendente
     └── regex CHECKSUM   -> aggiunge checksum alla riga pendente
```

L'idea della "riga pendente" (`pending_row`): quando incontriamo una riga di risultato, la creiamo ma non la salviamo subito — aspettiamo le righe successive che contengono distribuzione e checksum. La salviamo quando arriva il prossimo tag `[impl]` o la fine del file.

### Perché `current_N` / `current_P` / `current_ks` come stato

La riga `--- N=100000000 P=256 ---` appare una volta, poi seguono i risultati di 3 implementazioni diverse (baseline, autovec, avx2) che non ripetono N e P. Dobbiamo "ricordare" il contesto.

---

## Logica di parsing: `parse_cuda_file`

Simile ma più semplice: ogni blocco CUDA ha 4 righe di timing (H->D, Kernel, D->H, Totale) e 2 righe di throughput. Li accumuliamo in un dizionario `pending` e lo salviamo quando incontriamo il prossimo contesto.

---

## Output: formato CSV

### `cpu_results.csv`
```
experiment,impl,N,P,key_space,median_ms,stddev_ms,throughput_Mkeys_s,dist_min,dist_max,dist_ratio,checksum
Experiment 1: Sweep N (P=256),baseline,1000000,256,0,0.28,0.006,3570.4,3764,4070,1.0419,14802eb66a61103f
```

### `cuda_results.csv`
```
experiment,N,P,h2d_ms,kernel_ms,d2h_ms,total_ms,tput_kernel_Mkeys_s,tput_e2e_Mkeys_s,checksum
```

I CSV usano `csv.DictWriter` per garantire l'ordine delle colonne e l'escape corretto dei valori.

---

## Ricerca automatica dei file

Senza argomenti, lo script cerca in `results/` tutti i `.txt` e `.out`:
- File con "cuda" nel nome -> parsati come CUDA
- File con "bench" o "cpu" nel nome -> parsati come CPU

Con argomenti espliciti:
```bash
python3 analysis/parse_results.py results/bench_cpu.txt results/bench_cuda.txt
```
La distinzione CPU/CUDA avviene anche qui dal nome del file.
