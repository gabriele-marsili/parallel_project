# Handoff — Companion orale Modulo 2 SPM (2026-07-09)

## Obiettivo del lavoro in corso
Preparare l'orale del **Modulo 2** SPM (partitioned hash join con duplicati, C++ threads)
creando un documento "companion" di studio (come già fatto per il Modulo 1) + esperimenti
nuovi, in `module_2/extra_experiments/`, SENZA toccare il report consegnato. Il companion deve
**spiegare e motivare il perché** di ogni scelta in modo pedagogico (meccanismo prima, esperimenti
come conferma), con riferimenti al codice, non limitarsi a mostrare numeri.

## Stato attuale
- **Fatto (completo e verificato):**
  - `module_2/extra_experiments/COMPANION_M2.pdf` — **21 pagine**, entry point per l'orale.
    Generato da `COMPANION_M2.md` con `bash build_companion.sh` (pandoc+xelatex).
  - **6 esperimenti** in `extra_experiments/0N_*/`, ognuno con `.cpp`, `run_*.sh`, `plot_*.py`,
    `README.md`, `results/`, `plots/`. Tutti girati su **nodo Ivy Bridge** (node01/02,
    partizione `normal`), lo stesso tipo di nodo del report M2.
    1. `01_baseline_vs_mine` — ablation {mod,fib}x{umap,flat}. Finding: hash+FlatCountMap sono
       una COPPIA OBBLIGATA (1.58x end-to-end). Diagramma struttura + meccanismo bit passo-passo.
    2. `02_flatcountmap` — anatomia: probe 4x vs unordered_map; load factor (curva blowup vicino
       al 100%, `loadfactor_bench.cpp`); crossover L3; false sharing (padded vs packed, 2.2x).
    3. `03_barrier_vs_threadpool` — barrier vs pool: microsync coda fino a 200x piu' cara,
       end-to-end pari. Bulk-synchronous -> barriera obbligata.
    4. `04_join_load_balance` — cyclic/block/dynamic/lpt, uniforme vs skew, con barre d'errore.
    5. `05_histogram_roofline` — I=0.125 derivata + misurata (perf ~11 istr/record); a 16 core
       histo=40GB/s=tetto read -> memory-bound.
    6. `06_amdahl` — frazione seriale: fit R²=0.983 (f=0.078) vs seriale letterale MISURATO 0.1%
       (`serial_fraction.cpp`); direzioni opposte con P -> la f e' banda, non codice.
  - **Analisi dai CSV consegnati** (in §4 del companion, con figure del report convertite in
    `report_figures/*.png`): Amdahl, strong/weak scaling, phase breakdown, partition sensitivity,
    duplicate density. Tutti i numeri del report riprodotti.
  - **Commenti sull'intensità operazionale (I=0.125)** aggiunti al codice consegnato:
    `src/hashjoin_seq.cpp` (compute_histogram) e `src/hashjoin_parallel.cpp` (Phase 0). Compilano,
    naive_verify=PASS.
  - Memoria aggiornata: `~/.claude/projects/.../memory/m2-extra-experiments.md` + MEMORY.md.

- **In corso:** niente a metà. Ultimo intervento chiuso = correzione duplicate density (la frase
  "chiavi distinte ≈ min(NR,max_key)" era sbagliata; giusto e' palline-nelle-urne
  `max_key·(1−e^(−NR/max_key))`; a max_key=10M sono 6.32M distinte, 1.58 record/chiave, non 10M).
  Verificato che l'ERRORE E' ANCHE NEL REPORT ("near-unique keys", "hash table fully populated"):
  segnalato in §4.6 e §5 del companion come imprecisione di linguaggio (conclusione regge).

- **Non iniziato:** i punti del `todo` che riguardano il **REPORT stesso** (vedi Prossimi passi).
  Moduli 3 e 4 (stesso schema companion) non iniziati.

## Decisioni attive — NON rinegoziare
- **Il report consegnato (report.tex/report.pdf) NON si modifica.** Tutto il materiale extra sta
  in `module_2/extra_experiments/`, separato.
- **Nodo cluster giusto per M2 = Ivy Bridge (node01-08, `--partition=normal`)**, NON node09 (era il
  nodo GPU del Modulo 1). Compilare SUL nodo con `-march=native` (login=Haswell, compute=Ivy
  Bridge: ISA diverse).
- **Nessun numero inventato.** Ogni claim va misurato o riformulato. Numeri che "spuntano dal
  nulla" sono un problema grave: vanno DERIVATI (es. pavimento 3.62 = 22.6%/6.25%; block 10.8 = 3
  partizioni calde x 22.5% x 16).
- **Quando affermi qualcosa sul codice, RIPORTA il codice e di' DOVE (file:riga).** Regola
  permanente, richiesta esplicitamente.
- **Il companion deve SPIEGARE il perché (pedagogico), non elencare numeri.** Meccanismo prima,
  esperimento come conferma. Diagrammi per le strutture dati.
- **Onestà sopra tutto.** Se gli esperimenti mostrano che cyclic NON è più veloce (lo è: è pari
  sotto uniforme, un filo peggiore sotto skew), dirlo. cyclic si sceglie per **semplicità a parità
  di prestazioni**, non per velocità. Non avere la botte piena e la moglie ubriaca.
- **Terminologia:** `hashjoin_seq_baseline.cpp` = "baseline DI PARTENZA" (riferimento fornito);
  `hashjoin_seq.cpp` (V3) = "versione CONSEGNATA". MAI chiamare V0 "baseline consegnata"; nei
  grafici NON usare "la mia versione" (usa "consegnata"). Nessuna freccia nel grafico ablation.
