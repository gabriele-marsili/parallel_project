#ifndef UTILITIES_FNS_HPP
#define UTILITIES_FNS_HPP

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include "common_structs.hpp"
#include "common.hpp"


// command-line parsing
static inline bool read_arg_u64(int argc, char** argv, const std::string& name, std::uint64_t& out) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (name == argv[i]) {
            out = std::strtoull(argv[i + 1], nullptr, 10);
            return true;
        }
    }
    return false;
}

static inline bool read_arg_str(int argc, char** argv, const std::string& name, std::string& out) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (name == argv[i]) {
            out = argv[i + 1];
            return true;
        }
    }
    return false;
}

static inline bool read_arg_double(int argc, char** argv, const std::string& name, double& out) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (name == argv[i]) {
            out = std::strtod(argv[i + 1], nullptr);
            return true;
        }
    }
    return false;
}

static inline void usage_seq(const char* prog) {
    std::cerr
        << "Usage:\n"
        << "  " << prog << " -nr NR -ns NS -seed SEED -max-key K -p P\n\n"
        << "Parameters:\n"
        << "  -nr         Number of records in relation R\n"
        << "  -ns         Number of records in relation S\n"
        << "  -seed       Deterministic seed\n"
        << "  -max-key    Keys are generated in [0, max-key)\n"
        << "  -p          Number of partitions (power of two)\n";
}

static inline void usage_omp(const char* prog) {
    std::cerr
        << "Usage:\n"
        << "  " << prog << " -nr NR -ns NS -seed SEED -max-key K -p P [-t THREADS]\n\n"
        << "Parameters:\n"
        << "  -nr         Number of records in relation R\n"
        << "  -ns         Number of records in relation S\n"
        << "  -seed       Deterministic seed\n"
        << "  -max-key    Keys are generated in [0, max-key)\n"
        << "  -p          Number of partitions (power of two)\n"
        << "  -mode       Loop or task mode\n"
        << "  -hot        Hot key quantity\n"        
        << "  -skew       RHO value\n";
}

static inline bool is_power_of_two(std::uint32_t x) {
    return x != 0 && (x & (x - 1U)) == 0;
}

// utility to print the skew profile
static inline void print_partition_histogram(const std::vector<Record>& R, std::uint32_t P, unsigned shift) {
    // compute histogram
    std::vector<std::size_t> hist(P, 0);
    for (const auto& r : R) {
        hist[hash_key(r.key, shift)]++;
    }

    // find max and mean
    std::size_t max_val = 0;
    std::size_t sum = 0;
    for (std::size_t v : hist) {
        if (v > max_val) max_val = v;
        sum += v;
    }

    double mean = static_cast<double>(sum) / P;
    double max_mean_ratio = static_cast<double>(max_val) / mean;

    // Gini coeff to evaluate how much the distribution is skewed:
    // ( sum_{i,j} |x_i - x_j| ) / (2 * P^2 * mean)
    double diff_sum = 0.0;
    for (std::uint32_t i = 0; i < P; ++i) {
        for (std::uint32_t j = 0; j < P; ++j) {
            // cast into double to avoid underflow on size_t
            diff_sum += std::abs(static_cast<double>(hist[i]) - static_cast<double>(hist[j]));
        }
    }
    double gini = diff_sum / (2.0 * P * P * mean);

    // print results
    std::cout << "--- Skew Profile ---\n";
    std::cout << "N. Records : " << sum << "\n";
    std::cout << "Partitions : " << P << "\n";
    std::cout << "Mean load  : " << mean << "\n";
    std::cout << "Max load   : " << max_val << "\n";
    std::cout << "Max / Mean : " << max_mean_ratio << " (Target: >= 20)\n";
    std::cout << "Gini Coeff : " << gini << " (0.0 = uniform, 1.0 = max skew)\n";
    std::cout << "--------------------\n";
}

#endif // UTILITIES_FNS_HPP
