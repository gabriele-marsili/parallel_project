# Guida alla stesura del Report — Modulo 1 SPM

> **Obiettivo**: scrivere un PDF di massimo **4 pagine** che presenti le scelte
> progettuali, le evidenze di vettorizzazione, la strategia intrinsics,
> i risultati sperimentali e — soprattutto — la discussione su **cosa limita le
> prestazioni** (compute vs memory vs overhead).

---

## 0. Struttura consigliata del report

```
1. Introduzione e scelte progettuali        (~0.7 pagine)
2. Evidence di auto-vettorizzazione GCC      (~0.6 pagine)
3. Strategia AVX2 intrinsics                 (~0.7 pagine)
4. Risultati sperimentali e analisi          (~1.2 pagine)
5. CUDA (opzionale)                          (~0.5 pagine)
6. Conclusioni                               (~0.3 pagine)
```

Sotto, sezione per sezione, trovi **cosa scrivere**, **quali dati usare**,
e soprattutto **quali concetti delle lezioni collegare** con riferimenti
precisi alle slide.

---

## 1. Introduzione e scelte progettuali

### Cosa scrivere

Descrivi il problema (mapping N chiavi uint64_t → partition id in [0,P)),
poi giustifica le tue due scelte chiave:

#### 1a. Funzione hash: Fibonacci multiply-shift

```
h(k) = (A · k) >> (64 − log₂P)      dove A = 0x9E3779B97F4A7C15
```

Motiva la scelta con tre argomenti:

1. **Una sola moltiplicazione + uno shift** → nessuna divisione, nessun
   modulo, nessun branch. È il tipo di operazione elementare che si
   vettorizza meglio (cfr. L7&8 slide 7: *"the same instruction is
   broadcasted to all ALUs"* — la nostra operazione è identica per ogni
   chiave, senza divergenza).

2. **P potenza di 2** → lo shift `(64 − log₂P)` è una costante nota a
   compile-time, quindi il compilatore può ottimizzarlo. Nessuna necessità
   di divisione o modulo.

3. **Distribuzione quasi-uniforme**: A è il golden ratio scalato
   (floor(2⁶⁴/φ)), che garantisce distribuzione quasi-universale con
   probabilità di collisione ≤ 2/P per coppia di chiavi distinte.
   I risultati sperimentali lo confermano: max/atteso ≤ 1.01 per
   N=100M (vedi grafico 06_distribution_quality).

#### 1b. Layout dati: SoA-like, stride unitario

Spiega che il loop ha accesso a stride unitario su `keys[]` (lettura) e
`part_ids[]` (scrittura), entrambi array contigui. Questo è il layout
ideale per la vettorizzazione, come indicato in L7&8 slide 30:
*"First choice: design with SoA from the start — best SIMD efficiency
and cache locality"* e slide 39: *"Prefer unit stride access in the
innermost loop"*.

#### Dati da includere

- La formula hash con la costante A
- Le dimensioni dei tipi: input uint64_t (8B), output uint32_t (4B)
- La tabella dei flag di compilazione:

| Versione  | Flag principali                                   |
|-----------|---------------------------------------------------|
| Baseline  | `-O3 -march=native -fno-tree-vectorize`           |
| Autovec   | `-O3 -march=native -mavx2 -mfma -ftree-vectorize` |
| AVX2      | `-O3 -march=native -mavx2 -mfma`                  |

---

## 2. Evidence di auto-vettorizzazione GCC

### Cosa scrivere

Questa sezione deve dimostrare che GCC ha effettivamente vettorizzato il
loop, e poi analizzare **come** lo ha fatto.

#### 2a. Report del compilatore

Includi l'excerpt del report GCC (dal file `results/vec_report_optimized.txt`):

```
src/plain.cpp:28:26: optimized: loop vectorized using 16 byte vectors
```

Spiega che:
- Il compilatore ha riconosciuto il loop come vettorizzabile
- Ha usato **vettori da 16 byte (128 bit, registri xmm)**, non da 32 byte
  (256 bit, registri ymm)

#### 2b. Perché il loop è vettorizzabile

