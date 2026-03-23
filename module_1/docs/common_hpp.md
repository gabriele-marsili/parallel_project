# common.hpp — Header condiviso

## Tipi

```cpp
using spm_key_t = uint64_t;  // chiave a 64 bit
using part_t    = uint32_t;  // partition id
```

## Hash Function: XOR-fold + Fibonacci multiply-shift a 32 bit

```cpp
static constexpr uint32_t HASH_A32 = 0x9E3779B9u; // floor(2^32 / phi)

inline part_t hash_key(spm_key_t key, unsigned shift32) {
    uint32_t k_lo = static_cast<uint32_t>(key);
    uint32_t k_hi = static_cast<uint32_t>(key >> 32);
    return (uint32_t)(((k_lo ^ k_hi) * HASH_A32) >> shift32);
}

inline unsigned compute_shift(uint32_t P) {
    return 32 - __builtin_ctz(P);
}
```

### Come funziona

1. **XOR-fold**: la chiave a 64 bit viene "ripiegata" in 32 bit tramite XOR delle due metà (`k_lo ^ k_hi`). Questo preserva l'entropia di entrambe le metà.
2. **Fibonacci multiply**: il valore XOR-folded viene moltiplicato per `A32 = 0x9E3779B9` (il golden ratio a 32 bit, floor(2³²/φ)). La moltiplicazione distribuisce i bit uniformemente.
3. **Shift**: lo shift a destra `>> (32 - log2P)` estrae i bit più significativi del prodotto come partition id.

### Perché a 32 bit e non 64 bit

La scelta di operare a 32 bit è motivata dalla compatibilità SIMD:
- AVX2 ha `_mm256_mullo_epi32` **(nativa, 1 istruzione, 8 mul in parallelo)**
- AVX2 **NON** ha `_mm256_mullo_epi64` (disponibile solo da AVX-512)
- Emulare una mul64 in AVX2 richiede 3× `vpmuludq` + shift + add → overhead che annulla il vantaggio SIMD

### La costante `0x9E3779B9`

- floor(2³²/φ) dove φ = rapporto aureo = (1+√5)/2
- **Dispari** (invertibile mod 2³²) → la mappa è una biiezione
- **Massima equidistribuzione** (Weyl sequence, Knuth TAOCP Vol. 3)
- Distribuzione misurata: max/atteso ≤ 1.005 su 100M chiavi con P=256

### Lo shift

Calcolato come `32 - log2(P)` con `__builtin_ctz(P)`:

```cpp
const unsigned shift = compute_shift(P);  // = 32 - log2(P)
```

## Generatore di chiavi (xoshiro256**)

Classe `KeyGenerator` con generazione deterministica da seed. Usa SplitMix64 per inizializzare lo stato di xoshiro256**.

## Utilità

- `alloc_aligned<T>(N)`: allocazione allineata a 32 byte (per load/store SIMD efficienti)
- `compute_checksum(data, N)`: FNV-1a a 64 bit per verifica correttezza tra implementazioni
- `benchmark(times, N)`: calcola mediana, stddev, throughput
- `print_result(label, res)`: output formattato dei risultati
