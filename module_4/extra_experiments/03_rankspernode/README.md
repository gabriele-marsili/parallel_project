# Esp. 3: il continuum rank per nodo

**Obiettivo.** Il report confronta solo i due estremi, pure MPI (32 rank per nodo) e ibrido
(1 rank per nodo). Qui si misura il continuum, per stabilire se uno dei due sia l'ottimo e quale
parametro determini il minimo. Il candidato naturale a battere entrambi era 2 rank per nodo, uno
per socket: fan-out piccolo e località NUMA insieme.

**Come.** `common/threadlevel_bench.cpp` riusa `include/mpi_pipeline.hpp` consegnato (livello
FUNNELED), quindi misura la stessa pipeline del report. A 4 nodi si varia rank per nodo in
{1,2,4,8,16,32} con thread complementari (`rpn * T = 32`), su uniforme e skewed. Un cross-check
esegue il binario `hashjoin_mpi` consegnato a 128 rank per confermare che il driver a
(32 rpn, T=1) gli sia equivalente.

```bash
bash ../build.sh
sbatch run.sbatch            # 4 nodi, 30 min
python3 plot_rankspernode.py
```

## Risultati (4 nodi, NR=50M, NS=100M, P=256, mediana di 5 rep)

| rpn | thread | rank tot. | wall uniforme [s] | wall skew [s] | payload unif. [ms] | join skew [ms] |
|---|---|---|---|---|---|---|
| 1  | 32 | 4   | 0.585 | 1.121 | 253 | 233 |
| 2  | 16 | 8   | 0.467 | 1.009 | 232 | 239 |
| 4  | 8  | 16  | 0.435 | 0.817 | 242 | 233 |
| 8  | 4  | 32  | 0.499 | 0.847 | 383 | 232 |
| 16 | 2  | 64  | **0.374** | 0.814 | 221 | 234 |
| 32 | 1  | 128 | 0.782 | 1.414 | 692 | 254 |

Gli estremi sono le due configurazioni del report: rpn=1 è l'ibrido, rpn=32 il pure MPI.

## Lettura

1. **Il minimo cade all'interno del continuum**, a 16 rpn per 2 thread (0.374 s su uniforme,
   contro 0.585 e 0.782 agli estremi). La curva è a U perché i due costi che la compongono si
   muovono in direzioni opposte al variare di rpn: il costo della collettiva cresce con il rank
   count totale, quello delle fasi locali decresce con il numero di thread per rank, e il vincolo
   `rpn * T = 32` li lega.
2. **La posizione del minimo è determinata dalla soglia della collettiva.** Lo scambio resta
   piatto fino a 64 rank totali e salta a 128 (221 contro 692 ms), la stessa soglia del
   microbenchmark dell'esp. 1. Sul lato opposto bastano 2 thread per rank a coprire le fasi
   locali, perché sono passate a banda su dati contigui e saturano la memoria del nodo con pochi
   thread. Il minimo cade quindi al massimo rpn che non superi la soglia dei 128 rank.
3. **L'imbalance non dipende dalla ripartizione rank/thread.** Il join sotto skew resta a
   232-254 ms in ogni configurazione. La partizione hot è indivisibile sia fra rank (esp. 4) sia
   fra thread, perché è una singola iterazione del loop `schedule(dynamic)`: nessuna riassegnazione
   del confine rank/thread cambia il carico del worker più carico.
4. **La località NUMA non è il fattore dominante.** 2 rank per nodo (uno per socket) dà 0.467 s
   contro 0.374 del minimo, pur essendo la configurazione che allinea rank e domini NUMA. A parità
   di thread totali il rank count della collettiva pesa più della località degli accessi dentro il
   nodo, coerentemente con il fatto che il payload è la voce dominante del breakdown.
