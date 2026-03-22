/*
 * dataset_creator.cpp -- Genera dataset di chiavi come file binari.
 *
 * Separa la generazione dal benchmarking: i kernel caricano i file
 * via mmap (zero-copy) senza includere il tempo di generazione nelle misure.
 * La ri-esecuzione è idempotente: se un file esiste ed è valido lo salta.
 *
 * Formato binario:
 *   [0..7]   magic  0x53504D4B455953AA
 *   [8..15]  N
 *   [16..23] seed
 *   [24..31] key_space (0 = full 64-bit)
 *   [32..39] riservato
 *   [40..]   N x uint64_t chiavi
 *
 * Uso:
 *   ./dataset_creator                              crea i 5 dataset default
 *   ./dataset_creator --list                       elenca dataset in data/
 *   ./dataset_creator --custom -N 50000000 -k 1000 crea dataset personalizzato
 *   ./dataset_creator --force                      ricrea tutti anche se esistono
 */

#include "common.hpp"
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>

static constexpr uint64_t MAGIC = 0x53504D4B455953AAULL;
static constexpr size_t HEADER_SIZE = 40; //5 campi × 8 byte = 40 byte 

struct DatasetHeader {
    uint64_t magic;
    uint64_t N;
    uint64_t seed;
    uint64_t key_space;
    uint64_t reserved;
};

struct DatasetConfig {
    size_t N;
    uint64_t seed;
    uint64_t key_space;
    const char* name;
    const char* filename;
};

// Dataset di default (seed fisso):
//  - piccolo per verifica element-wise
//  - medio per iterazioni rapide
//  - grande per timing stabili ("decine di milioni")
//  - molto grande per stressare la bandwidth
//  - alto tasso di duplicati per studiare la sensibilità alla distribuzione
static const DatasetConfig DEFAULTS[] = {
    {     1'000'000, 42,          0, "1M_full_range",   "ds_1M_full.bin"    },
    {    10'000'000, 42,          0, "10M_full_range",  "ds_10M_full.bin"   },
    {   100'000'000, 42,          0, "100M_full_range", "ds_100M_full.bin"  },
    {   200'000'000, 42,          0, "200M_full_range", "ds_200M_full.bin"  },
    {   100'000'000, 42,  1'000'000, "100M_high_dup",   "ds_100M_dup1M.bin"},
};
static constexpr size_t NUM_DEFAULTS = sizeof(DEFAULTS) / sizeof(DEFAULTS[0]);

// -- Utility fns (posix -> per portabilità): ----

static bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static size_t file_size(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return static_cast<size_t>(st.st_size);
}

static void mkdirs(const std::string& path) {
    mkdir(path.c_str(), 0755);// ignora errore se esiste già
}

static std::string basename_of(const std::string& path) {
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

// -- operazioni I/O sul dataset --

/**
 * alloca N*8 byte allineati a 32 byte 
 * genera le chiavi (con xoshiro256** => deterministico)
 * apre il file in modalità binaria e scrive header ed array di chiavi 
 * chiude e verifica, poi libera la memoria
 */
static bool write_dataset(const std::string& path, size_t N,
                           uint64_t seed, uint64_t key_space) {
    
    //allocazione mem alligned:
    spm_key_t* keys = alloc_aligned<spm_key_t>(N);
    //generazione chiavi:
    KeyGenerator::generate(keys, N, seed, key_space);

    //apertura file in modalità binaria
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cerr << "  errore: impossibile aprire " << path << "\n";
        std::free(keys);
        return false;
    }

    //creazione header:
    DatasetHeader hdr{};
    hdr.magic = MAGIC;
    hdr.N = static_cast<uint64_t>(N);
    hdr.seed = seed;
    hdr.key_space = key_space;
    hdr.reserved = 0;

    //scrittura file:
    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    ofs.write(reinterpret_cast<const char*>(keys), N * sizeof(spm_key_t));
    ofs.close();
    std::free(keys);

    return bool(ofs); //true => nessun errore (sfrutta conversione di std::ofstrem)
}

