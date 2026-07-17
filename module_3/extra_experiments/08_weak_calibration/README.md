# Esp. 8 — la calibrazione del weak scaling

**Obiettivo.** Il weak scaling del report (`report.tex:366-394`) misura una efficienza
sopra il 100% a T=2 (1.06) e T=4 (1.09), e la lascia senza meccanismo. L'esperimento
verifica che non sia scaling superlineare ma un artefatto della calibrazione, e separa i
due effetti che nel setup del report agiscono insieme e in direzioni opposte.

**Il difetto del setup.** `scripts/run_weak.sh:17-21` scala `NR = 2M * T` tenendo fissi
`max_key = 5M` e `P = 128`. Con P fisso, le tuple per partizione sono `NR/128 = 15625*T`:
crescono con T. La `FlatCountMap` e' dimensionata sui RECORD della partizione
(`join_phases.hpp:70`, `n = next_pow2(2*r_count)`) mentre le chiavi distinte saturano a
`max_key/P = 39062`. Quindi al crescere di T la tabella si allarga ma il numero di chiavi
inserite no: il riempimento crolla e il footprint esplode.

Il lavoro per thread e' costante in numero di operazioni (2M build + 4M probe a ogni T,
e il costo per match e' nullo perche' il join aggrega per chiave invece di materializzare
le coppie, `join_phases.hpp:130-134`), ma il costo per operazione dipende da T. Non e' un
weak scaling iso-granulare.

## Il meccanismo: perché il braccio A va sopra il 100%

È la catena che l'esperimento verifica, ed è il cuore di tutto il resto:

1. NR cresce con T ma **max_key resta 5M**, quindi crescono i duplicati per chiave
   (NR/max_key va da 0.4 a 12.8).
2. Le chiavi **distinte** per partizione non possono superare max_key/P = 39062, quindi
   **saturano**. La tabella invece è dimensionata sui **record** (`next_pow2(2*r_count)`),
   che crescono con T.
3. Quindi la tabella si allarga mentre le chiavi inserite no: **alpha crolla** (0.393 a
   T=1, 0.037 a T=32, misurato).
4. Load factor più basso vuol dire **catene di probe lineare più corte**: il probe accelera
   (15.19 ns a T=1, 4.80 a T=16, misurato).
5. Quindi il join per thread **diventa più veloce al crescere di T** pur facendo sempre gli
   stessi 4M probe, e questo **compensa e supera** il peggioramento delle altre fasi (a T=4
   il join guadagna 21.5 ms mentre hist+scatter ne perdono 10.8).
6. Il tempo totale scende, e l'efficienza va sopra 1.

Non è superlinearità da cache: il problema per thread non è costante, è diventato più
facile. Il numero di operazioni sì che è costante (2M build + 4M probe a ogni T, perché il
costo per match è nullo: il join aggrega per chiave invece di materializzare le coppie,
`join_phases.hpp:130-134`), ma il costo **per operazione** dipende da T.

I bracci B e C spezzano la catena al punto 1 e il resto non accade.

## Design: tre bracci

A parita' di lavoro nominale per thread (2M tuple R):

| braccio | max_key | P | alpha | tabella per thread |
|---|---|---|---|---|
| A (= report) | 5M | 128 | cala 0.39 -> 0.04 | cresce 512 KB -> 16 MB |
| B (iso-alpha) | 5M * T | 128 | costante ~0.39 | cresce 512 KB -> 16 MB |
| C (iso-granulare) | 5M * T | 128 * T | costante ~0.39 | costante 512 KB |

- **A vs B** isola il load factor: in B la densita' di duplicati resta 0.4 a ogni T, quindi
  le distinte per partizione crescono in proporzione alla tabella.
- **B vs C** isola il footprint: stesso alpha, ma in C la partizione (e quindi la tabella)
  non cresce.
- **C** e' il weak scaling che il report avrebbe dovuto misurare. Cio' che resta di
  degrado in C e' overhead parallelo vero piu' banda: il volume totale cresce con T in
  tutti e tre i bracci.

T=20 e' escluso: `compute_shift()` (`common.hpp`) assume P potenza di due e in C sarebbe
P=2560. Non e' una perdita, T=20 e' il punto di wrap-around dove 4 core prendono 2 thread
e l'effetto SMT confonde la lettura.