- **Niente em dash (—) né doppio trattino (--) nel testo per l'utente.** Usa virgole/due punti.
- **Niente frasi-slogan da LLM, niente citazioni "(lez. N)" nei report** (preferenza ereditata).
- Lo **skew NON è richiesto dal comando del Modulo 2** (verificato su ModularProject-Modulo2.pdf:
  chiede solo speedup + strong/weak scaling + phase breakdown, su dati uniformi-con-duplicati).
  L'Esp.4 skew è materiale EXTRA, oltre il richiesto. Lo skew è roba del Modulo 3.

## File toccati
- `module_2/extra_experiments/` (INTERA cartella, untracked) — tutto il materiale nuovo: 6
  esperimenti, COMPANION_M2.md/pdf, build_companion.sh, pandoc_header.tex, run_all.sh,
  sbatch_all.sh, report_figures/*.png, README.md.
- `module_2/src/hashjoin_seq.cpp` (modified) — aggiunti SOLO commenti sull'intensità operazionale
  I=0.125 in compute_histogram. Nessuna modifica di comportamento.
- `module_2/src/hashjoin_parallel.cpp` (modified) — aggiunto SOLO commento I=0.125 alla Phase 0.
- `~/.claude/projects/-Users-...-SPM-project-and-midterms/memory/m2-extra-experiments.md` +
  `MEMORY.md` — memoria persistente aggiornata.

## Stato git
- Branch: **main**. Ultimo commit: **5bc154f "consegna"**.
- Non committato (M2): `src/hashjoin_seq.cpp` e `src/hashjoin_parallel.cpp` (modified, solo
  commenti), `extra_experiments/` (untracked), `report/report.pdf` (untracked),
  `src/hashjoin_seq_baseline.cpp` (untracked), `Modulo2_MarsiliGabriele.zip` (deleted).
- **NON committare senza chiedere all'utente.** Nessun commit fatto in questa sessione. Se si
  committa: valutare se tenere separati i commenti-codice dal materiale extra_experiments.

## Prossimi passi (in ordine)
1. **Affrontare i punti del `todo` che riguardano il REPORT** (file `module_2/utils/todo`), a cui
   l'utente ha già acconsentito implicitamente ("se vuoi posso passare in rassegna..."). I
   principali ancora aperti:
   - strong scaling con efficienza/speedup > 100% a p=1 (1.03, 1.04): perché? (seq e par sono
     binari distinti, best-of-5 diverge <=4%).
   - phase breakdown misurato 2 volte a p=1 (769 vs 753ms): perché, e quale usato.
   - partition sensitivity: l'utente contesta il "plateau [256,1024]" (dice: tra 256 e 512 c'è
     cambiamento, il vero plateau è 512-1024, poi risale; variazione 86-130ms non trascurabile a
     20 thread). Verificare se i tempi sono e2e di tutte le fasi. (I CSV sono in
     `module_2/results/partition_sensitivity.csv`.)
   - notazione T vs p (a P=16 < T=20): T = thread? renderla consistente con p.
   - stima "≈205 constructor/destructor calls a P=4096": da dove viene (= ceil(P/p)).
   - claim NUMA "not directly measured" e il paragrafo NUMA-interleave: l'utente vuole valutarli/
     toglierli (ma il report non si modifica -> discuterli nel companion o a voce).
   - i segni "§" residui nei riferimenti di sezione (nel report), la frase "now available",
     "Join Local tells a different story" (troppo informale), "(a general characteristic...)".
   - duplicate density: GIA' FATTO questa sessione (vedi §4.6 companion).
   NB: molti di questi sono critiche al report che NON si può modificare -> vanno gestiti come
   note per l'orale nel companion (§ onestà) o come chiarimenti a voce, NON editando il report.
2. In alternativa/dopo: simulare domande-trappola d'orale sul Modulo 2 (skill `spm-defense`).
3. Poi: passare al **Modulo 3** (stesso schema: extra_experiments + companion), poi Modulo 4.

## Trappole note
- **`perf` è SOLO sul login node (spmln), non sui compute node.** La misura OI (byte/record, istr/
  record) è stata fatta sul login (Haswell): il conteggio è algoritmo-dipendente, non microarch.
- **Partizione `normal`: MaxTime = 30 min.** sbatch con `--time=00:30:00` max.
- **Accesso cluster:** `ssh spmcluster` (utente g.marsili9, alias in ~/.ssh/config, BatchMode ok).
  Sorgenti in `~/module_2/`. Run: `srun --partition=normal --nodes=1 --ntasks=1 --cpus-per-task=32
  --exclusive --time=00:XX:00 bash <script>` oppure `sbatch extra_experiments/sbatch_all.sh` (~11
  min). I 6 binari sono ESCLUSI dal rsync (per-arch, ricompilati sul nodo).
- **Riprodurre tutto:** su un compute node (salloc) `bash extra_experiments/run_all.sh`, poi in
  locale pull dei `results/` + `bash build_companion.sh` per il PDF.
- **build_companion.sh** sanitizza i glyph che Helvetica Neue non ha (→ ≡ ⌈ ⌉ − · ∝ ≫ ≪);
  tiene ≈ × ∞ apici. Se aggiungi glyph nuovi al .md, controlla il warning "Missing character".
- **La tabella dello strong scaling nel report ha S(1)=1.03/1.04 (>100%)**: non è un bug, seq e par
  sono eseguibili distinti (nota già nel report).
- **Il report ha imprecisioni reali** (near-unique/fully-populated su duplicate density; possibili
  altre nei punti del todo). Non si correggono nel report: si spiegano nel companion/all'orale.
- Le figure del report in §4 del companion sono PNG convertite da `../results/plots/*.pdf` in
  `extra_experiments/report_figures/` (rigenerabili con pdftoppm se cambiano i PDF del report).
