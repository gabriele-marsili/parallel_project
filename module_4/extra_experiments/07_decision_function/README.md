# Esp. 7: quale algoritmo sceglie la libreria, e dove sbaglia

**Obiettivo.** Chiudere la domanda lasciata aperta dall'esp. 1, che ha forzato gli algoritmi solo a
128 rank: il default degrada perché la decision function cambia scelta salendo di rank, oppure
perché l'algoritmo che sceglie collassa sopra una soglia di concorrenza? La risposta cambia cosa si
può affermare sul dip dello strong scaling.

**Come.** Stesso microbenchmark dell'esp. 1 (`common/alltoallv_bench.cpp`), ma forzando ogni
algoritmo a **ogni** rank count e confrontando le curve: dove il default coincide con un algoritmo
forzato, è quello che la libreria sta eseguendo. Due sweep:

- `run.sbatch` varia i rank a 8 nodi fissi, per isolare il rank count dal node count. La config
  `dyn_ignore` (`use_dynamic_rules=1` con `algorithm=0`) isola l'effetto della sola attivazione
  delle dynamic rules da quello della scelta dell'algoritmo.
- `run_curve.sbatch` misura i **quattro punti della curva di strong scaling del report**
  (32 rank per nodo: 1/32, 2/64, 4/128, 8/256), dove nodi e rank variano insieme.

Il verbose interno di `tuned` non è utilizzabile: questa build riporta `Internal debug support: no`,
quindi le stampe di selezione sono compilate via. L'inferenza dai tempi è comunque più forte, perché
misura il comportamento invece di leggere una dichiarazione.

```bash
bash ../build.sh
sbatch 07_decision_function/run.sbatch        # da ~/module_4/extra_experiments, 8 nodi
sbatch 07_decision_function/run_curve.sbatch
python3 plot_decision.py
```

## Risultati

Sweep del rank count a 8 nodi (mediane di 10 rep; tetto di banda 114 ms):

| rank | msg/coppia | default | dyn_ignore | basic linear | pairwise | chi esegue |
|---|---|---|---|---|---|---|
| 8   | 18.7 MB | 0.133 | 0.134 | **0.133** | 0.173 | linear |
| 16  | 4.7 MB  | 0.126 | 0.126 | **0.126** | 0.157 | linear |
| 32  | 1.17 MB | 0.290 | 0.292 | **0.277** | 0.230 | linear |
| 64  | 293 KB  | 0.123 | 0.123 | 0.333 | **0.123** | **pairwise** |
| 128 | 73 KB   | 0.398 | 0.400 | **0.402** | 0.128 | **linear** |
| 256 | 18 KB   | 0.127 | 0.336 | 0.553 | **0.230** | pairwise |

I quattro punti della curva del report (32 rank per nodo):

| nodi | rank | default | basic linear | pairwise | tetto | chi esegue | utilizzo del link |
|---|---|---|---|---|---|---|---|
| 1 | 32  | 0.088 | **0.088** | 0.070 | n/a (intra-nodo) | linear | --- |
| 2 | 64  | 0.262 | 0.265 (4/10 collassano) | **0.262** | 261 ms | **pairwise** | 100% |
| 4 | 128 | 0.531 | **0.530** | 0.397 (min 0.201) | 196 ms | **linear** | 37% |
| 8 | 256 | 0.230 | 0.550 | **0.336** | 114 ms | **pairwise** | 50% (100% nelle run buone) |

## Lettura

1. **Le dynamic rules di per sé non cambiano nulla**: `default` e `dyn_ignore` coincidono a ogni
   rank count. Quindi ogni differenza osservata è attribuibile all'algoritmo, non all'attivazione
   del meccanismo di selezione.
2. **A 128 rank la libreria esegue basic linear** (0.398 contro 0.402, indistinguibili) mentre
   pairwise farebbe 0.128 con CV 1%. **A 64 rank esegue pairwise** (0.123 contro 0.123) e forzare
   basic linear peggiora di 2.7x. Le due inferenze sono solide perché in entrambi i casi i due
   algoritmi sono nettamente separati e il default segue uno solo dei due.
3. **Il dip dello strong scaling è un cambio di scelta, non un collasso.** A 2 nodi la libreria
   sceglie pairwise e satura il link (0.262 contro 261 ms di tetto: 100%); a 4 nodi passa a basic
   linear e il link scende al 37%; a 8 nodi torna a pairwise e nelle run buone tocca 0.115 contro
   114 ms di tetto. Il meccanismo di basic linear è il fan-out concorrente: posta tutte le R-1 send
   insieme, più mittenti convergono sullo stesso ricevente (incast), la coda della NIC si satura e
   il link resta inutilizzato mentre TCP attende. Pairwise esegue R-1 round in cui ogni round è una
   permutazione, quindi ogni destinatario ha un solo mittente.
4. **Il recupero da 4 a 8 nodi si scompone in due fattori esatti**: 1.71x perché il tetto scende
   (227 -> 132 MB per NIC, dato che il traffico si divide su 8 schede invece di 4) e 1.48x perché
   l'algoritmo torna a essere pairwise. Il prodotto 2.53x coincide con il rapporto misurato sul
   payload della pipeline (1.346 -> 0.532 s).
5. **A basso rank count la euristica ha ragione**: a 8 e 16 rank pairwise sarebbe più lento
   (0.173 contro 0.133), perché con messaggi da 18 MB i round serializzati non aiutano e conviene
   postare tutto insieme. La euristica non è sbagliata in generale: sbaglia a 128 rank.

## Ricadute su cosa si può affermare del report

Il report (§ strong scaling) sostiene che *"the run-to-run variance comes from the algorithm the
library selects in this regime, while the cost itself is the 128-way fan-out"*. Entrambe le parti
non reggono:

- **La varianza non viene dalla selezione.** A 128 rank la scelta è fissa: basic linear su tutte e
  dieci le ripetizioni, con CV 4%. Un algoritmo fisso e stabile non produce varianza da selezione.
  Il CV 18% del default a 128 rank viene da un singolo outlier (0.632 contro nove valori fra 0.384
  e 0.469).
- **Il costo non è il fan-out.** Il fan-out semantico (R-1 destinatari, R-1 messaggi, stessi byte)
  è identico nei due algoritmi, e con pairwise gli stessi 128 rank scendono al tetto di banda. A
  costare è il fan-out concorrente, che è un parametro dell'algoritmo, non del problema.

La formulazione difendibile: a 128 rank la decision function sceglie basic linear mentre a 64 e 256
sceglie pairwise; quella scelta lascia il link al 37% e forzare pairwise riporta la collettiva al
tetto di banda. Il dip è un difetto dell'euristica della libreria in questo regime.

## Cosa resta non spiegato

- **La regola della decision function non è ricavabile.** La scelta non è monotona né nel rank count
  né nella dimensione del messaggio (linear a 8/16/32, pairwise a 64, linear a 128, pairwise a 256),
  e con `Internal debug support: no` non è leggibile dall'interno senza ricompilare Open MPI. Va
  riportata come misura, non spiegata.
- **La bimodalità è un fenomeno sovrapposto e indipendente dall'algoritmo.** A 4 e 8 nodi anche
  pairwise alterna fra il tetto e 2-3 volte il tetto (a 4 nodi: 0.201 in quattro run, 0.40 nelle
  altre sei), mentre a 2 nodi è stabile al millesimo (CV 0%). Colpisce anche il default e
  `dyn_ignore`. Nessun meccanismo isolato: servirebbero i contatori di rete.
