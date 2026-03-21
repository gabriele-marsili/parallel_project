# `src/dataset_creator.cpp`

Tool standalone che genera dataset di chiavi come file binari. I kernel (plain, avx2, cuda) caricano poi questi file via mmap per il benchmarking.

---

## Perché un tool separato

Il progetto dice: "Input keys are generated deterministically from a seed, so that the output is reproducible." Potremmo generare le chiavi dentro ogni binario di benchmark (come facciamo attualmente con `KeyGenerator::generate`), ma avere i dataset su file ha vantaggi:

1. **Separazione**: il tempo di generazione non è incluso nelle misurazioni del kernel. Per N=200M la generazione richiede ~2 secondi, che falserebbero il benchmark se non scorporati.
2. **Condivisione**: tutti i kernel leggono lo stesso file → garanzia assoluta che lavorano sugli stessi dati.
3. **Idempotenza**: se il file esiste già ed è valido, non lo ricrea. Risparmi tempo quando devi rieseguire solo i benchmark.

---

## Formato binario

```
Offset    Tipo        Contenuto
[0..7]    uint64_t    magic = 0x53504D4B455953AA
[8..15]   uint64_t    N (numero di chiavi)
[16..23]  uint64_t    seed
[24..31]  uint64_t    key_space (0 = range completo a 64 bit)
[32..39]  uint64_t    riservato (0, per uso futuro)
[40..]    uint64_t[]  N chiavi
```

### Il magic number

```cpp
static constexpr uint64_t MAGIC = 0x53504D4B455953AAULL;
```

Letto come byte ASCII: `S P M K E Y S 0xAA`. Il magic serve per:
- Verificare che un file `.bin` sia effettivamente un dataset nostro e non un file qualsiasi
- Rilevare corruzione (se i primi 8 byte non corrispondono, il file è invalido)
- L'ultimo byte `0xAA` (10101010 in binario) aiuta a rilevare problemi di endianness

### L'header come struct

```cpp
struct DatasetHeader {
    uint64_t magic;
    uint64_t N;
    uint64_t seed;
    uint64_t key_space;
    uint64_t reserved;
};
```

5 campi × 8 byte = 40 byte di header, poi i dati. Il campo `reserved` è per eventuali estensioni future (es. potremmo aggiungere un checksum delle chiavi). Per ora è sempre 0.

---

## Dataset di default

```cpp
static const DatasetConfig DEFAULTS[] = {
    {     1'000'000, 42,          0, "1M_full_range",   "ds_1M_full.bin"    },
    {    10'000'000, 42,          0, "10M_full_range",  "ds_10M_full.bin"   },
    {   100'000'000, 42,          0, "100M_full_range", "ds_100M_full.bin"  },
    {   200'000'000, 42,          0, "200M_full_range", "ds_200M_full.bin"  },
    {   100'000'000, 42,  1'000'000, "100M_high_dup",   "ds_100M_dup1M.bin"},
};
```

| Dataset | N | key_space | A cosa serve |
|---------|---|-----------|-------------|
| 1M full | 1M | 0 (64-bit) | Debug rapido, development, verifica correttezza |
| 10M full | 10M | 0 | Test intermedi, timing approssimativi |
| 100M full | 100M | 0 | **Benchmark principale** — "tens of millions" come richiesto |
| 200M full | 200M | 0 | Stressare la bandwidth di memoria, verificare scalabilità con N |
| 100M dup | 100M | 1M | Solo 1M chiavi distinte (ogni chiave appare ~100 volte in media). Serve per studiare la sensibilità della hash alla distribuzione dell'input |

I separatori `'` nei numeri (es. `1'000'000`) sono una feature C++14 per leggibilità. Il compilatore li ignora completamente.

Tutti usano seed=42. Il seed è fisso perché serve riproducibilità: stessi parametri → stesso file → stesso output da tutti i kernel.

---

## Funzioni di utilità POSIX

```cpp
static bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}
```

Usiamo `stat()` di POSIX anziché `std::filesystem::exists()` del C++17 perché `<filesystem>` ha problemi di portabilità:
- Su macOS con clang, il namespace può essere `std::__1::__fs::filesystem` e dare errori
- Su GCC vecchi (< 9) bisogna linkare `-lstdc++fs` separatamente
- Il cluster potrebbe avere un GCC non recentissimo

