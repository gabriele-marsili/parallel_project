# Esperimento 5 — Rerun CPU + CUDA su node09 (riproduzione tabelle)

**Obiettivo.** Rifare le run (incluso CUDA) su node09 per verificare che i numeri del
report si riproducano. Scrive in questa cartella, non tocca i CSV consegnati.

```bash
srun --partition=gpu-excl --nodelist=node09 --ntasks=1 --cpus-per-task=1 --time=00:20:00 \
     bash module_1/extra_experiments/05_rerun/run_rerun.sh
python3 plot_rerun.py   # in locale
```

## CPU (Tabella 1) — riprodotta

Throughput (Mkeys/s), P=256, mediana su 11 rip:

| N | baseline | autovec (S) | avx2 (S) |
|---|---|---|---|
| 10⁶ | 923 | 1394 (1.51×) | 1287 (1.39×) |
| 10⁷ | 912 | 1306 (1.43×) | 1182 (1.30×) |
| 5·10⁷ | 908 | 1326 (1.46×) | 1211 (1.33×) |
| 10⁸ | 911 | 1330 (1.46×) | 1193 (1.31×) |
| 2·10⁸ | 903 | 1333 (1.48×) | 1199 (1.33×) |

Coincide col report (baseline ~910, autovec ~1.43–1.46×, avx2 ~1.30×). Speedup piatto
su tutte le taglie → kernel bandwidth-bound a ogni N. Checksum identici al report.

## CUDA (Tabella 2) — riprodotta, con una scoperta sull'affinità NUMA

Il kernel GPU è invariato (~66.6 GMkeys/s = ~800 GB/s HBM, A30, 24 GB). Ma
l'**end-to-end dipende dal posizionamento NUMA** del processo host su node09 (EPYC
2 socket, 8 domini NUMA; la GPU è sul dominio 4):

| binding host | H→D | D→H | totale | e2e (Mkeys/s) | vs baseline CPU |
|---|---|---|---|---|---|
| NUMA node 0 (GPU-far) | 108.4 ms | 64.3 ms | 174.2 ms | **574** | 0.63× (GPU perde) |
| **NUMA node 4 (GPU-local)** | 63.3 ms | 35.2 ms | 99.9 ms | **1000** | 1.11× |
| report (Tab. 2) | 63.2 ms | 32.3 ms | 97.0 ms | 1031 | 1.12× |

Il **1031 del report è riproducibile solo bindando al dominio NUMA della GPU** (node 4):
lì la banda PCIe è ~12.6 GB/s; sul socket lontano scende a ~7.4 GB/s e l'e2e crolla a
574. Verificato con `numactl --cpunodebind=N --membind=N` (non ipotizzato): node 4 → 1000,
node 5/6 → ~965, node 0 → 573. La topologia `nvidia-smi topo -m` dà `GPU0 NUMA affinity = 4`.

## Lettura (come difenderlo all'orale)

1. **Tabella 1 confermata**: gli stessi rapporti di speedup, stessi checksum. Il modulo è
   riproducibile.
2. **Le PCIe dominano** (98–99% del tempo end-to-end): il kernel a 1.5 ms è invisibile. La
   tesi del report "offloadare questo kernel isolato non conviene" **regge in entrambi i
   casi**: al meglio (GPU-local) è 1.11× sul baseline, al peggio (GPU-far) 0.63× (perde).
3. **Novità difendibile**: il numero del report (1031) non è "sbagliato", ma **fragile**:
   assume implicitamente il placement GPU-local. Il codice/script consegnato non fa
   binding, quindi il valore dipende dallo scheduler SLURM. Un offload robusto pinnerebbe
   l'host al dominio NUMA della GPU (lez. 18, thread/memory affinity; qui vale ~1.75× di
   banda PCIe). È un ottimo aggancio teoria↔misura.
4. **Anche nel caso migliore la GPU non paga**: 1.11× a fronte di transfer che sono il 98%
   del tempo. Conviene solo con dati già on-device o pipeline successive su GPU.

## File

- `run_rerun.sh` — sweep CPU + CUDA su node09.
- `results/rerun_node09.txt` — output grezzo.
- `results/cpu_rerun.csv`, `results/cuda_numa.csv` — misure per i grafici.
- `plots/cpu_rerun_vs_N.png` — throughput CPU vs N (Tabella 1).
- `plots/cuda_numa_breakdown.png` — breakdown CUDA e sensibilità NUMA.
