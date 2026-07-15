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

Sei distribuzioni di chiavi (seed 42, generatore xoshiro256\*\* identico a `common.hpp`):

- `uniform`: chiavi random a 64 bit (caso "medio", realistico).
- `sequential`: `k=i` (surrogate key / row-id contigui).
- `strided_pow2`: `k=i·4096` (offset allineati, id con tag nei bit bassi).
- `low16_zero`: entropia solo nei bit alti, 16 bit bassi a 0.
- `high32_only`: entropia SOLO nei 32 bit alti (32 bit bassi a 0).
- `dup_1000`: universo di 1000 chiavi distinte (molti duplicati; = `key_space=1000` del report).

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

## Lettura dei risultati (come difenderlo all'orale)

1. **Su chiavi uniformi vanno bene tutte** (fib32 = 1.005, riproduce l'1.005 del
   report). Onestà: `mod` non è peggiore su dati random. La differenza non è lì.
2. **Bit alti vs bassi (perché `>> shift`)**: su `strided`, prendere i bit bassi
   del prodotto (`fib32_lowbits`) sbanda a 2.68 con 160 partizioni vuote; prendere
   i bit alti (`fib32`) resta a 1.000. I bit alti del prodotto hanno più entropia.
3. **XOR-fold (perché mescolare hi in lo)**: su `high32_only`, senza folding
   (`mult32_nofold`) si collassa (256×); con il folding `fib32` resta a 1.004.
4. **Perché non `mod` naive**: `mod` = ultimi log₂P bit della chiave. Collassa ogni
   volta che quei bit mancano di entropia (offset allineati, id con tag, chiavi con
   struttura: casi comunissimi). Su tre distribuzioni su sei va a 256×.
5. **32 vs 64 bit non cambia la QUALITÀ**: `fib32` e `fib64` sono identici ovunque.
   Quindi i 32 bit non sacrificano nulla in distribuzione; la scelta dei 32 bit è
   motivata solo dal SIMD (vpmulld nativo vs decomposizione vpmuludq), non dalla
   bontà della hash. Vedi esperimento 04 (counterfactual sui tempi).
6. **`dup_1000` riproduce `dist_ratio=1.2827`** del `cpu_results.csv` consegnato →
   il generatore/harness è fedele al codice del progetto. Qui `mod` è pure un filo
   migliore (1.029 vs 1.283): con poche chiavi distinte fib32 le sparpaglia creando
   un lieve sbilanciamento, ma nessuna collassa (H/log₂P ≈ 0.999).
7. **Il "vantaggio" di `mod` in dup è fragile (dup_1000 vs dup_struct)**: la vittoria di
   `mod` (1.03) su `dup_1000` esiste solo perché i 1000 valori distinti sono contigui
   (0…999), caso gentile per i bit bassi. Con gli **stessi** 1000 valori distinti e la
   stessa densità di duplicati ma **strutturati** (`·65536`), `mod` **collassa a 256×**
   mentre `fib32` resta **1.28** invariata. Cioè: `mod` scommette sulla forma dei dati,
   `fib32` garantisce indipendentemente da essa. In un hash join non controlli le chiavi
   → prendi la garanzia.

**Tesi in una frase.** Fib32 si sceglie per **robustezza** (non collassa mai, su
nessuna distribuzione) più **SIMD-friendliness**, pagando un costo trascurabile su
dati uniformi. Non è "sempre meglio di mod": è robusta dove `mod` è fragile.

## File

- `hash_quality.cpp` — sorgente (riusa hash e generatore del progetto).
- `results/summary_N100M_P256.csv` — metriche per (dist, hash).
- `results/occupancy_N100M_P256.csv` — occupazione per-partizione (per gli istogrammi).
- `plots/hash_matrix.png` — matrice hash × distribuzione (la più leggibile: verde/rosso).
- `plots/hash_imbalance_bars.png` — max/atteso per (dist × hash), scala log.
- `plots/partition_occupancy.png` — occupazione delle 256 partizioni, fib32 vs mod.