/*
controlla se un file esistente corrisponde ai parametri attesi
=> apre il file, legge header, verifica magic, N, seed, key space e controlla che la dim del file sia 40 + N*8 byte
*/
static bool validate_dataset(const std::string& path, size_t N,
                              uint64_t seed, uint64_t key_space) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;

    //creazione e validazione dell'header:
    DatasetHeader hdr{};
    ifs.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!ifs) return false;
    if (hdr.magic != MAGIC) return false;
    if (hdr.N != static_cast<uint64_t>(N)) return false;
    if (hdr.seed != seed) return false;
    if (hdr.key_space != key_space) return false;

    //controllo sulla dim del file
    auto expected = static_cast<std::streamoff>(HEADER_SIZE + N * sizeof(spm_key_t));
    ifs.seekg(0, std::ios::end);
    return ifs.tellg() == expected;
}

//utility fn per stampare info dataset
static void print_dataset_info(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) { std::cout << "  " << path << "  [NON TROVATO]\n"; return; }

    DatasetHeader hdr{};
    ifs.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!ifs || hdr.magic != MAGIC) {
        std::cout << "  " << path << "  [FORMATO NON VALIDO]\n";
        return;
    }

    double size_mb = static_cast<double>(file_size(path)) / (1024.0 * 1024.0);
    std::cout << "  " << std::left << std::setw(28) << basename_of(path)
              << "  N=" << std::setw(12) << hdr.N
              << "  seed=" << std::setw(6) << hdr.seed
              << "  key_space=" << std::setw(12) << hdr.key_space
              << "  " << std::fixed << std::setprecision(1) << size_mb << " MB\n";
}

