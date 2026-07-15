# Modulo 4 — esperimenti aggiuntivi (materiale per l'orale)

Materiale a sé, separato dal codice e dal report consegnati (che non vengono toccati).

- `esperimenti_aggiuntivi_M4.pdf` — deck da mostrare: un grafico per pagina con commento.
- `COMPANION_M4.pdf` — companion di studio: risposte al todo, walkthrough, esperimenti,
  deep dive MPI, punti di onestà, cheat-sheet.
- `common/` — sorgenti: `alltoallv_bench.cpp` (microbenchmark della collettiva),
  `pingpong_bench.cpp` (alpha/beta di Hockney), `mpi_remap.cpp` (pipeline con mappa
  partizione-rank parametrica mod/greedy e barrier disattivabili),
  `threadlevel_bench.cpp` (driver ibrido con -threadlevel e -reps, riusa la pipeline
  consegnata).
- `0N_*/run.sbatch` — job SLURM multi-nodo; `0N_*/plot_*.py` — grafici dai CSV;
  `0N_*/results/` — dati misurati (job 696527-696532, nodi Ivy Bridge, fino a 8 nodi).

Esecuzione: `bash build.sh` sul login node (compila in `bin/` con -march=ivybridge),
poi `sbatch 0N_cartella/run.sbatch` da `~/module_4/extra_experiments`.
Build dei PDF: `bash build_deck.sh`, `bash build_companion.sh`.
