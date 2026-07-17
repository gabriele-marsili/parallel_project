# Esp. 4: imbalance sotto skew, remapping e metodologia dei barrier

**Obiettivo.** Due cose. (a) Quantificare il pavimento che il report cita senza numero: il volume
ricevuto per rank, max contro media, e stabilire se un remapping consapevole dei pesi recuperi
qualcosa senza arrivare alla replicazione del build side. (b) Verificare la metodologia di timing,
rieseguendo le stesse run senza i `MPI_Barrier` davanti alle collettive.

**Come.** `common/mpi_remap.cpp`: pipeline pura MPI autonoma con mappa partizione-rank parametrica
(`-remap mod|greedy`) e barrier disattivabili (`-barrier 0`). Il greedy costruisce l'istogramma
globale dei pesi per partizione (un Allreduce su P contatori, disponibile prima dello scambio),
ordina per peso decrescente e assegna ogni partizione al rank più scarico, cioè LPT applicato ai
rank. Il piano è deterministico e identico su tutti i rank, quindi non richiede comunicazione
oltre all'Allreduce iniziale.

```bash
bash ../build.sh
sbatch run.sbatch            # 4 nodi, 25 min
python3 plot_remap.py
```

## Risultati (NR=50M, NS=100M, P=256, mediana di 5 rep)

Mapping mod contro greedy:

| workload | rank | remap | totale [s] | payload [ms] | join [ms] | recv_max | recv_mean | piano [ms] |
|---|---|---|---|---|---|---|---|---|
| skew    | 8   | mod    | 1.697 | 417 | 717 | 58.1M | 18.8M | 0 |
| skew    | 8   | greedy | 0.884 | 393 | 256 | 22.6M | 18.8M | 3.4 |
| skew    | 128 | mod    | 1.084 | 644 | 253 | 22.6M | 1.17M | 0 |
| skew    | 128 | greedy | 1.900 | 1502 | 355 | 22.6M | 1.17M | 2.5 |
| uniform | 128 | mod    | 0.891 | 781 | 60  | 1.17M | 1.17M | 0 |
| uniform | 128 | greedy | 0.859 | 751 | 61  | 1.17M | 1.17M | 2.2 |

Barrier on/off, mapping mod, 128 rank:

| workload | barrier | totale [s] | comm_sizes [ms] | payload [ms] | reduce_final [ms] |
|---|---|---|---|---|---|
| uniform | 1 | 0.854 | 0.92 | 746 | 0.24 |
| uniform | 0 | 0.814 | 4.14 | 744 | 207.8 |
| skew    | 1 | 1.080 | 0.97 | 634 | 0.29 |
| skew    | 0 | 1.129 | 6.51 | 654 | 433.0 |

## Lettura

1. **Il pavimento vale 19x** (recv_max 22.6M contro recv_mean 1.17M, 128 rank sotto skew). Il
   massimo coincide con la partizione hot di S, che da sola contiene `rho/h + (1-rho)/P` circa il
   22.5% di 100M record. Poiché l'unità di assegnazione è la partizione, nessuna mappa
   partizione-rank può produrre un massimo inferiore al peso della partizione più pesante: è un
   lower bound sul makespan, l'analogo distribuito del bound `max(job_i)` che vale per LPT su
   macchine parallele. Il greedy infatti ottiene lo stesso recv_max del mod.
2. **Il remapping agisce sulle collisioni, non sul pavimento.** A 8 rank il mod assegna più
   partizioni hot allo stesso rank (58.1M ricevuti, circa due hot di S più una di R) perché
   `pid mod R` non conosce i pesi: il greedy le separa, raggiunge il pavimento e porta il totale da
   1.697 a 0.884 s, con il join del rank più carico da 717 a 256 ms. Il costo del piano è 2-3 ms,
   trascurabile rispetto al recupero. È il regime in cui LPT dà il suo bound di Graham: con job
   pesanti separati, il makespan si avvicina al massimo fra media e job più grande.
3. **A 128 rank il greedy peggiora il totale del 75%** (1.084 a 1.900 s). La collettiva passa da
   644 a 1502 ms: il mod produce send count quasi uniformi (ogni rank riceve `P/R` partizioni
   contigue nell'ordine dest-major), mentre il greedy assegna partizioni di peso molto diverso,
   e i blocchi di `MPI_Alltoallv` diventano irregolari. Un all-to-all con blocchi sbilanciati
   degrada perché il round più lento detta il passo. Il join peggiora del 40% (253 a 355 ms) a
   parità di recv_max: *meccanismo non isolato dalla misura*, va riportato come dato. Su uniforme
   le due mappe coincidono entro il rumore (0.859 contro 0.891), come atteso quando i pesi sono
   già uniformi e il greedy non ha nulla da riordinare.
4. **La metodologia dei barrier regge, per una ragione diversa da quella attesa.** Senza barrier
   il totale varia entro il 5% in entrambi i sensi (0.814 contro 0.854 su uniforme, 1.129 contro
   1.080 su skew), ma il tempo di attesa non scompare: si concentra sull'ultima sincronizzazione
   globale, cioè `reduce_final`, che passa da 0.24 a 207.8 ms su uniforme e da 0.29 a 433.0 ms su
   skew. `comm_sizes` ne assorbe 3-6 ms. Il motivo è che `MPI_Allreduce` non può completare finché
   ogni rank non vi è entrato, quindi accumula lo skew di arrivo prodotto dalle fasi precedenti.
   La difesa corretta è quindi che i barrier attribuiscono l'attesa alla fase che la genera invece
   di scaricarla sulla collettiva finale, e che il loro effetto sul totale è marginale.
