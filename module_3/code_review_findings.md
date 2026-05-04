# Code Review — Modulo 3 vs Teoria SPM

Riferimenti file (sempre assoluti):
- `/Users/piccoletto/Desktop/Everything/pisa/corsi/magistrale/primo_anno/SPM/project_and_midterms/module_3/src/hashjoin_OpenMP.cpp`
- `/Users/piccoletto/Desktop/Everything/pisa/corsi/magistrale/primo_anno/SPM/project_and_midterms/module_3/include/join_phases.hpp`
- `/Users/piccoletto/Desktop/Everything/pisa/corsi/magistrale/primo_anno/SPM/project_and_midterms/module_3/scripts/run_schedule.sh`
- `/Users/piccoletto/Desktop/Everything/pisa/corsi/magistrale/primo_anno/SPM/project_and_midterms/module_3/scripts/run_strong.sh`
- `/Users/piccoletto/Desktop/Everything/pisa/corsi/magistrale/primo_anno/SPM/project_and_midterms/module_3/scripts/run_affinity.sh`

## Riepilogo
- Punti CONFORMI: 6/11 (1, 2, 4, 5, 6, 8)
- Punti PARZIALI: 4/11 (3, 7, 9, 10)
- Punti NON CONFORMI: 1/11 (11)

Nota: il punto 6 (affinity) è conforme dal lato codice; gli script lo gestiscono via env var, ma il binding non è impostato dentro il codice — coerente con la prassi delle slide (lez. 19 raccomanda OMP_PROC_BIND/OMP_PLACES via env, non hard-coded).

---

## Dettaglio per punto

### Punto 1 — Pattern task generation
- **Stato attuale:** `hashjoin_OpenMP.cpp:201-227`
  ```
  201  #pragma omp parallel
  202  {
  206      #pragma omp single nowait
  207      {
  208          for (std::uint32_t idx = 0; idx < P; ++idx) {
  ...
  212              #pragma omp task firstprivate(pid) shared(...)
  ...
  224      }
  ...
  227  }
  ```
- **Conformità:** OK
- **Motivazione:** È il pattern canonico di OpenMP2 (slide p.18): `parallel { single nowait { for ... task ... } }`. Il `nowait` sul `single` è effettivamente utile: senza di esso i T-1 thread bloccherebbero alla barriera implicita di fine `single` e potrebbero iniziare a scaricare la coda dei task solo dopo che il generatore ha finito di emetterli tutti. Con `nowait` i thread non-generatori entrano subito nella coda di scheduling. La barriera implicita di fine `parallel` (riga 227) garantisce comunque il completamento dei task prima dell'uscita (semantica equivalente a `taskwait` finale, perché `parallel` include una `taskgroup` implicita).
- **Patch:** nessuna.

### Punto 2 — Data scoping nei task
- **Stato attuale:** `hashjoin_OpenMP.cpp:212-213`
  ```
  #pragma omp task firstprivate(pid) shared(Rpart, Spart, thr_results)
  ```
- **Conformità:** OK (con minor)
- **Motivazione:** `firstprivate(pid)` cattura il valore della loop variable al momento della creazione del task, evitando il classico bug "tutti i task vedono pid=P-1" (slide OpenMP2 p.20-22). `shared(...)` esplicita le tre variabili condivise. Le variabili `order`, `T`, `j_start` non sono usate nel body del task, quindi non serve scoparle. **Minor:** il `parallel` esterno (riga 201) e il `single` (206) non hanno `default(none)`. Le slide OpenMP1 raccomandano `default(none)` per forzare scoping esplicito su ogni variabile usata. Il loop generatore usa `idx`, `P`, `order` — tutti automaticamente scoped correttamente, ma rendere esplicita la scelta migliora leggibilità e robustezza.
- **Patch suggerita (opzionale, didattica):**
  ```cpp
  // before:
  #pragma omp parallel
  {
      #pragma omp single nowait
      {
          for (std::uint32_t idx = 0; idx < P; ++idx) {
              const std::uint32_t pid = order[idx];
              #pragma omp task firstprivate(pid) shared(Rpart, Spart, thr_results)

  // after:
  #pragma omp parallel default(none) shared(P, order, Rpart, Spart, thr_results)
  {
      #pragma omp single nowait
      {
          for (std::uint32_t idx = 0; idx < P; ++idx) {
              const std::uint32_t pid = order[idx];
              #pragma omp task default(none) firstprivate(pid) \
                              shared(Rpart, Spart, thr_results)
  ```
  Stesso ragionamento si applica al `parallel` di `compute_phases` (`hashjoin_OpenMP.cpp:39`) e al `parallel for` del join loop (riga 139): non hanno `default(none)`.