## Predizioni (scritte prima dei dati, quindi falsificabili)

1. **Eff > 1 sparisce in B e C.** Se a T=2 e T=4 l'efficienza resta sopra 1.05 anche a
   alpha costante, l'ipotesi del load factor e' falsa e il meccanismo e' un altro.
2. **alpha:** braccio A da ~0.39 a ~0.04; bracci B e C piatti a ~0.39.
3. **Il ginocchio del join a T=8 si sposta o sparisce in C.** In A, a T=8 gli 8 thread di
   un socket tengono 8 tabelle da 4 MB = 32 MB contro 20 MB di L3, e il join passa da 71 a
   100 ms. In C la tabella resta 512 KB, aggregato 4 MB: se il ginocchio resta comunque,
   allora non e' residenza in cache ma banda, e va riscritto il commento del report.

Il punto 3 e' quello che chiude la questione lasciata aperta finora: banda e residenza
agiscono insieme e la misura attuale non le separa.

## Evidenza gia' disponibile

- **Esp. 1** (`01_schedule_sweep/results/schedule_granularity.csv`): a T=16 uniform, stesso
  lavoro, solo P diverso -> join 37.1 ms a P=128 contro 23.1 a P=512, **-38%**. La sola
  dimensione della partizione (4 MB contro 1 MB di tabella) vale il 38% del join. E' la
  conferma indipendente che il footprint della tabella domina.
- **Esp. 2 di M2** (`module_2/extra_experiments/02_flatcountmap/results/loadfactor.csv`):
  con tabella tenuta fissa a 2 MB, il probe costa 7.72 ns a alpha 0.40 e 5.69 a alpha 0.25.
  Isola il load factor dalla residenza, ed e' la curva che rende quantitativa la
  predizione 1.

**Correzione.** `COMPANION_M3.md:227` e `esperimenti_aggiuntivi_M3.md:45` riportano
"2 MB a P=128": la tabella e' di **4 MB**. Con NR=10M e P=128, r_count = 78125, quindi
`n = next_pow2(156250) = 262144` slot da 16 B = 4 MB. Il fattore 2 del sizing era stato
perso. La conclusione non cambia, si rafforza: 8 thread per socket fanno 32 MB contro
20 MB di L3.

## Verifica di calibrazione (gia' eseguita, hardware-indipendente)

alpha e le distinte dipendono solo dal generatore e dal sizing, non dalla macchina, quindi
sono verificabili fuori dal cluster. Misurato con `alpha_probe`:

| caso | alpha atteso | alpha misurato | distinte attese | misurate |
|---|---|---|---|---|
| T=1 (A=B=C) | 0.393 | 0.3934 | 12878 | 12889 |
| T=4 braccio A | 0.238 | 0.2378 | 31177 | 31175 |
| T=4 braccio C | 0.393 | 0.3923 | 12878 | 12855 |

I `ns_per_key` di quella prova NON valgono: girata su un portatile, non su Ivy Bridge.

## Risultati (node01, job 707273, 5 rep, uniform)

**Validazione del braccio A.** Riproduce il weak del report entro il 3% pur essendo un
binario diverso (`omp_ablation` invece di `hashjoin_omp`): join 89.8 / 79.1 / 69.7 / 99.2 /
115.0 ms contro 92.6 / 82.2 / 71.1 / 99.8 / 119.4 del CSV originale a T=1,2,4,8,16. Il
confronto fra bracci e' quindi legittimo.

**1. L'efficienza sopra 1 e' un artefatto: confermato.** Efficienza weak T(1)/T(p), loop:

| T | A (report) | B (alpha cost.) | C (iso-gran.) |
|---|---|---|---|
| 2 | **1.063** | 0.982 | 0.974 |
| 4 | **1.070** | 0.921 | 0.979 |
| 8 | 0.792 | 0.612 | 0.740 |
| 16 | 0.653 | 0.499 | 0.632 |
| 32 | 0.272 | 0.229 | **0.341** |

Basta tenere il load factor costante e il sopra-100% sparisce, in entrambi i bracci e in
entrambe le varianti (task: A 1.058/1.042, B 0.995/0.890, C 0.973/0.968). Non era scaling
superlineare.

**2. Il meccanismo, misurato.** alpha effettivo e probe (thread singolo, ns per chiave):

