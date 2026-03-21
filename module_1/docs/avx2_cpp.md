# `src/avx2.cpp`

Implementazione del kernel di partition mapping con intrinsics AVX2. Processa 4 chiavi a 64 bit in parallelo usando registri a 256 bit.

---

## Il problema centrale: moltiplicare 64×64 bit in AVX2

AVX2 **non ha** un'istruzione per moltiplicare due interi a 64 bit. L'unica moltiplicazione disponibile è:

```cpp
__m256i _mm256_mul_epu32(__m256i a, __m256i b);
```

Questa prende i **32 bit bassi** di ogni lane a 64 bit, li moltiplica tra loro, e produce un risultato a 64 bit per lane. Ignora completamente i 32 bit alti di a e b.

Esempio con una sola lane:
```
a = 0x00000003_00000005   (a_hi=3, a_lo=5)
b = 0x00000002_00000007   (b_hi=2, b_lo=7)
_mm256_mul_epu32(a, b) = 5 * 7 = 35 = 0x00000000_00000023
```
Nota: 3 e 2 (le parti alte) sono completamente ignorate.

---

## La decomposizione: `mul64_avx2`

Per ottenere il prodotto completo `(A * k) mod 2^64` decomponiamo i numeri a 64 bit nelle loro metà a 32 bit:

```
A = A_hi * 2^32 + A_lo
k = k_hi * 2^32 + k_lo
```

Il prodotto è:
```
A * k = A_lo * k_lo                    (termine 1)
      + (A_lo * k_hi) * 2^32           (termine 2)
      + (A_hi * k_lo) * 2^32           (termine 3)
      + (A_hi * k_hi) * 2^64           (termine 4 — overflow, ignorato)
```

Il termine 4 supera i 64 bit e viene scartato (lavoriamo modulo 2^64).

### Il codice riga per riga

```cpp
static inline __m256i mul64_avx2(__m256i a, __m256i k) {
```
Riceve due registri a 256 bit, ciascuno contenente 4 interi a 64 bit. Restituisce i 4 prodotti (low 64 bit).

```cpp
    __m256i lo_lo = _mm256_mul_epu32(a, k);
```
**Termine 1**: moltiplica i 32 bit bassi di `a` per i 32 bit bassi di `k`. Produce 4 risultati a 64 bit. Questa è l'unica operazione che produce un risultato completo a 64 bit.

```cpp
    __m256i a_hi = _mm256_srli_epi64(a, 32);
    __m256i k_hi = _mm256_srli_epi64(k, 32);
```
Shift logico a destra di 32: sposta i 32 bit alti nella posizione dei 32 bit bassi. Ora `a_hi` ha `A_hi` nei bit bassi di ogni lane (e zeri nei bit alti), pronto per `mul_epu32`.

```cpp
    __m256i a_lo_k_hi = _mm256_mul_epu32(a, k_hi);
    __m256i a_hi_k_lo = _mm256_mul_epu32(a_hi, k);
```
**Termini 2 e 3**: i due prodotti incrociati. Nota che passiamo `a` (non `a_lo`) alla prima: `mul_epu32` prende automaticamente solo i 32 bit bassi di `a`, che sono già `A_lo`.

```cpp
    __m256i cross = _mm256_add_epi64(a_lo_k_hi, a_hi_k_lo);
    cross = _mm256_slli_epi64(cross, 32);
```
Somma i due termini incrociati e shifta a sinistra di 32 (equivale a moltiplicare per 2^32). L'overflow oltre 64 bit si perde automaticamente, che è quello che vogliamo.

```cpp
    return _mm256_add_epi64(lo_lo, cross);
```
Somma tutto: termine 1 + (termini 2+3 shiftati). Risultato: i 64 bit bassi di `A * k` per ciascuna delle 4 lane.

### Costo totale
3 `mul_epu32` + 2 `srli_epi64` + 2 `add_epi64` + 1 `slli_epi64` = **8 istruzioni** per 4 chiavi.

---

## Il kernel principale

```cpp
void partition_map_avx2(const spm_key_t* __restrict__ keys,
                        part_t*          __restrict__ part_ids,
                        size_t N, unsigned shift)
```

### Setup
```cpp
const __m256i va = _mm256_set1_epi64x(static_cast<int64_t>(HASH_A));
```
Carica la costante HASH_A in tutte e 4 le lane a 64 bit del registro. Fatto una volta sola fuori dal loop.

