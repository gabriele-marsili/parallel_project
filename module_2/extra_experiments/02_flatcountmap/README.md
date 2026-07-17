# Esp. 2: anatomia della FlatCountMap

**Obiettivo.** Motivare la FlatCountMap in dettaglio: open addressing vs unordered_map, load
factor, residenza in L3, e il padding a 64 B contro il false sharing.

**Come.** `flatmap_bench.cpp` isola il build+probe di una partizione a thread singolo
(templato sul moltiplicatore di sizing per variare il load factor). `loadfactor_bench.cpp`
cronometra il probe al variare di alpha con la tabella FISSA a 2 MB. `probe_count_bench.cpp`
CONTA gli slot visitati sulle stesse alpha e chiavi (deterministico, non misura tempo): serve a
decidere se il disaccordo con Knuth sia nei probe o nel costo per probe. `false_sharing.cpp` fa
scrivere k thread su un array condiviso `volatile` (il volatile impedisce la register
promotion), packed (8 B) vs padded (64 B).

```bash
salloc --partition=normal --cpus-per-task=32 --exclusive --time=00:10:00
bash run_flatmap.sh
python3 plot_flatmap.py
```

## Risultati (node02, single core dove non indicato)

- **impl** (distinct=20k): probe umap 18.8 ns vs FlatCountMap 4.7 ns = **4x**. I 3 load factor
  (x1/x2/x4) sono quasi identici QUI (4.9/4.7/4.6 ns) perché la tabella è sparsa (duplicati ->
  alpha basso). Il load factor conta solo vicino al 100%.
- **loadfactor** (`loadfactor_bench.cpp`): probe vs alpha -> 3.6 ns a 10%, 10.4 a 50%, 22.2 a 90%,
  42.7 a 98%. **La tabella e' FISSA a 2^17 slot x 16 B = 2 MB in tutte le righe** (L3 = 20 MB),
  quindi il degrado e' attribuibile al load factor e NON alla residenza in cache: e' il
  complemento del test `cache`, che varia la dimensione tenendo il riempimento basso.
  **Onesta':** "piatto fino al 50%" e' impreciso, da alpha 0.1 a 0.5 il costo quasi triplica
  (2.9x; 2.7x nel re-run); il tratto davvero piatto finisce verso 0.25. x2 non e' gratis, e' un
  tetto limitato.
- **perche' alpha <= 0.5** (NON perche' li' c'e' il ginocchio della curva: a 0.5 non ce n'e'
  nessuno). Vincolo strutturale: `slot_of` usa `key & mask`, che sostituisce il modulo solo se il
  numero di slot e' potenza di due -> la dimensione e' vincolata a essere una potenza di due, e
  fissato r le dimensioni ammissibili non sono un continuo (next_pow2(r), il doppio, il quadruplo).
  Il tetto si ricava in un passaggio: la tabella e' `n = next_pow2(m*r)`, quindi per costruzione
  `n >= m*r`; nel caso peggiore (zero duplicati) le chiavi inserite sono r, dunque
      alpha = r/n <= r/(m*r) = 1/m
  e il tetto e' raggiunto quando n coincide con m*r, cioe' quando r e' potenza di due (verificato
  per m=1,2,4 su r=1..29999: solo le potenze di due toccano il tetto).
  Tetti: x1 -> 1.000, x2 -> 0.500, x4 -> 0.250.
  In questa famiglia non ci sono valori intermedi. (Precisazione: un moltiplicatore NON potenza di
  due darebbe tetti intermedi, m=4/3 da' esattamente 0.75, ma senza l'identita' sopra e con
  occupazione dipendente da dove cade r.)
  Il limite di x1 non e' prestazionale: tetto 1.000 = tabella piena, e `count()` di una chiave
  ASSENTE non termina (il `while` esce solo su slot vuoto o chiave uguale, e non esiste nessuno dei
  due). Nel join la chiave assente e' lo scenario ordinario (chiave di S non in R), e r =
  1024/2048/32768 sono dimensioni plausibili. Verificato eseguendolo. Quindi fra i sizing che
  garantiscono la terminazione x2 e' quello con la MINIMA OCCUPAZIONE DI MEMORIA (2x i record,
  contro 4x di x4). Le due grandezze vanno tenute distinte: lo 0.250 di x4 e' un riempimento
  piu' basso, NON un costo piu' basso.
- **perche' NON x4**: al punto operativo non da'
  guadagno, ed e' misurato. Da `flatmap_impl.csv`, a parita' di dati: x2 -> tabella 2.0 MB,
  alpha=0.153, probe 4.681 ns; x4 -> 4.0 MB, alpha=0.076, probe 4.648 ns. Cioe' **0.7%** per il
  DOPPIO della memoria. Nella config consegnata e' anche piu' netto: NR=10M, P=512, max_key=1M ->
  19531 record e ~1953 chiavi distinte per partizione (10 record/chiave), quindi con x2 il
  riempimento OPERATIVO e' **0.030** (tabella vuota al 97%, ben dentro il tratto piatto); con x4
  sarebbe 0.015. Il vantaggio di x4 (10.4 -> 5.7 ns) esiste SOLO nel caso peggiore a zero
  duplicati, che questo carico non raggiunge: non va usato per giustificare la scelta.
- **probe contati vs Knuth** (`probe_count_bench.cpp`): il modello sbaglia i probe o il costo per
  probe? Contando gli slot visitati invece di cronometrarli: contati 1.059 / 1.505 / 5.375 /
  23.296 vs Knuth 1.056 / 1.500 / 5.500 / 25.500 ad alpha = 0.1 / 0.5 / 0.9 / 0.98. **Knuth ha
  ragione sui probe** (entro il 2-9%): il disaccordo e' tutto nel COSTO per probe. Il modello
  pertinente e' la ricerca CON successo, `0.5*(1+1/(1-alpha))` (NON `0.5*(1+1/(1-alpha)^2)`,
  che e' senza successo).