| T | A: alpha / probe | B: alpha / probe | C: alpha / probe |
|---|---|---|---|
| 1 | 0.393 / 15.19 | 0.393 / 15.16 | 0.393 / 15.11 |
| 4 | 0.238 / 6.57 | 0.393 / 13.50 | 0.392 / 12.04 |
| 16 | 0.074 / 4.80 | 0.393 / 13.27 | 0.391 / 16.78 |
| 32 | 0.037 / 5.20 | 0.393 / 12.81 | 0.393 / 15.00 |

Nel braccio A il probe costa il **68% in meno** a T=16 che a T=1 (15.19 -> 4.80 ns) a
parita' di chiavi probate: e' il regalo del load factor, ed e' l'intero meccanismo
dell'efficienza sopra 1. In B e C alpha resta 0.393 a ogni T e il probe non ha trend
sistematico (oscilla fra 12 e 17 ns, dispersione fra le rep).

**3. Banda e residenza, finalmente separate.** Join (ms), loop uniform:

| T | A | B | C |
|---|---|---|---|
| 1 | 89.8 | 89.9 | 89.8 |
| 4 | 69.7 | 89.5 | 80.5 |
| 8 | 99.2 | 147.9 | 110.0 |
| 16 | 115.0 | 176.7 | 114.9 |
| 32 | 255.3 | 344.8 | **134.5** |

- Il ginocchio a T=8 **resta anche in C** (80.5 -> 110.0, +37%), dove la tabella e' 512 KB
  e l'aggregato per socket e' 4 MB, cioe' ampiamente dentro i 20 MB di L3. Quel salto non
  e' residenza in cache: e' il socket che si riempie, cioe' banda.
- Tutto il degrado **successivo** e' invece footprint: da T=8 a T=32 il braccio C passa da
  110 a 134 ms (+22%) mentre B va da 148 a 345 (+133%), a parita' di alpha. Con la tabella
  tenuta a 512 KB il join fa weak scaling quasi ideale, 89.8 -> 134.5 ms su 32 thread.
- Il braccio B e' il peggiore ovunque, ed e' coerente: paga il footprint senza incassare lo
  sconto sul load factor.

**4. Il report sottostima il proprio codice a T alto.** A T=32 l'efficienza del weak
iso-granulare e' 0.341 contro 0.272 del report (task: 0.334 contro 0.260; a T=16 task
0.627 contro 0.484). La curva A incrocia la C: e' ottimista fino a T=16 grazie al load
factor, e pessimista a T=32, dove la tabella da 16 MB per thread domina. Il 26% del report
a T=32 non e' il limite del kernel, e' il limite di quella calibrazione.

## Esecuzione

```bash
sbatch --job-name=m3_08 sbatch_one.sh 08_weak_calibration
# oppure interattivo:
salloc --partition=normal --cpus-per-task=32 --exclusive --time=00:30:00
bash 08_weak_calibration/run.sh
python3 08_weak_calibration/plot_weak_calib.py
```

Produce `results/weak_calib.csv` (end-to-end, 3 bracci x 6 T x {loop,task}, 5 rep) e
`results/alpha_probe.csv` (alpha e ns per chiave, thread singolo). Il plot stampa anche la
tabella delle efficienze nei punti dove il report vede sopra il 100%.

## Caveat

- **Il braccio C cambia due cose insieme**, ed e' voluto: la partizione e il numero di
  partizioni per thread. In A il rapporto P/T scende (4 partizioni per thread a T=32),
  in C resta 128. Entrambe fanno parte della definizione di iso-granularita', ma vuol dire
  che C non e' un braccio a variabile singola rispetto a B.
- **P alto ha un costo proprio** sulle fasi di partizionamento: dall'esp. 1, a T=16 lo
  scatter cresce da 5.47 a 6.74 ms (R) e da 11.9 a 13.7 (S) fra P=128 e P=2048. Sul join il
  guadagno e' molto maggiore della perdita, ma il totale del braccio C va letto sapendolo.
- **Il confronto con il report e' sul braccio A**, non sui numeri del CSV originale:
  `omp_ablation` e `hashjoin_omp` sono binari diversi (stesse fasi, stesso kernel). Il
  braccio A serve anche a questo, cioe' a riprodurre la curva del report con lo stesso
  binario degli altri bracci.
