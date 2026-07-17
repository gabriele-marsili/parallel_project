# Esp. 2: i coefficienti di Hockney, misurati

**Obiettivo.** Il report usa il modello `(R-1)*alpha + beta*V` per spiegare il weak scaling senza
aver misurato alpha e beta. Qui si misurano e si verifica se il modello regge sui dati.

**Come.** `common/pingpong_bench.cpp`: ping-pong fra due rank, da 8 B a 32 MB, inter-nodo (2 nodi,
1 rank per nodo) e intra-nodo (2 rank sullo stesso nodo). `plot_hockney.py` stima alpha come media
del tempo one-way sui messaggi fino a 64 B e beta come mediana della banda sopra i 4 MB, poi
confronta il weak scaling dell'Alltoallv (dati dell'esp. 1) contro due modelli.

```bash
bash ../build.sh
sbatch run.sbatch            # 2 nodi, 15 min
python3 plot_hockney.py
```

## 2.1 I coefficienti misurati

![](plots/pingpong.png)

| | alpha | banda asintotica |
|---|---|---|
| inter-nodo | 22.4 us | 1.15 GB/s (10 GbE) |
| intra-nodo | 0.58 us | 4.4 GB/s |

I due coefficienti si estraggono dai regimi asintotici di `T = alpha + beta*m`: alpha dalla media
del tempo one-way sotto i 64 B, dove `beta*m` è trascurabile; beta dall'inverso della banda sopra
i 4 MB, dove domina il trasferimento. Il rapporto fra le due bande (circa 4x) e fra le due latenze
(circa 37x) misura quanto lo scambio intra-nodo sia più economico di quello di rete.

## 2.2 Verifica del modello sul weak scaling

![](plots/hockney_check.png)

Verifica sul weak scaling dell'Alltoallv (8 nodi, 48 MB inviati per rank):

| rank | misurato [s] | modello startup [s] | modello volume per NIC [s] |
|---|---|---|---|
| 8   | 0.045 | 0.042 | 0.036 |
| 16  | 0.083 | 0.042 | 0.073 |
| 32  | 0.277 | 0.043 | 0.146 |
| 64  | 0.696 | 0.043 | 0.291 |
| 128 | 1.046 | 0.045 | 0.582 |
| 256 | 3.116 | 0.047 | 1.164 |

Modello startup: `(R-1)*alpha + beta*V_rank`, quello citato dal report. Modello volume per NIC:
`beta * (rank_per_nodo * V_rank * quota_off_node)`, cioè il traffico che ogni scheda di rete deve
iniettare. Il *volume off-node* è la parte del traffico di un nodo che esce davvero sulla rete: dei
R destinatari di un rank, `rank_per_nodo - 1` stanno sullo stesso nodo e passano da shared memory,
i restanti `R - rank_per_nodo` sono remoti. Da cui `quota_off_node = (R - rank_per_nodo) / R`.

1. **Il termine di startup non spiega la crescita.** A 256 rank vale `255 * 22.4 us = 5.6 ms`,
   contro 3.116 s misurati. La ragione è strutturale: in weak scaling il volume per rank è fisso,
   quindi il termine `beta*V_rank` è costante per costruzione (41.7 ms) e l'unico termine che
   varia è quello lineare in R, che con alpha misurato resta nell'ordine dei millisecondi. Il
   modello è piatto a 42-47 ms su tutto il range.
2. **Il termine che cresce è il volume per scheda di rete.** Con 32 rank per nodo ogni nodo
   inietta `32 * 48 MB * 7/8 = 1.3 GB` su un link da 1.15 GB/s. Il modello NIC segue la forma
   della curva entro un fattore 1.1-2.7 e resta sistematicamente sotto il misurato: la differenza
   è il costo di contesa e incast, che il modello non rappresenta perché assume il link occupato
   in modo ideale.
3. **La conclusione del report resta valida, il meccanismo cambia.** Ridurre i partecipanti alla
   collettiva è la scelta giusta, ma non per il risparmio di startup: a parità di payload globale
   l'ibrido tiene un solo processo per nodo a iniettare traffico, mentre pure MPI ne mette 32 in
   contesa sullo stesso link.
4. **A basso rank count i due modelli non sono distinguibili** (42 contro 36 ms a 8 rank, misurato
   45), perché entrambi sono dominati dal termine di banda. La discriminazione fra i due è
   possibile solo ad alto rank count, che è il regime in cui il report invoca il modello.

## 2.3 Prova per esclusione: nessun algoritmo salva il weak scaling

**Obiettivo.** L'esp. 7 mostra che nello strong il collo è l'algoritmo della collettiva (a 128 rank
la libreria sceglie basic linear e il link resta al 15%). Se valesse anche nel weak, la tesi di
questa sezione (il collo è la banda per NIC) sarebbe sbagliata.

