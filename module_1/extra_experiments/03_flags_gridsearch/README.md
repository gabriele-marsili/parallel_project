# Esperimento 3 — Grid search dei flag di compilazione

**Obiettivo.** Motivare con numeri i flag del Makefile e isolare il contributo di
ciascuno. Include il caso naive senza flag (`-O0`).

**Come.** Stesso sorgente scalare consegnato (`src/plain.cpp`), ricompilato con 9 set di
flag, misurato a N=10⁸, P=256 su node09 (11 rip, mediana).

```bash
srun --partition=gpu-excl --nodelist=node09 --ntasks=1 --cpus-per-task=1 --time=00:15:00 \
     bash module_1/extra_experiments/03_flags_gridsearch/run_flags.sh
python3 plot_flags.py   # in locale
```

## Risultati (node09, Mkeys/s)

| configurazione | throughput | lettura |
|---|---|---|
| O0 (naive) | 145 | nessuna ottimizzazione: 9× più lento |
| O1 | 906 | register allocation, niente vettorizzazione |
| O2 | 912 | GCC a O2 NON auto-vettorizza i loop di default |
| O3 | 1216 | **O3 attiva `-ftree-vectorize`: +33%** (vettori generici/SSE) |
| O3 + march=native | 1317 | ISA e tuning del nodo: **+8%** su O3 (il contributo è il tuning, punto 2) |
| O3 + march + novec (baseline) | 908 | `-fno-tree-vectorize` riporta al livello scalare |
| O3 + march + vec (autovec) | 1325 | configurazione consegnata |
| O3 + march + vec + unroll | 1339 | `-funroll-loops`: **+1%** marginale |
| Ofast + march | 1332 | `-Ofast` = O3 + fast-math: **nessun guadagno** (kernel intero) |

## Lettura (come difenderlo all'orale)

1. **Il salto O2 → O3 (912 → 1216, +33%) è esattamente la vettorizzazione**: a O2 GCC
   non auto-vettorizza i loop, a O3 sì (`-ftree-vectorize` è on da O3). Questa è la prova
   isolata che lo speedup dell'autovec viene dalla vettorizzazione.
2. **`-march=native` vale +8%** (1216 → 1317), ma il meccanismo non è l'ampiezza dei
   vettori. Il confronto con l'ablation lo mostra: abilitare AVX2 **senza** `-march`
   (`-O3 -mavx2 -mfma -ftree-vectorize` → 1114) sta **sotto** `-O3` puro (1216), che
   vettorizza con la sola SSE2 del baseline x86-64. L'unica variabile fra i due è
   `-mavx2 -mfma`, quindi l'ISA più larga da sola **costa** l'8%, e il guadagno di
   `-march=native` va attribuito al tuning per la microarchitettura (Zen1). Le due misure
   sono confrontabili: stesso sorgente, stesso nodo, stessi N/P/seed/ripetizioni, e la
   banda di riproducibilità è ~1% (autovec misurato 1324.6 nella grid e 1312.5
   nell'ablation), mentre lo scarto qui è dell'8%.
   *Ipotesi non verificata in questo esperimento:* su Zen1 le istruzioni vettoriali a 256
   bit sono eseguite come due micro-op da 128 bit, e il tuning `znver1` di GCC preferisce
   di conseguenza vettori a 128 bit; senza `-march` il tuning generico userebbe i 256 bit
   pagandone lo split. Servirebbe l'ispezione dell'assembly emesso sul nodo (`ymm` contro
   `xmm`) per confermarlo.
3. **baseline vs autovec = 908 vs 1325 = 1.46×**: la baseline è "O3+march con
   vettorizzazione OFF"; tutto il guadagno è attribuibile alla sola vettorizzazione.
   Coincide con l'1.43–1.46× del report.
4. **`-funroll-loops` dà +1%**: in un kernel memory-bound srotolare non aiuta (il collo è
   la memoria, non l'issue delle istruzioni). Giustamente non è nel Makefile.
5. **`-Ofast` non serve e sarebbe rischioso**: abilita fast-math (riassociazione FP non
   IEEE), inutile su un kernel a interi e potenzialmente pericoloso altrove. Escluso a
   ragione.

Conclusione: i flag consegnati (`-O3 -march=native -mavx2 -mfma`, con/senza
`-ftree-vectorize`) sono la scelta corretta e minimale; ogni flag aggiuntivo o è neutro
(`-Ofast`) o marginale (`-funroll-loops`).

## Ablation dalla config autovec consegnata

La grid sopra è una "scala" (O0→O3→+flag). Per giustificare **esattamente i flag
dell'autovec del report** serve invece un'ablation: si parte dalla config consegnata e si
toglie/cambia un flag alla volta (`run_ablation.sh`).

| config | throughput | Δ vs autovec |
|---|---|---|
| autovec consegnato (riferimento) | 1312 | — |
| − ftree-vectorize | 903 | **−31%** |
| − march=native | 1114 | **−15%** |
| O3 → O2 | 1320 | +1% |
| O3 → O1 | 1317 | +0% |
| + funroll-loops | 1316 | +0% |

**Risultato preciso:** lo speedup poggia **solo su due flag**, `-ftree-vectorize` (−31% se
tolto) e `-march=native` (−15%). Il **livello di ottimizzazione è quasi irrilevante** una
volta accesa esplicitamente la vettorizzazione: O1/O2/O3 danno lo stesso throughput (~1315).
Questo raffina il "salto O2→O3" della grid: quel +33% era la vettorizzazione (O3 la accende
di default, O2 no), non l'ottimizzazione in sé. `-funroll-loops` e `-Ofast` sono neutri.
Quindi la config consegnata è giustificata: i due flag che contano ci sono, e nulla di
superfluo aggiungerebbe qualcosa.

## File

- `run_flags.sh` — grid a scala (O0→Ofast) su node09.
- `run_ablation.sh` — ablation dalla config autovec (toglie un flag alla volta).
- `results/flags_node09.csv`, `results/ablation_node09.csv` — misure.
- `plots/flags_gridsearch.png` — throughput per configurazione (grid a scala).
- `plots/flags_ablation.png` — contributo di ogni flag (ablation dall'autovec).
