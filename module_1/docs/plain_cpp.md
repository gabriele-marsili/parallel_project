# `src/plain.cpp`

Implementazione scalare (senza intrinsics) del kernel di partition mapping. Produce **due binari** dallo stesso sorgente:

- `bin/plain_baseline` — compilato con `-fno-tree-vectorize` (il compilatore non può usare istruzioni SIMD)
- `bin/plain_autovec` — compilato con `-O3 -march=native` (il compilatore tenta di vettorizzare automaticamente)

---

## Il kernel

```cpp
void partition_map(const spm_key_t* __restrict__ keys,
                   part_t*          __restrict__ part_ids,
                   size_t N,
                   unsigned shift) {
    for (size_t i = 0; i < N; i++) {
        part_ids[i] = static_cast<part_t>((HASH_A * keys[i]) >> shift);
    }
}
```

### Riga per riga

- **`const spm_key_t* __restrict__ keys`**: puntatore all'array di input. `const` dice che non modifichiamo le chiavi. `__restrict__` è un'estensione del compilatore che gli promette: "questo puntatore non punta alla stessa memoria di `part_ids`". Senza questa promessa il compilatore deve ipotizzare che scrivere in `part_ids[i]` potrebbe modificare `keys[j]`, e non può riordinare/vettorizzare liberamente.

- **`part_t* __restrict__ part_ids`**: array di output. Ogni posizione riceve il partition id corrispondente.

- **`HASH_A * keys[i]`**: moltiplicazione 64-bit. Il compilatore genera un'istruzione `IMUL` (su x86) che moltiplica due registri a 64 bit e produce il risultato troncato a 64 bit. Il costo è ~3 cicli di latenza su CPU moderne.

- **`>> shift`**: shift logico a destra. Estrae i bit più significativi del prodotto. È un'istruzione singola (`SHR`).

- **`static_cast<part_t>(...)`**: tronca da 64 a 32 bit. Non genera codice — i 32 bit bassi sono già nel registro, il compilatore semplicemente li usa come uint32_t.

### Perché è scritto così (per l'auto-vectorization)

Il compilatore GCC tenta di trasformare questo loop in istruzioni AVX2 automaticamente. Affinché ci riesca, il loop deve soddisfare delle condizioni (vedi lezioni 7&8):

| Requisito | Come lo soddisfiamo |
|-----------|---------------------|
| Conteggio iterazioni noto all'ingresso | `N` è un parametro, non cambia nel loop |
| Nessuna dipendenza tra iterazioni | `part_ids[i]` dipende solo da `keys[i]`, non da iterazioni precedenti |
| Stride unitario (accesso sequenziale) | `keys[i]` e `part_ids[i]` con i++ |
| Nessuna function call | `HASH_A*keys[i] >> shift` è tutto inline |
| Nessun aliasing | `__restrict__` su entrambi i puntatori |
| Nessun branch condizionale | Nessun if nel body |

Se GCC riesce a vettorizzare, il report (`-fopt-info-vec-optimized`) dirà qualcosa come:
```
src/plain.cpp:21: optimized: loop vectorized using 32 byte vectors
```

Se non riesce (es. la moltiplicazione 64-bit non ha un'istruzione AVX2 diretta), lo segnalerà nel report missed, e questo è un risultato valido da commentare nel report del progetto.

---

## Il main

### Parsing argomenti
```cpp
const size_t   N         = std::stoull(argv[1]);
const uint32_t P         = std::stoul(argv[2]);
const uint64_t seed      = (argc > 3) ? std::stoull(argv[3]) : 42;
const uint64_t key_space = (argc > 4) ? std::stoull(argv[4]) : 0;
const int      reps      = (argc > 5) ? std::stoi(argv[5])   : 11;
```
Parametri posizionali: N e P obbligatori, il resto ha default. `stoull` converte stringa → unsigned long long. I default (seed=42, key_space=0, reps=11) sono scelti per comodità: 42 è un seed "standard", 0 significa nessuna riduzione dello spazio chiavi, 11 ripetizioni danno una mediana senza ambiguità (numero dispari).

### Validazione di P
```cpp
if (P == 0 || (P & (P - 1)) != 0) { ... }
```
Il trucco `(P & (P-1)) == 0` verifica che P sia potenza di 2. Funziona perché una potenza di 2 in binario ha un solo bit a 1 (es. 256 = 10000000), e P-1 ha tutti i bit sotto quel bit a 1 (es. 255 = 01111111). Il loro AND è quindi 0. Per qualsiasi altro numero non è così.

### Calcolo dello shift
```cpp
const unsigned shift = 64 - __builtin_ctz(P);
```
`__builtin_ctz` = Count Trailing Zeros. Per P=256 (binario: 100000000), ci sono 8 zeri finali, quindi `ctz(256)=8` e shift=56. Questo significa: `(A*k) >> 56` prende i top 8 bit del prodotto → valori in [0, 255] = [0, P).

### Allocazione e generazione
```cpp
spm_key_t* keys    = alloc_aligned<spm_key_t>(N);
part_t*    part_ids = alloc_aligned<part_t>(N);
KeyGenerator::generate(keys, N, seed, key_space);
```
Memoria allineata a 32 byte (per AVX2) anche nel plain — così la generazione e il layout dei dati sono identici tra tutte le implementazioni.

### Warmup
```cpp
partition_map(keys, part_ids, N, shift);
```
La prima esecuzione riempie la cache L1/L2/L3, le TLB (Translation Lookaside Buffer), e fa il "warm-up" del branch predictor. Senza warmup la prima misurazione sarebbe sistematicamente più lenta e distorcerebbe i risultati. Non la includiamo nelle misure.

### Loop di benchmark
```cpp
for (int r = 0; r < reps; r++) {
    auto t0 = std::chrono::high_resolution_clock::now();
    partition_map(keys, part_ids, N, shift);
    auto t1 = std::chrono::high_resolution_clock::now();
    times.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
}
```
Misuriamo solo il kernel, non la generazione chiavi né il checksum. `high_resolution_clock` ha risoluzione tipica di ~1 ns su Linux. Per N=100M il tempo è nell'ordine di decine di ms, quindi la risoluzione è più che sufficiente.

### Compilazione condizionale
```cpp
#ifdef AUTOVEC_ENABLED
    print_result("plain (autovec)", result);
#else
    print_result("plain (no-vec)", result);
#endif
```
Il Makefile definisce `-DAUTOVEC_ENABLED` solo per il binario autovec. Così l'output distingue chiaramente quale versione ha prodotto i risultati.

### Stampa element-wise
```cpp
if (N <= 32) { ... }
```
Solo per N piccolo — serve per verificare manualmente che l'output sia corretto confrontando con gli altri binari. Per N grande sarebbe inutile e lentissimo.

### Analisi della distribuzione
```cpp
if (P <= 1024) {
    std::vector<uint64_t> counts(P, 0);
    for (size_t i = 0; i < N; i++) counts[part_ids[i]]++;
    ...
}
```
Conta quanti elementi finiscono in ogni partizione. Una buona hash produce `min ≈ max ≈ N/P`. Il rapporto `max/atteso` vicino a 1.0 indica distribuzione uniforme. Lo limitiamo a P≤1024 perché per P molto grandi l'array `counts` sarebbe enorme e il conteggio lento.

### Deallocazione
```cpp
std::free(keys);
std::free(part_ids);
```
`std::free` perché la memoria è stata allocata con `std::aligned_alloc` (che internamente usa il sistema di allocazione C). Non usare `delete[]` — quello è per `new[]`.
