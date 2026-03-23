# avx2.cpp — Implementazione con intrinsics AVX2

## Panoramica

Implementazione del kernel partition_map usando intrinsics AVX2 espliciti.
La hash XOR-fold + Fibonacci mul32 permette di usare `_mm256_mullo_epi32`
(istruzione `vpmulld`), che è **nativa** in AVX2.

## Il loop principale: 8 chiavi per iterazione

```cpp
for (size_t i = 0; i < simd_end; i += 8) {
    // 1. Carica 8 chiavi uint64 in 2 registri ymm
    __m256i vk0 = _mm256_load_si256(&keys[i]);      // chiavi 0-3
    __m256i vk1 = _mm256_load_si256(&keys[i + 4]);   // chiavi 4-7

    // 2. XOR fold: k_lo ^ k_hi per ogni chiave
    __m256i hi0 = _mm256_srli_epi64(vk0, 32);   // estrai k_hi
    __m256i hi1 = _mm256_srli_epi64(vk1, 32);
    __m256i x0  = _mm256_xor_si256(vk0, hi0);   // XOR nei 32 bit bassi
    __m256i x1  = _mm256_xor_si256(vk1, hi1);

    // 3. Pack: combina 4+4 valori a 32 bit in un singolo ymm
    __m256i p0    = _mm256_permutevar8x32_epi32(x0, shuf);  // [x0,x1,x2,x3,?,?,?,?]
    __m256i p1    = _mm256_permutevar8x32_epi32(x1, shuf);  // [x4,x5,x6,x7,?,?,?,?]
    __m256i mixed = _mm256_permute2x128_si256(p0, p1, 0x20); // 8 valori

    // 4. Fibonacci mul32 NATIVA: 8 moltiplicazioni in 1 istruzione
    __m256i prod = _mm256_mullo_epi32(mixed, va32);

    // 5. Shift per partition id
    __m256i h = _mm256_srli_epi32(prod, shift);

    // 6. Store 8 partition id
    _mm256_storeu_si256(&part_ids[i], h);
}
```

## Conteggio istruzioni

| Istruzione               | Conteggio | Operazione                    |
|--------------------------|-----------|-------------------------------|
| `_mm256_load_si256`      | 2         | Carica 4+4 chiavi (aligned)   |
| `_mm256_srli_epi64`      | 2         | Estrai k_hi (32 bit alti)     |
| `_mm256_xor_si256`       | 2         | XOR fold                      |
| `_mm256_permutevar8x32`  | 2         | Pack 64-bit → 32-bit          |
| `_mm256_permute2x128`    | 1         | Combina 4+4 → 8 valori        |
| `_mm256_mullo_epi32`     | 1         | **Fibonacci mul32 (NATIVA!)**  |
| `_mm256_srli_epi32`      | 1         | Shift per partition id         |
| `_mm256_storeu_si256`    | 1         | Store 8 risultati              |
| **Totale**               | **12**    | **per 8 chiavi = 1.5 instr/key** |

## Perché non mul64?

Un approccio alternativo usa `(A64 * k) >> (64 - log2P)` con
decomposizione in 3× `vpmuludq`. Questo richiede ~11 istruzioni per
4 chiavi (2.75/key) e risulta **più lento dello scalare** su Zen 1
perché il costo computazionale extra non è compensato dal parallelismo
in un kernel memory-bound.

## Correttezza

Il binario verifica automaticamente che i checksum di scalare e AVX2
siano identici prima di procedere con il benchmark.
