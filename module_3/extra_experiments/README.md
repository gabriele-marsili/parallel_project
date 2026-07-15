# Modulo 3 — esperimenti aggiuntivi (materiale per l'orale)

Materiale a sé, separato dal codice e dal report consegnati (che non vengono toccati).

- `esperimenti_aggiuntivi_M3.pdf` — deck da mostrare: un grafico per pagina con commento.
- `COMPANION_M3.pdf` — companion di studio: risposte al todo, walkthrough del report,
  esperimenti, deep dive OpenMP, punti di onestà, cheat-sheet.
- `common/omp_ablation.cpp` — copia strumentata di `src/hashjoin_OpenMP.cpp` con knob a
  runtime: `-sched`, `-order`, `-hotmul`, `-pf-scatter`, `-pf-probe`, `-firsttouch`,
  `-tchunks`, `-reps` (più macro `NO_NOWAIT`).
- `common/seq_opts.cpp` — baseline sequenziale con i prefetch attivabili (esp. 6).
- `0N_*/run.sh` — script per il compute node; `0N_*/plot_*.py` — grafici dai CSV;
  `0N_*/results/` — dati misurati (nodo Ivy Bridge, job SLURM esclusivi 696505-696510).

Esecuzione: `sbatch --job-name=m3_expN sbatch_one.sh 0N_cartella` dalla home del cluster
(`~/module_3/extra_experiments`). Build dei PDF: `bash build_deck.sh`, `bash build_companion.sh`.