`stat()` funziona ovunque (è POSIX, disponibile su qualsiasi Unix/Linux/macOS).

Lo stesso ragionamento vale per `mkdir()` (anziché `std::filesystem::create_directories`), `opendir`/`readdir`/`closedir` (anziché `std::filesystem::directory_iterator`).

---

## Scrittura del dataset

```cpp
static bool write_dataset(const std::string& path, size_t N,
                           uint64_t seed, uint64_t key_space) {
    spm_key_t* keys = alloc_aligned<spm_key_t>(N);
    KeyGenerator::generate(keys, N, seed, key_space);

    std::ofstream ofs(path, std::ios::binary);
    // ... scrive header, poi chiavi ...
    ofs.close();
    std::free(keys);
    return bool(ofs);
}
```

1. Alloca N × 8 byte allineati a 32 byte
2. Genera le chiavi con xoshiro256** (deterministico dato il seed)
3. Apre il file in modalità binaria (`std::ios::binary` — importante: senza questo, su Windows i `\n` verrebbero convertiti in `\r\n`, corrompendo i dati)
4. Scrive l'header (40 byte) e poi l'array di chiavi in un colpo solo
5. Chiude e verifica che la scrittura sia andata a buon fine (`bool(ofs)` è false se c'è stato un errore di I/O)
6. Libera la memoria

Il `return bool(ofs)` sfrutta il fatto che `std::ofstream` ha un operatore di conversione a bool che è `true` se lo stream non è in stato di errore.

---

## Validazione

```cpp
static bool validate_dataset(const std::string& path, size_t N,
                              uint64_t seed, uint64_t key_space) {
```

Controlla se un file esistente corrisponde ai parametri attesi:
1. Apre il file e legge l'header
2. Verifica magic, N, seed, key_space
3. Verifica che la dimensione del file sia esattamente `40 + N*8` byte

Se tutto corrisponde, il file è valido e non serve ricrearlo. Se un qualsiasi check fallisce (es. il file è troncato, o è stato creato con seed diverso), restituisce `false` e il dataset verrà rigenerato.

---

## Generazione nome automatico

```cpp
static std::string make_auto_path(const std::string& dir, size_t N,
                                   uint64_t seed, uint64_t key_space) {
```

Per il mode `--custom` senza `-o`, genera un nome leggibile. Esempio:
- N=50000000, seed=42, key_space=0 → `data/ds_50M_full_s42.bin`
- N=10000000, seed=7, key_space=1000 → `data/ds_10M_ks1000_s7.bin`

I suffissi M/K/G sono applicati solo se N è un multiplo esatto (50000000 → 50M, ma 12345678 resta com'è).

---

## Modalità di esecuzione

### DEFAULT (nessun argomento)
Itera sui 5 dataset in `DEFAULTS[]`. Per ciascuno:
1. Controlla se il file esiste ed è valido
2. Se sì → `SKIP`
3. Se no → genera, scrive, riporta tempo e dimensione

### `--force`
Come DEFAULT ma ignora i file esistenti e li ricrea tutti.

### `--list`
Apre la directory `data/`, trova tutti i `.bin`, legge l'header di ciascuno e stampa N, seed, key_space, dimensione. Usa `opendir`/`readdir` di POSIX.

### `--custom`
Crea un singolo dataset con parametri specificati dall'utente:
```
./dataset_creator --custom -N 50000000 -k 1000 -s 7
```
Se il file esiste già con gli stessi parametri, lo salta.

---

## Parsing degli argomenti

```cpp
for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--list")              { mode = LIST; }
    else if (a == "-N" && i+1 < argc) { custom_N = std::stoull(argv[++i]); }
    ...
}
```

Il parsing è manuale (no librerie esterne come getopt). Il `++i` dentro l'accesso ad `argv` avanza il contatore di 1 per "consumare" il valore dopo il flag. Il check `i+1 < argc` evita di leggere oltre la fine dell'array.

L'`enum Mode` decide quale ramo del main eseguire. I flag sono mutuamente esclusivi (l'ultimo vince se ne passi più di uno, ma non è un caso d'uso realistico).