### Punto 3 — Schedule choice nel join phase loop
- **Stato attuale:** `hashjoin_OpenMP.cpp:139-140`
  ```
  #pragma omp parallel for schedule(dynamic,1) reduction(+: ...)
  ```
- **Conformità:** PARZIALE
- **Motivazione:** La scelta `dynamic,1` è giustificabile teoricamente per workload sbilanciato (skew=0.9 con hot partitions): le slide OpenMP1 (sezione schedule) indicano `dynamic` come scelta corretta quando il costo per iterazione è eterogeneo. Tuttavia è **hard-coded**: `OMP_SCHEDULE` non ha effetto, il che rende lo script `run_schedule.sh` semanticamente vuoto (vedi punto 11). Il commento nello script (`run_schedule.sh:5-19`) lo riconosce esplicitamente. La scelta corretta dal punto di vista didattico è `schedule(runtime)` controllato via `OMP_SCHEDULE`, lasciando `dynamic,1` come fallback compile-time.
- **Patch suggerita:**
  ```cpp
  // before (hashjoin_OpenMP.cpp:139):
  #pragma omp parallel for schedule(dynamic,1) \
      reduction(+: join_count, checksum1, checksum2)

  // after:
  #ifdef RUNTIME_SCHEDULE
    #pragma omp parallel for schedule(runtime) \
        reduction(+: join_count, checksum1, checksum2)
  #else
    #pragma omp parallel for schedule(dynamic,1) \
        reduction(+: join_count, checksum1, checksum2)
  #endif
  ```
  Compila lo script di sensitivity con `make EXTRA_CXXFLAGS=-DRUNTIME_SCHEDULE`.

### Punto 4 — Reduction strategy
- **Stato attuale:** loop mode usa `reduction(+: join_count, checksum1, checksum2)` (`hashjoin_OpenMP.cpp:140`); task mode usa array padded `std::vector<PaddedResult> thr_results(T)` (`hashjoin_OpenMP.cpp:164-166, 197, 219-221, 234-238`):
  ```
  164  struct alignas(64) PaddedResult { JoinResult r{}; };
  165  static_assert(sizeof(PaddedResult) == 64, ...);
  ```
- **Conformità:** OK
- **Motivazione:** Nel loop mode la `reduction` nativa è applicabile perché si riducono tre scalari `uint64_t` (slide OpenMP1 reduction). Nel task mode il pattern padded array è giustificato:
  1. `reduction` su `#pragma omp parallel for` non si applica a task espliciti (in OMP < 5.0 servirebbe `task_reduction`/`in_reduction`, vedi punto 10);
  2. `JoinResult` è una struct multi-campo: la reduction nativa OMP funziona ma è meno didatticamente trasparente del pattern array-padded che le slide OpenMP1 mostrano esplicitamente come tecnica anti-false-sharing;
  3. `alignas(64)` + `static_assert(sizeof == 64)` (riga 165) garantisce che ogni accumulatore occupi esattamente una cache line — esattamente ciò che le slide raccomandano contro il false sharing.
- **Patch:** nessuna. La motivazione del padding va citata nel report (slide false-sharing OpenMP1).

### Punto 5 — Barriers tra fasi
- **Stato attuale:** `hashjoin_OpenMP.cpp:39-109`. Le barriere usate:
  - riga 49 (commento): barrier implicita di `single` dopo init `local_hists.resize` / `cursors.resize`;
  - riga 55: `#pragma omp barrier` esplicita dopo l'init delle righe per-thread;
  - riga 65 (commento): barrier implicita di `for` (no `nowait`) dopo histogram phase;
  - riga 97 (commento): barrier implicita di `single` dopo merge+prefix;
  - riga 105: barrier implicita di `for` dopo scatter.
- **Conformità:** OK
- **Motivazione:** Tutte le sincronizzazioni necessarie sono in posizione corretta. La barrier esplicita (55) è obbligatoria perché `local_hists[tid].assign(...)` e `cursors[tid].resize(...)` (52-53) avvengono FUORI da una worksharing construct: senza barrier un thread potrebbe entrare nel loop histogram (62) e leggere `local_hists[tid']` di un altro thread non ancora inizializzato. Il commento alla riga 66 ricorda esplicitamente che rimuovere `nowait` dal `for` histogram era critico per correttezza (la `single` successiva legge `local_hists` aggregati). Coerente con slide OpenMP1 sezione "implicit barriers".
- **Patch:** nessuna.

