// ============================================================================
// dataset_creator.cpp — Deterministic dataset generator for partition mapping
// ============================================================================
//
// DESIGN RATIONALE:
//
// The project specification requires keys to be "generated deterministically
// from a seed, so that the output is reproducible." This tool pre-generates
// key arrays and saves them as binary files, which kernel binaries then
// memory-map (mmap) for zero-copy loading. This design:
//
//   1. SEPARATES generation from benchmarking — key generation time is NOT
//      included in kernel measurements, yielding cleaner timing results.
//   2. GUARANTEES reproducibility — same seed + parameters = same file,
//      byte-for-byte. Re-running skips existing files (idempotent).
//   3. ENABLES sharing — all kernel variants (baseline, autovec, avx2, cuda)
//      read the exact same binary file, eliminating any risk of input drift.
//
// BINARY FILE FORMAT:
//   Bytes 0-7    : magic number 0x53504D4B455953AA ("SPMKEYS" + 0xAA)
//   Bytes 8-15   : N (uint64_t, number of keys)
//   Bytes 16-23  : seed (uint64_t)
//   Bytes 24-31  : key_space (uint64_t, 0 = full 64-bit range)
//   Bytes 32-39  : reserved (zero, for future use)
//   Bytes 40+    : N × uint64_t keys (little-endian on x86/ARM)
//
// USAGE:
//   Default (creates 5 standard datasets):
//     ./dataset_creator
//
//   Custom dataset:
//     ./dataset_creator --custom -N 50000000 -s 42 -k 1000000 -o data/my_dataset.bin
//
//   List existing datasets:
//     ./dataset_creator --list
//
// ============================================================================

#include "common.hpp"
#include <fstream>
#include <filesystem>
#include <string>
#include <sys/stat.h>

namespace fs = std::filesystem;

// ============================================================================
// File format constants
// ============================================================================
static constexpr uint64_t MAGIC = 0x53504D4B455953AAULL;  // "SPMKEYS\xAA"
static constexpr size_t   HEADER_SIZE = 40;                // 5 × uint64_t

struct DatasetHeader {
    uint64_t magic;
    uint64_t N;
    uint64_t seed;
    uint64_t key_space;
    uint64_t reserved;
};

// ============================================================================
// Default dataset configurations
// ============================================================================
// Chosen to cover the experimental space required by the project:
//   - Small N for correctness/element-wise verification
//   - Medium N for development iteration
//   - Large N for stable timings ("tens of millions")
//   - Very large N to stress memory bandwidth
//   - Different key_space values to study duplicate sensitivity
// ============================================================================
struct DatasetConfig {
    size_t   N;
    uint64_t seed;
    uint64_t key_space;   // 0 = full 64-bit range
    const char* name;     // human-readable label
    const char* filename;
};

static const DatasetConfig DEFAULT_DATASETS[] = {
    // name                  N            seed  key_space  label                   filename
    {         1'000'000,      42,           0,  "1M_full_range",    "ds_1M_full.bin"      },
    {        10'000'000,      42,           0,  "10M_full_range",   "ds_10M_full.bin"     },
    {       100'000'000,      42,           0,  "100M_full_range",  "ds_100M_full.bin"    },
    {       200'000'000,      42,           0,  "200M_full_range",  "ds_200M_full.bin"    },
    {       100'000'000,      42,    1'000'000, "100M_high_dup",    "ds_100M_dup1M.bin"   },
};
static constexpr size_t NUM_DEFAULT = sizeof(DEFAULT_DATASETS) / sizeof(DEFAULT_DATASETS[0]);

// ============================================================================
// Write a dataset to disk
// ============================================================================
static bool write_dataset(const std::string& path, size_t N, uint64_t seed, uint64_t key_space) {
    // Allocate and generate
    spm_key_t* keys = alloc_aligned<spm_key_t>(N);
    KeyGenerator::generate(keys, N, seed, key_space);

    // Write header + keys
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        std::cerr << "  ERROR: cannot open " << path << " for writing" << std::endl;
        std::free(keys);
        return false;
    }

    DatasetHeader hdr{};
    hdr.magic     = MAGIC;
    hdr.N         = static_cast<uint64_t>(N);
    hdr.seed      = seed;
    hdr.key_space = key_space;
    hdr.reserved  = 0;

    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    ofs.write(reinterpret_cast<const char*>(keys), N * sizeof(spm_key_t));
    ofs.close();

    std::free(keys);

    if (!ofs) {
        std::cerr << "  ERROR: write failed for " << path << std::endl;
        return false;
    }
    return true;
}

