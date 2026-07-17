# Esp. 1: anatomia di MPI_Alltoallv

**Obiettivo.** Isolare il dip a 4 nodi / 128 rank del report fuori dalla pipeline, separando il
contributo del rank count, del node count e del volume per messaggio, e verificare se la
selezione dell'algoritmo fatta dalla libreria lo spiega.

**Come.** `common/alltoallv_bench.cpp` esegue il solo scambio, su buffer sintetici di `uint64_t`
(lo stesso `Record` da 8 byte della pipeline), con warm-up più 10 ripetizioni misurate, barrier
prima del timer e riduzione max sui rank. Quattro misure: rank sweep a volume globale fisso
(150M record, cioè NR+NS del report), rank sweep a volume per rank fisso (6M record), algoritmo
forzato via MCA a 128 rank su 4 e 8 nodi, sweep del volume a 128 rank.

```bash
bash ../build.sh
sbatch run.sbatch            # 8 nodi, 25 min
python3 plot_alltoallv.py
```

## Risultati

Rank sweep a volume globale fisso, 150M record su 8 nodi (mediana di 10 rep):

| rank | MB/rank | mediana [s] | min-max [s] |
|---|---|---|---|
| 8   | 150.0 | 0.134 | 0.133-0.137 |
| 16  | 75.0  | 0.126 | 0.123-0.301 |
| 32  | 37.5  | 0.288 | 0.122-0.306 |
| 64  | 18.75 | 0.124 | 0.123-0.127 |
| 128 | 9.38  | 0.429 | 0.375-0.612 |
| 256 | 4.69  | 0.338 | 0.122-0.747 |

Algoritmo forzato (`coll_tuned_use_dynamic_rules=1`, `coll_tuned_alltoallv_algorithm`), 128 rank,
9.38 MB per rank:

| nodi | algoritmo | mediana [s] | min-max [s] |
|---|---|---|---|
| 4 | default          | 0.516 | 0.490-0.703 |
| 4 | basic linear (1) | 0.527 | 0.477-0.696 |
| 4 | pairwise (2)     | 0.300 | 0.199-0.404 |
| 8 | default          | 0.400 | 0.383-0.638 |
| 8 | basic linear (1) | 0.418 | 0.385-0.446 |
| 8 | pairwise (2)     | 0.128 | 0.124-0.128 |

Volume sweep, 128 rank su 4 nodi:

| MB/rank | mediana [s] | min-max [s] | max/min |
|---|---|---|---|
| 1.17  | 0.312 | 0.280-1.273 | 4.6x |
| 2.34  | 0.345 | 0.306-2.553 | 8.3x |
| 4.69  | 0.395 | 0.361-0.452 | 1.2x |
| 9.38  | 0.526 | 0.507-0.544 | 1.1x |
| 18.75 | 0.921 | 0.708-1.001 | 1.4x |
| 37.50 | 1.386 | 1.296-1.834 | 1.4x |

## Lettura

1. **Il costo non è monotono nel volume per rank.** A volume globale fisso 64 rank scambiano in
   0.124 s e 128 rank in 0.429 s, con metà dei dati ciascuno. Il tempo di un all-to-all non
   dipende dal solo volume: ogni rank apre R-1 flussi verso destinatari distinti, quindi salendo
   di rank il messaggio per coppia si rimpicciolisce mentre cresce il numero di flussi concorrenti
   sulla stessa scheda di rete. Il fenomeno si riproduce con buffer sintetici, senza join e senza
   generatore: non dipende dalla pipeline.
2. **Pairwise contro la selezione automatica**, 0.128 contro 0.400 s a 8 nodi, con spread di 4 ms
   contro 255. Pairwise esegue R-1 round e al passo k il rank r invia a `(r+k) mod R` e riceve da
   `(r-k) mod R`: ogni round è una permutazione, quindi ogni destinatario ha un solo mittente e il
   traffico per round è bilanciato. Basic linear posta le R-1 send insieme, più mittenti
   convergono sullo stesso destinatario e la coda della NIC ricevente si satura (incast): il link
   resta inutilizzato mentre TCP attende. Il default coincide con basic linear (0.516 contro 0.527
   a 4 nodi), e l'esp. 7 lo verifica direttamente forzando gli algoritmi a ogni rank count: a 128
   rank la libreria esegue basic linear, a 64 e 256 esegue pairwise.
