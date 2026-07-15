# Handoff — Deck "esperimenti aggiuntivi" M1/M2 per l'orale SPM (2026-07-09)

## Obiettivo del lavoro in corso
Preparare due PDF **da mostrare al professore** all'orale SPM: `esperimenti_aggiuntivi_M1.pdf`
e `esperimenti_aggiuntivi_M2.pdf`, che raccolgono gli esperimenti extra (oltre i report) come
documento tecnico, un grafico per pagina con commento a punti in tono report/paper. In parallelo,
mantenere e correggere i companion di studio.

## Stato attuale
- **Fatto (completo e verificato):**
  - `module_1/extra_experiments/esperimenti_aggiuntivi_M1.pdf` (**12 pagine**: indice + 11 grafici)
    e `module_2/extra_experiments/esperimenti_aggiuntivi_M2.pdf` (**17 pagine**: indice + 16
    grafici). Un grafico per pagina, testo **a punti in tono report/paper**. Build:
    `bash <modulo>/extra_experiments/build_deck.sh` (pandoc+xelatex). Sorgenti
    `esperimenti_aggiuntivi_MN.md`, header condiviso `deck_header.tex`.
  - Prima pagina di ogni deck: tabella neutra "Esperimento -> Riferimento nel report" (NON la
    vecchia "Quando nel report parli di X mostra Y", rimossa perché da bigino).
  - **Titoli di TUTTI i grafici neutralizzati** (nei `plot_*.py`, poi rigenerati): tolti em dash,
    MAIUSCOLE enfatiche, frasi retoriche, e i marker "(consegnata)/(baseline)/(progetto)/(atomic)".
  - Grafici sistemati e rigenerati: **flatmap_impl** (tolto pannello B, unità "ns per operazione su
    una chiave", scritta verde dentro il bordo, tolto "(più basso = meglio)"); **flatmap_loadfactor**
    (semplificato: solo curva misurata + marker x2/x1; tolti forma teorica di Knuth, zone
    sicura/patologica, riferimento "(tabella in L3)"); **flatmap_structure** (header dentro i bordi,
    tolto "array di bucket" sovrapposto); **join_lb_uniform_vs_skew** (scritta rossa "pavimento"
    centrata dentro i bordi, suptitle neutro, tolti "(progetto)/(atomic)"); **serial_fraction**
    (legenda spostata in basso a destra, tolte le due annotazioni che coprivano le linee).
  - **Colori Esp.4 uniformati** su tutti e 3 i grafici: cyclic=verde, block=rosso, dynamic=blu,
    lpt=viola.
  - **NUOVO esperimento eseguito sul cluster** (node05, srun): Esp.4 thread-sweep (tempo fase join
    vs numero di thread, per strategia, uniforme + skew). Nuovi file in
    `module_2/extra_experiments/04_join_load_balance/`: `run_join_lb_threadsweep.sh`,
    `plot_join_lb_threads.py`, `results/join_lb_threadsweep.csv` (dati veri), `plots/join_lb_threads.png`.
    Pagina aggiunta al deck M2 (Esp.4). Dato chiave: sotto uniforme le 4 coincidono fino a 16 core;
    dynamic guadagna solo in HT (20-24); block collassa sotto skew.
  - Companion di studio M2 (`COMPANION_M2.pdf`, **24 pagine**) corretto/ampliato questa sessione:
    §1 phase-breakdown ora dice "leggermente ottimistico" (non "prudente", era un mio errore),
    §4.7 nuovo (soglia `min_items_per_thread` + costo O(k·P) del merge), §5 punti di onestà.
    COMPANION_M1 invariato (8 pagine).
  - Testo dei deck riscritto in tre passaggi: a punti -> humanizer -> **tono report/paper
    professionale** (ultima versione consegnata).

- **In corso:** niente a metà.

- **Non iniziato:** materiale extra per Moduli 3 e 4. Eventuali ritocchi ai grafici M1 se l'utente
  ne segnala di specifici (finora i grafici M1 ispezionati sono di qualità pulita).

## Decisioni attive — NON rinegoziare
- **I PDF `esperimenti_aggiuntivi` si MOSTRANO AL PROFESSORE.** Sono un documento tecnico neutro:
  NIENTE scaffolding da bigino ("Da dire", "Quando nel report parli di X mostra Y", "materiale di
  supporto per l'orale", "cosa dire"). Titolo "Modulo N: esperimenti aggiuntivi".
- **Testo dei deck: A PUNTI, tono da report/paper, professionale ma umano.** NO colloquialismi
  ("si mangia", "occhio", "roba", "batte", "fa quanto"). NO lead-in meccanici da AI
  ("Meccanismo:/Vantaggi:/Scopo:/Unità:/Prova decisiva:"). Frasi complete e impersonali.
- **Nei GRAFICI (titoli, legende, assi, annotazioni): NO marker auto-referenziali**
  "(consegnata)/(baseline)/(progetto)/(atomic)/(mia)". NO em dash (—) né trattino medio (–). NO
  MAIUSCOLE enfatiche (MISURATI, NON, PEGGIORA, OPPOSTE, SOPRA...). NO frasi retoriche ("Perché...").
- **Colori Esp.4 (tutti e 3 i grafici):** cyclic=#2E7D32 (verde), block=#D32F2F (rosso),
  dynamic=#1565C0 (blu), lpt=#7B1FA2 (viola).
- **Il REPORT (`module_2/report/report.tex` e .pdf) NON si modifica.** I COMPANION di studio
  (COMPANION_M1/M2) invece SI (sono materiale extra separato); il report è congelato.
- **Nessun numero/claim inventato = errore fatale.** Tutto misurato o riformulato. I grafici si
  rigenerano SOLO da dati reali (CSV in locale o presi dal cluster). Vietato fabbricare CSV/PNG.
- **Niente em dash (—) né doppio trattino (--) nel testo per l'utente** (regola globale ereditata).
- **Onestà sopra tutto.** cyclic NON è "più veloce" (pareggia sotto uniforme, un filo peggio sotto
  skew): si sceglie per semplicità a parità di prestazioni. La barriera NON è più veloce del thread
  pool (pareggiano end-to-end): si sceglie perché serve comunque a ogni confine + semplicità.
- **Nodi cluster:** M2 = Ivy Bridge (Xeon E5-2640 v2: 16 core fisici / 32 HT, L2 256KB/core, L3
  20MB/socket, 2 NUMA). M1 = node09 (AMD EPYC 7301, Zen1). Ogni modulo gira sul suo nodo.
- **Posso eseguire esperimenti sul cluster** (fatto questa sessione su node05): `ssh spmcluster`
  (login = spmln), compute via `srun --partition=normal ...`, MaxTime 30 min, compilare sul nodo
  con `-march=native`.

## File toccati
- `module_1/extra_experiments/` e `module_2/extra_experiments/` (INTERE cartelle, untracked): tutti
  i `esperimenti_aggiuntivi_MN.md`/`.pdf`, `build_deck.sh`, `deck_header.tex`, i `plot_*.py` (titoli
  neutralizzati), i `plots/*.png` (rigenerati).
- `module_2/extra_experiments/04_join_load_balance/`: NUOVI `run_join_lb_threadsweep.sh`,
  `plot_join_lb_threads.py`, `results/join_lb_threadsweep.csv`, `plots/join_lb_threads.png`.
- `module_2/extra_experiments/COMPANION_M2.md` + `.pdf`: corretto/ampliato questa sessione.
- `module_2/src/hashjoin_seq.cpp`, `hashjoin_parallel.cpp` (modified, solo commenti I=0.125 da
  sessione precedente).
- Sul CLUSTER: `~/module_2/extra_experiments/04_join_load_balance/run_join_lb_threadsweep.sh`
  sincronizzato (rsync).

## Stato git
- Branch **main**, ultimo commit **5bc154f "consegna"**.
- Tutto il lavoro di questa sessione è **untracked** dentro `module_1/extra_experiments/` e
  `module_2/extra_experiments/`, più il companion M2 modificato. **NON committato.**
- **NON committare senza chiedere all'utente.**
- Nota: `module_4/include/join_phases.hpp` risulta modified ma NON è stato toccato in questa
  sessione (era aperto nell'IDE).

## Prossimi passi (in ordine)
1. Se l'utente segnala altri grafici (M1 o M2) con problemi (overflow, affollamento, tono),
   sistemarli mirati: editare il `plot_*.py`, rigenerare (`cd <dir> && python3 plot_X.py`), poi
   `bash <modulo>/extra_experiments/build_deck.sh`.
2. In alternativa: drill orale (skill `spm-defense`) su M1/M2, oppure altre domande concettuali.
3. Poi: eventuale materiale extra per Moduli 3 e 4 (stesso schema deck).
4. Valutare commit del materiale extra e archiviazione dei due handoff.

## Trappole note
- **Shell locale = zsh:** `for X in $VAR` NON fa word-splitting (bash sì). Gli script hanno
  `#!/bin/bash` e vanno lanciati con `bash script.sh`.
- **Rigenerare un grafico:** `cd <cartella_dello_script> && python3 plot_X.py` (i path si risolvono
  da soli via `__file__`). matplotlib/numpy/pandas/scipy sono presenti in locale.
- **I titoli DENTRO i PNG non sono sanitizzati dal build** (sono immagini): la neutralizzazione va
  fatta nei `plot_*.py` e poi si rigenera. Il build_deck.sh sanitizza solo il testo `.md`.
- `build_deck.sh` sanitizza i glyph che Helvetica Neue non ha (→ · − ⌈ ⌉ ...). Evita ≤ ∈ nel `.md`
  (non gestiti); ≈ × ∞ ² vanno. Se aggiungi glyph, controlla il warning "missing char".
- **Il vecchio `HANDOFF_2026-07-09_m2-companion-orale.md` è SUPERATO** da questa sessione (companion
  M2 ulteriormente corretto + tutto il lavoro sui deck). Si può archiviare.
- Deck M2 = 16 grafici (17 pagine); deck M1 = 11 grafici (12 pagine). Ordine: indice, poi Esp.1..6
  (M2) / Esp.1..5 (M1). Alcuni esperimenti occupano più pagine (un grafico ciascuna).
- Cluster: partizione `normal` MaxTime 30 min; il thread-sweep gira in ~3 min. `perf` solo sul login
  node. I binari per-arch si ricompilano sul nodo (esclusi dal rsync).
- Esp.4: il "pavimento 3.62" è il limite inferiore dell'imbalance imposto dai dati (partizione più
  calda = 22.6% del lavoro, quota equa 6.25% = 1/16, 22.6/6.25 = 3.62, partizione indivisibile).
  Spiegato per esteso nel deck.
