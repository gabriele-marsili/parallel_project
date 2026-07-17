# Esp. 6: MPI_THREAD_FUNNELED contro MPI_THREAD_MULTIPLE

**Obiettivo.** Il report sostiene che MULTIPLE aggiunge locking interno senza beneficio per questo
codice, dato che tutte le collettive stanno fuori dalle regioni OpenMP. È un'affermazione, non una
misura: qui si misura.

**Come.** `common/threadlevel_bench.cpp` esegue la stessa pipeline ibrida (4 rank su 4 nodi, 32
thread) chiedendo il livello a runtime con `-threadlevel funneled|multiple`. Il livello
effettivamente fornito da `MPI_Init_thread` è riportato nella colonna `provided` del CSV e va
verificato, perché la libreria può concedere meno di quanto richiesto. Qui `provided` coincide
sempre con il livello richiesto.

```bash
bash ../build.sh
sbatch run.sbatch            # 4 nodi, 15 min
python3 plot_threadlevel.py
```

## Risultati (4 rank x 32 thread, NR=50M, NS=100M, P=256, mediana di 5 rep)

| workload | livello | totale [s] | wall [s] |
|---|---|---|---|
| uniform | funneled | 0.488 | 0.577 |
| uniform | multiple | 0.498 | 0.587 |
| skew    | funneled | 0.935 | 1.125 |
| skew    | multiple | 0.931 | 1.118 |

## Lettura

1. **La differenza è dentro il rumore**: sotto il 2% su entrambi i carichi e di segno opposto
   (MULTIPLE +1.9% su uniforme, -0.4% su skewed). Con segni discordi non c'è una direzione
   stabile, quindi il costo non è misurabile su questa pipeline.
2. **Il meccanismo spiega il risultato.** MULTIPLE obbliga la libreria a proteggere il proprio
   stato interno perché più thread possono chiamare MPI insieme. Qui ogni chiamata MPI è emessa
   dal solo main thread, fuori dalle regioni OpenMP: i lock vengono acquisiti ma mai contesi, e un
   lock non conteso costa un'operazione atomica, cioè decine di nanosecondi contro fasi da
   centinaia di millisecondi.
3. **Formulazione corretta della scelta.** L'argomento del report è potenziale e su questo codice
   non si osserva. La ragione difendibile per FUNNELED è che richiedere il livello minimo
   necessario documenta il contratto di concorrenza (solo il main thread chiama MPI) e non lascia
   costi latenti se la pipeline cambiasse pattern di chiamata o implementazione MPI. Non è un
   argomento prestazionale.
