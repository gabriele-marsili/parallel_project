# Esperimento 1 — Qualità di distribuzione della hash di partizionamento

**Obiettivo.** Giustificare con numeri la scelta della hash del report (Fibonacci
multiply-shift a 32 bit) contro alternative, e verificare se davvero produce
partizioni bilanciate su dati realistici e su dati "avversari".

**Perché è materiale a sé.** Non modifica report né codice consegnati: è un
esperimento aggiuntivo per l'orale. Il kernel del report resta invariato.

## Cosa confronta

Cinque mappe `k -> [0,P)`, tutte con P potenza di due:

| hash | formula | ruolo |
|---|---|---|
| `fib32` (report) | `((k_lo ^ k_hi) * 0x9E3779B9) >> (32-log2 P)` | quella scelta: XOR-fold + bit alti |
| `fib64` | `(k * 0x9E3779B97F4A7C15) >> (64-log2 P)` | multiplicative a 64 bit, per isolare l'effetto "32 vs 64 bit" |
| `mod` (naive) | `k & (P-1)` cioè `k % P` | il riferimento naive richiesto |
| `fib32_lowbits` | `((k_lo ^ k_hi) * A32) & (P-1)` | prende i bit BASSI del prodotto: isola il perché dello shift |
| `mult32_nofold` | `(k_lo * A32) >> shift` | niente XOR-fold: isola il perché del folding |