3. **La soglia sul volume è a circa 4 MB per rank**, dove max/min passa da 8.3x a 1.2x. È il
   corollario del punto 2: l'incast pesa finché il trasferimento di un singolo messaggio è breve
   rispetto all'attesa in coda sul ricevente. Sopra la soglia domina il termine `beta*m` del
   modello di Hockney, che è deterministico, e le ripetizioni si stabilizzano.
4. **Il sintetico riproduce il pattern, non la magnitudine**: 0.52 s contro 1.35 s della pipeline
   a 4 nodi e 128 rank, a parità di volume e di record size. *Ipotesi non isolata dalla misura*:
   qui warm-up e ripetizioni riusano lo stesso buffer, già faultato, mentre la pipeline esegue una
   sola Alltoallv su buffer appena scritti dallo scatter e paga i page fault dentro la regione
   cronometrata.

## Corollario: la scelta di Alltoallv contro Isend/Irecv con overlap

Il report motiva la collettiva bloccante (sez. 2.3) e lascia l'overlap come lavoro futuro. La
variante non bloccante non è implementata; qui si misurano le due quantità su cui la decisione si
regge.

**Costo dello scheduling che si perderebbe.** Spezzare la collettiva in scambi per partizione
richiede di ricostruire a mano l'ordine degli scambi. L'algo forcing lo quantifica a parità di
dati e hardware: 1.7x a 4 nodi, 3.1x a 8 nodi.

**Tetto dell'overlap.** Il risparmio non può eccedere `min(T_scambio, W_post)`, dove W_post è il
lavoro post-scambio sovrapponibile (`histogram_post + scatter_post + join_local`). Da
`results/cluster/breakdown.csv` a 4 nodi:

| configurazione | W_post | payload | wall | tetto |
|---|---|---|---|---|
| pure MPI uniforme | 0.093 s | 1.145 s | 1.305 s | 7.1% |
| ibrido uniforme   | 0.116 s | 0.269 s | 0.612 s | 19.0% |
| pure MPI skewed   | 0.686 s | 0.755 s | 1.532 s | 44.8% |
| ibrido skewed     | 0.299 s | 0.557 s | 1.175 s | 25.5% |

Il 7-19% del report copre il solo carico uniforme. Sotto skew il tetto sale perché i rank che
ricevono le partizioni hot rifanno histogram e scatter su circa 20x i record medi (esp. 4), quindi
W_post cresce fino a diventare paragonabile allo scambio.

**Il tetto non cala applicando pairwise.** W_post non dipende dall'algoritmo della collettiva:
restano 93 ms. In percentuale il tetto sale, perché pairwise riduce il denominatore. *Proiezione*
del rapporto 1.7x del sintetico sulla pipeline, non misurata direttamente:

```
pure MPI uniforme, 4 nodi, oggi:    93 ms / 1305 ms = 7.1%
con pairwise (payload 1145 -> 666): 93 ms /  826 ms = 11.3%
```

**Conclusione.** Il confronto è fra ritorni: pairwise vale circa 480 ms e richiede una variabile
d'ambiente, l'overlap al massimo 93 ms e richiede di riscrivere la redistribuzione rifacendo uno
scheduling che vale fino a 3.1x. I due non competono per lo stesso tempo: dopo pairwise l'overlap
vale ancora 93 ms assoluti. Sotto skew il margine sarebbe più ampio (44.8%), e questo la sez. 2.3
del report non lo dice.

**Raccordo con l'esp. 2.** Pairwise rimuove la penalità dello scheduling, non quella della banda
per nodo. Applicando il modello a volume per NIC, `beta * (rpn * V_rank * quota_off_node)`:

| | volume off-node per nodo | predetto | pairwise misurato |
|---|---|---|---|
| 8 nodi (16 rpn) | 16 x 9.375 MB x 7/8 = 131 MB | 114 ms | 128 ms |
| 4 nodi (32 rpn) | 32 x 9.375 MB x 3/4 = 225 MB | 196 ms | 300 ms |

Con pairwise la collettiva raggiunge il tetto di banda della scheda di rete (+12% a 8 nodi, +53%
a 4). A 8 nodi i 128 rank tornano al livello dei 64 (0.128 contro 0.124 s) perché il tetto è
basso; a 4 nodi il tetto stesso è alto, dato che 32 processi per nodo condividono un link, e
nessuna scelta di algoritmo può scendere sotto il termine di banda.
