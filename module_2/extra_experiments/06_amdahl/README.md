# Esp. 6: la frazione seriale di Amdahl (come si stima, e perché è apparente)

**Obiettivo.** Spiegare rigorosamente da dove viene f ≈ 0.078 (S∞ ≈ 12.9) del report, e
dimostrare che è una frazione seriale **apparente**, non il codice seriale.

**Come.** Due cose:
1. Il fit: `plot_amdahl.py` rifà il least-squares del modello Amdahl `S(p)=1/(f+(1-f)/p)` sui
   dati di strong scaling consegnati (`../../results/strong_scaling.csv`), con R² e residui.
2. La misura del seriale VERO: `serial_fraction.cpp` replica fedelmente la pipeline consegnata
   e cronometra SEPARATAMENTE la barrier completion (merge dei k istogrammi + prefix sum +
   offset), che è l'unica parte davvero seriale.

```bash
salloc --partition=normal --cpus-per-task=32 --exclusive --time=00:10:00
bash run_amdahl.sh
python3 plot_amdahl.py    # in locale (usa anche i CSV del report)
```

## Risultati (node01 Ivy Bridge)

- Fit NR=20M, P=128: **f = 0.078 -> S∞ = 12.9**, R² = 0.983.
- Fit P=512: f = 0.058 -> S∞ = 17.5.
- Seriale LETTERALE misurato (merge+prefix), P=128 @ t=32: **0.095%** del tempo.
- Seriale LETTERALE misurato, P=512 @ t=32: 1.09%.

## Perche' f e' un numero unico: il minimo della funzione errore E(f)

f non e' nessuna delle stime punto-per-punto (`(1/S - 1/p)/(1 - 1/p)`, che variano con p). E'
il valore che minimizza la funzione **errore totale**, che dipende solo da f (p e S sono i dati):

```
E(f) = somma su tutti i p di [ S_misurato(p) - 1/(f + (1-f)/p) ] al quadrato
```

Per ogni f si disegna la curva del modello, si misura di quanto manca ogni punto, si eleva al
quadrato e si somma. E(f) e' una conca con un solo fondo; quel fondo e' f. Tabella calcolata da
`plot_amdahl.py` sui dati NR=20M, P=128:

```
    f=0.020 -> E= 548.725
    f=0.050 -> E=  58.230
    f=0.070 -> E=   4.846
    f=0.078 -> E=   1.973   <- minimo
    f=0.085 -> E=   4.105
    f=0.094 -> E=  11.100
    f=0.120 -> E=  45.080
    f=0.200 -> E= 164.528
```

Il numero e' unico perche' la conca ha un solo fondo, non perche' i punti convergano ai p alti.
`curve_fit` trova questo minimo per via numerica (parte da 0.05, scende lungo la pendenza fino a
dove la derivata dE/df si annulla): da' f = 0.0776 -> S∞ = 12.9.

## Lettura (come difenderlo)

1. f si **stima col fit** (least-squares), non si conta dal codice. R²=0.98 -> i dati seguono
   davvero la forma di Amdahl, quindi f è un riassunto sensato.
2. Il codice seriale VERO è ~0.1% (misurato), **80× più piccolo** della f fittata (7.8%). Quindi
   la f del fit NON è codice seriale: incassa la **saturazione di banda** delle fasi memory-bound.
   Ecco perché il picco misurato (9.64×) resta sotto S∞=12.9: il limite è la banda, non un pezzo
   seriale fisso.
3. **Prova decisiva**: la f fittata CALA con P (0.078 -> 0.058, il join entra in L3), il seriale
   letterale CRESCE con P (0.095% -> 1.09%, merge O(P·k)). Direzioni opposte -> la f del fit
   misura la banda, non righe di codice.