// ============================================================================
// Validate an existing dataset file (header check)
// ============================================================================
static bool validate_dataset(const std::string& path, size_t N, uint64_t seed, uint64_t key_space) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;

    DatasetHeader hdr{};
    ifs.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!ifs) return false;

    // Check magic, N, seed, key_space match
    if (hdr.magic != MAGIC)                     return false;
    if (hdr.N != static_cast<uint64_t>(N))      return false;
    if (hdr.seed != seed)                        return false;
    if (hdr.key_space != key_space)              return false;

    // Check file size is correct
    ifs.seekg(0, std::ios::end);
    auto file_size = ifs.tellg();
    auto expected  = static_cast<std::streamoff>(HEADER_SIZE + N * sizeof(spm_key_t));
    if (file_size != expected) return false;

    return true;
}

// ============================================================================
// Print info about a dataset file
// ============================================================================
static void print_dataset_info(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cout << "  " << path << "  [NOT FOUND]" << std::endl;
        return;
    }

    DatasetHeader hdr{};
    ifs.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!ifs || hdr.magic != MAGIC) {
        std::cout << "  " << path << "  [INVALID FORMAT]" << std::endl;
        return;
    }

    ifs.seekg(0, std::ios::end);
    double size_mb = static_cast<double>(ifs.tellg()) / (1024.0 * 1024.0);

    std::cout << "  " << std::left << std::setw(30) << fs::path(path).filename().string()
              << "  N=" << std::setw(12) << hdr.N
              << "  seed=" << std::setw(6) << hdr.seed
              << "  key_space=" << std::setw(12) << hdr.key_space
              << "  size=" << std::fixed << std::setprecision(1) << size_mb << " MB"
              << std::endl;
}

// ============================================================================
// Usage
// ============================================================================
static void print_usage(const char* prog) {
    std::cerr << "Usage:" << std::endl;
    std::cerr << "  " << prog << "                          Create default datasets (if not existing)" << std::endl;
    std::cerr << "  " << prog << " --list                   List all datasets in data/" << std::endl;
    std::cerr << "  " << prog << " --custom [options]       Create a custom dataset" << std::endl;
    std::cerr << "  " << prog << " --force                  Recreate default datasets even if existing" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Custom options:" << std::endl;
    std::cerr << "  -N <num>        Number of keys (required)" << std::endl;
    std::cerr << "  -s <seed>       RNG seed (default: 42)" << std::endl;
    std::cerr << "  -k <key_space>  Key universe size, 0=full 64-bit (default: 0)" << std::endl;
    std::cerr << "  -o <path>       Output file path (default: auto-generated in data/)" << std::endl;
    std::cerr << std::endl;
    std::cerr << "Examples:" << std::endl;
    std::cerr << "  " << prog << "                                # create 5 standard datasets" << std::endl;
    std::cerr << "  " << prog << " --custom -N 50000000            # 50M keys, full range, seed=42" << std::endl;
    std::cerr << "  " << prog << " --custom -N 10000000 -k 1000 -s 7  # 10M keys, 1000 distinct, seed=7" << std::endl;
    std::cerr << "  " << prog << " --custom -N 500000 -o data/small_test.bin" << std::endl;
}

