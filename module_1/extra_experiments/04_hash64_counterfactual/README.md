# Esperimento 4 — Counterfactual hash 32-bit vs 64-bit in AVX2

**Obiettivo.** Verificare il claim del report: "con una hash a 64 bit, l'AVX2 sarebbe più
lento dello scalare, per via della decomposizione a 3× vpmuludq". Misurato su node09.

**Come.** Un solo binario (`avx2_hash64.cpp`, riusa `common.hpp`) implementa e cronometra
quattro kernel a N=10⁸, P=256, verificando la correttezza (AVX2 == scalare) per checksum:

- `scalar32`: `((k_lo^k_hi)·A32) >> (32-log2P)` (hash del report, no autovec).
- `avx2_32`: idem in intrinsics, 1 `vpmulld`, 8 chiavi/iter.
- `scalar64`: `(k·A64) >> (64-log2P)` (multiplicative a 64 bit, no autovec).
- `avx2_64`: idem in intrinsics, decomposizione a 3× `vpmuludq`, 4 chiavi/iter.

```bash
srun --partition=gpu-excl --nodelist=node09 --ntasks=1 --cpus-per-task=1 --time=00:10:00 \
     bash module_1/extra_experiments/04_hash64_counterfactual/run_hash64.sh
python3 plot_hash64.py   # in locale
```

## Risultati misurati (node09, N=10⁸, P=256)

| kernel | Mkeys/s | speedup SIMD vs scalare pari-larghezza |
|---|---|---|
| scalar32 | 910 | riferimento 32-bit |
| avx2_32 | 1199 | **1.32× (SIMD aiuta)** |
| autovec32 (compiler) | 1325 | miglior risultato assoluto |
| scalar64 | 1100 | riferimento 64-bit |
| avx2_64 | 1075 | **0.98× (SIMD peggiora)** |

Correttezza: `avx2_32==scalar32 OK`, `avx2_64==scalar64 OK`.
objdump: il loop `avx2_32` usa `vpmulld`; il loop `avx2_64` usa **3 `vpmuludq`** + shift +
add (schoolbook per i 64 bit bassi del prodotto), come previsto.

## Lettura (come difenderlo all'orale)

1. **Claim del report confermato**: `avx2_64/scalar64 = 0.98×`. In AVX2 non esiste una
   mul 64×64 nativa: servono 3 `vpmuludq` (32×32→64) + shift + add per i 64 bit bassi,
   e si processano solo 4 chiavi per registro invece di 8. Il costo extra annulla (anzi
   inverte) il vantaggio SIMD. Con i 32 bit invece `vpmulld` fa 8 mul/registro in una
   istruzione → `avx2_32/scalar32 = 1.32×`.
2. **Sfumatura importante**: `scalar64 (1100) > scalar32 (910)`. In scalare la hash a 64
   bit è più semplice (una sola moltiplicazione, niente XOR-fold né estrazione delle due
   metà), quindi più veloce. Perciò i 32 bit **non** si scelgono perché più economici in
   scalare: si scelgono perché **vettorizzano** con un'istruzione nativa. È esattamente il
   punto: in un mondo senza SIMD la 64-bit sarebbe preferibile; con SIMD la 32-bit vince.
3. **Il migliore in assoluto resta l'autovec a 32 bit (1325)**: la vettorizzazione della
   hash 32-bit (dal compilatore) batte tutto. Ranking: autovec32 > avx2_32 > scalar64 >
   avx2_64 > scalar32.
4. **Combinato con l'esperimento 1** (qualità): fib32 e fib64 hanno distribuzione
   identica → i 32 bit non costano nulla in qualità. Quindi la scelta 32-bit è "gratis"
   sul fronte distribuzione e "vincente" sul fronte SIMD. È la decisione giusta.

## File

- `avx2_hash64.cpp` — i 4 kernel + verifica + bench.
- `run_hash64.sh` — build/run/objdump su node09.
- `results/hash64_node09.txt`, `results/hash64_summary.csv` — misure.
- `plots/hash32_vs_hash64.png` — throughput scalare vs SIMD, per larghezza.
- `plots/simd_32_vs_64.png` — schema: 8 chiavi/1 vpmulld (32 bit) vs 4 chiavi/3 vpmuludq (64 bit).