```cpp
const size_t simd_end = N - (N % 4);
```
Il loop SIMD processa 4 chiavi alla volta. Se N=102, processa 100 chiavi nel loop SIMD e le ultime 2 in un loop scalare separato ("tail").

```cpp
const __m256i perm_idx = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
```
Indici di permutazione per il packing (spiegato sotto). Anche questo è costante e va fuori dal loop.

### Il loop SIMD

```cpp
__m256i vk = _mm256_load_si256(reinterpret_cast<const __m256i*>(&keys[i]));
```
Carica 4 chiavi consecutive (4 × 64 bit = 256 bit) dalla memoria al registro. `_mm256_load_si256` richiede che l'indirizzo sia allineato a 32 byte (garantito da `alloc_aligned`).

```cpp
__m256i prod  = mul64_avx2(va, vk);
__m256i vhash = _mm256_srli_epi64(prod, shift);
```
Calcola i 4 prodotti e poi shifta a destra per ottenere i partition id. Dopo lo shift, ogni lane a 64 bit contiene un valore piccolo (< P, quindi sta nei 32 bit bassi).

### Packing dei risultati

Problema: abbiamo 4 valori a 32 bit, ma ognuno occupa una lane a 64 bit:
```
vhash = [ h0 (64b) | h1 (64b) | h2 (64b) | h3 (64b) ]
```
Vogliamo scriverli come 4 `uint32_t` contigui (128 bit totali).

Se guardiamo il registro come 8 "slot" da 32 bit:
```
slot:    0     1     2     3     4     5     6     7
       [h0]  [ 0 ] [h1]  [ 0 ] [h2]  [ 0 ] [h3]  [ 0 ]
```
I valori utili sono nelle posizioni 0, 2, 4, 6.

```cpp
__m256i perm_idx = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
__m256i packed = _mm256_permutevar8x32_epi32(vhash, perm_idx);
```
`permutevar8x32` riordina gli 8 slot da 32 bit secondo gli indici dati. Dopo la permutazione:
```
slot:    0     1     2     3     4     5     6     7
       [h0]  [h1]  [h2]  [h3]  [ 0 ] [ 0 ] [ 0 ] [ 0 ]
```
I 4 valori sono ora nei primi 128 bit.

```cpp
_mm_storeu_si128(reinterpret_cast<__m128i*>(&part_ids[i]),
                 _mm256_castsi256_si128(packed));
```
- `_mm256_castsi256_si128`: estrae i 128 bit bassi del registro a 256 bit (nessuna istruzione generata, è solo un "cast" per il tipo)
- `_mm_storeu_si128`: scrive 128 bit (= 4 × uint32_t) in memoria. `storeu` = store unaligned (l'array di part_t è allineato a 32 byte, ma a posizioni i che non sono multiple di 8 potrebbe non essere allineato a 16)

### Coda scalare
```cpp
for (size_t i = simd_end; i < N; i++)
    part_ids[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
```
Processa gli ultimi `N % 4` elementi con lo stesso codice di `plain.cpp`. Necessario perché il loop SIMD processa solo multipli di 4.

---

## Verifica di correttezza

Il main contiene anche un kernel scalare di riferimento (`partition_map_scalar`) identico a quello in `plain.cpp`. Prima del benchmark:

1. Esegue entrambi i kernel sugli stessi dati
2. Calcola il checksum FNV-1a su entrambi gli output
3. Se i checksum differiscono, cerca il primo mismatch e stampa i dettagli
4. Per N ≤ 32 stampa il confronto element-wise

Questo garantisce che qualsiasi bug nell'implementazione AVX2 venga catturato senza bisogno di un binario esterno di riferimento.

---

## Benchmark e speedup

Il programma misura separatamente il tempo del kernel AVX2 e del kernel scalare, poi calcola lo speedup:

```
Speedup = median_scalare / median_avx2
```

Ci aspettiamo uno speedup tra 2× e 4×. Non raggiungiamo il massimo teorico di 4× (4 chiavi per iterazione) perché:
- La moltiplicazione decomposta richiede 8 istruzioni anziché 1
- Il packing dei risultati ha un costo
- Per N grandi il bottleneck è la bandwidth di memoria, non il compute
