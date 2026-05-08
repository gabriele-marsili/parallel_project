# SPM — Project and Midterms

Corso: **Scalable and Parallel Multicore programming**, Università di Pisa — Magistrale, primo anno.
Studente: Gabriele Marsili. Lingua di lavoro: italiano (codice e identificatori in inglese).

---

## 1. Riferimenti di teoria (USARE SEMPRE)

Ogni decisione di design, ogni scelta implementativa, ogni paragrafo dei report deve essere ancorato alla teoria del corso. Le fonti autoritative sono, in ordine:

1. **Lezioni del corso** — `/Users/piccoletto/Desktop/Everything/pisa/corsi/magistrale/primo_anno/SPM/lezioni/`
   - PDF numerati per argomento (es. `10-Metrics_and_Laws.pdf`, `14-WorkloadBalancing.pdf`, `18-ThreadAffinity.pdf`, `19-OpenMP1.pdf`, `22-OpenMP2.pdf`, `4-SLURM.pdf`, ecc.).
   - Prima di proporre un'ottimizzazione o di motivare una scelta nel report, **leggi il PDF rilevante** e cita esplicitamente il concetto (es. "secondo Amdahl/Gustafson…", "schedule dynamic riduce load imbalance come da lez. 14…").
2. **Appunti markdown** in `../` (es. `18-ThreadAffinity-Appunti.md`, `19-OpenMP1-Appunti.md`, `22-OpenMP2-Appunti.md`) — versione condensata delle lezioni.
3. **Dispensa generale** — `../dispensa & utils/` e `../dispense-SPM.pdf`.
4. **Codici di esempio del docente** — `../codes/`.

**Regola**: nessuna affermazione tecnica senza supporto dalla teoria. Se la teoria non copre il punto, dichiaralo e fai ricerca con `WebSearch` / `last30days`, poi cita la fonte.

---

## 2. Comando del modulo (CONTRATTO)

Ogni modulo ha la sua specifica in `module_X/comando_module_X.pdf` (oppure `ModularProject-ModuloX.pdf` per il modulo 1). **Questo PDF è il contratto**: definisce cosa va consegnato, vincoli, criteri di valutazione.

Workflow obbligatorio per ogni modulo:
- All'inizio di una sessione di lavoro su un modulo, **leggi il `comando_*.pdf`** (usa `Read` con `pages` se è lungo).
- Prima di marcare un modulo come "pronto", **rileggi il comando** e verifica punto per punto che ogni requisito sia soddisfatto (codice, risultati, grafici, sezioni del report).
- Se trovi una discrepanza, dilla esplicitamente e proponi la correzione.

---

## 3. Cluster SPM (SSH + VPN)

Tutto ciò che riguarda l'accesso al cluster (SSH, chiavi, VPN dell'Università, SLURM job submission) è documentato in:

```
/Users/piccoletto/Desktop/Everything/pisa/corsi/magistrale/primo_anno/SPM/cluster_stuff/
  spmcluster-access.pdf
  ssh_keys/
```

Prima di suggerire comandi `ssh`, `scp`, `sbatch`, `srun`, controlla qui. Le misurazioni di scalability/breakdown nei report **devono** provenire dal cluster (non dalla macchina locale) — la cartella `results/cluster/` di ciascun modulo è la fonte ufficiale.

---

## 4. Modello e orchestrazione

- **Opus** (questo modello, `Codex-opus-4-7`) → planning, decisioni architetturali, refactor critici, scrittura/revisione finale dei report, debug non banale.
- **Sonnet** (`Codex-sonnet-4-6`) → tutto il resto: edit di routine, esecuzione di plan già definiti, generazione di plot, piccoli fix.
- **Haiku** → solo per task massivi e meccanici (es. rinominazioni in batch).

Per orchestrare il lavoro usa **GSD**:
- `/gsd-plan-phase` o `/gsd-discuss-phase` per pianificare un modulo o una fase.
- `/gsd-execute-phase` o `/gsd-quick` per eseguire.
- `/gsd-code-review` o `/caveman-review` per revisionare.
- `/gsd-ship` quando il modulo è pronto per la consegna (zip + commit).
- `/gsd-resume-work` a inizio sessione per ripartire con contesto.

---

## 5. Caveman mode (token efficiency)

Sessioni lunghe → attiva `/caveman` per ridurre i token di output ~75%. Per i commit usa `/caveman-commit`, per le review `/caveman-review`. **Eccezione**: la scrittura dei report (LaTeX) NON va in caveman — lì serve prosa accademica completa.

---

## 6. Scrittura dei report

