# `src/cuda_kernel.cu`

Implementazione CUDA del kernel di partition mapping (task opzionale). Ogni thread GPU mappa una singola chiave alla sua partizione. La GPU su node09 è una NVIDIA A30 (compute capability 8.0).

---

## Il kernel CUDA

```cpp
__global__
void partition_map_cuda(const uint64_t* __restrict__ keys,
                        uint32_t*       __restrict__ part_ids,
                        size_t N, uint64_t hash_a, unsigned shift) {
    size_t idx = static_cast<size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    if (idx < N)
        part_ids[idx] = static_cast<uint32_t>((hash_a * keys[idx]) >> shift);
}
```

### Riga per riga

- **`__global__`**: dice al compilatore NVIDIA (nvcc) che questa funzione viene lanciata dalla CPU ma eseguita sulla GPU. Non può restituire un valore (void).

- **`const uint64_t*` anziché `const spm_key_t*`**: nel codice device usiamo tipi primitivi CUDA per evitare potenziali problemi con tipi definiti nell'header (anche se qui sono alias identici).

- **`hash_a` e `shift` come parametri**: la costante HASH_A e lo shift vengono passati come argomenti al kernel anziché usare `constexpr`. I parametri del kernel finiscono nella *constant memory* della GPU, che è cachata e veloce da leggere.

- **Calcolo dell'indice globale**:
  ```cpp
  size_t idx = (size_t)blockIdx.x * blockDim.x + threadIdx.x;
  ```
  La GPU organizza i thread in blocchi. `blockIdx.x` è l'indice del blocco corrente (0, 1, 2, ...), `blockDim.x` è il numero di thread per blocco (256 nel nostro caso), `threadIdx.x` è l'indice del thread dentro il blocco (0..255). La formula dà un indice unico per ogni thread.

  Esempio con N=1000, blockDim=256:
  - Blocco 0: thread 0-255 -> idx 0-255
  - Blocco 1: thread 0-255 -> idx 256-511
  - Blocco 2: thread 0-255 -> idx 512-767
  - Blocco 3: thread 0-255 -> idx 768-1023

- **`if (idx < N)`**: l'ultimo blocco può avere più thread del necessario (nell'esempio sopra, i thread 1000-1023 del blocco 3 non devono fare nulla). Il guard evita accessi out-of-bounds.

- **Il corpo**: identico alla versione scalare CPU. La GPU ha istruzioni native per la moltiplicazione 64×64 bit, quindi non serve la decomposizione che facciamo in AVX2.

---

## Macro `CUDA_CHECK`

```cpp
#define CUDA_CHECK(call) do {                                                 \
    cudaError_t err = (call);                                                 \
    if (err != cudaSuccess) {                                                 \
        fprintf(stderr, "CUDA error %s:%d: %s\n",                            \
                __FILE__, __LINE__, cudaGetErrorString(err));                  \
        exit(1);                                                              \
    }                                                                         \
} while (0)
```

Ogni chiamata all'API CUDA restituisce un codice di errore. Senza questo check, un errore (es. memoria esaurita, parametri invalidi) verrebbe ignorato silenziosamente, e il programma si comporterebbe in modo imprevedibile. La macro stampa il file, la riga, e il messaggio di errore leggibile. Il `do { ... } while(0)` è un idioma C per macro multi-statement che funziona correttamente dopo un `if` senza parentesi graffe.

---

## Allocazione memoria

### Lato host (CPU)
```cpp
CUDA_CHECK(cudaMallocHost(&h_keys,     N * sizeof(spm_key_t)));
CUDA_CHECK(cudaMallocHost(&h_part_gpu, N * sizeof(part_t)));
```
`cudaMallocHost` alloca **pinned memory** (page-locked). La differenza con `malloc`:

- Memoria normale (`malloc`): il SO può spostarla su disco (swap). Quando la GPU deve copiarla via DMA (Direct Memory Access), il driver CUDA deve prima copiarla in un buffer interno pinned, poi trasferirla alla GPU. Double copy.
- Pinned memory (`cudaMallocHost`): le pagine sono bloccate in RAM fisica. Il DMA della GPU può accedervi direttamente. Single copy, circa 2× più veloce per i trasferimenti PCIe.

Svantaggio: riduce la RAM disponibile per il SO. Per i nostri dataset (~1.5 GB max) non è un problema.

### Lato device (GPU)
```cpp
CUDA_CHECK(cudaMalloc(&d_keys, N * sizeof(uint64_t)));
CUDA_CHECK(cudaMalloc(&d_part, N * sizeof(uint32_t)));
```
Alloca memoria sulla VRAM della GPU. I puntatori `d_keys` e `d_part` non sono dereferenziabili dalla CPU — li usi solo nelle chiamate `cudaMemcpy` e nel lancio del kernel.