**Come.** `run_weak_algo.sbatch` ripete il regime weak del report (32 rank per nodo, nodi 1/2/4/8,
48 MB per rank) forzando ogni algoritmo. Attenzione al setup: nel weak del report `rank_per_nodo`
resta **costante** a 32 e a crescere è la quota off-node (da 0 a 0.875); un rank sweep a nodi fissi
misurerebbe un weak diverso, in cui a crescere è `rank_per_nodo`, e i suoi punti intermedi non
esistono nel report. Un job per numero di nodi: variare `--nodes` fra srun della stessa allocazione
fa morire lo stato PMIx di Open MPI 5.0.3.

```bash
for N in 1 2 4 8; do sbatch --nodes=$N 02_hockney/run_weak_algo.sbatch; done
python3 plot_hockney.py                   # figura 3: weak_algo.png
```

![](plots/weak_algo.png)

| nodi | rank | quota off-node | default | basic linear | pairwise | tetto per NIC | startup |
|---|---|---|---|---|---|---|---|
| 1 | 32  | 0.000 | 0.113 | **0.113** | 0.090 | 0 (intra-nodo) | 0.042 |
| 2 | 64  | 0.500 | 0.671 | 1.039 | **0.670** | **0.668** | 0.043 |
| 4 | 128 | 0.750 | 1.645 | **1.583** | 2.383 | 1.002 | 0.045 |
| 8 | 256 | 0.875 | 3.212 | 2.361 | **3.021** | 1.169 | 0.047 |

1. **Il modello per NIC azzecca il punto a 2 nodi**: predice 668 ms, misurato 671, errore dello
   0.4%, con il link saturo al 100%. Gli startup lì valgono 43 ms. Le distribuzioni sono separate
   in modo netto (default 0.670-0.673, pairwise 0.669-0.673, basic linear 0.911-1.105, CV 0%),
   quindi l'inferenza sulla scelta non dipende dal rumore.
2. **La libreria sceglie sul rank count e ignora la taglia del messaggio.** Confrontando con lo
   strong (esp. 7), la scelta è identica a parità di rank (32 linear, 64 pairwise, 128 linear,
   256 pairwise) benché le taglie siano tutt'altre. Ed è il difetto, perché l'algoritmo ottimale
   dipende dalla taglia:

   ```
   128 rank, messaggi da  73 KB (strong): pairwise 3.1x MEGLIO -> sceglie linear: SBAGLIA
   128 rank, messaggi da 375 KB (weak):   pairwise 1.5x PEGGIO -> sceglie linear: INDOVINA
   ```

   Stessa scelta, esiti opposti: nel weak a 4 nodi la euristica indovina per caso. A 8 nodi
   sbaglia di nuovo, scegliendo pairwise mentre basic linear sarebbe 1.36x meglio (2.361 contro
   3.212).
3. **Ma l'algoritmo non è il collo, e questa è la prova.** Efficienza dell'Alltoallv
   (`t_1nodo / t_N`):

   ```
   tetto imposto dalla fisica:      0.113 / 1.169 = 0.097
   default:                         0.113 / 3.212 = 0.035
   col miglior algoritmo (linear):  0.113 / 2.361 = 0.048
   ```

   Il crollo da 1.0 a 0.097 è strutturale: è il gradino fra 1 nodo (zero traffico di rete) e 2 nodi
   (768 MB per NIC), più la quota off-node che sale da 0 a 0.875. Il miglior algoritmo possibile
   recupera solo da 0.035 a 0.048, un terzo della strada verso 0.097, e a 8 nodi resta comunque 2x
   sopra il tetto per contesa e incast. Nello strong forzare pairwise portava il link dal 15%
   all'89%: qui non c'è nulla di paragonabile da recuperare.
4. **Il modello startup resta piatto a 42-47 ms** contro tutti e tre gli algoritmi, mentre il
   misurato arriva a 3.2 s. Nessuna scelta di algoritmo cambia il numero di startup, quindi se
   fossero loro il collo le tre curve sarebbero indistinguibili e piatte. Non lo sono.
5. **Perché l'ibrido tiene 0.58 di efficienza contro 0.21.** Il volume off-node per nodo è
   `rank_per_nodo * V_rank * quota_off_node`: in weak scaling `V_rank` è fisso e `quota_off_node`
   satura verso 1, quindi l'unico fattore che resta è `rank_per_nodo`. Pure MPI lo tiene a 32
   (1344 MB per nodo a 8 nodi, su un link da 1.15 GB/s), l'ibrido a 1 (42 MB). Il rapporto 32x fra
   le due configurazioni è esattamente il rapporto dei rank per nodo, e non c'è algoritmo che lo
   tocchi.

*Nota sui dati.* `weak_algo_ranksweep_8nodi.csv` conserva un primo tentativo con nodi fissi e rank
variabili: misura un weak diverso (rpn da 1 a 32) ed è troppo rumoroso per inferire la scelta
(a 64 rank il default alterna fra 0.313 e 0.932 senza struttura). Tenuto come controprova del
punto 3, non usato per il resto.