Collega a L7&8 slide 34 (*Tips & Caveats for auto-vectorization*).
Il nostro loop soddisfa tutte le condizioni:

- ✅ **Conteggio iterazioni noto**: `for (i = 0; i < N; i++)` — il bound
  `N` è costante nel loop (slide 40: *"The loop count must be known at
  entry to the loop"*)
- ✅ **Nessuna dipendenza tra iterazioni**: `part_ids[i]` dipende solo da
  `keys[i]`, mai da iterazioni precedenti (slide 34-35: niente RAW, WAR, WAW)
- ✅ **Nessuna function call**: `HASH_A * keys[i] >> shift` è tutto inline
  (slide 40: *"No function calls"*)
- ✅ **No aliasing**: uso di `__restrict__` su entrambi i puntatori
  (slide 38: *"Use __restrict__ to tell the compiler that there is no aliasing"*)
- ✅ **Stride unitario**: accesso sequenziale a `keys[i]` e `part_ids[i]`
  (slide 39: *"Prefer unit stride access"*)

#### 2c. Perché 128 bit e non 256 bit

Punto critico. GCC 12 vettorizza la moltiplicazione 64-bit con registri
xmm (128 bit = 2 lane da 64 bit), non ymm (256 bit = 4 lane). Questo è
confermabile dal disassembly (`objdump -d bin/plain_autovec`): il loop
principale usa solo istruzioni `vpmuludq %xmm`, mai `%ymm`.

La ragione è architetturale: su AMD Zen 1 (EPYC 7551 di node09), le
operazioni AVX2 a 256 bit sui registri ymm sono internamente decomposte
dal processore in due micro-operazioni da 128 bit. GCC lo sa e preferisce
emettere direttamente codice SSE a 128 bit, evitando l'overhead del
`vzeroupper` e le penalità di transizione AVX↔SSE.

#### Dati da includere

- Snippet del report GCC (2-3 righe)
- Breve excerpt del disassembly che mostra `xmm` (2-3 righe)
- La spiegazione sopra (Zen 1 decompone ymm → 2× micro-ops)

---

## 3. Strategia AVX2 intrinsics

### Cosa scrivere

#### 3a. Il problema: AVX2 non ha mul64 nativo

AVX2 non fornisce `_mm256_mullo_epi64` (disponibile solo da AVX-512).
L'unica primitiva di moltiplicazione intera è `_mm256_mul_epu32`, che
moltiplica i 32 bit bassi di ogni lane a 64 bit producendo un risultato
a 64 bit.

Cita L7&8 slide 8: *"Not all intrinsic functions map one-to-one to a
single assembly instruction — some may be implemented using multiple
instructions"*. Questo è esattamente il nostro caso: la mul64 richiede
una decomposizione manuale.

#### 3b. La decomposizione

Spiega la formula algebrica:

```
A·k mod 2⁶⁴ = A_lo·k_lo + (A_lo·k_hi + A_hi·k_lo) << 32
```

dove `A_hi·k_hi << 64` trabocca e viene ignorato.

Elenco delle intrinsics usate per 4 chiavi in parallelo:

| Istruzione               | Operazione                    | Lane |
|--------------------------|-------------------------------|------|
| `_mm256_load_si256`      | Carica 4 chiavi (aligned)     | 4×64 |
| `_mm256_mul_epu32`  ×3   | `lo·lo`, `lo·k_hi`, `hi·k_lo`| 4×64 |
| `_mm256_srli_epi64`      | Estrai 32 bit alti            | 4×64 |
| `_mm256_add_epi64`  ×2   | Somma cross-terms + lo·lo     | 4×64 |
| `_mm256_slli_epi64`      | Shift sx cross-terms di 32    | 4×64 |
| `_mm256_srli_epi64`      | Shift dx finale (÷ partition) | 4×64 |
| `_mm256_permutevar8x32`  | Pack 4×64 → 4×32 contigui    | 8×32 |
| `_mm_storeu_si128`       | Scrivi 4 partition id         | 4×32 |

**Totale**: ~11 istruzioni SIMD per 4 chiavi = **~2.75 istruzioni/chiave**.
Lo scalare usa 1 `IMUL` per chiave.

#### 3c. Correttezza

Menziona la strategia di verifica: checksum FNV-1a sull'output +
confronto element-wise per N piccolo. Tutte le implementazioni producono
output identico (stesso checksum per ogni configurazione N, P, seed).

#### Dati da includere

- La formula della decomposizione
- La tabella delle intrinsics sopra
- Una riga di esempio dal test di correttezza (checksum match)

---

## 4. Risultati sperimentali e analisi

### Questa è la sezione più importante del report.

La traccia chiede esplicitamente: *"a discussion of what limits
performance (compute vs memory vs overhead)"*. Questa è l'occasione per
dimostrare che hai capito i concetti delle lezioni.

### 4a. Piattaforma di test

Indica brevemente:
- **Node09**: 2× AMD EPYC 7551 (64 core, 2-way SMT) @ 2 GHz, DDR4
- **GPU**: NVIDIA A30 (56 SM, 3584 CUDA core, 24 GB HBM2, 993 GB/s)
- **Partizione SLURM**: `gpu-excl` (nodo esclusivo, nessuna interferenza)
- **Metodologia**: 11 ripetizioni, mediana, deviazione standard

### 4b. Tabella riassuntiva (N=200M, P=256)

| Implementazione   | Mediana (ms) | Stddev | Throughput (Mkeys/s) | Speedup |
|-------------------|-------------|--------|---------------------|---------|
| Baseline (no-vec) | 184.4       | 5.1    | 1084                | 1.00×   |
| Auto-vectorized   | 178.9       | 5.3    | 1118                | 1.03×   |
| AVX2 intrinsics   | 189.2       | 4.7    | 1057                | 0.97×   |

Includi i grafici più significativi (scegli 2-3 tra quelli generati):
- `01_throughput_vs_N.png` — mostra che le tre curve sono quasi
  sovrapposte (~1060-1120 Mkeys/s per ogni N)
- `03_speedup_vs_N.png` — mostra chiaramente il plateau attorno a 1.0×

### 4c. Analisi: il kernel è memory-bound

**Questo è il cuore del report.** Segui il ragionamento visto in L5&6
slide 4-5 (von Neumann bottleneck, esempio dot product):

#### Passo 1: calcola l'intensità operazionale

Per ogni chiave il kernel esegue:
- **Byte trasferiti**: 8 B (lettura uint64_t) + 4 B (scrittura uint32_t) = **12 byte**
- **Operazioni**: 1 moltiplicazione + 1 shift = **2 operazioni**

Intensità operazionale: OI = 2 ops / 12 byte ≈ **0.17 ops/byte**

Confronta col dot product della lezione L5&6 slide 5:
*"2 FLOP / 16 GiB → il kernel è memory bound"*. Il nostro caso è analogo:
pochissimo compute per byte trasferito.

#### Passo 2: calcola t_comp e t_mem (come nella lezione)

Seguendo l'approccio di L5&6 slide 5:

```
Per N = 200M chiavi:

t_comp = N × (1 ciclo IMUL) / freq
       = 200×10⁶ / (2×10⁹ Hz)
       = 100 ms

t_mem  = (N × 12 byte) / BW_singolo_core
       = 2.4 GB / ~15 GB/s
       ≈ 160 ms

t_exec ≥ max(t_comp, t_mem) = 160 ms    → MEMORY BOUND
```

Il tempo osservato (~184 ms) è coerente con `t_mem` stimato,
confermando che il bottleneck è la bandwidth DRAM.

#### Passo 3: spiega perché la vettorizzazione non aiuta

Dalla lezione L7&8 slide 7: *"Performance improvement (speedup) is
roughly vector_width × efficiency"*.

La vettorizzazione riduce `t_comp` (processando 2 o 4 chiavi per ciclo),
ma `t_comp` **non è il bottleneck**. Il bottleneck è `t_mem`, che dipende
dalla bandwidth DRAM e non cambia con la vettorizzazione. Quindi:

- **Autovec** (2 chiavi/ciclo via SSE): `t_comp` si dimezza → ~50 ms,
  ma `t_exec = max(50, 160) = 160 ms` — nessun miglioramento
  (l'~3% osservato viene dal minor loop overhead, non dal parallelismo SIMD)

- **AVX2** (4 chiavi/ciclo): `t_comp` diventa ~25 ms, ma il tempo totale
  resta ~160 ms. In più, la decomposizione mul64 aggiunge istruzioni
  extra (~11 SIMD vs 1 IMUL) che consumano risorse di esecuzione e
  generano micro-ops aggiuntive, causando un leggero overhead che
  spiega il rallentamento del ~3%.

#### Passo 4: verifica con la bandwidth

Calcola la bandwidth effettivamente utilizzata:

```
BW = throughput × 12 byte/chiave
   = 1084 Mkeys/s × 12 B
   = 13.0 GB/s
```

Questo è coerente con la bandwidth accessibile da un singolo core su
EPYC 7551 (DDR4, ~15-20 GB/s per core stimati; il nostro kernel ne usa
~65-85%).

### 4d. Sweep P

Mostra che il throughput è **indipendente da P** (tutte le curve
sovrapposte nel grafico 02_throughput_vs_P). Questo è atteso: cambiare
P modifica solo il valore di `shift`, non la quantità di dati trasferiti
né il numero di operazioni. Il bottleneck (bandwidth) non dipende da P.

### 4e. Sweep key_space (distribuzione)

Mostra che il throughput è indipendente dalla distribuzione delle chiavi.
Questo conferma che la funzione hash Fibonacci ha costo costante
(mul+shift), indipendente dal valore della chiave, e che non ci sono
effetti cache-dipendenti dalla distribuzione (nessun branch nel kernel).

### 4f. Stabilità delle misurazioni

CV < 5% per tutte le configurazioni (grafico 08). Cita che l'uso della
partizione esclusiva SLURM elimina l'interferenza di altri job, come
insegnato nella lezione SLURM (L4).

---

## 5. CUDA (sezione opzionale)

### 5a. Implementazione

- 1 thread per chiave, 256 thread per blocco (multiplo del warp size 32,
  come indicato in L9 slide 23: *"often set to a multiple of 32"*)
- Accesso coalesced: il thread `idx` legge `keys[idx]` → thread
  consecutivi nello stesso warp leggono indirizzi consecutivi
  (L9 slide 28: *"Coalescing: Global memory is efficient when threads in
  a warp access contiguous and aligned addresses"*)
- Pinned memory (`cudaMallocHost`) per i buffer host → consente DMA
  diretto CPU↔GPU senza page-fault
- Timing via `cudaEvent` (separato per H→D, kernel, D→H)

### 5b. Risultati (N=100M, P=256)

| Fase           | Tempo (ms) | % totale |
|----------------|-----------|----------|
| H→D transfer   | 65.2      | 65.1%    |
| **Kernel**     | **1.49**  | **1.5%** |
| D→H transfer   | 33.5      | 33.4%    |
| **Totale**     | **100.3** | 100%     |

| Metrica                  | Valore            |
|--------------------------|-------------------|
| Throughput kernel-only   | 67,177 Mkeys/s    |
| Throughput end-to-end    | 997 Mkeys/s       |
| BW kernel (HBM2)         | 804 GB/s (81%)    |
| BW H→D (PCIe 4.0)       | 12.3 GB/s (49%)   |
| BW D→H (PCIe 4.0)       | 11.9 GB/s (48%)   |

### 5c. Analisi

Il kernel CUDA raggiunge l'81% della bandwidth teorica HBM2 (993 GB/s),
un risultato eccellente. Anche sulla GPU il kernel è memory-bound, ma
la HBM2 ha una bandwidth ~50× superiore alla DDR4.

Tuttavia end-to-end il throughput (~1000 Mkeys/s) è comparabile alla
CPU perché il **98.5% del tempo è trasferimento PCIe**. Il PCIe 4.0 x16
ha ~25 GB/s teorici; i nostri ~12 GB/s effettivi (48-49%) sono tipici
con pinned memory.

**Conclusione CUDA**: l'offload su GPU ha senso solo se i dati sono già
in memoria device (es. in una pipeline di operazioni GPU-resident) dove
il costo PCIe viene ammortizzato. Per una singola operazione di
partitioning, il round-trip PCIe annulla completamente il vantaggio
del kernel.

Collega a L9 slide 24: i dati A30 (993 GB/s HBM2, PCIe 4.0 x16).

---

## 6. Conclusioni

Riassumi in 3-4 frasi:

1. Il kernel di partition mapping ha intensità operazionale molto bassa
   (~0.17 ops/byte), il che lo rende **memory-bound** su CPU.
2. In questa condizione, la vettorizzazione SIMD (sia auto che manuale)
   non può migliorare significativamente le prestazioni, perché il
   bottleneck è la bandwidth DRAM, non il throughput computazionale.
3. Su GPU il kernel è 60× più veloce (grazie alla HBM2), ma il
   trasferimento PCIe domina il tempo end-to-end.
4. Questi risultati sono coerenti con l'analisi del von Neumann
   bottleneck presentata nel corso (L5&6 slide 5): quando l'intensità
   operazionale è bassa, le prestazioni sono limitate dalla memoria
   indipendentemente dalla potenza di calcolo disponibile.

---

## Appendice: dati e grafici disponibili

### File CSV con i dati
- `results/cpu_results.csv` — 55 righe, tutte le configurazioni CPU
- `results/cuda_results.csv` — 10 righe, tutte le configurazioni CUDA

### Grafici generati (in `results/plots/`)

| # | File | Contenuto | Consigliato per |
|---|------|-----------|-----------------|
| 01 | `01_throughput_vs_N.png` | Throughput vs N (P=256) | **Sez. 4b** |
| 02 | `02_throughput_vs_P.png` | Throughput vs P (N=100M) | **Sez. 4d** |
| 03 | `03_speedup_vs_N.png` | Speedup vs N | **Sez. 4b** |
| 04 | `04_speedup_vs_P.png` | Speedup vs P | Sez. 4d (opz.) |
| 05 | `05_time_vs_N.png` | Tempo vs N | Opzionale |
| 06 | `06_distribution_quality.png` | max/atteso per P | **Sez. 1a** |
| 07 | `07_keyspace_sensitivity.png` | Throughput per key_space | Sez. 4e (opz.) |
| 08 | `08_measurement_stability.png` | CV% per N | **Sez. 4f** |
| 09 | `09_bandwidth_utilization.png` | GB/s per N | **Sez. 4c** |
| 10 | `10_time_per_key.png` | ns/chiave per impl | Opzionale |
| 11 | `11_summary_table.png` | Tabella riepilogativa | **Sez. 4b** |
| 12 | `12_cuda_breakdown.png` | Stacked bar H2D/kern/D2H | **Sez. 5b** |
| 13 | `13_cuda_vs_cpu.png` | Confronto CUDA vs CPU | Sez. 5c (opz.) |

**Suggerimento**: nel report da 4 pagine, includi 4-5 grafici massimo.
I più importanti sono: 01 o 03 (throughput/speedup CPU), 09 (bandwidth),
12 (CUDA breakdown), e 11 (summary table).

### Report vettorizzazione GCC
- `results/vec_report_optimized.txt` — loop vettorizzati

### Assemblaggio (per l'analisi del disassembly)
Comandi per ottenere il disassembly:
```bash
objdump -d bin/plain_baseline | grep -A 20 '<_Z13partition_mapPKmPjmj>:'
objdump -d bin/plain_autovec  | grep -A 40 '<_Z13partition_mapPKmPjmj>:'
objdump -d bin/avx2           | grep -A 20 '<_Z18partition_map_avx2PKmPjmj>:'
```

---

## Riferimenti alle lezioni (riepilogo)

| Concetto | Lezione | Slide | Cosa dice |
|----------|---------|-------|-----------|
| Von Neumann bottleneck | L5&6 | 3-5 | Gap tra velocità CPU e bandwidth memoria; esempio dot product memory-bound |
| Speedup SIMD | L7&8 | 7 | *"speedup ≈ vector_width × efficiency"* |
| Intrinsics naming | L7&8 | 9 | `_mm<width>_<op>_<modifier>` |
| Aligned allocation | L7&8 | 11-12 | AVX richiede 32B alignment |
| Auto-vectorization flags | L7&8 | 32-33 | `-O3 -march=native`, `-fopt-info-vec-*`, `-fno-tree-vectorize` |
| Condizioni per vettorizzazione | L7&8 | 34, 40 | Loop count noto, no dipendenze, no function call, `__restrict__` |
| Data layout (SoA, stride) | L7&8 | 30, 39 | SoA > AoS; stride unitario per cache locality |
| CUDA kernel launch | L9 | 22-23 | `<<<blocks, threads>>>`, 256 tpb multiplo di 32 |
| A30 specs | L9 | 24 | 993 GB/s HBM2, PCIe 4.0 x16 |
| Memory coalescing | L9 | 28 | Thread consecutivi → indirizzi consecutivi → efficiente |
| GPU memory hierarchy | L9 | 10 | Global, Shared, L2, Registers |
| Pinned memory + streams | L9 | slides su cudaMemcpyAsync | Per overlap transfer/compute |
| Speedup definition | L10 | 5 | S(p) = T_seq / T_par(p) |
| Efficiency definition | L10 | 6 | E(p) = S(p) / p |

---

## Template LaTeX consigliato

```latex
\documentclass[10pt,a4paper,twocolumn]{article}
\usepackage[utf8]{inputenc}
\usepackage[T1]{fontenc}
\usepackage[english]{babel}
\usepackage{geometry}
\geometry{margin=1.8cm}
\usepackage{graphicx}
\usepackage{booktabs}
\usepackage{amsmath}
\usepackage{listings}
\usepackage{xcolor}
\usepackage{hyperref}
\usepackage{caption}
\captionsetup{font=small}

\lstset{
  basicstyle=\ttfamily\scriptsize,
  breaklines=true,
  frame=single,
  backgroundcolor=\color{gray!10}
}

\title{SPM Module 1: Vectorization of the Partition Mapping Kernel}
\author{Nome Cognome — Matricola}
\date{}

\begin{document}
\maketitle

\section{Design Choices}
% 1a. Hash function, 1b. Data layout, tabella flag

\section{Auto-Vectorization Evidence}
% 2a. GCC report, 2b. Perché vettorizzabile, 2c. 128 vs 256 bit

\section{AVX2 Intrinsics Strategy}
% 3a. Problema mul64, 3b. Decomposizione, 3c. Correttezza

\section{Performance Results and Analysis}
% 4a. Piattaforma
% 4b. Tabella riassuntiva + grafici
% 4c. Analisi memory-bound (t_comp vs t_mem, OI, bandwidth)
% 4d. Sweep P (indipendenza)
% 4e. Sweep key_space (opzionale)
% 4f. Stabilità (opzionale, se c'è spazio)

\section{CUDA Implementation (Optional)}
% 5a. Design, 5b. Breakdown table, 5c. Analisi PCIe vs HBM2

\section{Conclusions}
% 3-4 frasi di sintesi

\end{document}
```

---

## Checklist finale prima della consegna

- [ ] Il report è ≤ 4 pagine
- [ ] C'è la formula hash con la costante A e la motivazione
- [ ] C'è l'excerpt del report GCC (`loop vectorized using 16 byte vectors`)
- [ ] C'è la spiegazione della decomposizione mul64 per AVX2
- [ ] C'è la tabella riassuntiva con mediana, stddev, throughput, speedup
- [ ] C'è il calcolo di OI (ops/byte) e la classificazione memory-bound
- [ ] C'è il calcolo `t_comp` vs `t_mem` (come L5&6 slide 5)
- [ ] C'è la spiegazione di perché lo speedup SIMD è ~1.0× (memory-bound)
- [ ] C'è il breakdown CUDA (H→D / kernel / D→H) con percentuali
- [ ] C'è la conclusione che il kernel CUDA è PCIe-bound end-to-end
- [ ] Ci sono 4-5 grafici significativi (non di più)
- [ ] La verifica di correttezza è menzionata (checksum + element-wise)
- [ ] I riferimenti ai concetti delle lezioni sono presenti (anche impliciti)
