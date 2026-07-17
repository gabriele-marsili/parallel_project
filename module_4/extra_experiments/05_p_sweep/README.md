# Esp. 5: sensibilità al numero di partizioni P

**Obiettivo.** Il report usa P=256, i moduli 2 e 3 usavano 128. Verificare che P=256 non penalizzi
nessuna configurazione e stabilire quale fase risponda a P. Vincolo strutturale: P deve essere
multiplo del rank count, quindi il pure MPI a 128 rank ammette solo P >= 128.

**Come.** `threadlevel_bench` per l'ibrido (4 rank su 4 nodi, 32 thread) e `mpi_remap` per il pure
MPI (128 rank, mapping mod, fasi locali sequenziali), con P in {128, 256, 512, 1024} su uniforme e
skewed.

```bash
bash ../build.sh
sbatch run.sbatch            # 4 nodi, 25 min
python3 plot_p_sweep.py
```

## Risultati (NR=50M, NS=100M, mediana di 3 rep)

Ibrido, 4 rank x 32 thread:

| P | totale unif. [s] | join unif. [ms] | totale skew [s] | join skew [ms] |
|---|---|---|---|---|
| 128  | 0.516 | 73.5 | 0.870 | 239 |
| 256  | 0.516 | 63.8 | 0.953 | 231 |
| 512  | 0.504 | 54.2 | 0.868 | 235 |
| 1024 | 0.499 | 39.5 | 0.819 | 222 |

Pure MPI, 128 rank:

| P | totale unif. [s] | payload unif. [ms] | join unif. [ms] | totale skew [s] | payload skew [ms] |
|---|---|---|---|---|---|
| 128  | 1.124 | 1006 | 68.3 | 1.092 | 1064 |
| 256  | 0.880 | 768  | 62.0 | 1.111 | 652  |
| 512  | 0.865 | 753  | 48.9 | 1.290 | 930  |
| 1024 | 0.714 | 646  | 34.5 | 1.503 | 1100 |

## Lettura

1. **Il join scala con P, il totale dell'ibrido no** (join da 73.5 a 39.5 ms, totale da 0.516 a
   0.499 s). Alzare P riduce la cardinalità media di ogni partizione, quindi la hash table del
   build side rientra nei livelli di cache alti e il probe smette di andare in DRAM: è lo stesso
   effetto misurato nei moduli 2 e 3. Sul totale dell'ibrido non si vede perché il join pesa circa
   il 13% e la voce dominante è lo scambio, che P non tocca.
2. **Su pure MPI uniforme P=1024 batte P=128** (0.714 contro 1.124 s). Qui il guadagno è doppio:
   il join va in cache come sopra, e il payload cala da 1006 a 646 ms perché con più partizioni
   per rank i send count si distribuiscono in modo più regolare fra i destinatari.
3. **Su skewed l'ordine si inverte oltre P=256** (1.503 s a P=1024 contro 1.092 a P=128). Il join
   resta piatto a circa 250 ms, coerente col pavimento dell'esp. 4, mentre il payload risale a
   1100 ms: aumentando P le partizioni hot restano poche e pesanti mentre le altre si assottigliano,
   quindi i blocchi di `MPI_Alltoallv` diventano più disomogenei ed è il round più lento a dettare
   il tempo. È lo stesso meccanismo che nell'esp. 4 penalizza il greedy a 128 rank.
4. **P=256 è un compromesso, non l'ottimo di nessuna configurazione.** È l'unico valore che
   soddisfa il vincolo `P multiplo di 256 rank` per il pure MPI a 8 nodi e non penalizza né
   uniforme né skewed. Un P per configurazione avrebbe dato tempi migliori a scapito
   dell'omogeneità dei confronti fra configurazioni.