I report sono in LaTeX (`module_X/report/report.tex`) e vanno consegnati come PDF. Regole:

1. **Tono accademico, prosa fluida**, in italiano salvo richiesta esplicita di inglese. Niente bullet point ovunque: alternare paragrafi argomentativi e liste solo dove utile.
2. **Ogni scelta motivata dalla teoria** del corso (cita lezione/concetto: "load imbalance — lez. 14", "false sharing — lez. 5&6", "Amdahl — lez. 10").
3. **Anti-AI**: dopo aver scritto un blocco di report, passa con la skill `humanizer` per rimuovere i pattern tipici di scrittura AI (em-dash overuse, rule of three, vocabolario gonfiato, vague attributions). Mantieni però registro tecnico-accademico — il `humanizer` serve a rendere la prosa naturale, non casual.
4. **Numeri reali**: tutte le tabelle e i grafici devono provenire da `results/cluster/*.csv`. Mai inventare valori. Se manca un dato, dichiaralo e propone come ottenerlo.
5. **Verifica incrociata** prima della consegna: rileggi il `comando_*.pdf` e marca ogni requisito come coperto / non coperto.

---

## 7. Verifica di correttezza (NON NEGOZIABILE)

Prima di dichiarare un risultato "pronto":

- **Codice**: compila pulito (`-Wall -Wextra -Wpedantic`), nessun warning, validation log (`validation_*.log`) senza errori, output del parallelo identico al sequenziale (a meno di tolleranze numeriche giustificate).
- **Risultati numerici**: speedup ≤ p (numero thread), efficienza ∈ [0,1], tempi monotoni decrescenti su strong scaling (entro varianza). Se un valore è fuori range, **investiga prima di pubblicarlo nel report**.
- **Grafici**: assi etichettati con unità, legende leggibili, scala (lin/log) coerente con la metrica, curve ideale (linear speedup) inclusa dove sensata. Apri il PNG/PDF e guardalo — non fidarti che lo script sia corretto.
- **Coerenza report ↔ dati**: i numeri citati nel testo devono coincidere con quelli nei CSV e nei grafici.

Usa `/gsd-verify-work` o `/gsd-validate-phase` per audit strutturato.

---

## 8. Stile del codice

Codice C++/OpenMP che **non sembri AI-generated** ma resti corretto e performante:

- **Commenti**: pochi e mirati. Spiegano il *perché* (vincolo non ovvio, motivazione di una scelta dalla teoria, workaround per un comportamento specifico). Non spiegano il *cosa*. No blocchi-docstring multi-riga inutili.
- **Naming**: identificatori inglesi, descrittivi senza essere prolissi (`build_ht`, non `buildHashTablePhaseFunction`). Snake_case per funzioni/variabili, PascalCase per tipi.
- **No emoji** in codice, commenti, output, log, commit.
- **Performance prima di tutto** — è un corso di parallel computing:
  - cache locality, evita false sharing (padding, allocazione per-thread)
  - `schedule(static|dynamic|guided)` scelto in base al carico (motiva nel report)
  - thread affinity quando rilevante (lez. 18)
  - misura prima di ottimizzare; profila (`perf`, `likwid`, breakdown timer)
- **Idiomi C++ moderni** (C++17/20 a seconda del modulo): RAII, niente `new`/`delete` raw, `std::span`/`std::vector`, evita allocazioni in hot path.
- **No abstraction premature**. Tre righe simili sono meglio di una factory.

Per Rust (se mai serve) le rules sono in `~/.Codex/rules/rust/`.

---

## 9. Ottimizzazione dei risultati

Quando si lavora sul codice di un modulo, l'obiettivo è **massimizzare** speedup ed efficienza compatibilmente con la teoria. Pipeline tipica:

1. Profila baseline (sequenziale + parallelo naïve). Identifica il bottleneck (sync, memoria, load imbalance).
2. Confronta opzioni (es. `static` vs `dynamic` vs `guided`, chunk sizes diverse, partitioning, NUMA-aware allocation) e **giustifica la scelta finale citando la lezione rilevante**.
3. Sweep di parametri sul cluster (strong/weak scaling). Salva CSV in `results/cluster/`.
4. Confronta con il modulo precedente quando applicabile (es. M3 vs M2 in `fig_m2_vs_m3.*`).
5. Se un'ottimizzazione "ovvia" non porta benefici, capisci perché (overhead? memory bound? Amdahl serial fraction?) e scrivilo nel report.

---

## 10. Obsidian — knowledge base trasversale

Vault: `~/Documents/ClaudeMemory/ClaudeMemory` (MCP attivo).

