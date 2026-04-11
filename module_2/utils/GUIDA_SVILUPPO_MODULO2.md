# 🧭 Guida di Sviluppo — SPM Modulo 2
## Partitioned Hash Join with Duplicates: From Partition Mapping to Full Join

> **Obiettivo**: Implementare una versione parallela (C++ threads) del Partitioned Hash Join con duplicati, partendo dal codice sequenziale fornito (`hashjoin_seq.cpp`), e produrre un report con analisi delle performance.

> **Deadline**: 20 Aprile (soft deadline)

> **Deliverables**: codice sorgente + Makefile + README + report PDF (max 5 pagine)

---

## 📑 Indice

1. [Panoramica dell'Algoritmo](#1-panoramica-dellalgoritmo)
2. [Teoria: Concetti Chiave dalle Lezioni](#2-teoria-concetti-chiave-dalle-lezioni)
3. [Step 0: Setup dell'Ambiente](#3-step-0-setup-dellambiente)
4. [Step 1: Integrare la Funzione di Mapping del Modulo 1](#4-step-1-integrare-la-funzione-di-mapping-del-modulo-1)
5. [Step 2: Capire il Codice Sequenziale](#5-step-2-capire-il-codice-sequenziale)
6. [Step 3: Analisi delle Fasi e Opportunità di Parallelismo](#6-step-3-analisi-delle-fasi-e-opportunità-di-parallelismo)
7. [Step 4: Implementazione Parallela](#7-step-4-implementazione-parallela)
8. [Step 5: Strategia di Validazione (Correctness)](#8-step-5-strategia-di-validazione-correctness)
9. [Step 6: Performance Evaluation](#9-step-6-performance-evaluation)
10. [Step 7: Scrivere il Report](#10-step-7-scrivere-il-report)
11. [Checklist Finale](#11-checklist-finale)
12. [Appendice: Comandi Utili & Riferimenti](#12-appendice-comandi-utili--riferimenti)

---

## 1. Panoramica dell'Algoritmo (Spiegazione Approfondita)

### 1.0 Il problema: cos'è un Join e perché è costoso?

Immagina di avere due tabelle di un database:

```
Relazione R (es. "Ordini")         Relazione S (es. "Prodotti")
┌───────┐                          ┌───────┐
│ key   │                          │ key   │
├───────┤                          ├───────┤
│  3    │                          │  5    │
│  7    │                          │  3    │
│  3    │                          │  3    │
│  5    │                          │  7    │
│  2    │                          │  1    │
└───────┘                          │  3    │
                                   │  2    │
                                   │  5    │
                                   └───────┘
```

L'operazione di **join** trova tutte le coppie `(r, s)` dove `r.key == s.key`.
In SQL sarebbe: `SELECT * FROM R JOIN S ON R.key = S.key`.

**Approccio naive** — confronta ogni record di R con ogni record di S:
```
Per ogni r in R:             ← NR iterazioni
    Per ogni s in S:         ← NS iterazioni
        if r.key == s.key:
            -> match!
```
Complessità: **O(NR × NS)**. Con 10 milioni di record per tabella -> 10¹⁴ confronti. Inaccettabile.

**Approccio hash join classico** — costruisci una hash table su R, poi probi con S:
```
1. Costruisci hash_table da R (key -> lista di record)     O(NR)
2. Per ogni s in S: cerca s.key nella hash_table           O(NS) in media
```
Complessità: **O(NR + NS)**. Molto meglio! Ma c'è un problema: se R e S sono enormi
(decine di milioni di record), la hash table potrebbe non entrare in cache, e ogni lookup
produce cache miss -> lento nella pratica.

### 1.1 L'idea del Partitioned Hash Join: "dividi e conquista"

La soluzione è **partizionare** prima i dati, in modo che ogni join locale lavori su un pezzo
piccolo che **sta in cache**:

```
IDEA CENTRALE:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Se applico la STESSA funzione hash h(key) sia a R che a S,
allora tutti i record con la stessa chiave finiranno nella
STESSA partizione.

Questo significa che posso fare il join SEPARATAMENTE su ogni
partizione, perché un record in R_partizione_0 non potrà MAI
matchare un record in S_partizione_1 (hanno hash diverse!).
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

Ecco il concetto visivamente:

```
PRIMA del partizionamento:          DOPO il partizionamento:

R = [3, 7, 3, 5, 2]                 R_part0 = [2]       (chiavi con h(k)=0)
S = [5, 3, 3, 7, 1, 3, 2, 5]       R_part1 = [3, 3, 5] (chiavi con h(k)=1)
                                     R_part2 = [7]       (chiavi con h(k)=2)
(le chiavi sono mescolate,
 non c'è ordine)                     S_part0 = [1, 2]
                                     S_part1 = [5, 3, 3, 3, 5]
                                     S_part2 = [7]

                                     Ora posso fare il join su ogni
                                     coppia (R_partX, S_partX) in modo
                                     INDIPENDENTE. Ogni pezzo è piccolo
                                     -> hash table locale sta in cache!
```

### 1.2 Collegamento con il Modulo 1: la funzione di mapping

Qui entra in gioco il **Modulo 1**. Nel Modulo 1 hai implementato e ottimizzato una funzione
che, data una chiave a 64 bit e un numero P di partizioni, restituisce un **partition id** in `[0, P)`:

```
h(key) -> partition_id ∈ {0, 1, 2, ..., P-1}
```

La tua implementazione dal Modulo 1 usa **XOR-fold + Fibonacci multiply-shift a 32 bit**:

```cpp
// 1. XOR-fold: comprimi i 64 bit della chiave in 32 bit
uint32_t k_lo = (uint32_t)(key);          // metà bassa
uint32_t k_hi = (uint32_t)(key >> 32);    // metà alta
uint32_t mixed = k_lo ^ k_hi;             // le "mescola" con XOR

// 2. Fibonacci multiply-shift: moltiplica per la costante aurea e shifta
partition_id = (mixed * 0x9E3779B9u) >> shift;
//              ↑ costante ≈ 2³²/φ           ↑ shift = 32 - log₂(P)
```

**Perché questa funzione è buona?**
- **Distribuzione uniforme**: le chiavi si distribuiscono equamente tra le P partizioni
  (nel Modulo 1 hai verificato che max/atteso ≤ 1.005)
- **Veloce**: solo XOR + una moltiplicazione a 32 bit + uno shift — niente divisioni
- **Vettorizzabile**: nel Modulo 1 hai visto che GCC può auto-vettorizzarla con AVX2

**La connessione tra i due moduli è questa**: nel Modulo 1 hai costruito e benchmarkato
questa funzione da sola. Nel Modulo 2, questa funzione diventa il "mattoncino" usato
**dentro** un algoritmo più grande (il partitioned hash join). Viene chiamata una volta
per ogni record di R e una volta per ogni record di S — quindi viene eseguita NR + NS volte.

Il codice fornito dal professore usa una hash volutamente semplicistica (`key & (P-1)`).
Tu devi **sostituirla** con la tua, mantenendola identica tra versione sequenziale e parallela.

### 1.3 Le fasi dell'algoritmo: visione d'insieme

```
┌──────────────────────────────────────────────────────────────────────┐
│                        PARTITIONED HASH JOIN                         │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  MACRO-FASE A: PARTIZIONAMENTO  (applicata sia a R che S)  │     │
│  │                                                             │     │
│  │   Input:  array disordinato di record                       │     │
│  │   Output: array riordinato dove i record della stessa       │     │
│  │           partizione sono CONTIGUI in memoria               │     │
│  │                                                             │     │
│  │   Sub-step:                                                 │     │
│  │   ┌─────────────┐   ┌──────────────┐   ┌─────────────┐    │     │
│  │   │ 1. Histogram │──▶│ 2. Prefix Sum│──▶│ 3. Scatter  │    │     │
│  │   │  (conteggi)  │   │  (offset)    │   │ (riordino)  │    │     │
│  │   └─────────────┘   └──────────────┘   └─────────────┘    │     │
│  └─────────────────────────────────────────────────────────────┘     │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  MACRO-FASE B: JOIN LOCALE (per ogni partizione p = 0..P-1)│     │
│  │                                                             │     │
│  │   Input:  R_p e S_p (porzioni contigue di R e S)            │     │
│  │   Output: join_count parziale + checksum parziali           │     │
│  │                                                             │     │
│  │   Sub-step:                                                 │     │
│  │   ┌───────────────┐   ┌───────────────┐                    │     │
│  │   │ 4. Build      │──▶│ 5. Probe      │                    │     │
│  │   │ (hash table   │   │ (cerca match  │                    │     │
│  │   │  su R_p)      │   │  da S_p)      │                    │     │
│  │   └───────────────┘   └───────────────┘                    │     │
│  └─────────────────────────────────────────────────────────────┘     │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │  MACRO-FASE C: ACCUMULAZIONE                                │     │
│  │   Somma i risultati parziali di tutte le partizioni         │     │
│  └─────────────────────────────────────────────────────────────┘     │
└──────────────────────────────────────────────────────────────────────┘
```

Vediamo ora ogni sub-step in dettaglio, tracciando un **esempio numerico completo**.

---

### 1.4 Esempio numerico completo (passo per passo)

Usiamo dati piccoli per tracciare tutto a mano:

```
Parametri:
  P = 4 partizioni
  Funzione hash: h(key) = key % 4  (semplificata per leggibilità)

Relazione R (NR = 8):
  indice:  0   1   2   3   4   5   6   7
  chiave: [5,  2,  8,  3,  2,  5,  7,  4]

Relazione S (NS = 6):
  indice:  0   1   2   3   4   5
  chiave: [3,  5,  2,  8,  5,  7]
```

#### STEP 1 — Mapping (key -> partition_id)

Applichiamo `h(key) = key % 4` a ogni record:

```
Relazione R:
  chiave:       5    2    8    3    2    5    7    4
  partition_id: 1    2    0    3    2    1    3    0
                ↑    ↑    ↑    ↑    ↑    ↑    ↑    ↑
              5%4  2%4  8%4  3%4  2%4  5%4  7%4  4%4

Relazione S:
  chiave:       3    5    2    8    5    7
  partition_id: 3    1    2    0    1    3
```

Il mapping **non sposta nulla**: associa solo un numero di partizione ad ogni record.

#### STEP 2 — Histogram (contare quanti record per partizione)

Scorriamo tutti i partition_id e contiamo:

```
Histogram di R:                         Histogram di S:
  part 0: 2 record  (key 8, key 4)       part 0: 1 record  (key 8)
  part 1: 2 record  (key 5, key 5)       part 1: 2 record  (key 5, key 5)
  part 2: 2 record  (key 2, key 2)       part 2: 1 record  (key 2)
  part 3: 2 record  (key 3, key 7)       part 3: 2 record  (key 3, key 7)

  hist_R = [2, 2, 2, 2]                  hist_S = [1, 2, 1, 2]
```

**A cosa serve?** Sapere quanti record ha ogni partizione ci permette di **pre-allocare
lo spazio** nell'array di output. Senza l'istogramma, non sapremmo dove inizia e dove
finisce ogni partizione nell'array riordinato.

#### STEP 3 — Prefix Sum (calcolare gli offset di inizio di ogni partizione)

L'exclusive prefix sum converte i conteggi in **posizioni di inizio**:

```
Prefix sum di hist_R = [2, 2, 2, 2]:

  begin[0] = 0                          (la partizione 0 inizia dalla posizione 0)
  begin[1] = 0 + 2 = 2                  (la partizione 1 inizia dopo i 2 record della part. 0)
  begin[2] = 2 + 2 = 4                  (la partizione 2 inizia dopo i 2+2 record)
  begin[3] = 4 + 2 = 6                  (la partizione 3 inizia dopo i 2+2+2 record)

  begin_R = [0, 2, 4, 6]
  end_R   = [2, 4, 6, 8]   (end[i] = begin[i] + hist[i])

  Mappa delle posizioni nell'array output di R:
  ┌─────────────────────────────────────────────┐
  │ posizione: 0  1 │ 2  3 │ 4  5 │ 6  7       │
  │          part_0  │part_1│part_2│part_3       │
  └─────────────────────────────────────────────┘

Prefix sum di hist_S = [1, 2, 1, 2]:

  begin_S = [0, 1, 3, 4]
  end_S   = [1, 3, 4, 6]

  Mappa delle posizioni nell'array output di S:
  ┌────────────────────────────────────────┐
  │ posizione: 0 │ 1  2 │ 3 │ 4  5        │
  │          pt_0│ pt_1  │p_2│ pt_3        │
  └────────────────────────────────────────┘
```

**A cosa serve?** Ora sappiamo che nella versione riordinata di R, la partizione 0
occupa le posizioni [0,2), la partizione 1 le posizioni [2,4), ecc. Queste posizioni
ci dicono **dove scrivere** durante lo scatter.

#### STEP 4 — Scatter (riordinare i record in posizioni contigue)

Ora scorriamo l'array originale di R e **copiamo** ogni record nella posizione corretta
dell'array di output, usando un cursore per partizione:

```
Cursori iniziali (= copia di begin_R):  next = [0, 2, 4, 6]

Scorro R originale:
  R[0] key=5, pid=1 -> scrivi in out[next[1]] = out[2], poi next[1]++ -> next=[0, 3, 4, 6]
  R[1] key=2, pid=2 -> scrivi in out[next[2]] = out[4], poi next[2]++ -> next=[0, 3, 5, 6]
  R[2] key=8, pid=0 -> scrivi in out[next[0]] = out[0], poi next[0]++ -> next=[1, 3, 5, 6]
  R[3] key=3, pid=3 -> scrivi in out[next[3]] = out[6], poi next[3]++ -> next=[1, 3, 5, 7]
  R[4] key=2, pid=2 -> scrivi in out[next[2]] = out[5], poi next[2]++ -> next=[1, 3, 6, 7]
  R[5] key=5, pid=1 -> scrivi in out[next[1]] = out[3], poi next[1]++ -> next=[1, 4, 6, 7]
  R[6] key=7, pid=3 -> scrivi in out[next[3]] = out[7], poi next[3]++ -> next=[1, 4, 6, 8]
  R[7] key=4, pid=0 -> scrivi in out[next[0]] = out[1], poi next[0]++ -> next=[2, 4, 6, 8]

Array R riordinato:
  posizione:  0    1  │  2    3  │  4    5  │  6    7
  chiave:    [8,   4] │ [5,   5] │ [2,   2] │ [3,   7]
              ╰part_0╯  ╰part_1╯   ╰part_2╯   ╰part_3╯
```

**Nota importante**: all'interno di una partizione l'ordine dei record NON è definito
(dipende dall'ordine in cui li incontriamo nello scan). Ma non importa, perché il join
deve solo contare i match, non preservare un ordine.

Analogamente per S:

```
Array S riordinato:
  posizione:  0  │  1    2  │  3  │  4    5
  chiave:    [8] │ [5,   5] │ [2] │ [3,   7]
             pt_0   pt_1     pt_2   pt_3
```

#### STEP 5 — Build (costruire la hash table locale su ogni R_p)

Per ogni partizione, scansioniamo la porzione di R e contiamo quante volte appare ogni chiave:

```
Partizione 0:  R_0 = [8, 4]
  countR = {8: 1, 4: 1}

Partizione 1:  R_1 = [5, 5]
  countR = {5: 2}           ← la chiave 5 appare 2 volte!

Partizione 2:  R_2 = [2, 2]
  countR = {2: 2}           ← la chiave 2 appare 2 volte!

Partizione 3:  R_3 = [3, 7]
  countR = {3: 1, 7: 1}
```

**A cosa serve countR?** Gestisce i **duplicati**. Se la chiave 5 appare 2 volte in R,
allora OGNI occorrenza di 5 in S deve contare come 2 match (uno per ogni copia in R).

#### STEP 6 — Probe (cercare match da S_p nella hash table di R_p)

Per ogni partizione, scansioniamo la porzione di S e per ogni chiave cerchiamo in countR:

```
Partizione 0:  S_0 = [8],  countR = {8: 1, 4: 1}
  key=8 -> trovata! multiplicity=1 -> join_count += 1

Partizione 1:  S_1 = [5, 5],  countR = {5: 2}
  key=5 -> trovata! multiplicity=2 -> join_count += 2
  key=5 -> trovata! multiplicity=2 -> join_count += 2
                                     ─────────────
                                     subtotale = 4
  (Spiegazione: 5 appare 2 volte in R e 2 volte in S -> 2×2 = 4 match)

Partizione 2:  S_2 = [2],  countR = {2: 2}
  key=2 -> trovata! multiplicity=2 -> join_count += 2

Partizione 3:  S_3 = [3, 7],  countR = {3: 1, 7: 1}
  key=3 -> trovata! multiplicity=1 -> join_count += 1
  key=7 -> trovata! multiplicity=1 -> join_count += 1
```

#### STEP 7 — Accumulazione

```
Totale join_count = 1 + 4 + 2 + 1 + 1 = 9
```

**Verifica con il metodo naive (tutti i confronti)**:
```
R = [5, 2, 8, 3, 2, 5, 7, 4]
S = [3, 5, 2, 8, 5, 7]

Coppie matchanti (r.key == s.key):
  R[0]=5  ↔ S[1]=5 ✓     R[3]=3 ↔ S[0]=3 ✓     R[6]=7 ↔ S[5]=7 ✓
  R[0]=5  ↔ S[4]=5 ✓     R[4]=2 ↔ S[2]=2 ✓
  R[1]=2  ↔ S[2]=2 ✓     R[5]=5 ↔ S[1]=5 ✓
  R[2]=8  ↔ S[3]=8 ✓     R[5]=5 ↔ S[4]=5 ✓

Totale = 9 ✓  (corrisponde!)
```

### 1.5 Perché partizionare è vantaggioso (la ragione profonda)

A prima vista, il partizionamento sembra **lavoro extra**: devi scansionare tutti i dati
due volte (histogram + scatter) prima ancora di iniziare il join. Perché conviene?

**Motivo 1 — Località di cache**:
```
Senza partizionamento:
  Hash table di R = NR entry -> può essere ENORME
  Ogni lookup durante il probe può causare un cache miss
  (la hash table non sta in L1/L2 cache)

Con partizionamento in P partizioni:
  Hash table di R_p ≈ NR/P entry -> PICCOLA
  Se scelgo P abbastanza grande, la hash table locale sta in L1/L2 cache
  Ogni lookup è un cache hit -> ordini di grandezza più veloce
```

**Motivo 2 — Indipendenza tra partizioni**:
```
Dopo il partizionamento, le P partizioni sono COMPLETAMENTE indipendenti.
  -> Non condividono dati
  -> Non servono lock/sincronizzazione
  -> Ogni partizione può essere processata da un thread diverso
  -> PARALLELISMO NATURALE (embarrassingly parallel)
```

**Motivo 3 — Gestione della memoria**:
```
Senza partizionamento: una hash table gigante, possibili resize, overhead allocatore
Con partizionamento: tante hash table piccole, dimensione prevedibile, meno overhead
```

Il costo del partizionamento (histogram + prefix sum + scatter) è **O(N)** per ogni relazione.
Il beneficio è che la fase di join passa da accessi random su una struttura grande a accessi
locali su strutture piccole. Per dataset grandi, il guadagno di cache supera di gran lunga il
costo del partizionamento.

### 1.6 La gestione dei duplicati: perché "with Duplicates"?

In questo progetto le chiavi possono ripetersi (duplicati). Questo cambia il conteggio dei match.

```
Se una chiave k appare:
  m volte in R   (m copie)
  n volte in S   (n copie)

Allora produce m × n match.

Esempio: key=5 appare 2 volte in R e 2 volte in S
  Match: (R[0],S[1]), (R[0],S[4]), (R[5],S[1]), (R[5],S[4])
  Totale: 2 × 2 = 4 match
```

L'algoritmo gestisce questo nella fase di **Build + Probe**:
- **Build**: non inserisce i record in una lista, ma **conta** le occorrenze (`countR[key]++`)
- **Probe**: quando trova un match, non aggiunge 1 ma aggiunge `countR[key]` (la molteplicità)

In questo modo, ogni singolo record di S che ha chiave k contribuisce `countR[k]` match,
il che è esattamente la semantica corretta del join con duplicati.

### 1.7 Checksum: come si verifica la correttezza senza materializzare le coppie

L'algoritmo non produce la lista delle coppie (sarebbe enorme). Verifica la correttezza
con due **checksum** calcolati in modo deterministico:

```cpp
// Per ogni match trovato (key k di S_p che matcha con multiplicity m in R_p):
result.checksum1 += splitmix64(k) * m;
result.checksum2 += splitmix64(k ^ 0x9e3779b97f4a7c15ULL) * m;
```

- `splitmix64` è una funzione di mixing (hash deterministica): dato lo stesso input, produce
  sempre lo stesso output
- Ogni match contribuisce al checksum in funzione della chiave, non della posizione
- Due checksum indipendenti (con diversi salt) riducono la probabilità di collisioni accidentali

Se la versione parallela produce gli stessi `join_count`, `checksum1`, `checksum2` della
versione sequenziale -> la correttezza è verificata (con altissima probabilità).

### 1.8 Mappa concettuale: collegamento tra Modulo 1 e Modulo 2

```
╔══════════════════════════════════════════════════════════════════════╗
║                         MODULO 1                                    ║
║   Hai implementato e ottimizzato UNA funzione:                      ║
║                                                                      ║
║     compute_partition_id(key, P) -> partition_id ∈ [0, P)            ║
║                                                                      ║
║   - Hash: XOR-fold + Fibonacci multiply-shift                       ║
║   - Varianti: plain (scalare), autovec (GCC), AVX2 (intrinsics)    ║
║   - Benchmark: throughput in Mkeys/s, distribuzione tra partizioni  ║
╚══════════════════════════╦═══════════════════════════════════════════╝
                           ║
                    la funzione h(key)
                    viene USATA qui
                           ║
                           ▼
╔══════════════════════════════════════════════════════════════════════╗
║                         MODULO 2                                    ║
║   Usi quella funzione DENTRO un algoritmo completo:                 ║
║                                                                      ║
║   1. PARTIZIONAMENTO (chiama h(key) per OGNI record di R e S)      ║
║      │  • Histogram: h(key) per contare                             ║
║      │  • Scatter:   h(key) per riordinare                          ║
║      │  -> h(key) viene chiamata 2×(NR + NS) volte in totale        ║
║      │                                                               ║
║   2. JOIN LOCALE (non usa più h(key), lavora sulle partizioni)      ║
║      │  • Build: conta chiavi in R_p con hash table locale          ║
║      │  • Probe: cerca chiavi di S_p nella hash table di R_p        ║
║      │                                                               ║
║   3. PARALLELIZZAZIONE con C++ threads                              ║
║      │  • Scegli QUALI fasi parallelizzare                          ║
║      │  • Implementa con std::thread, evita race condition          ║
║      │  • Misura speedup/efficienza/scalabilità                     ║
║      │                                                               ║
║   OUTPUT: join_count + checksum1 + checksum2                        ║
║   REPORT: strategie + grafici + discussione (max 5 pagine)         ║
╚══════════════════════════════════════════════════════════════════════╝
```

### 1.9 Pipeline completa: riassunto schematico

```
┌───────────────────────────────────────────────────────────────────┐
│  INPUT: Relazione R (NR record) e Relazione S (NS record)        │
│         Ogni record ha una chiave a 64 bit. Le chiavi possono    │
│         ripetersi (duplicati). P = numero di partizioni.         │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  MACRO-FASE A: Partizionamento di R                               │
│  (produce un array dove i record sono raggruppati per partizione) │
│                                                                   │
│    A.1  Histogram:   scansiona R, conta quanti record per        │
│                      partizione -> hist_R[P]                       │
│                                                                   │
│    A.2  Prefix Sum:  trasforma i conteggi in posizioni di inizio │
│                      -> begin_R[P]                                 │
│                                                                   │
│    A.3  Scatter:     riordina R nell'array di output,            │
│                      piazzando ogni record nella posizione        │
│                      corretta usando i cursori                    │
│                                                                   │
│  MACRO-FASE A': Partizionamento di S (identico)                   │
│    -> hist_S, begin_S, array S riordinato                          │
│                                                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  MACRO-FASE B: Join locale, per ogni partizione p = 0..P-1       │
│  (ogni partizione è INDIPENDENTE dalle altre)                     │
│                                                                   │
│    B.1  Build:  scansiona R_p, conta occorrenze di ogni key      │
│                 -> countR = {key: molteplicità}                    │
│                                                                   │
│    B.2  Probe:  scansiona S_p, per ogni record con chiave k:     │
│                 se k ∈ countR -> join_count += countR[k]           │
│                                 checksum1 += hash(k) * countR[k] │
│                                 checksum2 += hash'(k)* countR[k] │
│                                                                   │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  MACRO-FASE C: Accumulazione                                      │
│    Somma i join_count e i checksum di tutte le P partizioni       │
│                                                                   │
├───────────────────────────────────────────────────────────────────┤
│  OUTPUT: join_count, checksum1, checksum2                         │
└───────────────────────────────────────────────────────────────────┘
```

---

## 2. Teoria: Concetti Chiave dalle Lezioni

### 2.1 Metriche di Performance (Lezione 10 - Metrics_and_Laws)

**Speedup** — misura quanto è più veloce la versione parallela:
```
S(p) = T_seq / T_par(p)
```
- Lo speedup **ideale** è lineare: `S(p) = p`
- In pratica è sub-lineare a causa degli overhead di parallelizzazione

**Efficienza** — misura quanto bene si utilizzano le risorse:
```
E(p) = S(p) / p = T_seq / (T_par(p) × p)
```
- Valore ideale: `E(p) = 1` (100%)
- Se `E(p) < 1`, c'è overhead (comunicazione, sincronizzazione, idle time)

**Scalabilità (Relative Speedup)** — come scala il programma parallelo:
```
Scalability(p) = T_par(1) / T_par(p)
```
- Differenza col speedup: il baseline è la versione parallela con 1 thread, non il sequenziale

### 2.2 Strong vs Weak Scaling (Lezione 10)

**Strong Scaling**: la dimensione del problema è **fissa**, si aumentano i processori.
- Ideale: `T(p) = T(1)/p`
- Limitato dalla **Legge di Amdahl**: se una frazione `f` del codice è seriale:
```
S(p) ≤ 1 / (f + (1-f)/p)

Per p -> ∞:  S_max = 1/f
```
- **Messaggio chiave**: anche il 5% di codice seriale limita lo speedup massimo a 20x

**Weak Scaling**: la dimensione del problema cresce proporzionalmente a `p`.
- Ideale: `T(PS(1), 1) = T(PS(p), p)` (tempo costante)
- Regolata dalla **Legge di Gustafson**: `S(p) ≤ f + p(1-f)`
- **Weak Scaling Efficiency**: `WSE(p) = T(PS(1),1) / T(PS(p),p)`

### 2.3 Work-Span Model (Lezione 15 - ModelsOfComputation)

Un programma parallelo può essere rappresentato come un **DAG** (Directed Acyclic Graph):
- `T₁` = **work** totale (somma del costo di tutti i nodi)
- `T∞` = **span** (cammino critico, la catena di dipendenze più lunga)

**Lower bounds**:
```
T_p ≥ T₁/p          (work bound: non si può andare più veloci del lavoro diviso i processori)
T_p ≥ T∞             (span bound: non si può andare più veloci del cammino critico)
```

**Teorema di Brent** (upper bound):
```
T_p ≤ (T₁ - T∞)/p + T∞
```

**Parallelismo disponibile**: `T₁/T∞` — indica il massimo speedup teorico.

> **Applicazione al progetto**: ogni fase dell'algoritmo ha un suo `T₁` e `T∞`. Analizzare il rapporto `T₁/T∞` per ogni fase ti dice dove il parallelismo è conveniente.

### 2.4 Tipi di Parallelismo (Lezione 11 - TypesOfParallelism)

Per questo progetto si usa **Data Parallelism**:
- La stessa operazione viene applicata a partizioni diverse dei dati
- Pattern **Map**: il partizionamento delle relazioni (ogni thread gestisce un blocco di record)
- Pattern **Reduce**: l'accumulazione dei risultati parziali (somma di join_count, checksum)

Le fasi di join locale per partizione sono **embarrassingly parallel**: nessuna dipendenza tra partizioni diverse.

### 2.5 Data Partitioning e Workload Balancing (Lezione 14 - WorkloadBalancing)

**Strategie statiche di distribuzione**:
- **Block**: ogni thread riceve un blocco contiguo. `thread_i` gestisce `[i*block_size, (i+1)*block_size)`
- **Cyclic**: il record `t_i` va al thread `i mod p`
- **Block-Cyclic**: combina i due approcci, chunk di dimensione `c`, assegnati ciclicamente

**Quando usare distribuzione statica vs dinamica**:
- **Statica** (block/cyclic): quando il workload è regolare (es: histogram, prefix sum, scatter)
- **Dinamica** (on-demand/work-stealing): quando il workload è irregolare (es: join locale, dove partizioni diverse possono avere dimensioni molto diverse)

> **Per il progetto**: le fasi di partizionamento (histogram, scatter) hanno workload regolare -> block distribution. La fase di join locale potrebbe avere workload irregolare (partizioni di dimensioni diverse) -> valutare distribuzione dinamica o block con granularità fine.

### 2.6 C++ Threads (Lezione 13 - C++ConcurrencyBasics)

**Spawn e Join di thread**:
```cpp
#include <thread>
#include <vector>

std::vector<std::thread> threads;
for (int i = 0; i < nthreads; ++i) {
    threads.emplace_back([i, &shared_data]() {
        // lavoro del thread i
    });
}
for (auto& t : threads) t.join();
```

**Regole fondamentali**:
- Ogni `std::thread` deve essere **joined** o **detached** prima della distruzione
- `std::thread` è **move-only** (non copiabile)
- Evitare **oversubscription**: numero di thread ≈ numero di core

**Passaggio parametri**:
- Per **valore**: copia sicura ma potenzialmente costosa
- Per **riferimento** (`std::ref`): zero-copy ma attenzione alla lifetime del dato
- La memoria referenziata deve **persistere** per tutta la durata del thread

**Mutex e sincronizzazione**:
```cpp
std::mutex mtx;

// lock_guard: RAII, non rilasciabile manualmente
{
    std::lock_guard<std::mutex> lock(mtx);
    // sezione critica
}

// unique_lock: più flessibile, usabile con condition variables
{
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&]() { return condizione; });
}
```

**Compilazione**:
```bash
g++ -O3 -std=c++20 file.cpp -o output -pthread
```

### 2.7 Shared Memory e Cache (Lezione 5&6 - Shared-Memory)

**False Sharing**: quando thread diversi scrivono su variabili che risiedono nella stessa **cache line** (tipicamente 64 byte). Il sistema di coerenza della cache invalida continuamente le linee, causando rallentamenti enormi.

**Come evitarlo**:
```cpp
// Male: contatori adiacenti in memoria
uint64_t counts[nthreads]; // i thread scrivono su counts[tid] -> false sharing!

// Bene: padding per separare le cache lines
struct alignas(64) PaddedCounter { uint64_t value = 0; };
PaddedCounter counts[nthreads];
```

**Locality**: l'accesso sequenziale ai dati (stride unitario) è fondamentale per sfruttare le cache. Lo scatter dei record, ad esempio, ha accesso random e stresserà la cache.

---

## 3. Step 0: Setup dell'Ambiente

### 3.1 Struttura del progetto

```bash
cd /Users/piccoletto/Desktop/Everything/pisa/corsi/magistrale/primo_anno/SPM/project_and_midterms/module_2

# Crea la struttura
mkdir -p src include results

# Il file fornito
# hashjoin_seq.cpp   ← codice sequenziale di riferimento
```

Struttura consigliata:
```
module_2/
├── Makefile
├── README.md
├── include/
│   └── common.hpp           ← tipi, hash, utilità dal Modulo 1
├── src/
│   ├── hashjoin_seq.cpp      ← sequenziale (con la TUA hash)
│   └── hashjoin_par.cpp      ← versione parallela
├── results/
│   └── ...                   ← output dei benchmark
└── report/
    └── report.pdf            ← report finale (max 5 pagine)
```

### 3.2 Makefile di base

```makefile
CXX       = g++
CXXFLAGS  = -O3 -std=c++20 -Wall -Wextra -pthread
INCLUDES  = -Iinclude

all: hashjoin_seq hashjoin_par

hashjoin_seq: src/hashjoin_seq.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

hashjoin_par: src/hashjoin_par.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@

clean:
	rm -f hashjoin_seq hashjoin_par

.PHONY: all clean
```

### 3.3 Compilazione e test iniziale

```bash
# Compila il sequenziale (prima senza modifiche per verificare che funzioni)
g++ -O3 -std=c++20 -Wall hashjoin_seq.cpp -o hashjoin_seq -pthread

# Test con input piccolo
./hashjoin_seq -nr 5 -ns 8 -seed 13 -max-key 8 -p 4

# Test con input più grande
./hashjoin_seq -nr 1000000 -ns 2000000 -seed 42 -max-key 100000 -p 64
```

---

## 4. Step 1: Integrare la Funzione di Mapping del Modulo 1

Il codice fornito usa una hash **volutamente semplicistica** (`key & (P-1)`). Devi sostituirla con la tua implementazione dal Modulo 1.

### 4.1 La tua hash dal Modulo 1

Dal tuo `common.hpp` del Modulo 1, la tua funzione hash è:

```cpp
// XOR-fold + Fibonacci multiply-shift a 32 bit
// h(k) = ((k_lo ^ k_hi) * A32) >> shift
static constexpr uint32_t HASH_A32 = 0x9E3779B9u; // floor(2^32 / φ)

inline uint32_t hash_key(uint64_t key, unsigned shift) {
    uint32_t k_lo = static_cast<uint32_t>(key);
    uint32_t k_hi = static_cast<uint32_t>(key >> 32);
    return static_cast<uint32_t>(((k_lo ^ k_hi) * HASH_A32) >> shift);
}

inline unsigned compute_shift(uint32_t P) {
    return 32 - __builtin_ctz(P);  // 32 - log2(P)
}
```

### 4.2 Sostituzione nel codice sequenziale

Modifica `compute_partition_id` in `hashjoin_seq.cpp`:

```cpp
// PRIMA (default del professore):
static inline std::uint32_t compute_partition_id(std::uint64_t key, std::uint32_t p) {
    return static_cast<std::uint32_t>(key & static_cast<std::uint64_t>(p - 1U));
}

// DOPO (la tua dal Modulo 1):
static constexpr std::uint32_t HASH_A32 = 0x9E3779B9u;

static inline std::uint32_t compute_partition_id(std::uint64_t key, std::uint32_t p) {
    // Pre-calcola shift una volta sola nel main e passalo come parametro,
    // oppure calcolalo inline (costa poco):
    unsigned shift = 32 - __builtin_ctz(p);
    std::uint32_t k_lo = static_cast<std::uint32_t>(key);
    std::uint32_t k_hi = static_cast<std::uint32_t>(key >> 32);
    return static_cast<std::uint32_t>(((k_lo ^ k_hi) * HASH_A32) >> shift);
}
```

> ⚠️ **Ottimizzazione**: il `shift` dipende solo da `P` ed è costante durante tutta l'esecuzione. Conviene pre-calcolarlo e passarlo come parametro (o usare una variabile globale/di classe) per evitare il costo di `__builtin_ctz` ad ogni chiamata.

### 4.3 Verifica post-sostituzione

```bash
# Ricompila e riesegui con input piccolo
g++ -O3 -std=c++20 hashjoin_seq.cpp -o hashjoin_seq -pthread
./hashjoin_seq -nr 10 -ns 10 -seed 42 -max-key 8 -p 4

# Verifica che naive_join_count == join_count (per input piccoli)
# I checksum devono corrispondere
```

> **IMPORTANTE**: la stessa funzione di mapping deve essere usata **identicamente** sia nella versione sequenziale che parallela.

---

## 5. Step 2: Capire il Codice Sequenziale

Prima di parallelizzare, devi capire ogni riga del codice. Ecco un walkthrough dettagliato.

### 5.1 Generazione dei dati

```cpp
const auto R = generate_relation(NR, seed, max_key);
const auto S = generate_relation(NS, seed ^ 0xdeadebdecdeedef1ULL, max_key);
```
- R e S usano **seed diverse** (XOR con costante) -> non sono identiche
- Le chiavi sono generate con `splitmix64` e ridotte `mod max_key`
- `max_key` controlla il range delle chiavi -> più è piccolo rispetto a NR/NS, più duplicati ci sono

### 5.2 Histogram

```cpp
static std::vector<std::size_t> compute_histogram(const std::vector<Record>& rel, std::uint32_t p) {
    std::vector<std::size_t> hist(p, 0);
    for (const auto& rec : rel) {
        ++hist[compute_partition_id(rec.key, p)];
    }
    return hist;
}
```
- Scansione lineare dell'intera relazione -> `O(N)`
- Scrive su `hist[pid]` -> accesso random su un array piccolo (P entry)
- **Parallelizzabile**: ogni thread può avere il suo istogramma locale, poi si fa un merge

### 5.3 Prefix Sum (Exclusive Scan)

```cpp
static std::vector<std::size_t> exclusive_prefix_sum(const std::vector<std::size_t>& hist) {
    std::vector<std::size_t> begin(hist.size(), 0);
    std::size_t running = 0;
    for (std::size_t p = 0; p < hist.size(); ++p) {
        begin[p] = running;
        running += hist[p];
    }
    return begin;
}
```
- Opera sull'histogram (P entry, tipicamente piccolo: 64, 128, 256...)
- **Sequenziale per natura** (dipendenza loop-carried), ma su dati molto piccoli
- Tempo trascurabile -> **NON vale la pena parallelizzare**

### 5.4 Scatter

```cpp
static std::vector<Record> scatter_partitioned(const std::vector<Record>& rel,
                                               std::uint32_t p,
                                               const std::vector<std::size_t>& begin) {
    std::vector<Record> out(rel.size());
    std::vector<std::size_t> next = begin; // cursore di scrittura per partizione

    for (const auto& rec : rel) {
        const std::uint32_t pid = compute_partition_id(rec.key, p);
        out[next[pid]++] = rec;
    }
    return out;
}
```
- Scansione lineare dei record -> `O(N)`
- Scritture **random** nell'array output (dipende dalla partizione)
- Il cursore `next[pid]` è condiviso -> **ogni incremento è una dipendenza seriale**
- **Attenzione**: parallelizzare lo scatter è il punto più delicato!

### 5.5 Join locale (Build + Probe)

```cpp
// Build: conta occorrenze di ogni key in R_p
std::unordered_map<std::uint64_t, std::uint32_t> countR;
countR.reserve((r_end - r_begin) * 2);
for (std::size_t i = r_begin; i < r_end; ++i) {
    ++countR[Rpart.data[i].key];
}

// Probe: per ogni key in S_p, se esiste in R, aggiungi multiplicity match
for (std::size_t i = s_begin; i < s_end; ++i) {
    const auto it = countR.find(key);
    if (it != countR.end()) {
        result.join_count += it->second;
        result.checksum1 += splitmix64(key) * it->second;
        result.checksum2 += splitmix64(key ^ 0x9e3779b97f4a7c15ULL) * it->second;
    }
}
```
- Ogni partizione è **indipendente** dalle altre -> parallelismo perfetto!
- `std::unordered_map` è una scelta del codice di riferimento (hash table con chaining)
- Il professore nota esplicitamente che si può sostituire con strutture più efficienti

---

## 6. Step 3: Analisi delle Fasi e Opportunità di Parallelismo

Questa è la parte **più importante** del progetto. Il professore dice: *"The goal is not simply to introduce parallelism, but to understand where parallelism is effective and where it is not."*

### 6.1 Tabella di analisi per fase

| Fase | Complessità | Parallelizzabile? | Strategia | Note |
|------|------------|-------------------|-----------|------|
| **Histogram** | O(N) | ✅ Sì | Istogrammi locali + merge | Facile, buono speedup |
| **Prefix Sum** | O(P) | ❌ No (non conviene) | Sequenziale | P piccolo, tempo trascurabile |
| **Scatter** | O(N) | ⚠️ Possibile ma complesso | Multi-pass o scatter locale | Conflitti sugli indici di scrittura |
| **Join locale** | O(N) per partizione | ✅ Sì (naturale) | 1 thread per partizione(i) | Embarrassingly parallel |
| **Accumulazione** | O(P) | ❌ No (non conviene) | Sequenziale o reduce | P piccolo |

### 6.2 Analisi Work-Span per fase

Usando il modello Work-Span (Lezione 15):

**Histogram parallelo con k thread**:
- `T₁` = N (work: ogni record deve essere processato)
- `T∞` = N/k + P (span: lavoro locale + merge degli istogrammi)
- Parallelismo: `T₁/T∞` ≈ k se N >> P

**Join locale parallelo con k thread** (distribuendo le P partizioni):
- `T₁` = somma dei costi delle P partizioni
- `T∞` = max costo di una singola partizione × ceil(P/k)
- Se le partizioni sono bilanciate, parallelismo ≈ min(k, P)

### 6.3 Dove concentrare lo sforzo

Dalla **Legge di Amdahl**: se una fase domina il tempo totale, è lì che il parallelismo è più efficace.

Profila il sequenziale per capire la distribuzione del tempo:
```cpp
// Aggiungi timer attorno ad ogni fase
auto t_start = std::chrono::steady_clock::now();
// ... fase ...
auto t_end = std::chrono::steady_clock::now();
double elapsed = std::chrono::duration<double>(t_end - t_start).count();
std::cout << "fase_X: " << elapsed << " sec\n";
```

Tipicamente, per input grandi:
1. **Scatter** e **Join locale** dominano il tempo
2. **Histogram** è significativo
3. **Prefix sum** e **accumulazione** sono trascurabili

---

## 7. Step 4: Implementazione Parallela

### 7.1 Architettura generale

```cpp
// Struttura del programma parallelo
JoinResult partitioned_hash_join_parallel(
    const std::vector<Record>& R,
    const std::vector<Record>& S,
    std::uint32_t P,
    int nthreads)
{
    // --- FASE 1: Partizionamento parallelo di R ---
    PartitionedRelation Rpart = partition_relation_parallel(R, P, nthreads);

    // --- FASE 2: Partizionamento parallelo di S ---
    PartitionedRelation Spart = partition_relation_parallel(S, P, nthreads);

    // --- FASE 3: Join locale parallelo ---
    JoinResult total = parallel_join(Rpart, Spart, P, nthreads);

    return total;
}
```

### 7.2 Strategia A: Histogram parallelo (Raccomandata)

Ogni thread calcola un istogramma locale sulla propria porzione di dati, poi si fa un merge.

```cpp
std::vector<std::size_t> compute_histogram_parallel(
    const std::vector<Record>& rel,
    std::uint32_t P,
    int nthreads)
{
    const std::size_t N = rel.size();

    // Istogrammi locali per thread (evita false sharing!)
    // Ogni thread ha il suo array di P contatori
    std::vector<std::vector<std::size_t>> local_hists(nthreads, std::vector<std::size_t>(P, 0));

    // Fase 1: ogni thread calcola il proprio istogramma locale
    std::vector<std::thread> threads;
    for (int t = 0; t < nthreads; ++t) {
        threads.emplace_back([&, t]() {
            // Block distribution dei record
            std::size_t start = (N * t) / nthreads;
            std::size_t end   = (N * (t + 1)) / nthreads;

            for (std::size_t i = start; i < end; ++i) {
                std::uint32_t pid = compute_partition_id(rel[i].key, P);
                ++local_hists[t][pid];
            }
        });
    }
    for (auto& th : threads) th.join();

    // Fase 2: merge degli istogrammi locali (sequenziale, O(P × nthreads))
    std::vector<std::size_t> global_hist(P, 0);
    for (int t = 0; t < nthreads; ++t) {
        for (std::uint32_t pid = 0; pid < P; ++pid) {
            global_hist[pid] += local_hists[t][pid];
        }
    }

    return global_hist;
}
```

**Teoria correlata (Lezione 14)**: questa è una distribuzione **block statica** dei record tra i thread. Funziona bene perché il costo di calcolare la hash è uniforme (workload regolare).

### 7.3 Strategia B: Scatter parallelo

Lo scatter è la fase più delicata. Ci sono diverse strategie:

#### Strategia B1: Scatter indipendente con offset locali (Raccomandata)

Ogni thread conosce, grazie agli istogrammi locali, **esattamente** dove scrivere. Si calcolano offset per-thread-per-partizione.

```cpp
/*
 * Idea:
 * 1. Ogni thread t ha il suo local_hist[t][pid] = quanti record del blocco t
 *    vanno nella partizione pid
 * 2. Calcolo offsets per-thread: per ogni partizione pid, il thread t scrive
 *    a partire da offset[t][pid]
 * 3. Scatter parallelo: ogni thread scorre il suo blocco e scrive nella
 *    posizione corretta SENZA conflitti
 */

// Step 1: calcola gli offset per thread
// global_begin[pid] = posizione di inizio della partizione pid nell'array output
// thread_offset[t][pid] = posizione di inizio per il thread t nella partizione pid

std::vector<std::vector<std::size_t>> thread_offset(nthreads, std::vector<std::size_t>(P));

for (std::uint32_t pid = 0; pid < P; ++pid) {
    std::size_t offset = global_begin[pid];
    for (int t = 0; t < nthreads; ++t) {
        thread_offset[t][pid] = offset;
        offset += local_hists[t][pid];
    }
}

// Step 2: scatter parallelo (SENZA lock! ogni thread scrive in posizioni uniche)
std::vector<Record> out(rel.size());
std::vector<std::thread> threads;
for (int t = 0; t < nthreads; ++t) {
    threads.emplace_back([&, t]() {
        std::size_t start = (N * t) / nthreads;
        std::size_t end   = (N * (t + 1)) / nthreads;

        // Cursori locali (copia degli offset di questo thread)
        std::vector<std::size_t> cursor = thread_offset[t];

        for (std::size_t i = start; i < end; ++i) {
            std::uint32_t pid = compute_partition_id(rel[i].key, P);
            out[cursor[pid]++] = rel[i];
        }
    });
}
for (auto& th : threads) th.join();
```

**Vantaggi**: Zero sincronizzazione! Ogni thread scrive in regioni non sovrapposte dell'output.

**Teoria correlata**: questo è un pattern **map** (Lezione 11). Ogni thread mappa indipendentemente i propri record nelle posizioni corrette.

#### Strategia B2: Scatter con atomic (più semplice, meno efficiente)

```cpp
// Usa atomici per i cursori (semplice ma con contention)
std::vector<std::atomic<std::size_t>> next(P);
for (std::uint32_t pid = 0; pid < P; ++pid) {
    next[pid].store(global_begin[pid]);
}

// Ogni thread fa fetch_add atomico
std::size_t pos = next[pid].fetch_add(1, std::memory_order_relaxed);
out[pos] = rec;
```

Questo approccio è più semplice ma soffre di **contention** quando molti thread scrivono nella stessa partizione.

### 7.4 Strategia C: Join locale parallelo (Molto efficace)

Questa è la parte con il parallelismo più naturale. Ogni partizione è indipendente.

```cpp
JoinResult parallel_join(const PartitionedRelation& Rpart,
                         const PartitionedRelation& Spart,
                         std::uint32_t P,
                         int nthreads)
{
    // Risultati per thread (con padding per evitare false sharing!)
    struct alignas(64) PaddedResult {
        JoinResult result{};
    };
    std::vector<PaddedResult> thread_results(nthreads);

    std::vector<std::thread> threads;
    for (int t = 0; t < nthreads; ++t) {
        threads.emplace_back([&, t]() {
            // Distribuzione delle partizioni: block o cycling
            // Block distribution:
            for (std::uint32_t pid = t; pid < P; pid += nthreads) {
                JoinResult local = join_one_partition(Rpart, Spart, pid);
                thread_results[t].result.join_count += local.join_count;
                thread_results[t].result.checksum1  += local.checksum1;
                thread_results[t].result.checksum2  += local.checksum2;
            }
        });
    }
    for (auto& th : threads) th.join();

    // Reduce finale
    JoinResult total{};
    for (int t = 0; t < nthreads; ++t) {
        total.join_count += thread_results[t].result.join_count;
        total.checksum1  += thread_results[t].result.checksum1;
        total.checksum2  += thread_results[t].result.checksum2;
    }

    return total;
}
```

**Nota sulla distribuzione delle partizioni**: nel codice sopra sto usando una distribuzione **cyclic** (`pid = t, t+nthreads, t+2*nthreads, ...`). Questo è generalmente meglio di block perché bilancia meglio il carico se le dimensioni delle partizioni variano.

> **Teoria (Lezione 14)**: Se le partizioni hanno dimensioni diverse (workload irregolare), una distribuzione ciclica tende a bilanciare meglio il carico rispetto a block. Per workload ancora più irregolari, si potrebbe usare **dynamic scheduling** (es: coda condivisa da cui ogni thread preleva la prossima partizione da processare).

### 7.5 Distribuzione dinamica delle partizioni (opzionale, avanzato)

```cpp
// Scheduler dinamico basato su atomic counter
std::atomic<std::uint32_t> next_partition{0};

for (int t = 0; t < nthreads; ++t) {
    threads.emplace_back([&]() {
        JoinResult my_result{};
        while (true) {
            std::uint32_t pid = next_partition.fetch_add(1, std::memory_order_relaxed);
            if (pid >= P) break;

            JoinResult local = join_one_partition(Rpart, Spart, pid);
            my_result.join_count += local.join_count;
            my_result.checksum1  += local.checksum1;
            my_result.checksum2  += local.checksum2;
        }
        // ... accumula my_result ...
    });
}
```

### 7.6 Ottimizzazione: sostituire std::unordered_map

Il professore dice esplicitamente che `std::unordered_map` è solo una scelta di riferimento. Per le performance, si può usare una struttura più efficiente:

```cpp
// Opzione 1: array denso (se max_key è piccolo)
// Pro: O(1) lookup, cache-friendly
// Contro: spreco di memoria se le chiavi sono sparse
std::vector<std::uint32_t> countR(max_key, 0);
for (std::size_t i = r_begin; i < r_end; ++i) {
    ++countR[Rpart.data[i].key];
}

// Opzione 2: flat hash map (es: robin hood hashing)
// Molto più veloce di std::unordered_map per lookup
// Si può implementare manualmente o usare una libreria

// Opzione 3: ordinamento + linear scan
// Se le chiavi nella partizione sono poche e dense
```

### 7.7 Pipeline completa del partizionamento parallelo

Mettendo insieme tutto:

```cpp
PartitionedRelation partition_relation_parallel(
    const std::vector<Record>& rel,
    std::uint32_t P,
    int nthreads)
{
    const std::size_t N = rel.size();

    // --- 1. Histogram locale parallelo ---
    std::vector<std::vector<std::size_t>> local_hists(nthreads, std::vector<std::size_t>(P, 0));

    {
        std::vector<std::thread> threads;
        for (int t = 0; t < nthreads; ++t) {
            threads.emplace_back([&, t]() {
                std::size_t start = (N * t) / nthreads;
                std::size_t end   = (N * (t + 1)) / nthreads;
                for (std::size_t i = start; i < end; ++i) {
                    ++local_hists[t][compute_partition_id(rel[i].key, P)];
                }
            });
        }
        for (auto& th : threads) th.join();
    }

    // --- 2. Merge histogram + prefix sum (sequenziale, O(P × nthreads)) ---
    std::vector<std::size_t> global_hist(P, 0);
    for (int t = 0; t < nthreads; ++t)
        for (std::uint32_t pid = 0; pid < P; ++pid)
            global_hist[pid] += local_hists[t][pid];

    std::vector<std::size_t> global_begin(P, 0);
    {
        std::size_t running = 0;
        for (std::uint32_t pid = 0; pid < P; ++pid) {
            global_begin[pid] = running;
            running += global_hist[pid];
        }
    }

    // --- 3. Calcolo offset per-thread-per-partizione ---
    std::vector<std::vector<std::size_t>> thread_offset(nthreads, std::vector<std::size_t>(P));
    for (std::uint32_t pid = 0; pid < P; ++pid) {
        std::size_t offset = global_begin[pid];
        for (int t = 0; t < nthreads; ++t) {
            thread_offset[t][pid] = offset;
            offset += local_hists[t][pid];
        }
    }

    // --- 4. Scatter parallelo (lock-free!) ---
    std::vector<Record> out(N);
    {
        std::vector<std::thread> threads;
        for (int t = 0; t < nthreads; ++t) {
            threads.emplace_back([&, t]() {
                std::size_t start = (N * t) / nthreads;
                std::size_t end   = (N * (t + 1)) / nthreads;
                auto cursor = thread_offset[t]; // copia locale
                for (std::size_t i = start; i < end; ++i) {
                    std::uint32_t pid = compute_partition_id(rel[i].key, P);
                    out[cursor[pid]++] = rel[i];
                }
            });
        }
        for (auto& th : threads) th.join();
    }

    // --- 5. Calcolo end offsets ---
    std::vector<std::size_t> global_end(P, 0);
    for (std::uint32_t pid = 0; pid < P; ++pid)
        global_end[pid] = global_begin[pid] + global_hist[pid];

    return PartitionedRelation{
        .data  = std::move(out),
        .begin = global_begin,
        .end   = global_end
    };
}
```

### 7.8 Thread Pool vs Spawn-Join

Nel codice sopra, i thread vengono **creati e distrutti** ad ogni fase. Questo introduce overhead di spawn.

**Opzione avanzata**: usare un **thread pool** o creare i thread una volta e usare barriere per sincronizzarli tra le fasi.

```cpp
// Barriera C++20 per sincronizzare thread tra fasi
#include <barrier>

std::barrier sync_point(nthreads);

// In ogni thread:
// ... fase histogram ...
sync_point.arrive_and_wait();
// ... fase scatter (il master ha calcolato prefix sum) ...
sync_point.arrive_and_wait();
// ... fase join ...
```

**Teoria**: ogni spawn/join aggiunge overhead che, dalla Legge di Amdahl con overhead lineari (Lezione 10), diventa: `O(p) = c × nthreads`. Riutilizzare i thread riduce questo overhead.

---

## 8. Step 5: Strategia di Validazione (Correctness)

### 8.1 Per input piccoli (NR, NS ≤ 500)

Il codice fornisce `naive_join_verifier` che fa il join con complessità O(|R|×|S|):

```bash
# Esegui entrambe le versioni con gli stessi parametri
./hashjoin_seq -nr 50 -ns 80 -seed 13 -max-key 8 -p 4
./hashjoin_par -nr 50 -ns 80 -seed 13 -max-key 8 -p 4 -t 4

# Verifica che:
# 1. join_count sequenziale == join_count parallelo
# 2. checksum1 e checksum2 corrispondano
# 3. Per input piccoli: join_count == naive_join_count
```

### 8.2 Per input grandi

Confronta l'output della versione parallela con quella sequenziale:

```bash
#!/bin/bash
# Script di validazione
SEED=42
MAX_KEY=100000
P=64

for NR in 100000 1000000 10000000; do
    NS=$((NR * 2))
    echo "=== NR=$NR NS=$NS ==="

    SEQ_OUT=$(./hashjoin_seq -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P)
    PAR_OUT=$(./hashjoin_par -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P -t 8)

    SEQ_COUNT=$(echo "$SEQ_OUT" | grep "join_count" | head -1)
    PAR_COUNT=$(echo "$PAR_OUT" | grep "join_count" | head -1)

    if [ "$SEQ_COUNT" = "$PAR_COUNT" ]; then
        echo "  ✅ join_count MATCH"
    else
        echo "  ❌ MISMATCH: seq=$SEQ_COUNT par=$PAR_COUNT"
    fi
done
```

### 8.3 Testing multi-thread

Testa con numeri diversi di thread per esporre data race:

```bash
for T in 1 2 4 8 16; do
    echo "=== Threads=$T ==="
    ./hashjoin_par -nr 1000000 -ns 2000000 -seed 42 -max-key 1000 -p 64 -t $T
done
# Tutti devono produrre gli STESSI risultati
```

### 8.4 Edge cases da testare

```bash
# Poche partizioni
./hashjoin_par -nr 1000 -ns 1000 -seed 1 -max-key 10 -p 2 -t 4

# Molte partizioni (più di threads)
./hashjoin_par -nr 1000000 -ns 1000000 -seed 1 -max-key 10000 -p 256 -t 4

# Chiavi molto ripetute (max-key piccolo -> molti duplicati)
./hashjoin_par -nr 1000000 -ns 1000000 -seed 1 -max-key 10 -p 8 -t 4

# Partizioni vuote (max-key << P)
./hashjoin_par -nr 100 -ns 100 -seed 1 -max-key 4 -p 64 -t 4

# Thread > partizioni
./hashjoin_par -nr 1000000 -ns 1000000 -seed 1 -max-key 10000 -p 4 -t 16
```

---

## 9. Step 6: Performance Evaluation

### 9.1 Parametri da variare

Il report richiede **speedup, strong scaling e weak scaling**. Ecco i benchmark da fare:

#### Strong Scaling (fissare il problem size, variare i thread)

```bash
#!/bin/bash
# strong_scaling.sh
NR=10000000   # 10M records
NS=20000000   # 20M records
SEED=42
MAX_KEY=1000000
P=128
REPS=5

echo "threads,time_sec"
for T in 1 2 4 8 12 16 20; do
    BEST=999999
    for ((r=0; r<REPS; r++)); do
        TIME=$(./hashjoin_par -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P -t $T | grep time_sec | cut -d= -f2)
        if (( $(echo "$TIME < $BEST" | bc -l) )); then
            BEST=$TIME
        fi
    done
    echo "$T,$BEST"
done
```

Poi calcola:
```
Speedup(t) = T_seq / T_par(t)
Efficiency(t) = Speedup(t) / t
```

#### Weak Scaling (problem size cresce con i thread)

```bash
#!/bin/bash
# weak_scaling.sh
BASE_NR=1000000  # 1M per thread
SEED=42
MAX_KEY=1000000
P=128
REPS=5

echo "threads,nr,ns,time_sec"
for T in 1 2 4 8 12 16 20; do
    NR=$((BASE_NR * T))
    NS=$((NR * 2))
    BEST=999999
    for ((r=0; r<REPS; r++)); do
        TIME=$(./hashjoin_par -nr $NR -ns $NS -seed $SEED -max-key $MAX_KEY -p $P -t $T | grep time_sec | cut -d= -f2)
        if (( $(echo "$TIME < $BEST" | bc -l) )); then
            BEST=$TIME
        fi
    done
    echo "$T,$NR,$NS,$BEST"
done
```

Weak Scaling Efficiency:
```
WSE(t) = T(PS(1), 1) / T(PS(t), t)
```
Dove `PS(t) = t × PS(1)`.

#### Breakdown per fase

Aggiungi timer per ogni fase e riporta la percentuale del tempo totale:

```
Fase           |  1 thread  |  8 thread  | Speedup fase
---------------|-----------|-----------|-------------
Histogram R    |  xxx ms   |  xxx ms   | x.xx
Scatter R      |  xxx ms   |  xxx ms   | x.xx
Histogram S    |  xxx ms   |  xxx ms   | x.xx
Scatter S      |  xxx ms   |  xxx ms   | x.xx
Join locale    |  xxx ms   |  xxx ms   | x.xx
Accumulazione  |  xxx ms   |  xxx ms   | x.xx
TOTALE         |  xxx ms   |  xxx ms   | x.xx
```

### 9.2 Come misurare correttamente

**Regole di benchmarking** (dalle lezioni):

1. **Warm-up**: fai almeno 1 esecuzione di warm-up prima di misurare
2. **Ripetizioni**: almeno 5 ripetizioni, prendi la **mediana** (o rimuovi min/max e calcola la media)
3. **Non misurare** la generazione dei dati (il codice già lo fa correttamente)
4. **Compila con ottimizzazioni**: `-O3` è fondamentale
5. **Benchmark sul cluster**: i risultati devono essere ottenuti su un nodo dell'`spmcluster`

### 9.3 Benchmark sull'spmcluster

```bash
# Connessione al cluster
ssh <username>@spmcluster.di.unipi.it

# Compilazione sul cluster
g++ -O3 -std=c++20 -Wall -Wextra src/hashjoin_par.cpp -o hashjoin_par -pthread

# Verifica il numero di core
nproc   # oppure lscpu

# Esecuzione (assicurati di non condividere il nodo con altri)
# Oppure usa SLURM se configurato
```

Se il cluster usa **SLURM** (Lezione l4-SLURM), crea uno script di submit:

```bash
#!/bin/bash
#SBATCH --job-name=hashjoin
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=20
#SBATCH --time=00:30:00
#SBATCH --output=results/bench_%j.out

./hashjoin_par -nr 10000000 -ns 20000000 -seed 42 -max-key 1000000 -p 128 -t $SLURM_CPUS_PER_TASK
```

---

## 10. Step 7: Scrivere il Report

### 10.1 Struttura del report (max 5 pagine)

```
1. Introduzione (0.5 pagina)
   - Breve descrizione del problema e dell'obiettivo

2. Strategia di Parallelizzazione (1.5 pagine)
   - Analisi delle fasi dell'algoritmo
   - Per ogni fase: decisione di parallelizzare o meno, e motivazione
   - Descrizione tecnica della strategia adottata
   - Come si evitano race condition e false sharing

3. Risultati Sperimentali (2 pagine)
   - Tabella/grafico speedup (strong scaling)
   - Tabella/grafico weak scaling efficiency
   - Breakdown del tempo per fase
   - Piattaforma di test (nodo del cluster, #core, cache, etc.)

4. Discussione (1 pagina)
   - Quali fasi beneficiano del parallelismo e quali no
   - Bottleneck identificati (memoria, overhead spawn, false sharing...)
   - Confronto con i limiti teorici (Amdahl)
   - Possibili miglioramenti futuri
```

### 10.2 Grafici da includere

1. **Speedup vs #threads** (strong scaling) — con linea ideale `S(p)=p` come riferimento
2. **Efficienza vs #threads** — mostra il degradamento
3. **Breakdown tempo per fase** — stacked bar chart o tabella
4. **Weak scaling efficiency** — come cambia al crescere del problema

---

## 11. Checklist Finale

- [ ] **Funzione hash del Modulo 1** integrata in `compute_partition_id`
- [ ] **Versione sequenziale** compila e produce output corretto
- [ ] **Versione parallela** implementata con `std::thread`
- [ ] **Correctness**: output parallelo == output sequenziale (per tutti i test)
- [ ] **Edge cases** testati (poche partizioni, molti duplicati, thread > partizioni)
- [ ] **Niente race condition**: testato con `-fsanitize=thread` (opzionale)
- [ ] **Niente false sharing**: padding/allineamento dove necessario
- [ ] **Benchmark strong scaling** eseguiti sul cluster
- [ ] **Benchmark weak scaling** eseguiti sul cluster
- [ ] **Breakdown per fase** misurato e riportato
- [ ] **Makefile** presente con istruzioni di compilazione
- [ ] **README** con istruzioni per compilare, eseguire, e validare
- [ ] **Report PDF** ≤ 5 pagine con grafici e discussione
- [ ] **ZIP**: `Modulo2_NomeCognome.zip` contenente tutto
- [ ] **Email** con oggetto "SPM Modulo2"

---

## 12. Appendice: Comandi Utili & Riferimenti

### Compilazione

```bash
# Standard
g++ -O3 -std=c++20 -Wall -Wextra -pthread src/hashjoin_par.cpp -o hashjoin_par

# Con thread sanitizer (per debug race condition)
g++ -O1 -g -std=c++20 -fsanitize=thread -pthread src/hashjoin_par.cpp -o hashjoin_par_tsan

# Con address sanitizer (per debug memory)
g++ -O1 -g -std=c++20 -fsanitize=address -pthread src/hashjoin_par.cpp -o hashjoin_par_asan
```

### Profilazione

```bash
# Perf (su Linux/cluster)
perf stat ./hashjoin_par -nr 10000000 -ns 20000000 -seed 42 -max-key 1000000 -p 128 -t 8
perf record ./hashjoin_par ...
perf report

# Valgrind / cachegrind
valgrind --tool=cachegrind ./hashjoin_par ...
```

### Contare i core

```bash
# macOS
sysctl -n hw.ncpu

# Linux (cluster)
nproc
lscpu | grep "CPU(s):"
```

### Riferimenti alle lezioni

| Argomento | Lezione |
|-----------|---------|
| Speedup, Efficienza, Amdahl, Gustafson | `10-Metrics_and_Laws.pdf` |
| Data/Stream/Task Parallelism | `11-TypesOfParallelism.pdf` |
| C++ auto, move, STL, lambdas | `12-C++Essentials.pdf` |
| std::thread, mutex, CV, future/promise | `13-C++ConcurrencyBasics.pdf` |
| Block/Cyclic distribution, Dynamic scheduling | `14-WorkloadBalancing.pdf` |
| Work-Span, Brent's theorem, PRAM | `15-ModelsOfComputation.pdf` |
| Cache, False Sharing, Roofline model | `l5 & 6-Shared-Memory.pdf` |
| SLURM | `l4-SLURM.pdf` |

### Pattern chiave riassunti

```
┌─────────────────────────────────────────────────────────────┐
│ PATTERN: Data Parallelism con Histogram locale              │
│                                                             │
│  Thread 0: scan block_0 -> local_hist_0                      │
│  Thread 1: scan block_1 -> local_hist_1                      │
│  ...                                                        │
│  Thread k: scan block_k -> local_hist_k                      │
│  BARRIER                                                    │
│  Master:   merge local_hist_0..k -> global_hist              │
│                                                             │
│  Complessità: O(N/k) per thread + O(P×k) merge             │
│  Speedup atteso: ~k per N >> P×k                            │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ PATTERN: Lock-free Scatter con offset pre-calcolati         │
│                                                             │
│  PRE: ogni thread t conosce local_hist[t][pid]              │
│  PRE: offset[t][pid] calcolato da prefix sum cumulativo     │
│                                                             │
│  Thread t: scan block_t -> out[offset[t][pid]++]             │
│                                                             │
│  Zero sincronizzazione! Regioni di scrittura non overlap.   │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│ PATTERN: Join locale embarassingly parallel                 │
│                                                             │
│  Distribuisci le P partizioni tra k thread                  │
│  (cyclic per load balancing, dynamic se workload irregolare)│
│                                                             │
│  Thread t: for each partizione assegnata:                   │
│              Build hash table su R_p                         │
│              Probe S_p -> accumula risultato locale          │
│  BARRIER                                                    │
│  Reduce dei risultati locali                                │
└─────────────────────────────────────────────────────────────┘
```

---

> **Buon lavoro!** 🚀 Ricorda: il professore valuta non solo la correttezza ma la tua **capacità di ragionare** sulle scelte di design e di supportarle con dati sperimentali. Non esiste una strategia unica "giusta" — l'importante è giustificare le tue scelte.
