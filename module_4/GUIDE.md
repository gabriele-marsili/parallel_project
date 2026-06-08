# Module 4 — Distributed Partitioned Hash Join (MPI)

Guida di implementazione. Documento vivo: ogni decisione qui dentro va giustificata o cambiata esplicitamente, mai aggirata. Tutti i riferimenti alle lezioni puntano a file in `../lezioni/` (PDF) e ai relativi appunti markdown in `../`.

---

## 1. Contratto del modulo (sintesi `comando_module_4.pdf`)

Cosa va consegnato:

1. **Implementazione MPI pura** della partitioned hash join.
2. **Implementazione ibrida MPI+OpenMP** (opzionale per il comando, obbligatoria per noi).
3. **Baseline sequenziale**: la stessa versione migliorata usata in M3, copiata qui (`src/hashjoin_seq.cpp`) così il modulo è autonomo.
4. **Correttezza**: `join_count`, `checksum1`, `checksum2` identici al sequenziale per ogni configurazione.
5. **Esperimenti su spmcluster**: strong e weak scaling su 1, 2, 4, 8 nodi; speedup vs seq; **breakdown** comm/redistribuzione vs local-join.
6. **Dataset**: uniform (obbligatorio) + skewed (opzionale ma incluso).
7. **Report PDF** ≤ 6 pp + 1 pp extra per la sezione hybrid. Soft deadline: **3 giugno 2026**.
8. **Zip di consegna**: `Modulo4_MarsiliGabriele.zip` (source, README, Makefile, report.pdf).

Vincoli da rispettare:

- Stesso **mapping function** dei moduli precedenti (Fibonacci hash da `common.hpp`).
- Stesso **schema di generazione input** (`generator.hpp` di M3 — `splitmix64`).
- **Verifica fuori dalla regione misurata**.
- Campagna sperimentale **focalizzata**: poche configurazioni rappresentative, non sweep esaustivi.

---

## 2. Riferimenti di teoria

Ogni scelta del modulo si appoggia su:

- **Lez. 10 — Metrics and Laws**: Amdahl/Gustafson, speedup, efficienza. Anche in distribuito il limite seriale resta.
- **Lez. 14 — Workload Balancing**: imbalance fra rank è il problema dominante nel join distribuito con dati reali.
- **Lez. 23 — Distributed Systems Background**: modello a memoria distribuita, costo della comunicazione `α + β·n` (latenza + inverso della banda), relazione fra `t_comp` e `t_comm`.
- **Lez. 24 — MPI1**: rank, comm world, blocking send/recv, collective basics.
- **Lez. 25 — MPI2**: collective avanzate (Alltoall, Alltoallv, Allreduce), non-blocking (Isend/Irecv), overlap comm/comp.

Riferimenti puntuali andranno citati nel report a ogni scelta tecnica (`secondo lez. 25, ...`).

---

## 3. Algoritmo distribuito — scelta di alto livello

Il partitioned hash join è naturale da distribuire perché la **stessa funzione di partizionamento** già esistente è anche la regola di assegnazione dei dati ai rank. Nessuna duplicazione: ogni record finisce su esattamente un rank.

Pipeline per ogni rank `r` di un comunicatore di `R` rank:

```
[locale]   1. Genera la sua fetta locale di R e S  (R_r, S_r) usando lo stesso
              seed globale ma offset deterministico → input identico a seq.
[locale]   2. Histogram + scatter LOCALI su P partizioni (riuso di M3).
[comm ]    3. Mapping partizione → rank di destinazione:
              dest(pid) = pid % R           (round-robin sul rank)
              ovvero pid / (P / R)          (block-cyclic se P è multiplo di R)
              Scelta: pid % R, semplice e bilanciata sul caso uniform.
[comm ]    4. Redistribuzione: ogni rank manda le sue partizioni "non-locali"
              al rank proprietario e riceve dagli altri quelle che gli spettano.
[locale]   5. Local join (build+probe) sulle partizioni di sua competenza,
              riusando join_phases.hpp di M3.
[comm ]    6. Allreduce di (join_count, checksum1, checksum2).
```

**Numero di partizioni** `P`: come in M3, potenza di due, e `P ≥ R` con `P % R == 0` per mappatura pulita. Default `P = 128` (sufficiente a tenere ogni `R_p` build-table in L2).

---

## 4. Strategia di redistribuzione — design space

Tre opzioni concrete; il comando dice esplicitamente che la scelta è parte del design space e va giustificata.

