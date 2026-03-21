# `include/common.hpp`

Header condiviso da tutti i sorgenti. Contiene tipi, funzione hash, generatore di chiavi, allocazione allineata, checksum, caricamento dataset e utilità di benchmarking.

---

## Tipi

```cpp
using spm_key_t = uint64_t;   // chiave a 64 bit
using part_t    = uint32_t;   // id di partizione (bastano 32 bit anche per P grandi)
```

L'alias si chiama `spm_key_t` e non `key_t` perché su macOS/POSIX `key_t` è già definito in `<sys/types.h>` come `int32_t` (è il tipo per le chiavi IPC di System V). Su Linux con GCC non dà problemi, ma su clang/macOS sì.

---

## Funzione hash

```cpp
static constexpr spm_key_t HASH_A = 0x9E3779B97F4A7C15ULL;

inline part_t hash_key(spm_key_t key, unsigned shift) {
    return static_cast<part_t>((HASH_A * key) >> shift);
}
```

### Cosa fa
Prende una chiave `key` e restituisce un intero in `[0, P)` dove `P = 2^(64-shift)`.

### Come funziona passo per passo
1. `HASH_A * key` — moltiplicazione intera a 64 bit. Il risultato è troncato a 64 bit (overflow naturale di `uint64_t`). Questo prodotto "mescola" tutti i bit della chiave.
2. `>> shift` — prende i `64 - shift` bit più significativi del prodotto. Se P=256, shift=56, quindi prendiamo i top 8 bit → valori in [0, 255].
3. `static_cast<part_t>(...)` — tronca a 32 bit (i bit alti sono già zero dopo lo shift).

### La costante `0x9E3779B97F4A7C15`
È `floor(2^64 / φ)` dove φ = rapporto aureo = (1+√5)/2 ≈ 1.618.

Perché proprio questa:
- **Dispari**: in aritmetica mod 2^64, solo i numeri dispari hanno inverso moltiplicativo. Questo significa che `k → A*k mod 2^64` è una biiezione (ogni chiave produce un prodotto diverso). Se A fosse pari, il bit meno significativo del prodotto sarebbe sempre 0 → perdi informazione.
- **Buon mixing**: la rappresentazione binaria ha una distribuzione irregolare di 0 e 1 senza pattern ripetitivi. Quando moltiplichi per questo numero, ogni bit del risultato dipende da molti bit dell'input.
- **Equidistribuzione di Weyl**: la teoria dice che le parti frazionarie di `k/φ` per k=1,2,3,... sono distribuite il più uniformemente possibile sull'intervallo [0,1). Questo si traduce in distribuzione uniforme delle chiavi tra le partizioni.

### Il parametro `shift`
Calcolato come `64 - log2(P)` nel main di ogni programma:
```cpp
const unsigned shift = 64 - __builtin_ctz(P);
```
`__builtin_ctz(P)` conta gli zeri finali di P in binario, che per una potenza di 2 è esattamente log2(P). Esempio: P=256 → `__builtin_ctz(256)` = 8 → shift = 56.

---

## Generatore di chiavi (`KeyGenerator`)

```cpp
class KeyGenerator {
public:
    static void generate(spm_key_t* keys, size_t N, uint64_t seed, uint64_t key_space = 0);
};
```

### Cosa fa
Riempie un array preallocato con N chiavi pseudo-casuali generate deterministicamente a partire dal seed.

### L'algoritmo: xoshiro256**

È un PRNG (generatore di numeri pseudo-casuali) di alta qualità progettato da Blackman e Vigna. Lo stato è un array di 4 `uint64_t` (256 bit totali).

**Inizializzazione dello stato** — usa SplitMix64 per trasformare il singolo seed in 4 valori di stato:
```cpp
for (int i = 0; i < 4; i++) {
    seed += 0x9E3779B97F4A7C15ULL;   // incremento golden-ratio (stessa costante!)
    uint64_t z = seed;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;  // mixing
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;  // mixing
    z = z ^ (z >> 31);
    s[i] = z;
}
```
SplitMix64 prende un contatore e lo trasforma in un valore dall'aspetto casuale tramite xor-shift-multiply. Lo usiamo perché xoshiro256** ha bisogno di 4 parole di stato ben inizializzate (non possiamo semplicemente mettere il seed in s[0] e zeri altrove — produrrebbe output scadente all'inizio).

**Generazione** — ad ogni passo:
```cpp
const uint64_t result = rotl(s[1] * 5, 7) * 9;
```
Questo è lo "scrambler" di xoshiro256**: prende `s[1]`, moltiplica per 5, ruota a sinistra di 7, moltiplica per 9. Le costanti e le operazioni sono scelte per passare i test statistici (BigCrush, PractRand).

Poi aggiorna lo stato con una transizione lineare:
```cpp
const uint64_t t = s[1] << 17;
s[2] ^= s[0]; s[3] ^= s[1];
s[1] ^= s[2]; s[0] ^= s[3];
s[2] ^= t;
s[3] = rotl(s[3], 45);
```
La transizione mescola i 4 registri di stato con xor e shift. Il periodo è 2^256 - 1 (lunghissimo, non si esaurisce mai in pratica).

**Controllo dei duplicati** — se `key_space > 0`:
```cpp
keys[i] = (key_space > 0) ? (result % key_space) : result;
```
Con key_space=1000000, le chiavi sono in [0, 999999] → in media ogni chiave compare N/1000000 volte. Serve per testare come si comporta la hash con input ad alta duplicazione.

