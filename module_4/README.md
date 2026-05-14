# Module 4 — Distributed Partitioned Hash Join (MPI)

Implementazione distribuita della partitioned hash join con MPI e variante
ibrida MPI+OpenMP. Vedi `GUIDE.md` per la motivazione delle scelte tecniche
e i riferimenti alle lezioni del corso.

## Build

Locale (richiede un'installazione MPI con `mpicxx` nel PATH, p.es. Open MPI
via Homebrew: `brew install open-mpi`):

```sh
make            # costruisce hashjoin_mpi e hashjoin_mpi_omp
make mpi        # solo MPI puro
make hybrid     # solo MPI+OpenMP
```

Sul cluster (dopo `module load` del compilatore richiesto):

```sh
make cluster CXX=g++-XX MPICXX=mpicxx
```

Per rigenerare la baseline sequenziale (codice di M3, usata per speedup e
verifica di correttezza):

```sh
make seq_baseline
```

## Run locale

```sh
mpirun -n 4 ./hashjoin_mpi      -nr 1000000 -ns 2000000 \
                                -seed 42 -max-key 500000 -p 32

mpirun -n 2 ./hashjoin_mpi_omp  -nr 1000000 -ns 2000000 \
                                -seed 42 -max-key 500000 -p 32 -t 4
```

Vincoli sui parametri:

- `P` deve essere potenza di due, `>= nranks`, e multiplo di `nranks`.
- `seed` e `max_key` identici fra la versione MPI e il sequenziale di M3:
  garantisce input byte-per-byte equivalente per la verifica di correttezza.

## Test di correttezza

```sh
bash scripts/validate_local.sh
```

(da fare nella prossima fase). Compara `join_count`, `checksum1`, `checksum2`
prodotti da `hashjoin_mpi` / `hashjoin_mpi_omp` per `nranks ∈ {1, 2, 4, 8}`
contro `../module_3/hashjoin_seq` sugli stessi parametri di input. Niente
verifica dentro la regione misurata.

## Run sul cluster

Sketch del flusso (dettagli in `scripts/deploy_and_run.sh`, ancora da scrivere):

```sh
bash scripts/deploy_and_run.sh           # sync, build, smoke test
sbatch scripts/run_strong.sh             # strong scaling 1/2/4/8 nodi
sbatch scripts/run_weak.sh               # weak scaling
sbatch scripts/run_breakdown.sh          # breakdown comm vs join
sbatch scripts/run_skewed.sh             # skewed dataset
```

I CSV finiscono in `results/cluster/` e sono la fonte ufficiale per il
report.

## Layout

```
module_4/
  GUIDE.md          # decisioni di design (contratto del modulo)
  README.md         # questo file
  Makefile
  include/          # symlink agli header di M3 + mpi_common.hpp
  src/
    hashjoin_mpi.cpp        # MPI puro
    hashjoin_mpi_omp.cpp    # MPI + OpenMP
  scripts/          # validazione locale, deploy, run cluster, plot
  tests/
  results/cluster/  # source of truth per il report
  report/           # LaTeX + figure + PDF finale
```

## Stato attuale

- [x] Scaffold (Makefile, sorgenti compilabili, header riusati da M3).
- [x] Pipeline MPI completa con `MPI_Alltoallv`.
- [x] Variante ibrida MPI+OpenMP (riuso del kernel di M3 per il local join).
- [x] Validation locale vs `hashjoin_seq` di M3 (36/36 PASS).
- [x] Validation cluster (18/18 PASS, uniform + skewed).
- [x] Campagna sperimentale su spmcluster (strong, weak, breakdown, skewed).
- [x] Grafici e report PDF.

## Riproducibilità della campagna

Build cluster: `bash scripts/deploy_and_run.sh --sync-only` sincronizza
M3 e M4 sul cluster e compila i binari con `-march=ivybridge` (la ISA dei
nodi di calcolo, non Haswell come il login). Per la sola validation:
`bash scripts/deploy_and_run.sh --validate-only`. Per la campagna
completa (seq baseline + strong + weak + breakdown):
`bash scripts/deploy_and_run.sh --bench-only`. Il fetch dei CSV verso
`results/cluster/` avviene con `bash scripts/deploy_and_run.sh --fetch`.

I grafici si rigenerano con `python3 scripts/plots/plot_*.py` e
finiscono in `report/`. Il report PDF si ricostruisce con
`(cd report && pdflatex -interaction=nonstopmode report.tex)`.