### Punto 6 — Thread affinity
- **Stato attuale:** Il codice non chiama `omp_set_proc_bind` né legge `OMP_PROC_BIND`/`OMP_PLACES`. Gli script:
  - `run_strong.sh:10` documenta nell'header: `OMP_PROC_BIND=close OMP_PLACES=cores ./scripts/run_strong.sh` ma **non setta** le variabili dentro lo script — l'utente deve passarle in CLI;
  - `run_affinity.sh:31-33,57` sweepa `(close, spread, master) × (cores, threads)`.
- **Conformità:** OK
- **Motivazione:** Le slide OpenMP1 raccomandano di gestire affinity via env var (portabilità tra cluster). Il codice non lo forza, lo script di sensitivity lo varia, lo script di scaling lo lascia all'utente. **Minor:** `run_strong.sh` potrebbe esportare valori di default per evitare run accidentali con bind random sul cluster.
- **Patch suggerita (opzionale, robustness):**
  ```bash
  # all'inizio di run_strong.sh dopo set -euo pipefail:
  : "${OMP_PROC_BIND:=close}"
  : "${OMP_PLACES:=cores}"
  export OMP_PROC_BIND OMP_PLACES
  ```

### Punto 7 — First-touch NUMA
- **Stato attuale:**
  - `hashjoin_OpenMP.cpp:304-305`: `Rpart.data.resize(NR); Spart.data.resize(NS);` chiamate dal main thread PRIMA di entrare nella regione parallela.
  - `hashjoin_OpenMP.cpp:31-34`: `hist`, `offsets`, `local_hists`, `cursors` allocati prima di `parallel`. Però `local_hists[tid].assign(P, 0)` (riga 52) e `cursors[tid].resize(P)` (53) avvengono **dentro** la regione parallela, quindi le righe interne sono first-touched dal thread che le userà — corretto.
  - `compute_phases` riga 80: `part.end.resize(P)` dentro `single` — eseguito dal thread che entra in `single`, non dal thread che leggerà `part.end[pid]` nel join. P è piccolo (≤256), trascurabile.
  - **Critico:** `R = generate_relation(...)` e `S = generate_relation(...)` (`hashjoin_OpenMP.cpp:299-300`) sono sequenziali → tutta la memoria input è first-touched dal main thread → su NUMA finisce su un solo socket. Lo stesso vale per `Rpart.data` / `Spart.data` (304-305): `vector::resize` zero-inizializza in-place sul thread chiamante, quindi tutta l'area di scatter risiede sul nodo del main thread.
- **Conformità:** PARZIALE
- **Motivazione:** Le slide OpenMP1 NUMA dicono che la first-touch policy assegna le pagine al nodo del primo writer. Qui: input R/S e buffer di scatter sono zero-touched dal main thread → su un cluster multi-socket (es. due socket Xeon) tutti i thread del socket remoto pagheranno traffico cross-socket per ogni accesso a `relation[i]` e per ogni store `part.data[...] = relation[i]`. Questo si vede come degrado nel weak scaling oltre il numero di core di un socket.
- **Patch suggerita:**
  ```cpp
  // before (hashjoin_OpenMP.cpp:303-305):
  PartitionedRelation Rpart, Spart;
  Rpart.data.resize(NR);
  Spart.data.resize(NS);

  // after — first-touch parallelo:
  PartitionedRelation Rpart, Spart;
  Rpart.data.resize(NR);
  Spart.data.resize(NS);
  #pragma omp parallel for schedule(static)
  for (std::size_t i = 0; i < NR; ++i) Rpart.data[i].key = 0;
  #pragma omp parallel for schedule(static)
  for (std::size_t i = 0; i < NS; ++i) Spart.data[i].key = 0;
  ```
  Per R/S input la patch è più invasiva (cambiare `generate_relation` per scrivere parallelamente con `schedule(static)` matching). Da citare nel report come limite noto se la patch non viene applicata.