### `rotl`
```cpp
static inline uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}
```
Rotazione a sinistra: sposta i bit di k posizioni a sinistra, i bit che "escono" a sinistra rientrano da destra. Diversa dallo shift perché non perde bit. È un'operazione standard nella crittografia e nei PRNG.

---

## Allocazione allineata

```cpp
inline void* aligned_alloc_wrapper(size_t alignment, size_t size) {
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
    void* ptr = std::aligned_alloc(alignment, aligned_size);
    ...
}

template<typename T>
T* alloc_aligned(size_t count, size_t alignment = 32) {
    return static_cast<T*>(aligned_alloc_wrapper(alignment, count * sizeof(T)));
}
```

### Perché 32 byte
Le istruzioni AVX2 come `_mm256_load_si256` richiedono che l'indirizzo di memoria sia multiplo di 32 byte. Se non lo è, il programma crasha con un segfault. La versione "unaligned" (`_mm256_loadu_si256`) funziona con qualsiasi indirizzo ma può essere più lenta quando l'accesso attraversa il confine di una cache line (64 byte).

### L'arrotondamento
```cpp
size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
```
`std::aligned_alloc` richiede che la dimensione sia multiplo dell'allineamento. Questa formula arrotonda per eccesso. Esempio: size=100, alignment=32 → aligned_size=128.

Il trucco con `& ~(alignment - 1)`: se alignment=32 (=0b100000), allora `alignment-1` = 0b011111 e `~(alignment-1)` = 0b...100000 (maschera che azzera i 5 bit bassi). Sommando alignment-1 prima, garantiamo l'arrotondamento per eccesso.

---

## Checksum FNV-1a

```cpp
inline uint64_t compute_checksum(const part_t* part_ids, size_t N) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < N; i++) {
        h ^= static_cast<uint64_t>(part_ids[i]);
        h *= 0x100000001B3ULL;
    }
    return h;
}
```

### Cosa fa
Calcola un "riassunto" a 64 bit dell'intero array di output. Se due implementazioni producono lo stesso checksum, con probabilità schiacciante (1 - 2^-64) hanno prodotto lo stesso identico output.

### Come funziona
FNV-1a è un hash non crittografico molto semplice:
1. Parti da un valore iniziale (offset basis = `0xCBF29CE484222325`)
2. Per ogni elemento: XOR con l'elemento, poi moltiplica per il prime FNV (`0x100000001B3`)
3. Il risultato finale è il checksum

L'ordine XOR-poi-moltiplica (variante "1a") dà una distribuzione migliore della variante originale (moltiplica-poi-XOR).

### Perché non un semplice XOR o somma
Un semplice `h ^= part_ids[i]` non distinguerebbe array con gli stessi elementi in ordine diverso. La moltiplicazione per il prime rende il checksum sensibile all'ordine e alla posizione degli elementi.

---

## Caricamento dataset via mmap

```cpp
struct DatasetView {
    const spm_key_t* keys;      // punta direttamente nel file mappato
    size_t           N;
    uint64_t         seed;
    uint64_t         key_space;
    void*            mmap_base;  // per munmap
    size_t           mmap_len;
};
```

### `dataset_load`

1. `open(path, O_RDONLY)` — apre il file in sola lettura
2. `fstat(fd, &st)` — recupera la dimensione del file
3. `mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0)` — mappa il file in memoria virtuale. Da questo momento `base` punta al contenuto del file come se fosse un array in RAM, ma il kernel del SO lo carica effettivamente dal disco solo quando lo accedi (demand paging)
4. Parsing dell'header: i primi 5 uint64_t contengono magic, N, seed, key_space, reserved
5. Le chiavi partono da offset 40 (= 5 × 8 byte)

### Perché mmap e non fread
- **Zero-copy**: non c'è allocazione separata né copia dal buffer del kernel al nostro array. Le pagine di memoria puntano direttamente alla cache del filesystem
- **Lazy loading**: le pagine vengono caricate dal disco solo al primo accesso
- **Shared**: se più processi mappano lo stesso file, il SO condivide le pagine fisiche in RAM

### `dataset_unload`
```cpp
munmap(view.mmap_base, view.mmap_len);
```
Rilascia la mappatura. Dopo questa chiamata i puntatori non sono più validi.

---

## Utilità di benchmarking

```cpp
struct BenchResult {
    double median_ms;               // tempo mediano
    double stddev_ms;               // deviazione standard
    double throughput_Mkeys_per_s;  // milioni di chiavi al secondo
    size_t N;
};
```

### `benchmark(times_ms, N)`
1. Ordina i tempi misurati
2. Prende la mediana (valore centrale). La mediana è più robusta della media perché ignora outlier (es. la prima esecuzione potrebbe essere lenta per cold cache, o un'esecuzione potrebbe essere disturbata dal SO)
3. Calcola media e varianza per la deviazione standard
4. Throughput = N / tempo mediano, convertito in Mkeys/s

### Perché mediana e non media
Con 11 ripetizioni, la mediana scarta automaticamente i 5 tempi più alti e i 5 più bassi, prendendo il valore centrale. Questo elimina variazioni dovute a context switch, interruzioni, cold cache etc. Il progetto richiede esplicitamente "median time and standard deviation".