### 4.1 `MPI_Alltoallv` (collective) — **scelta primaria**

- Ogni rank costruisce due buffer contigui (uno per R, uno per S) ordinati per partizione di destinazione (l'output dello scatter di M3 lo è già): basta un `send_counts[r] = sum(hist[pid] for pid s.t. dest(pid)==r)` e `send_displs[r]` per layout.
- Una `MPI_Alltoall(send_counts, recv_counts)` scambia le size, poi `MPI_Alltoallv` muove i dati.
- **Pro**: una sola chiamata, l'implementazione MPI sceglie l'algoritmo collettivo migliore per la topologia del cluster (lez. 25). Misura del costo netta (un singolo intervallo `t_comm`).
- **Contro**: sincronizzazione globale, niente overlap con computazione.
- **Riferimento**: lez. 25, sezione collective comm.

### 4.2 `MPI_Isend/Irecv` punto-punto

- Per ogni coppia (r→r'), una posting non-bloccante, poi `MPI_Waitall`.
- **Pro**: spazio per overlap se si fa la parte locale (build dei rank propri) durante l'attesa.
- **Contro**: bookkeeping di `R*(R-1)` request, attenzione a out-of-order recv, congestion sul rete se i pattern non sono ben gestiti. Più difficile da spiegare nel report.

### 4.3 Hash-aware ring/butterfly

- Riduzione a passi log₂(R), tipica per ricalcoli associativi (es. reduce-scatter). Non si applica bene perché la quantità da spostare per coppia è quasi tutta (non c'è "accumulazione" come in una reduce). Scartata.

### Decisione

**Default = Alltoallv (4.1)**. Implementiamo subito questa versione. Se la campagna cluster mostra che il `t_comm` è la metà o più del totale e c'è del local-build "indipendente" che si potrebbe sovrapporre, si **valuta** una variante 4.2 e si confronta nel report. Altrimenti la giustificazione finale è "comm sotto soglia, niente da ottimizzare con overlap".

Citazione report: "Alltoallv è la primitiva canonica per la redistribuzione totale (all-to-all) di dati partizionati (lez. 25); l'overhead di una `MPI_Alltoall` preliminare per le size è trascurabile (`O(R²)` byte) rispetto al payload (`O(N)`)."

---

## 5. Data layout per lo scambio

Record è semplicemente `{ uint64_t key }` → 8 byte fissi. Niente strutture variable-length, niente stringhe. Si scambiano `uint64_t` puri.

```cpp
// Definito una volta:
MPI_Datatype MPI_RECORD = MPI_UINT64_T;     // perfetto allineamento
```

Buffer di partenza (per R; per S identico):

```
local_R_partitioned  // output dello scatter locale di M3, contiguo per partizione
local_R_begin[P+1]   // offset di ogni partizione

// Per dest r in [0, R):
send_counts_R[r] = sum_{pid: dest(pid)==r} (local_R_begin[pid+1] - local_R_begin[pid])
send_displs_R[r] = prefix_sum(send_counts_R, esclusivo)
```

Con `dest(pid) = pid % R` e `P % R == 0`, le partizioni assegnate a un rank sono `{r, r+R, r+2R, ...}`. Per rendere il send buffer contiguo per destinazione senza copia, **scattere localmente direttamente nell'ordine giusto**: nella fase 2 si computa `dest_for_record = hash_key(...) % R` e si fa lo scatter in `P` partizioni *raggruppate per destinazione* (le R partizioni del rank r sono adiacenti nel buffer). Equivalente a uno scatter su `P` partizioni seguito da una permutazione gratuita — la permutazione è solo nel modo in cui si numerano le partizioni.

**Trick**: si rinumerano le partizioni globali come `pid' = (pid % R) * (P/R) + (pid / R)`. Così le prime `P/R` partizioni nel buffer locale sono quelle che vanno al rank 0, ecc. La parametrizzazione di scatter di M3 non cambia, cambia solo come si calcola il send_count per rank (somma a blocchi di `P/R` consecutivi).

Dopo `MPI_Alltoallv` il rank ha tutte le partizioni che gli competono (sue `P/R`), in ordine per sorgente. Per il local join non serve riordinare ulteriormente: basta un secondo histogram su `P/R` partizioni e prefix sum locale (è O(N_local), già misurato come `histogram_post` nel breakdown). Alternativa: il mittente prepara il buffer raggruppando le sue partizioni in `recv-side partition order` con un secondo scatter — costoso. Scelta: **scatter in destination order solo lato sorgente, ricomporre lato destinatario** con un histogram locale che è quasi gratis rispetto al join stesso.

---

## 6. Breakdown del tempo

Stesso schema di M3 (`PhaseTiming` in `common_structs.hpp`), ma con campi distribuiti:

```
PhaseTimingMPI {
    double generation;        // input gen (fuori dalla regione misurata principale)
    double histogram_local;   // R+S local hist
    double scatter_local;     // R+S local scatter (dest-ordered)
    double comm_sizes;        // MPI_Alltoall dei counts (R+S)
    double comm_payload;      // MPI_Alltoallv (R+S)
    double histogram_post;    // re-histogram lato ricevente per recuperare i begin per pid
    double join_local;        // build+probe
    double reduce_final;      // MPI_Allreduce
    double total;             // max sul comm di (sum sopra)
}
```

Tutti i tempi vanno **stampati per rank** e poi aggregati: per il report si prende il **max** sul comm (collo di bottiglia) e si riporta anche la **deviazione standard** per evidenziare imbalance.

Misurazione: barrier prima di ogni intervallo, `MPI_Wtime`. Il barrier va dentro la regione misurata, **non fuori**, altrimenti il tempo collettivo è ingannevole.

---

## 7. Hybrid MPI+OpenMP

Si riusa il kernel OpenMP di M3 (`hashjoin_OpenMP.cpp`) **solo per la fase local join**. Le fasi MPI restano single-threaded per rank.

Strategia di mapping:

- **1 rank per nodo**, `T` thread = numero di core fisici del nodo. Riuso pieno della cache locale e delle ottimizzazioni di M3 (thread affinity, NUMA first-touch).
- Alternativa: 1 rank per socket (2 rank/nodo su nodo dual-socket). Più granularità, ma raddoppia il volume `Alltoallv` per ogni nodo. Da provare solo se la versione 1-rank/nodo mostra contesa NUMA evidente.

Si fa anche **2 OpenMP nello scatter locale** se `T > 4`: M3 ha già lo scatter parallelo (con histogram per-thread). Niente da rifare.

`MPI_THREAD_FUNNELED` è sufficiente (solo il main thread chiama MPI). Niente `MPI_THREAD_MULTIPLE` — meno overhead.

Compilazione: `mpicxx -fopenmp`. `OMP_PROC_BIND=close` + `OMP_PLACES=cores`. Eventuale `I_MPI_PIN` o `SLURM_CPU_BIND` se la distribuzione MPI è Intel/OpenMPI specifica — da verificare sul cluster.

---

## 8. Generazione dell'input distribuita

Per garantire che il risultato distribuito sia identico a quello sequenziale dobbiamo generare lo stesso identico R e S "logico". Due opzioni:

1. **Generazione locale identica**: rank 0 genera tutto, scattera fette ai rank con `MPI_Scatterv`. Garantisce identità, ma include il costo della distribuzione iniziale nel timing — non quello che ci interessa.
2. **Generazione locale indipendente** con seed offset: ogni rank genera `[r·N/R, (r+1)·N/R)` chiamando `splitmix64_next` con stato iniziale `seed + r·N/R · k` (skip-ahead). `splitmix64` ammette skip-ahead esatto perché `state += 0x9e... · stride` è chiuso. Identità garantita rispetto al seq.

**Scelta**: opzione 2. La generazione resta fuori dalla regione misurata e ogni rank lavora indipendentemente. Il sequenziale è la versione di M3 invariata (copiata in `src/hashjoin_seq.cpp`), quindi il confronto è onesto.

Implementazione skip-ahead per `splitmix64_next`:

```cpp
// splitmix64_next: state += GOLDEN; return mix(state)
// Per skippare k call: state_after = state_before + GOLDEN * k
constexpr uint64_t SPLITMIX_GOLDEN = 0x9e3779b97f4a7c15ULL;
uint64_t local_seed = global_seed + SPLITMIX_GOLDEN * (uint64_t)(rank * (N/R));
```

Verifica con un test piccolo: la concatenazione delle fette locali deve essere `==` all'output del generator sequenziale. Caso `R = 1` deve coincidere con M3 byte-per-byte.

---

## 9. Correttezza — validation strategy

Tre livelli:

1. **Naive O(N²)** per input piccoli (`NR, NS ≤ 500`): già presente in `verifier.hpp`. Rank 0 lo esegue dopo `MPI_Allreduce` e confronta con `result_global`.
2. **Cross-check vs M3 seq**: lo stesso `NR/NS/seed/max_key/P` deve dare gli stessi tre numeri (join_count, ck1, ck2) sia su `hashjoin_seq` sia su `hashjoin_mpi` per ogni `R ∈ {1, 2, 4, 8, 16}`. Script `tests/validate_mpi.sh`.
3. **Idempotenza al rank count**: aumentando R i tre numeri non devono cambiare. Test automatico.

Output di validation in `results/validation_mpi_*.log`. Tutti i confronti **fuori** dalla regione misurata.

---

## 10. Esperimenti cluster

### 10.1 Strong scaling
- `NR = 50M`, `NS = 100M`, `max_key = 25M`, `seed = 42`, `P = 128`.
- Configurazioni MPI puro: `R ∈ {1, 2, 4, 8, 16, 32}` su `nodes ∈ {1, 1, 1, 1, 2, 4}` (1 rank/core finché entra in un nodo, poi multi-nodo).
- Configurazioni hybrid: `nodes × ranks_per_node × threads_per_rank` con `nodes ∈ {1, 2, 4, 8}`, `1` rank per nodo, `T = cores_per_node`.
- Reps: 5. Riportiamo mediana e min/max.

### 10.2 Weak scaling
- Carico per rank costante: `NR_local = 6.25M`, `NS_local = 12.5M` → `NR_global = NR_local × R`.
- Stessi `R` di sopra.
- Efficienza weak attesa < 1 a causa di `comm_payload` che scala con N totale.

### 10.3 Breakdown
- Configurazione singola (es. `nodes=4`, `R=4` puro e `R=4 T=cores` hybrid), `REPS=5`.
- Tabella + grafico stacked: generation / histogram / scatter / comm_sizes / comm_payload / histogram_post / join_local / reduce_final.

### 10.4 Skewed
- Stesse configurazioni di strong scaling, generate con `generate_skewed_relation(rho=0.9, hot=4)`.
- Discussione: impatto del load imbalance distribuito (alcuni rank ricevono partizioni "hot" molto più grandi → ritardo nel local join, e payload `Alltoallv` non bilanciato).
- Possibile mitigazione discussa nel report: partition-aware mapping (`dest(pid) = round_robin sui rank in base a hist globale prevista`). Da implementare solo se il tempo lo permette.

### 10.5 Confronto M3 vs M4
- Stesso nodo, stessa workload, M3 OpenMP con `T = cores` vs M4 hybrid `R=1, T=cores`. Aspettativa: M4-hybrid ≈ M3 (overhead MPI minimo a 1 rank). Sanity check del setup MPI.
- Poi scaling fra 1 e 8 nodi.

Tutti i CSV in `results/cluster/`. Schema colonne uniforme con M3 + colonne `ranks, nodes, ranks_per_node, threads_per_rank`.

---

## 11. Layout dei file

```
module_4/
  comando_module_4.pdf            # contratto
  GUIDE.md                        # questo file
  README.md                       # build + run (locale e cluster)
  Makefile
  include/
    -> link/copy degli header di M3 (common*, generator, join_phases, utilities_fns, verifier)
    mpi_common.hpp                # MPI_Datatype, helper, PhaseTimingMPI
  src/
    hashjoin_mpi.cpp              # MPI puro
    hashjoin_mpi_omp.cpp          # hybrid
    # seq baseline: si usa quello di M3 (riferimento, non duplicato qui)
  scripts/
    validate_local.sh             # mpirun -n {1,2,4,8} confronto vs seq M3
    deploy_and_run.sh             # sync su spmcluster, sbatch
    run_strong.sh                 # sbatch job per strong
    run_weak.sh
    run_breakdown.sh
    run_skewed.sh
    plots/                        # gnuplot/python per figure del report
  tests/
    correctness_small.cpp         # opzionale, alternativa allo script
  results/
    cluster/                      # SOURCE OF TRUTH per il report
  report/
    report.tex
    fig_strong.pdf, fig_weak.pdf, fig_breakdown.pdf,
    fig_skew.pdf, fig_m3_vs_m4.pdf
```

Per gli header: **copia** degli header di M3 in `module_4/include/` (più `mpi_common.hpp` e `mpi_pipeline.hpp` propri del modulo), così il modulo compila ed esegue senza dipendere da `module_3`. In fase di scaffold erano symlink; sono stati materializzati in copie reali per rendere la consegna autonoma.

---

## 12. Makefile (schema)

```make
MPICXX ?= mpicxx
CXX    ?= g++
CXXFLAGS = -O3 -std=c++20 -Wall -Wextra -Wpedantic
OMPFLAG  = -fopenmp
INC      = -Iinclude

all: hashjoin_mpi hashjoin_mpi_omp hashjoin_seq

hashjoin_mpi: src/hashjoin_mpi.cpp
	$(MPICXX) $(CXXFLAGS) $(INC) $< -o $@

hashjoin_mpi_omp: src/hashjoin_mpi_omp.cpp
	$(MPICXX) $(CXXFLAGS) $(OMPFLAG) $(INC) $< -o $@

# Baseline sequenziale vendored: niente MPI, compilatore C++ semplice.
hashjoin_seq: src/hashjoin_seq.cpp
	$(CXX) $(CXXFLAGS) $(INC) $< -o $@

clean:
	rm -f hashjoin_mpi hashjoin_mpi_omp hashjoin_seq
```

---

## 13. SLURM job (schema)

```bash
#!/bin/bash
#SBATCH --job-name=m4_strong
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=1
#SBATCH --cpus-per-task=<CORES_PER_NODE>
#SBATCH --time=00:30:00
#SBATCH --output=results/cluster/slurm-%j.out

module load mpi          # se richiesto dal cluster
export OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK
export OMP_PROC_BIND=close
export OMP_PLACES=cores

srun ./hashjoin_mpi_omp -nr 50000000 -ns 100000000 \
     -seed 42 -max-key 25000000 -p 128
```

Configurazioni esatte (nodes, ntasks-per-node, cpus-per-task) saranno parametrizzate da `scripts/run_strong.sh`. Verificare prima `lscpu` sul nodo del cluster (vedi `cluster_stuff/spmcluster-access.pdf`).

---

## 14. Workflow operativo (questa è la "to-do list maestra")

1. **Scaffold codice + Makefile** (no logica MPI, solo struttura compila vuota).
2. **Generazione distribuita + verifica byte-per-byte vs seq** per `R ∈ {1, 2, 4}` su input piccolo.
3. **MPI puro — versione completa** con `Alltoallv`. Validation contro seq M3.
4. **Hybrid** = MPI puro con local join sostituito dal kernel OpenMP di M3.
5. **Run locale**: `mpirun -n {1,2,4,8}` (anche se sulla macchina locale niente cluster — solo per validare correttezza e niente race/hang).
6. **Deploy spmcluster**: rsync, compile, smoke test (input piccolo).
7. **Campagna**: strong → weak → breakdown → skewed.
8. **Analisi CSV**: cerchiamo bottleneck (comm vs join). Se comm > 50% del totale anche a 1 rank/nodo, valutiamo Isend/Irecv con overlap (sezione 4.2).
9. **Plot**: tutti i grafici richiesti, ispezionati visualmente.
10. **Report LaTeX**: 6 pp + 1 pp hybrid. Passare per `humanizer` mantenendo registro accademico.
11. **Checklist consegna** (sez. 15 di `../CLAUDE.md`) + zip.

---

## 15. Rischi noti e contromisure

| Rischio | Probabilità | Contromisura |
|---|---|---|
| Generazione distribuita ≠ seq | media | Test byte-per-byte, R=1 deve coincidere |
| `Alltoallv` `count > INT_MAX` con N grandi | bassa (≤ 100M record × 8 B = 800 MB, sotto soglia) | Limitare N o usare `MPI_Alltoallv` derivato dal datatype contiguo |
| Imbalance skewed → uno rank diventa straggler | alta | Atteso, discusso nel report; mitigazione opzionale (mappatura partition→rank pesata) |
| Cluster MPI version mismatch fra nodi | bassa | `mpicxx -v` e `srun` sullo stesso build |
| Misure rumorose (jitter di rete) | media | Median di 5 reps + report min/max |
| Pin OpenMP non rispettato su nodo multi-socket | media | `numactl --hardware` di sanity, `OMP_PLACES=cores` + verifica con `KMP_AFFINITY=verbose` o equivalente |

---

## 16. Cosa NON facciamo (out of scope)

- Algoritmi diversi dal partitioned hash join (es. sort-merge, radix join puro).
- Tuning per cluster diversi da spmcluster.
- Compressione del payload `Alltoallv` (key è già 8 byte densi).
- One-sided MPI (`MPI_Put`/`MPI_Get`, RMA). Citato come direzione futura.
- Fault tolerance / checkpoint MPI.
