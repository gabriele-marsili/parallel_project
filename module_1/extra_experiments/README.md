# Modulo 1 — Esperimenti extra per l'orale

Materiale **aggiuntivo** per la preparazione dell'orale. Non modifica report né codice
consegnati. Tutti i numeri sono misurati **su node09** (AMD EPYC 7301, `gpu-excl`), lo
stesso nodo del report.

**Punto di partenza:** [`COMPANION_M1.md`](COMPANION_M1.md) / `COMPANION_M1.pdf` —
walkthrough del report, risposte ai dubbi del todo, sintesi dei cinque esperimenti.

## Esperimenti

| # | cartella | domanda del todo | finding |
|---|---|---|---|
| 1 | `01_hash_quality/` | motivare la hash, confronto vs mod, imbalance | fib32 robusta; mod collassa su chiavi strutturate; fib32≡fib64 in qualità |
| 2 | `02_bandwidth_ceiling/` | come si arriva al 96% della banda | tetto matched misurato 19.15 GB/s → autovec ~83%, non 96% |
| 3 | `03_flags_gridsearch/` | motivare i flag, grid search | O2→O3 = vettorizzazione (+33%); baseline vs autovec 1.46× |
| 4 | `04_hash64_counterfactual/` | 32 vs 64 bit (3× vpmuludq) | avx2_64/scalar64 = 0.98× (SIMD peggiora); avx2_32 = 1.32× |
| 5 | `05_rerun/` | rifare run compreso CUDA | Tab.1 riprodotta; CUDA e2e dipende da affinità NUMA (1000 vs 574) |

Ogni cartella ha `README.md` (metodo + comando `srun` + tabella + come difenderlo),
`results/` (CSV e output grezzi), `plots/` (grafici).

## Riprodurre

Ogni esperimento si lancia con lo stesso schema:
```bash
srun --partition=gpu-excl --nodelist=node09 --ntasks=1 --cpus-per-task=1 --time=00:15:00 \
     bash module_1/extra_experiments/NN_.../run_*.sh
python3 module_1/extra_experiments/NN_.../plot_*.py   # in locale
```
(l'esperimento 5, per il test NUMA della GPU, usa `--exclusive` + `numactl`.)