Usalo per:
- Cercare prior research / decisioni prese in altri progetti SPM o paralleli (cerca prima di reinventare).
- Salvare findings non banali (`/obsidian-save`) che potrebbero servire in moduli successivi (es. trick di tuning OpenMP, peculiarità del cluster, paper letti).
- Collegamenti tra lezioni e implementazioni: se applichi un concetto della lez. X in modo non ovvio, registralo.

Memoria di sessione (`~/.Codex/projects/.../memory/`) → contesto specifico del progetto. Obsidian → conoscenza che attraversa i moduli e i corsi.

---

## 11. Skill da usare proattivamente

- `humanizer` → ogni volta che si scrive testo del report.
- `caveman` / `caveman-commit` / `caveman-review` → per ridurre token nelle azioni meccaniche.
- `gsd-*` → orchestrazione dell'intero ciclo di vita di un modulo.
- `last30days <topic>` → ricerca aggiornata su tecniche (es. "hash join SIMD", "OpenMP task affinity 2025").
- `graphify` → mappare codebase grandi quando serve una vista d'insieme.
- `simplify` → revisione di codice modificato per ridondanze.
- `superpowers:systematic-debugging` → debug di bug paralleli (race, deadlock, false sharing, risultati instabili). Non fixare alla cieca.
- `superpowers:brainstorming` → prima di iniziare un nuovo modulo o un'ottimizzazione non banale.
- `superpowers:test-driven-development` → quando si aggiungono test (es. validation contro sequenziale).
- `superpowers:verification-before-completion` → SEMPRE prima di dichiarare "fatto".

---

## 12. Anti-allucinazione (CRITICO)

- **Non inventare** API, flag di compilatore, parametri SLURM, comportamenti di OpenMP. Se non sei sicuro, leggi il PDF della lezione, controlla `man`/cppreference, o usa `WebSearch`.
- **Non inventare** numeri, percentuali, speedup. Se non hai eseguito la misura, dichiara "non misurato" e proponi di farlo.
- **Non inventare** citazioni di lezioni: se citi "lez. 14", quella lezione deve effettivamente trattare il tema.
- Quando c'è incertezza, dichiarala esplicitamente al posto di un'affermazione confidente sbagliata.

---

## 13. Soglia di comprensione 95% — CHIEDERE

Prima di iniziare qualsiasi task non triviale, **fai domande** finché non hai ≥95% di chiarezza su:
- Qual è il deliverable esatto (codice / report / grafico / tutto)?
- Quali vincoli del comando sono coinvolti?
- Quali metriche / parametri vanno usati?
- Su quale macchina vanno eseguite le misure (locale o cluster)?
- C'è un modulo precedente con cui confrontarsi?

Una domanda in più all'inizio costa meno di un'ora di lavoro nella direzione sbagliata. **Better-ask-than-assume** è la regola.

Eccezione: task palesemente meccaniche (es. "rinomina questa variabile") → procedi.

---

## 14. Layout del repository

```
project_and_midterms/
  module_1/   # primo modulo (vedere ModularProject-Modulo1.pdf)
  module_2/   # hash join sequenziale + parallelo (C++ threads)
  module_3/   # hash join OpenMP (build/probe phase, schedule sweep, breakdown)
  utils/      # script comuni
  Modulo3_MarsiliGabriele.zip   # archivio di consegna
```

Struttura standard di un modulo:
```
module_X/
  comando_module_X.pdf          # CONTRATTO
  README.md                     # istruzioni di build/run
  Makefile / makefile
  include/  src/                # codice
  scripts/                      # build, run cluster, plot
  results/                      # local + cluster CSV/log
    cluster/                    # SOURCE OF TRUTH per il report
  report/                       # LaTeX + figure + PDF
  tests/                        # validation
```

---

## 15. Checklist di consegna modulo

Prima di creare lo zip / pushare:

- [ ] `comando_module_X.pdf` riletto, ogni requisito coperto.
- [ ] Codice compila pulito senza warning.
- [ ] Validation parallelo == sequenziale.
- [ ] CSV cluster aggiornati, grafici rigenerati da quei CSV.
- [ ] Grafici ispezionati visivamente (assi, unità, legende).
- [ ] Numeri nel report coincidono con CSV/figure.
- [ ] Ogni scelta tecnica motivata con riferimento alla teoria.
- [ ] Report passato in `humanizer` mantenendo registro accademico.
- [ ] README aggiornato (build, run locale, run cluster).
- [ ] Commit con messaggio in stile conventional (`/caveman-commit`).
