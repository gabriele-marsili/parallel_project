# Esp. 4: distribuzione del carico nella fase join

**Obiettivo.** Motivare la scelta "partitions distributed cyclically" e spiegare LPT, con
esperimenti sotto carico uniforme e skewed.

**Come.** `join_lb.cpp` partiziona R e S con la hash fib, poi esegue SOLO la fase join con 4
strategie di assegnamento delle P partizioni ai k thread: cyclic (progetto), block, dynamic
(contatore atomico), LPT (ordina per costo, assegna al thread meno carico). Include un
generatore con `-skew RHO -hot H` (partizioni calde) per creare sbilanciamento. Misura tempo
di parete e imbalance = max(busy)/mean(busy).

```bash
salloc --partition=normal --cpus-per-task=32 --exclusive --time=00:10:00
bash run_join_lb.sh
python3 plot_join_lb.py
```

## Risultati (node02, NR=10M, P=128, t=16, mean +/- std su 15 run)

| strategia | uniforme (ms, imb) | skew 0.9 hot=4 (ms, imb) | sched |
|---|---|---|---|
| cyclic (progetto) | 33.1±1.0, 1.08 | 95.4±0.5, 3.78 | 7.5 us |
| block | 33.5±4.3, 1.10 | **256.9±0.5, 10.79** | 8.9 us |
| dynamic | 32.6±1.4, 1.06 | 96.0±1.1, 3.74 | 1.4 us |
| LPT | 33.4±4.2, 1.09 | 92.8±0.7, 3.66 | 20.1 us |

Pavimento teorico (partizione più calda / quota media) = **3.62**: nessuno può scendere sotto.

## Lettura (onesta)

1. **Uniforme** (il regime del Modulo 2): le 4 strategie sono **indistinguibili entro il rumore**
   (spread medie 0.9 ms, std fino a 4.3 ms; imbalance 1.06-1.09). Nessuna è più veloce.
2. **Skew**: block collassa (imbalance 10.8, derivazione nel companion: 3 partizioni calde in un
   blocco -> 67% del lavoro su un thread). LPT è un filo MIGLIORE (imb 3.66), cyclic un filo
   PEGGIORE (3.78); ~3%, tutti al pavimento 3.62 (= 22.6% partizione più calda / 6.25% quota equa).
3. I costi delle alternative sono **trascurabili misurati**: sort LPT 20 us, cyclic 7.5, dynamic
   1.4 us, su un join da ~90 ms = 0.02%. L'atomica di dynamic a P=128 non degrada nulla.
4. **Perché cyclic**: NON per velocità (è pari o un filo peggiore), ma per **semplicità** (una
   formula `pid += nt`, niente atomica/sort/stato condiviso), a parità di prestazioni sotto il
   workload uniforme del Modulo 2. Il margine di LPT esiste solo sotto skew = Modulo 3. Non è "la
   migliore in assoluto", e va detto. L'unica da bocciare è il block.