- **perche' il costo per probe NON e' costante** (plot `flatmap_lf_cost.png`): "ns per probe" e'
  una MEDIA su accessi molto diversi. Una lookup = 1 accesso iniziale in posizione casuale
  (`key & mask`) + (probe-1) accessi di SEGUITO, sequenziali e con l'indirizzo CALCOLATO
  (`(h+1)&mask`), non letto. Modello: `ns = C_primo(alpha) + (probe-1)*C_seguito`.
  Derivazione (non "a occhio"): per alpha >= 0.90 il footprint e' fermo (2035->2046 KB) quindi
  C_primo e' costante e SI CANCELLA nella differenza fra due punti di quella regione ->
  `C_seguito = (ns(0.98)-ns(0.90)) / (probe(0.98)-probe(0.90)) = 20.576/17.921 = 1.148 ns`
  (pendenza misurata dove l'altro termine e' fermo, non dipende dal modello). Poi per differenza
  punto per punto: `C_primo(alpha) = ns - (probe-1)*C_seguito`, es. alpha=0.50 -> 10.439 -
  0.505*1.148 = 9.86 ns. Risultato:

  | alpha | C_primo | KB toccati |
  |-------|---------|------------|
  | 0.10  | 3.53 ns | 680        |
  | 0.50  | 9.86 ns | 1819       |
  | 0.90  | 17.15 ns| 2035       |
  | 0.95  | 17.46 ns| 2042       |
  | 0.98  | 17.15 ns| 2046       |

  C_primo cresce col footprint e SATURA a ~17 ns esattamente dove satura il footprint: ordine di
  grandezza di una latenza L3. Da qui: ad alpha basso i probe sono ~1, quindi la lookup costa
  quasi solo C_primo che sta crescendo -> probe +42%, tempo +190% (Knuth sotto-predice, non
  modella il footprint). Ad alpha alto C_primo e' saturo e gli accessi in piu' costano 1.15 ns
  invece di 17 -> probe +333%, tempo +93% (Knuth sovra-predice). Stessa decomposizione = perche'
  batte l'unordered_map: la' OGNI passo e' un salto di puntatore, cioe' paga C_primo ogni volta.
  *Limite:* decomposizione a 2 costi, C_seguito stimato sulla coda, attribuzione di C_primo ai
  livelli di cache per ordine di grandezza e non con i contatori. Controllo: alpha=0.95 NON entra
  nella stima e C_primo vi cade entro il 2% degli altri due punti saturi.
- **riproducibilita'** (VERIFICATA, re-run su node02 il 2026-07-16, job slurm 706972):
  - `probe_count.csv` e' aritmetica intera deterministica (niente clock, niente thread) -> byte
    IDENTICI (stesso MD5) su TRE macchine: Mac (clang, arm64), node09 e node02 (GCC 12.2, x86_64),
    e a -O0/-O2/-O3 -march=native. Se cambia, e' un bug, non rumore di misura.
  - `loadfactor.csv` sono ns wall-clock best-of-9, specifici di node02. Il re-run li riproduce
    entro l'1% su 7 alpha su 10; +3-7% ad alpha 0.4/0.5/0.6 e +11.9% ad alpha=0.10 (la misura piu'
    piccola, ~4 ns, la piu' sensibile al rumore). Dati grezzi: `results/loadfactor_rerun_node02.csv`.
  - Le conclusioni non dipendono dallo scarto: costo marginale 1.15 ns (CSV) vs 1.16 (re-run);
    ns/probe 1.8-7.4 vs 1.8-7.6, massimo ad alpha=0.6 in entrambi. Il CSV committato resta
    l'artefatto di riferimento: il re-run lo conferma, non lo sostituisce.
- **cache**: probe FlatCountMap 3.8 ns (64 KB, L2) -> 5.8 (16 MB, L3) -> 14.4 ns (1 GB, DRAM).
  Il divario con unordered_map si allarga fuori da L3 (flat 14 vs umap 77 ns): fuori cache ogni
  accesso e' un miss a ~100 ns; umap fa pointer chasing (miss casuali, PIU' per lookup: bucket poi
  nodi sparsi), flat ne paga ~UNO solo. La motivazione NON e' che il prefetcher
  anticipa lo scorrimento, e' che qui lo scorrimento quasi non avviene. Questo test gira ad
  alpha ~0.25 in tutte le righe (sizing x2 + duplicati di R: distinct/slot = 0.244-0.250), dove i
  probe contati sono 1.167 -> in media 0.167 probe extra -> almeno l'83% delle lookup tocca UN solo
  slot. Motiva "fittare in L3".
- **false sharing** (stesso lavoro): packed 123 ms (1t) -> 349 ms (32t); padded piatto ~130 ms.
  Fino a **2.2x** di penalità a 16-32 thread.

## Lettura (come difenderlo)

1. Open addressing contiguo (16 B/slot, 4 per cache line) batte separate chaining: niente
   allocazioni per-nodo, niente pointer chasing.
2. **Onestà 1:** nel join consegnato l'accumulo è thread-local con una sola scrittura finale,
   quindi il padding è difensivo (il false sharing è reale ma non è la hot loop).
3. **Onestà 2:** il campo `_p` dello Slot è ridondante con l'allineamento naturale (lo Slot
   sarebbe 16 B comunque); rende solo esplicito l'intento.