---

## Configurazione del lancio

```cpp
const int tpb = 256;    // threads per block
const int nblocks = static_cast<int>((N + tpb - 1) / tpb);
```

- **256 thread per blocco**: scelta standard. Un blocco deve avere almeno 128-256 thread per occupare bene un SM (Streaming Multiprocessor). 256 è un buon compromesso: abbastanza thread per nascondere la latenza di memoria, non così tanti da limitare i registri per thread.

- **Numero di blocchi**: arrotondamento per eccesso di N/256. Con N=100M e tpb=256, servono 390625 blocchi. La GPU A30 ha 56 SM, ciascuno può eseguire più blocchi in contemporanea.

- **`(N + tpb - 1) / tpb`**: formula classica per la divisione intera con arrotondamento per eccesso. Equivale a `ceil(N/tpb)` ma senza usare floating point.

---

## Timing con CUDA Events

```cpp
cudaEvent_t e0, e1, e2, e3;
```

I CUDA Events sono timestamp registrati sulla timeline della GPU. Sono molto più precisi di `std::chrono` per misurare operazioni GPU perché:
- Non soffrono della latenza di sincronizzazione CPU-GPU
- Misurano il tempo effettivo di esecuzione sulla GPU, non il tempo percepito dalla CPU

### Il ciclo di benchmark

```cpp
CUDA_CHECK(cudaEventRecord(e0));                           // timestamp 0
CUDA_CHECK(cudaMemcpy(d_keys, h_keys, ..., H2D));         // copia host -> device
CUDA_CHECK(cudaEventRecord(e1));                           // timestamp 1
partition_map_cuda<<<nblocks, tpb>>>(...);                  // lancio kernel
CUDA_CHECK(cudaEventRecord(e2));                           // timestamp 2
CUDA_CHECK(cudaMemcpy(h_part_gpu, d_part, ..., D2H));     // copia device -> host
CUDA_CHECK(cudaEventRecord(e3));                           // timestamp 3
CUDA_CHECK(cudaEventSynchronize(e3));                      // aspetta completamento
```

Poi:
```cpp
cudaEventElapsedTime(&h2d,  e0, e1);   // tempo H->D
cudaEventElapsedTime(&kern, e1, e2);   // tempo kernel
cudaEventElapsedTime(&d2h,  e2, e3);   // tempo D->H
```

Il progetto richiede esplicitamente di riportare questi tre tempi separatamente. Questo perché per un kernel così semplice, il tempo di trasferimento PCIe domina il tempo totale — il kernel in sé è velocissimo.

---

## Lancio del kernel

```cpp
partition_map_cuda<<<nblocks, tpb>>>(d_keys, d_part, N, HASH_A, shift);
```

La sintassi `<<<blocchi, thread_per_blocco>>>` è specifica di CUDA (estensione di nvcc). Lancia `nblocks × tpb` thread in totale sulla GPU. Il lancio è **asincrono**: la CPU non aspetta che la GPU finisca. L'attesa avviene con `cudaEventSynchronize(e3)`.

---

## Verifica di correttezza

Dopo il benchmark, confronta l'output GPU con il riferimento CPU calcolato all'inizio:
```cpp
uint64_t cksum_gpu = compute_checksum(h_part_gpu, N);
bool ok = (cksum_cpu == cksum_gpu);
```

La GPU ha un'aritmetica intera a 64 bit identica alla CPU (standard IEEE per gli interi), quindi il risultato deve essere bit-per-bit identico. Se non lo è, c'è un bug.

---

## Cosa aspettarsi nei risultati

- **H->D transfer**: proporzionale a N×8 byte. Per N=100M -> 800MB. Su PCIe 4.0 x16 (~25 GB/s) -> ~32 ms.
- **Kernel**: molto veloce, ordine di 1-5 ms per N=100M. La GPU ha migliaia di core e istruzioni native per mul64.
- **D->H transfer**: proporzionale a N×4 byte = 400MB -> ~16 ms.
- **Totale end-to-end**: dominato dai trasferimenti (~50 ms), kernel trascurabile.

Questo è un risultato atteso e interessante da discutere nel report: per kernel così semplici, la GPU è limitata dal trasferimento dati, non dal compute. Ha senso usarla solo se il kernel fosse più complesso o se i dati fossero già sulla GPU.

---

## Cleanup

```cpp
CUDA_CHECK(cudaFree(d_keys));
CUDA_CHECK(cudaFree(d_part));
CUDA_CHECK(cudaFreeHost(h_keys));
CUDA_CHECK(cudaFreeHost(h_part_gpu));
std::free(h_part_cpu);
```

Nota la distinzione: `cudaFree` per memoria device, `cudaFreeHost` per pinned memory host, `std::free` per memoria allocata con `malloc`. Mescolarli causa memory leak o crash.
