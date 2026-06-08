# Module 4: Distributed Partitioned Hash Join (MPI)

Implementazione distribuita della partitioned hash join con MPI e variante
ibrida MPI+OpenMP.

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

La baseline sequenziale (la versione migliorata di M3, usata per speedup e
verifica di correttezza) è inclusa in questo modulo (`src/hashjoin_seq.cpp`):

```sh
make seq          # oppure: make hashjoin_seq
```

Il modulo è autonomo: gli header e il sorgente sequenziale sono copiati
qui, non servono altri moduli per compilare o eseguire.

## Run locale

```sh
mpirun -n 4 ./hashjoin_mpi      -nr 1000000 -ns 2000000 \
                                -seed 42 -max-key 500000 -p 32

mpirun -n 2 ./hashjoin_mpi_omp  -nr 1000000 -ns 2000000 \
                                -seed 42 -max-key 500000 -p 32 -t 4
```

Vincoli sui parametri:

- `P` deve essere potenza di due, `>= nranks`, e multiplo di `nranks`.
- `seed` e `max_key` identici fra la versione MPI e il sequenziale:
  garantisce input byte-per-byte equivalente per la verifica di correttezza.

## Test di correttezza

```sh
bash scripts/validate_local.sh
```

Compara `join_count`, `checksum1`, `checksum2` prodotti da `hashjoin_mpi` /
`hashjoin_mpi_omp` per `nranks ∈ {1, 2, 4, 8}` contro `./hashjoin_seq` sugli
stessi parametri di input. Niente verifica dentro la regione misurata.

## Run sul cluster

Flusso (dettagli in `scripts/deploy_and_run.sh`):

```sh
bash scripts/deploy_and_run.sh           # sync, build, smoke test
sbatch scripts/run_seq_baseline.sh       # baseline sequenziale
sbatch scripts/run_strong.sh             # strong scaling 1/2/4/8 nodi (uniform + skewed)
sbatch scripts/run_weak.sh               # weak scaling
sbatch scripts/run_breakdown.sh          # breakdown comm vs join
```

Lo skewed dataset è prodotto da `run_strong.sh` e `run_breakdown.sh` con i
flag `-skew/-hot`, non da uno script separato.

I CSV finiscono in `results/cluster/` e sono la fonte ufficiale per il
report.

## Layout

```
module_4/
  README.md         # questo file
  Makefile
  include/          # header (copia da M3) + mpi_common.hpp, mpi_pipeline.hpp
  src/
    hashjoin_mpi.cpp        # MPI puro
    hashjoin_mpi_omp.cpp    # MPI + OpenMP
    hashjoin_seq.cpp        # baseline sequenziale (vendored da M3)
  scripts/          # validazione locale, deploy, run cluster, plot
  results/cluster/  # source of truth per il report
  report/           # LaTeX + figure + PDF finale
```

## Riprodurre i risultati sul cluster

Build cluster: `bash scripts/deploy_and_run.sh --sync-only` sincronizza
M4 sul cluster e compila i binari (MPI, ibrido e baseline sequenziale) con
`-march=ivybridge` (la ISA dei nodi di calcolo, non Haswell come il login). Per la sola validation:
`bash scripts/deploy_and_run.sh --validate-only`. Per eseguire tutti i
benchmark (seq baseline + strong + weak + breakdown):
`bash scripts/deploy_and_run.sh --bench-only`. Il fetch dei CSV verso
`results/cluster/` avviene con `bash scripts/deploy_and_run.sh --fetch`.

I grafici si rigenerano con `python3 scripts/plots/plot_*.py` e
finiscono in `report/`.