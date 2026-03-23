# plain.cpp — Kernel scalare di partition mapping

## Panoramica

Compilato due volte dallo stesso sorgente:
- **baseline**: con `-fno-tree-vectorize` (puro scalare)
- **autovec**: con `-O3 -march=native` (GCC auto-vettorizza con AVX2)

## Il kernel

```cpp
void partition_map(const spm_key_t *__restrict__ keys,
                   part_t *__restrict__ part_ids,
                   size_t N, unsigned shift) {
    for (size_t i = 0; i < N; i++) {
        uint32_t k_lo = static_cast<uint32_t>(keys[i]);
        uint32_t k_hi = static_cast<uint32_t>(keys[i] >> 32);
        uint32_t mixed = k_lo ^ k_hi;
        part_ids[i] = (mixed * HASH_A32) >> shift;
    }
}
```

### Hash function: XOR-fold + Fibonacci mul32

La scelta della hash a 32 bit è motivata dalla compatibilità SIMD:
- `HASH_A32 * mixed` è una **mul32** → in AVX2 corrisponde a `vpmulld` (nativa)
- Con una hash a 64 bit (`HASH_A * key`) servirebbe una mul64, che in AVX2
  richiede 3× `vpmuludq` (non nativa) → overhead che annulla il vantaggio SIMD

### Condizioni per auto-vectorization (L7&8)

| Condizione                       | Soddisfatta? | Come                              |
|----------------------------------|--------------|-----------------------------------|
| Conteggio iterazioni noto        | ✅           | `for (i = 0; i < N; i++)`        |
| Nessuna dipendenza tra iterazioni| ✅           | `part_ids[i]` dipende solo da `keys[i]` |
| Nessuna function call            | ✅           | `XOR + MUL32 + SHIFT` tutto inline |
| No aliasing                      | ✅           | `__restrict__` su entrambi i puntatori |
| Stride unitario                  | ✅           | Accesso sequenziale a `keys[i]` e `part_ids[i]` |
| Operazione SIMD-native           | ✅           | `_mm256_mullo_epi32` (vpmulld) nativa in AVX2 |

### Evidence di auto-vettorizzazione

GCC con `-ftree-vectorize` produce nel report:
```
src/plain.cpp:33:26: optimized: loop vectorized using 32 byte vectors
src/plain.cpp:33:26: optimized: loop vectorized using 16 byte vectors
```

Con la hash a 32 bit, GCC genera codice con registri **ymm (256-bit)**
usando `vpmulld`, a differenza di una hash a 64 bit dove userebbe solo
registri xmm (128-bit) con la decomposizione `vpmuludq`.

## Risultati (N=100M, P=256, node09)

| Versione | Throughput   | Speedup | BW effettiva |
|----------|-------------|---------|--------------|
| Baseline | ~915 Mkeys/s | 1.00×  | ~11 GB/s     |
| Autovec  | ~1310 Mkeys/s| 1.43×  | ~15.7 GB/s   |