// ============================================================================
// Construct default output path for a custom dataset
// ============================================================================
static std::string make_auto_path(const std::string& data_dir, size_t N, uint64_t seed, uint64_t key_space) {
    // Format N as human-readable suffix
    std::string n_str;
    if (N >= 1'000'000'000 && N % 1'000'000'000 == 0)
        n_str = std::to_string(N / 1'000'000'000) + "G";
    else if (N >= 1'000'000 && N % 1'000'000 == 0)
        n_str = std::to_string(N / 1'000'000) + "M";
    else if (N >= 1'000 && N % 1'000 == 0)
        n_str = std::to_string(N / 1'000) + "K";
    else
        n_str = std::to_string(N);

    std::string ks_str = (key_space == 0) ? "full" : ("ks" + std::to_string(key_space));
    return data_dir + "/ds_" + n_str + "_" + ks_str + "_s" + std::to_string(seed) + ".bin";
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
    const std::string data_dir = "data";

    // Ensure data directory exists
    fs::create_directories(data_dir);

    // --- Parse mode ---
    enum Mode { DEFAULT, LIST, CUSTOM, FORCE_DEFAULT };
    Mode mode = DEFAULT;

    // Custom params
    size_t   custom_N         = 0;
    uint64_t custom_seed      = 42;
    uint64_t custom_key_space = 0;
    std::string custom_output = "";

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--list") {
            mode = LIST;
        } else if (arg == "--custom") {
            mode = CUSTOM;
        } else if (arg == "--force") {
            mode = FORCE_DEFAULT;
        } else if (arg == "-N" && i + 1 < argc) {
            custom_N = std::stoull(argv[++i]);
        } else if (arg == "-s" && i + 1 < argc) {
            custom_seed = std::stoull(argv[++i]);
        } else if (arg == "-k" && i + 1 < argc) {
            custom_key_space = std::stoull(argv[++i]);
        } else if (arg == "-o" && i + 1 < argc) {
            custom_output = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // ====================================================================
    // MODE: LIST
    // ====================================================================
    if (mode == LIST) {
        std::cout << "Datasets in " << data_dir << "/:" << std::endl;
        if (!fs::exists(data_dir) || fs::is_empty(data_dir)) {
            std::cout << "  (none — run without arguments to create defaults)" << std::endl;
            return 0;
        }
        for (const auto& entry : fs::directory_iterator(data_dir)) {
            if (entry.path().extension() == ".bin") {
                print_dataset_info(entry.path().string());
            }
        }
        return 0;
    }

    // ====================================================================
    // MODE: CUSTOM
    // ====================================================================
    if (mode == CUSTOM) {
        if (custom_N == 0) {
            std::cerr << "ERROR: --custom requires -N <num_keys>" << std::endl;
            print_usage(argv[0]);
            return 1;
        }

        std::string path = custom_output.empty()
                         ? make_auto_path(data_dir, custom_N, custom_seed, custom_key_space)
                         : custom_output;

        // Check if already exists with matching params
        if (fs::exists(path) && validate_dataset(path, custom_N, custom_seed, custom_key_space)) {
            std::cout << "SKIP (already exists and valid): " << path << std::endl;
            print_dataset_info(path);
            return 0;
        }

        std::cout << "Creating custom dataset:" << std::endl;
        std::cout << "  N         = " << custom_N << std::endl;
        std::cout << "  seed      = " << custom_seed << std::endl;
        std::cout << "  key_space = " << (custom_key_space == 0 ? "full 64-bit" : std::to_string(custom_key_space)) << std::endl;
        std::cout << "  output    = " << path << std::endl;

        auto t0 = std::chrono::high_resolution_clock::now();
        bool ok = write_dataset(path, custom_N, custom_seed, custom_key_space);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (ok) {
            std::cout << "  DONE in " << std::fixed << std::setprecision(1) << ms << " ms" << std::endl;
            print_dataset_info(path);
        }
        return ok ? 0 : 1;
    }

    // ====================================================================
    // MODE: DEFAULT / FORCE_DEFAULT
    // ====================================================================
    bool force = (mode == FORCE_DEFAULT);

    std::cout << "============================================" << std::endl;
    std::cout << "SPM Module 1 — Dataset Creator" << std::endl;
    std::cout << (force ? "Mode: FORCE recreate all" : "Mode: create if missing") << std::endl;
    std::cout << "Output directory: " << data_dir << "/" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << std::endl;

    size_t created  = 0;
    size_t skipped  = 0;
    double total_ms = 0;

    for (size_t d = 0; d < NUM_DEFAULT; d++) {
        const auto& cfg = DEFAULT_DATASETS[d];
        std::string path = data_dir + "/" + cfg.filename;

        std::cout << "[" << (d + 1) << "/" << NUM_DEFAULT << "] "
                  << cfg.name << "  (N=" << cfg.N
                  << ", seed=" << cfg.seed
                  << ", key_space=" << (cfg.key_space == 0 ? "full" : std::to_string(cfg.key_space))
                  << ")" << std::endl;

        // Check if valid dataset already exists
        if (!force && fs::exists(path) && validate_dataset(path, cfg.N, cfg.seed, cfg.key_space)) {
            std::cout << "  SKIP (already exists)" << std::endl;
            skipped++;
            continue;
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        bool ok = write_dataset(path, cfg.N, cfg.seed, cfg.key_space);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        total_ms += ms;

        if (ok) {
            double size_mb = static_cast<double>(HEADER_SIZE + cfg.N * sizeof(spm_key_t)) / (1024.0 * 1024.0);
            std::cout << "  CREATED  " << std::fixed << std::setprecision(1)
                      << size_mb << " MB in " << ms << " ms"
                      << "  (" << std::setprecision(0) << (cfg.N / (ms / 1e3)) / 1e6 << " Mkeys/s gen)" << std::endl;
            created++;
        }
    }

    std::cout << std::endl;
    std::cout << "Summary: " << created << " created, " << skipped << " skipped";
    if (created > 0)
        std::cout << " (total generation time: " << std::fixed << std::setprecision(0) << total_ms << " ms)";
    std::cout << std::endl;

    return 0;
}
