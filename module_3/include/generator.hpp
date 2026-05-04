#ifndef GENERATOR_HPP
#define GENERATOR_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include "common_structs.hpp"
#include "common.hpp"
#include <set>

// ------------------------------------------------------------
// Deterministic pseudo-random generation (splitmix64)
// ------------------------------------------------------------
static inline std::uint64_t splitmix64_mix(std::uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}

static inline std::uint64_t splitmix64(std::uint64_t x) {
    return splitmix64_mix(x + 0x9e3779b97f4a7c15ULL);
}

static inline std::uint64_t splitmix64_next(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    return splitmix64_mix(state);
}

static inline std::vector<Record> generate_relation(std::size_t n, std::uint64_t seed, std::uint64_t max_key) {
    std::vector<Record> out(n);
    std::uint64_t state = seed;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t r = splitmix64_next(state);
        out[i].key = (max_key == 0) ? 0ULL : (r % max_key);
    }
    return out;
}

static inline std::vector<Record> generate_skewed_relation(
    std::size_t n, 
    std::uint64_t seed, //state
    std::uint64_t max_key,
    std::uint32_t P ,
    double rho,
    std::uint32_t hot_count
){

    //checks:
    if (max_key == 0) max_key = 1; 
    if (hot_count == 0) rho = 0.0; // Se non ci sono hot keys, disabilita lo skew

    uint64_t state = seed;
    std::set<part_t> hot_partitions;
    std::vector<uint64_t> hot_keys;
    unsigned shift = compute_shift(P);
    std::vector<Record> out(n);

    if (hot_count > (std::uint32_t)P) hot_count = (std::uint32_t)P;
    while(hot_keys.size() < hot_count){
        u_int64_t random_key = splitmix64_next(state) % max_key;
        part_t partition = hash_key(random_key,shift);
        if(!hot_partitions.contains(partition)){
            hot_partitions.insert(partition);
            hot_keys.push_back(random_key);            
        }
    }

    // threshold in [0, 1000000) to match the modulo range
    std::uint64_t threshold = static_cast<std::uint64_t>(rho * 1000000.0);

    for (size_t i = 0; i < n; ++i){
        if((splitmix64_next(state) % 1000000) < threshold){ //hot record
            std::uint64_t random_index = splitmix64_next(state) % hot_count;
            out[i].key = hot_keys[random_index];
        }
        else{ //cold record
            out[i].key = splitmix64_next(state) % max_key;
        }
    }

    return out;

}

#endif // GENERATOR_HPP