Sette distribuzioni di chiavi (seed 42, generatore xoshiro256\*\* identico a `common.hpp`;
`r` indica un'estrazione a 64 bit del generatore):

| dist | generazione | che cosa modella |
|---|---|---|
| `uniform` | `k = r` | chiavi random a 64 bit: il caso medio, ed è il default del progetto (`key_space=0`) |
| `sequential` | `k = i` | surrogate key / row-id contigui |
| `strided_pow2` | `k = i·4096` | offset allineati a pagina, id con tag nei bit bassi |
| `low16_zero` | `k = r·2¹⁶` | entropia nei 48 bit alti, 16 bit bassi a 0 |
| `high32_only` | `k = r·2³²` | entropia nei soli 32 bit alti, 32 bit bassi a 0 |
| `dup_1000` | `k = r mod 1000` | universo piccolo: 1000 valori distinti **contigui** (0…999), uniformi, ciascuno replicato ~10⁵ volte (N=10⁸). È il `key_space=1000` del report |
| `dup_struct` | `k = (r mod 1000)·65536` | gli **stessi** 1000 valori con le **stesse** molteplicità, riscalati di 2¹⁶ |

Le ultime due sono un A/B controllato. Il generatore è riavviato con lo stesso seme per
ogni distribuzione e consuma un'estrazione per chiave, quindi `dup_struct` è chiave per
chiave `dup_1000` shiftata di 16 bit: stesso supporto (cardinalità 1000), stessa
molteplicità per valore, stessa statistica. Cambia solo la **codifica in bit** dei valori.
Ogni differenza fra le due colonne è quindi imputabile alla sensibilità della hash al
layout dei bit, non alla distribuzione delle chiavi.

**Metriche** per ogni coppia (dist, hash): `max/atteso` (1 = ideale), CoV, Gini,
chi²/dof (≈1 se uniforme), entropia normalizzata H/log₂P (1 = ideale), n. partizioni vuote.

## Come si esegue (su node09, per coerenza con gli altri test)

```bash
# build + run su node09
srun --partition=gpu-excl --nodelist=node09 --ntasks=1 --cpus-per-task=1 --time=00:05:00 \
  bash -c 'g++ -std=c++20 -O3 -march=native hash_quality.cpp -o hash_quality &&
           ./hash_quality 100000000 256 results/summary_N100M_P256.csv results/occupancy_N100M_P256.csv'
# grafici (in locale)
python3 plot_hash_quality.py
```

Nota: è aritmetica pura e deterministica, quindi i numeri su node09 sono risultati
**identici bit a bit** a quelli su un Mac arm64. Questo è di per sé un risultato: il
bilanciamento è una proprietà della matematica della hash, non della macchina.

## Risultati (N=10⁸, P=256, node09) — `max/atteso`

| distribuzione | fib32 | fib64 | mod | fib32_lowbits | mult32_nofold |
|---|---|---|---|---|---|
| uniform | 1.005 | 1.004 | 1.004 | 1.005 | 1.004 |
| sequential | 1.000 | 1.000 | 1.000 | 1.000 | 1.000 |
| strided k=i·4096 | **1.000** | 1.000 | **256 (collasso)** | 2.68 | 1.000 |
| low16=0 | **1.005** | 1.004 | **256 (collasso)** | 1.005 | 1.004 |
| high32 only | **1.004** | 1.004 | **256 (collasso)** | 1.005 | **256 (collasso)** |
| dup contigui (ks=1000) | 1.283 | 1.283 | 1.029 | 1.029 | 1.283 |
| dup strutturati (·65536) | **1.284** | 1.286 | **256 (collasso)** | **256 (collasso)** | 1.284 |

"256" = tutte le 10⁸ chiavi in una sola partizione (255 vuote, Gini 0.996).

## Lettura dei risultati

1. **Su chiavi uniformi vanno bene tutte** (fib32 = 1.005, riproduce l'1.005 del
   report). `mod` non è peggiore su dati random: la differenza non è lì.
2. **Bit alti contro bit bassi (il ruolo dello `>> shift`)**: su `strided`, prendere i
   bit bassi del prodotto (`fib32_lowbits`) porta a 2.68 con 160 partizioni vuote;
   prendere i bit alti (`fib32`) resta a 1.000. Nella moltiplicazione modulare il bit
   *j* del prodotto dipende solo dai bit ≤ *j* degli operandi: i bit bassi del prodotto
   ereditano l'entropia dei soli bit bassi della chiave, i bit alti la raccolgono da
   tutta la chiave.
3. **XOR-fold (il ruolo del mescolamento di hi in lo)**: su `high32_only` la variante
   senza folding (`mult32_nofold`) collassa a 256×, perché moltiplica i soli 32 bit
   bassi, che qui sono a zero. Con il folding `fib32` resta a 1.004.
4. **Perché non `mod` naive**: `mod` seleziona gli ultimi log₂P bit della chiave, quindi
   collassa ogni volta che quei bit non portano entropia. Accade su 4 distribuzioni su 7
   (`strided`, `low16_zero`, `high32_only`, `dup_struct`): offset allineati, id con tag,
   valori riscalati per una potenza di due. Sono forme comuni in dati reali.
5. **La larghezza a 32 bit non entra nella qualità**: `fib32` e `fib64` coincidono su
   tutte e sette le distribuzioni (worst case 1.283 contro 1.286). La qualità è decisa
   dai tre ingredienti (fold, moltiplicazione, bit alti), non dalla larghezza. Quindi la
   scelta dei 32 bit non è arbitrabile con questo esperimento: è imposta da AVX2, che ha
   la moltiplicazione intera 32×32 nativa (`vpmulld`, 8 corsie) e non la 64×64
   (`vpmullq` esiste solo in AVX-512), che va scomposta in 3 `vpmuludq` su 4 corsie.
   L'esperimento 04 misura la conseguenza: la SIMD accelera la hash a 32 bit (1.32×) e
   non accelera quella a 64 bit (0.98×).
6. **`dup_1000` riproduce `dist_ratio=1.2827`** del `cpu_results.csv` consegnato: il
   generatore dell'harness è fedele al codice del progetto.
7. **Il 1.28 di fib32 su `dup` non è un difetto della hash, è quantizzazione.** Tutti i
   duplicati di un valore finiscono per costruzione nella stessa partizione, quindi il
   carico di una partizione è un multiplo intero di ~10⁵ chiavi. Con 1000 valori distinti
   su P=256 la media è 3.906 valori per partizione, e almeno una ne riceve ≥4: **nessuna
   hash può scendere sotto 4/3.906 = 1.024**. È un limite strutturale del rapporto
   `key_space/P`, non una proprietà della funzione.
8. **Il vantaggio di `mod` su `dup_1000` è un artefatto della contiguità.** Su valori
   0…999, `k & 255` è un round-robin esatto: 1000 = 3·256 + 232, quindi 232 partizioni
   ricevono 4 valori e 24 ne ricevono 3 (verificato in `occupancy_N100M_P256.csv`).
   `mod` centra esattamente il minimo strutturale (1.029 misurato contro 1.024 teorico;
   lo scarto è la fluttuazione multinomiale dei conteggi per valore, ~0.3%). `fib32`
   invece non sa che i valori sono contigui e li sparpaglia: l'occupazione diventa
   {3 valori: 43 part., 4: 194, 5: 19}, il massimo è 5 valori → 5/3.906 = 1.28
   (misurato 1.283). Il +25% di fib32 rispetto a `mod` è la fluttuazione statistica
   dell'assegnazione pseudo-casuale (balls-into-bins), non un difetto della funzione.
9. **Con la stessa distribuzione e un'altra codifica, il vantaggio si rovescia.** Su
   `dup_struct` (stessi valori, stesse molteplicità, riscalati di 2¹⁶) tutte le chiavi
   hanno i 16 bit bassi a zero: `k & 255 = 0` per ogni chiave, quindi `mod` mette le 10⁸
   chiavi in partizione 0 e ne lascia 255 vuote. `fib32` resta a 1.284, invariata.

**Tesi.** L'esperimento separa due decisioni indipendenti. La prima (famiglia `fib` invece
di `mod`) è motivata dalla robustezza: `mod` dipende dalla codifica in bit dei valori e
collassa su 4 distribuzioni su 7, `fib32` non collassa mai e nel caso peggiore paga 1.28,
che è quantizzazione strutturale e non difetto della funzione. In un hash join la
distribuzione delle chiavi non è nota a priori, quindi si sceglie la garanzia indipendente
dai dati e non l'ottimo condizionato a una forma particolare. La seconda (32 bit invece di
64) non è decidibile sulla qualità, che è identica, ed è motivata da AVX2 (esperimento 04).

## File

- `hash_quality.cpp` — sorgente (riusa hash e generatore del progetto).
- `results/summary_N100M_P256.csv` — metriche per (dist, hash).
- `results/occupancy_N100M_P256.csv` — occupazione per-partizione (per gli istogrammi).
- `plots/hash_matrix.png` — matrice hash × distribuzione di `max/atteso`.
- `plots/hash_imbalance_bars.png` — max/atteso per (dist × hash), scala log.
- `plots/partition_occupancy.png` — occupazione delle 256 partizioni, fib32 contro mod.