### Punto 8 — Untied / final / priority
- **Stato attuale:** `hashjoin_OpenMP.cpp:212` — `#pragma omp task firstprivate(pid) shared(...)` senza `untied`/`final`/`priority`. Il commento riga 216-217 sottolinea che i task sono **tied** by default, scelta intenzionale per poter chiamare `omp_get_thread_num()` in modo stabile dentro il task.
- **Conformità:** OK
- **Motivazione:** Tied è la scelta corretta: il body legge `omp_get_thread_num()` (riga 217) per indicizzare `thr_results[tid]`. Con `untied` il task potrebbe migrare e indicizzare un altro slot → race condition. `priority` sarebbe ridondante perché l'ordine di submission segue già l'ordinamento LPT (heaviest first, righe 187-193); su molti runtime OpenMP la coda è FIFO, quindi `priority` aggiungerebbe overhead senza guadagno (non garantisce strict ordering — è un hint). `final`/`if` non hanno senso qui (no recursion, P fisso).
- **Patch:** nessuna.

### Punto 9 — Taskloop alternative
- **Stato attuale:** `hashjoin_OpenMP.cpp:208-223` usa task espliciti con loop di submission manuale.
- **Conformità:** PARZIALE
- **Motivazione:** `taskloop` (slide OpenMP2 p.30+) genera N task con `grainsize`/`num_tasks` automatici, ma il runtime decide la partitioning interna del range — cioè raggruppa iterazioni contigue in task, non rispettando l'ordinamento LPT esterno. La scelta di task espliciti è quindi **giustificata** dal vincolo LPT: solo emettendo i task uno per uno nell'ordine di `order[]` si controlla la submission order. Va citato esplicitamente nel report.
- **Patch:** nessuna. Citazione nel report (vedi sezione finale).

### Punto 10 — task_reduction / in_reduction (OMP 5.0)
- **Stato attuale:** non usate. Si usa `thr_results[]` padded (vedi punto 4).
- **Conformità:** PARZIALE
- **Motivazione:** GCC 12.2 supporta `task_reduction`/`in_reduction` (OpenMP 5.0). L'alternativa idiomatica sarebbe:
  ```cpp
  #pragma omp parallel
  {
      #pragma omp single nowait
      {
          #pragma omp taskgroup task_reduction(+: join_count, checksum1, checksum2)
          for (...) {
              #pragma omp task firstprivate(pid) \
                              in_reduction(+: join_count, checksum1, checksum2) \
                              shared(Rpart, Spart)
              { ... }
          }
      }
  }
  ```
  La scelta dell'array padded è comunque difendibile: (a) didatticamente esplicita il pattern anti-false-sharing visto nelle slide, (b) overhead inferiore per T piccolo, (c) `JoinResult` ha 3 campi → 3 reduction clause separate sono verbose. Conviene **citare la scelta scartata** nel report per dimostrare che è una decisione consapevole.
- **Patch:** nessuna obbligatoria. Aggiungere commento nel codice o citazione nel report.

### Punto 11 — Schedule sensitivity binding (BUG)
- **Stato attuale:** `run_schedule.sh:67` esporta `OMP_SCHEDULE="$SCHED"` ma il binario ha `schedule(dynamic,1)` hard-coded a `hashjoin_OpenMP.cpp:139`. Tutte le righe della CSV sono in realtà la stessa configurazione `dynamic,1` con etichette diverse. Lo script lo riconosce nei commenti (`run_schedule.sh:5-19`) ma esegue lo stesso e produce risultati ingannevoli.
- **Conformità:** NON CONFORME
- **Motivazione:** I dati in `results/schedule_sensitivity.csv` sono **scientificamente non validi** per un report sul confronto delle policy: tutte le run usano `dynamic,1` indipendentemente da `OMP_SCHEDULE`. Il punto è esplicitamente trattato dalle slide OpenMP1 e l'esperimento è documentato in `comando_module_3.pdf` come parte del lavoro richiesto.
- **Patch suggerita (priorità ALTA):** applicare la patch del punto 3 (`#ifdef RUNTIME_SCHEDULE`) e modificare `makefile` per produrre due binari, oppure ricompilare prima di eseguire `run_schedule.sh`. Modifica minima allo script:
  ```bash
  # in run_schedule.sh, dopo set -euo pipefail:
  OMP=./hashjoin_omp_runtime    # binario compilato con -DRUNTIME_SCHEDULE
  ```
  e nel makefile aggiungere target:
  ```make
  hashjoin_omp_runtime: src/hashjoin_OpenMP.cpp
  	$(CXX) $(CXXFLAGS) -DRUNTIME_SCHEDULE -fopenmp -o $@ $<
  ```

