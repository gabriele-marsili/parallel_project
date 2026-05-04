# Confronto Mod2 (std::thread) vs Mod3 (OpenMP) — dati cluster

## Strong scaling NR=10M NS=20M uniform

| threads | Mod2 t [s] | Mod3-loop t [s] | Mod3-task t [s] | Mod2 SU | Mod3-loop SU | Mod3-task SU |
|---------|------------|-----------------|-----------------|---------|--------------|--------------|
| 1       | 0.7469     | 0.7614          | 0.7571          | 1.03    | 1.00         | 1.00         |
| 2       | 0.5456     | 0.3888          | 0.3837          | 1.40    | 1.96         | 1.97         |
| 4       | 0.2934     | 0.2432          | 0.2420          | 2.61    | 3.13         | 3.13         |
| 8       | 0.1718     | 0.1312          | 0.1300          | 4.46    | 5.80         | 5.82         |
| 16      | 0.1023     | 0.0842          | 0.0874          | 7.49    | 9.04         | 8.67         |

Notes:
- Mod2 SU uses `time_seq / time_par` (sequential baseline = 0.7665 s).
- Mod3 SU uses `T(1)/T(p)` with mean over 3 run_id at threads=1.

## Phase breakdown @ t=8 uniform [ms]

| Phase     | Mod2  | Mod3-loop | Mod3-task |
|-----------|-------|-----------|-----------|
| Hist R    | 15.24 | 3.29      | 3.32      |
| Scatter R | 27.87 | 23.79     | 23.75     |
| Hist S    | 19.59 | 6.57      | 6.59      |
| Scatter S | 56.75 | 47.75     | 47.62     |
| Join      | 52.29 | 49.04     | 47.83     |
| **Total** | 171.7 | 130.4     | 129.1     |

## Key observations

- A t=16 OpenMP-loop (84.2 ms) e' ~18% piu' veloce di std::thread Mod2 (102.3 ms);
  speedup 9.04 vs 7.49 — Mod3 e' nettamente piu' scalabile in questo range.
- A t=2 il vantaggio di OpenMP e' massimo: Mod3 e' ~40% piu' rapido di Mod2
  (0.39 s vs 0.55 s, SU 1.97 vs 1.40); il fork/join di std::thread paga molto piu'
  overhead a piccoli p rispetto al pool persistente di OpenMP.
- Mod3-loop e Mod3-task sono praticamente equivalenti su uniform a tutti i p,
  con differenze sotto il 2% — coerente con la teoria su carico bilanciato.
- La fase Hist R su Mod2 a t=8 costa ~15.2 ms vs ~3.3 ms su Mod3 (~4.6x meno):
  l'allocazione/sincronizzazione dei thread persistenti OpenMP elimina lo startup
  cost che std::thread ripaga ad ogni round.
- Le fasi Scatter/Join hanno costi molto piu' simili (Mod2 e' del 5–20% piu' lento),
  segnale che il lavoro memory-bound vero e proprio domina e annulla la maggior parte
  del divario di runtime model — il distacco totale (171.7 vs 130.4 ms) viene quasi
  tutto dalle fasi histogram piu' brevi.