//generazione nome automatico per dataset custom 
static std::string make_auto_path(const std::string& dir, size_t N,
                                   uint64_t seed, uint64_t key_space) {
    std::string n_str;
    if      (N >= 1'000'000'000 && N % 1'000'000'000 == 0) n_str = std::to_string(N / 1'000'000'000) + "G";
    else if (N >= 1'000'000     && N % 1'000'000     == 0) n_str = std::to_string(N / 1'000'000) + "M";
    else if (N >= 1'000         && N % 1'000         == 0) n_str = std::to_string(N / 1'000) + "K";
    else                                                    n_str = std::to_string(N);

    std::string ks = (key_space == 0) ? "full" : ("ks" + std::to_string(key_space));
    return dir + "/ds_" + n_str + "_" + ks + "_s" + std::to_string(seed) + ".bin";
}

static void print_usage(const char* prog) {
    std::cerr << "Uso:\n"
              << "  " << prog << "                       crea dataset default\n"
              << "  " << prog << " --list                elenca dataset esistenti\n"
              << "  " << prog << " --custom [opzioni]    crea dataset personalizzato\n"
              << "  " << prog << " --force               ricrea default anche se esistono\n\n"
              << "Opzioni custom:\n"
              << "  -N <num>        numero di chiavi (obbligatorio)\n"
              << "  -s <seed>       seed RNG (default: 42)\n"
              << "  -k <key_space>  universo chiavi, 0=full (default: 0)\n"
              << "  -o <path>       file di output (default: auto in data/)\n\n"
              << "Esempi:\n"
              << "  " << prog << " --custom -N 50000000\n"
              << "  " << prog << " --custom -N 10000000 -k 1000 -s 7\n";
}

int main(int argc, char* argv[]) {
    const std::string data_dir = "data";
    mkdirs(data_dir);

    enum Mode { DEFAULT, LIST, CUSTOM, FORCE };
    Mode mode = DEFAULT;

    size_t   custom_N  = 0;
    uint64_t custom_s  = 42;
    uint64_t custom_ks = 0;
    std::string custom_out;

    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help")   { print_usage(argv[0]); return 0; }
        else if (a == "--list")           { mode = LIST; }
        else if (a == "--custom")         { mode = CUSTOM; }
        else if (a == "--force")          { mode = FORCE; }
        else if (a == "-N" && i+1 < argc) { custom_N  = std::stoull(argv[++i]); }
        else if (a == "-s" && i+1 < argc) { custom_s  = std::stoull(argv[++i]); }
        else if (a == "-k" && i+1 < argc) { custom_ks = std::stoull(argv[++i]); }
        else if (a == "-o" && i+1 < argc) { custom_out = argv[++i]; }
        else { std::cerr << "argomento sconosciuto: " << a << "\n"; print_usage(argv[0]); return 1; }
    }

    // --- LIST ---
    if (mode == LIST) {
        std::cout << "Dataset in " << data_dir << "/:\n";
        DIR* d = opendir(data_dir.c_str());
        if (!d) { std::cout << "  (nessuno)\n"; return 0; }
        bool found = false;
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name.size() > 4 && name.substr(name.size() - 4) == ".bin") {
                print_dataset_info(data_dir + "/" + name);
                found = true;
            }
        }
        closedir(d);
        if (!found) std::cout << "  (nessuno -- esegui senza argomenti per creare i default)\n";
        return 0;
    }

    // --- CUSTOM ---
    if (mode == CUSTOM) {
        if (custom_N == 0) {
            std::cerr << "--custom richiede -N <num_chiavi>\n";
            print_usage(argv[0]);
            return 1;
        }
        std::string path = custom_out.empty()
                         ? make_auto_path(data_dir, custom_N, custom_s, custom_ks)
                         : custom_out;

        if (file_exists(path) && validate_dataset(path, custom_N, custom_s, custom_ks)) {
            std::cout << "SKIP (esiste già): " << path << "\n";
            print_dataset_info(path);
            return 0;
        }

        std::cout << "Creazione dataset custom:\n"
                  << "  N=" << custom_N << "  seed=" << custom_s
                  << "  key_space=" << (custom_ks == 0 ? "full" : std::to_string(custom_ks))
                  << "  -> " << path << "\n";

        auto t0 = std::chrono::high_resolution_clock::now();
        bool ok = write_dataset(path, custom_N, custom_s, custom_ks);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (ok) {
            std::cout << "  fatto in " << std::fixed << std::setprecision(1) << ms << " ms\n";
            print_dataset_info(path);
        }
        return ok ? 0 : 1;
    }

    // --- DEFAULT / FORCE ---
    bool force = (mode == FORCE);

    std::cout << "=== Dataset Creator ===\n"
              << (force ? "Modalità: ricrea tutti\n" : "Modalità: crea se mancanti\n")
              << "Directory: " << data_dir << "/\n\n";

    size_t created = 0, skipped = 0;
    double total_ms = 0;

    for (size_t d = 0; d < NUM_DEFAULTS; d++) {
        const auto& cfg = DEFAULTS[d];
        std::string path = data_dir + "/" + cfg.filename;

        std::cout << "[" << d+1 << "/" << NUM_DEFAULTS << "] " << cfg.name
                  << "  (N=" << cfg.N << ", seed=" << cfg.seed
                  << ", key_space=" << (cfg.key_space == 0 ? "full" : std::to_string(cfg.key_space))
                  << ")\n";

        if (!force && file_exists(path) && validate_dataset(path, cfg.N, cfg.seed, cfg.key_space)) {
            std::cout << "  SKIP\n";
            skipped++;
            continue;
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        bool ok = write_dataset(path, cfg.N, cfg.seed, cfg.key_space);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        total_ms += ms;

        if (ok) {
            double mb = double(HEADER_SIZE + cfg.N * sizeof(spm_key_t)) / (1024.0 * 1024.0);
            std::cout << "  CREATO  " << std::fixed << std::setprecision(1)
                      << mb << " MB in " << ms << " ms\n";
            created++;
        }
    }

    std::cout << "\nRiepilogo: " << created << " creati, " << skipped << " saltati";
    if (created > 0) std::cout << " (" << std::fixed << std::setprecision(0) << total_ms << " ms totali)";
    std::cout << "\n";
    return 0;
}