---

## Patch consolidate (priorità ordinata)

1. **[CRITICO — Punto 11/3]** Aggiungere `#ifdef RUNTIME_SCHEDULE` attorno al `schedule(dynamic,1)` del join loop e produrre un binario dedicato per `run_schedule.sh`. Senza questa patch i dati di schedule sensitivity sono invalidi.
2. **[ALTO — Punto 7]** First-touch parallelo dei buffer `Rpart.data` / `Spart.data` (zero-init parallelo prima del lancio delle fasi). Necessario per scaling oltre un socket NUMA.
3. **[MEDIO — Punto 6]** Default `OMP_PROC_BIND=close OMP_PLACES=cores` esportati dentro `run_strong.sh` e `run_weak.sh` per evitare run accidentali senza pinning.
4. **[BASSO — Punto 2]** Aggiungere `default(none)` ai `parallel` (`hashjoin_OpenMP.cpp:39`, `139`, `201`) e ai `task` (`212`) per esplicitare lo scoping (raccomandazione slide OpenMP1).
5. **[OPZIONALE — Punto 10]** Aggiungere commento di codice o paragrafo nel report che giustifica `thr_results[]` rispetto a `task_reduction`.

---

## Discussione per il report

Suggerimenti su come citare nel report le scelte progettuali con riferimenti alle slide:

- **Pattern task generation (Punto 1):** "Si adotta il canonical pattern `parallel { single nowait { for / task } }` mostrato nella slide 18 di OpenMP2 (lez. 22). Il `nowait` permette ai T-1 thread non generatori di entrare immediatamente nello scheduler della task queue, riducendo il bubble iniziale."

- **LPT + tied tasks (Punti 8-9):** "I task sono **tied** (default) per garantire la stabilità di `omp_get_thread_num()` all'interno del body, indicizzando in modo race-free l'array di accumulatori per-thread. Si è scelto di emettere task espliciti invece di `taskloop` (slide OpenMP2 p.30) perché `taskloop` raggruppa iterazioni contigue secondo `grainsize`/`num_tasks`, ignorando l'ordinamento LPT che invece guida la submission heaviest-first per minimizzare il makespan (longest processing time, slide load balancing)."

- **Reduction strategy (Punti 4, 10):** "Per la fase di join nel modo `task` si usa un array `PaddedResult thr_results[T]` con `alignas(64)` e `static_assert` sulla cache-line size. Questo materializza il pattern anti-false-sharing delle slide OpenMP1. L'alternativa OMP 5.0 (`taskgroup task_reduction(+:...) / task in_reduction(...)`) è funzionalmente equivalente ma meno didatticamente trasparente per una struct multi-campo come `JoinResult`."

- **Schedule choice (Punto 3):** "Per il join loop in modo `loop` si è scelto `schedule(dynamic,1)`: con workload skewed (`rho=0.9`) le partizioni hanno costi eterogenei e una assegnazione `static` concentrerebbe le hot partitions su pochi thread. La scelta è confermata dall'esperimento di sensitivity (`run_schedule.sh`), purché il binario sia compilato con `-DRUNTIME_SCHEDULE`."

- **Affinity (Punto 6):** "Tutti gli esperimenti sono eseguiti con `OMP_PROC_BIND=close OMP_PLACES=cores` (slide OpenMP1 affinity), evitando migrazioni dei thread e preservando la cache locality del hash table per-partizione. Lo script `run_affinity.sh` esplora la sensibilità a (close, spread, master) × (cores, threads)."

- **NUMA / first-touch (Punto 7):** "I buffer `Rpart.data` e `Spart.data` sono allocati e zero-inizializzati dal main thread; in caso di esecuzione su sistemi multi-socket questo costituisce un anti-pattern rispetto alla first-touch policy delle slide. Il fix consiste in un loop di init parallelo con `schedule(static)` matching che colloca le pagine sui nodi NUMA dei thread che le scriveranno nella fase di scatter."

- **Barriere (Punto 5):** "Le tre fasi (histogram, merge+prefix, scatter) sono separate da barriere implicite (`for` senza `nowait`, fine `single`); l'unica barrier esplicita (`hashjoin_OpenMP.cpp:55`) è necessaria perché l'init `local_hists[tid].assign(...)` avviene fuori da un costrutto worksharing. Coerente con la sezione 'implicit barriers' di OpenMP1."
