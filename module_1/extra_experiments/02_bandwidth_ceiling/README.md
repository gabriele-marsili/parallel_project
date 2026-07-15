# Esperimento 2 — Ancorare il tetto di banda single-core (il "96%")

**Obiettivo.** Rispondere a "come è stato misurato il 96% della banda single-core?"
Il report afferma: banda single-core ~16.4 GB/s, autovec 15.7 GB/s = 96% del tetto.

**Problema trovato.** Il valore 16.4 GB/s è dichiarato "misurato" in due punti
(`analysis/plot_results.py`: *"copy AVX2 256-bit, misurato"*; README della roofline
supplementare: *"già misurato"*), ma **nel repo non c'è nessun artefatto STREAM/likwid
su node09 che lo produca**: `stream_bandwidth.csv` contiene solo il nodo Intel. La stessa
`analisi-report-M1` elenca "ancorare il tetto di banda" tra gli esperimenti da fare.
Quindi il "96%" oggi poggia su un denominatore non verificato.

## Metodo (tutto su node09, single-core)

Nota sui byte: il report conta **12 B/chiave** (8 read + 4 write) con la convenzione
STREAM "solo traffico utile". Ma una store normale su una linea non in cache innesca un
**write-allocate (RFO)**: la linea viene letta prima di essere sovrascritta, quindi il
traffico DRAM reale è ~16 B/chiave. Per un tetto onesto misuro sia lo STREAM canonico sia
un microbench col **pattern identico al kernel** (read uint64 + write uint32).

- `stream_triad.c` (McCalpin) a 1 thread: Copy/Scale/Add/Triad su array di double.
- `mem_ceiling.cpp`: `read64` (sola lettura), `copy_tempo` (out32[i]=in64[i], store
  temporali), `copy_nt` (store non-temporali `_mm256_stream_si256`, niente RFO).
- I tre binari del modulo (`baseline`/`autovec`/`avx2`) per la banda RAGGIUNTA.

```bash
srun --partition=gpu-excl --nodelist=node09 --ntasks=1 --cpus-per-task=1 --time=00:15:00 \
     bash module_1/extra_experiments/02_bandwidth_ceiling/run_ceiling.sh
python3 plot_ceiling.py   # in locale
```

## Risultati misurati (node09, AMD EPYC 7301, single-core)

| tetto / kernel | GB/s | note |
|---|---|---|
| STREAM Copy | 26.63 | double, 16 B/iter |
| STREAM Triad | 19.30 | riferimento STREAM |
| STREAM Scale | 16.34 | **coincide col "16.4" del report** |
| copy matched (temporal) | **19.15** | pattern del kernel, 12 B utili (25.5 con RFO) |
| copy matched (non-temporal) | 19.45 | store NT, niente RFO |
| autovec (kernel) | 15.93 | 1327 Mkeys/s × 12 B |
| avx2 (kernel) | 14.38 | 1199 Mkeys/s × 12 B |
| baseline (kernel) | 10.91 | 909 Mkeys/s × 12 B |

## Provenienza del "16.4" (verifica con likwid-bench)

`likwid-bench` single-core su node09 conferma che **non esiste un tetto unico**: dipende dal
kernel e dalla convenzione byte. `load` 15.84, `copy` 13.67, `copy_avx` 15.07, `stream_avx`
18.00, `triad_avx` **19.31** GB/s. I benchmark "generici" (load/copy) stanno a 13.7-15.8 e,
con STREAM Scale (16.34), formano la nuvola in cui **cade il 16.4 del report**: quindi il
16.4 è un valore plausibile di banda generica, non inventato. Ma i benchmark col pattern
matched (triad_avx 19.31, STREAM Triad 19.30, copia read-u64/write-u32 19.15) **convergono su
~19 GB/s**: è il tetto giusto per il kernel. Dettagli in `results/likwid_node09.txt`.

## Ripetibilità (dipende dallo stato della macchina?)

Sì e no. I valori **assoluti** sono specifici di node09 nel suo stato (allocazione
esclusiva, single-core, memoria locale); su un'altra macchina o sotto contesa sarebbero
diversi. Ma sotto `--exclusive` sono **molto ripetibili**: tre run consecutivi danno
`copy_tempo` = 19.05 / 19.01 / 18.96 GB/s (spread < 0.5%), `copy_nt` = 19.45 / 19.46 /
19.41, `read64` = 17.64 / 17.65 / 17.67. Inoltre tre misure indipendenti dello stesso
regime (STREAM Triad 19.30, copy_tempo ~19.0, copy_nt ~19.4) **concordano** → il tetto
single-core ~19 GB/s è una proprietà stabile del sottosistema di memoria, non un caso.
Soprattutto: il **rapporto** kernel/tetto (~83%) è robusto perché numeratore e denominatore
sono misurati sulla **stessa macchina, stesso stato, back-to-back**; è proprio questo che il
16.4 del report non garantiva (denominatore di provenienza ignota).

## Conclusione (come difenderlo all'orale)

1. **Il tetto giusto per questo kernel è la copia matched: 19.15 GB/s** (stesso pattern
   read u64 / write u32, stessa convenzione 12 B). Rispetto a quello, l'autovec è a
   **15.93 / 19.15 = 83%**, non 96%.
2. **Il "16.4" del report coincide con lo STREAM Scale (16.34)**, non col Copy come dice
   il commento nel codice. Usare 16.4 come denominatore gonfia la saturazione al 96%.
3. **Il kernel resta memory-bound**: baseline 10.9, autovec 15.9, avx2 14.4 GB/s, tutti
   lontanissimi dal picco compute. La tesi qualitativa del report (memory-bound) è
   corretta; è la percentuale di saturazione a essere ottimistica.
4. **Il ~17% di margine** verso la copia pura è il costo del **compute hash parzialmente
   esposto**: a singolo core il controller DRAM non è saturo (serve tutto il socket),
   quindi la parte aritmetica non è completamente nascosta sotto la memoria. In
   elementi/s: memoria ~1.60 Gelem/s, kernel 1.33 Gkeys/s → 83%.
5. **Store non-temporali: guadagno single-core trascurabile** (19.45 vs 19.15). L'RFO non
   è il collo a 1 core; lo diventa a socket pieno. (Pre-risponde all'idea "NT stores
   darebbero ~25%": non a single-core.)

Messaggio onesto: il report ha ragione sul regime (memory-bound) ma il "96%" non è
ancorato; il numero difendibile, misurato su node09, è ~83% del tetto di copia matched.

## File

- `stream_triad.c`, `mem_ceiling.cpp`, `run_ceiling.sh` — sorgenti ed esecutore su node09.
- `results/ceiling_node09.txt` — output grezzo della run.
- `results/ceiling_summary.csv` — tabella per il grafico.
- `plots/bandwidth_ceiling.png` — kernel vs tetti misurati.
- `plots/access_pattern.png` — schema: pattern di byte di STREAM Scale (16.4) vs kernel (19.15).
- `results/likwid_node09.txt` — banda single-core con likwid-bench (provenienza del 16.4).
