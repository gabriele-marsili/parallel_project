#ifndef JOIN_PHASES_HPP
#define JOIN_PHASES_HPP

#include <cstdint>
#include <cstddef>
#include <vector>
#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"


// Shared algorithmic building blocks used by both sequential and parallel implementations.



// Exclusive prefix sum — O(P), inherently sequential
// Used by both seq and par (operates on small P-sized arrays).
static inline std::vector<std::size_t> exclusive_prefix_sum(const std::vector<std::size_t>& hist) {
    std::vector<std::size_t> begin(hist.size(), 0);
    std::size_t running = 0;
    for (std::size_t i = 0; i < hist.size(); ++i) {
        begin[i] = running;
        running += hist[i];
    }
    return begin;
}

// In-place variant: writes into a pre-allocated buffer of size hist.size().
// No heap allocation — safe inside a noexcept barrier completion function.
static inline void exclusive_prefix_sum_inplace(const std::vector<std::size_t>& hist,
                                                 std::vector<std::size_t>& out) noexcept {
    std::size_t running = 0;
    for (std::size_t i = 0; i < hist.size(); ++i) {
        out[i] = running;
        running += hist[i];
    }
}


/* FlatCountMap — open-addressing hash table with linear probing

Replaces std::unordered_map<uint64_t,uint32_t> for per-partition key counting. 

Advantages over std::unordered_map:
1. No heap allocations per slot (single contiguous vector)
2. No pointer chasing (separate chaining eliminated)
3. Better cache locality: each slot is 16 bytes; a cache line holds 4 slots 
    -> fewer misses during linear probing

Sentinel: UINT64_MAX (safe because keys are always < max_key ≤ 2^30).
Table is sized to the next power of two ≥ 2 × r_count, keeping
the load factor ≤ 50% and bounding the expected probe length.
Slot function: identity hash (key & mask) — avoids correlation
with the Fibonacci partitioning hash used in histogram/scatter.
*/
struct FlatCountMap {
    struct Slot {
        std::uint64_t key = ~0ULL; // sentinel: empty
        std::uint32_t cnt = 0;
        std::uint32_t _p  = 0; // padding -> 16-byte slot (4 per cache line)
    };

    std::vector<Slot> slots;
    std::uint32_t     mask; // slots.size() - 1 (power-of-two mask)
    std::uint32_t     shift; // unused — kept for struct alignment (16-byte FlatCountMap)

    explicit FlatCountMap(std::size_t r_count) {
        std::size_t n = 1;
        while (n < r_count * 2) n <<= 1;
        slots.assign(n, Slot{});
        mask  = static_cast<std::uint32_t>(n - 1);
        shift = 0; // unused — kept for struct alignment
    }

    // Map key -> initial slot index.
    // Identity-based: keys in [0, max_key) have uniform low bits,
    // so key & mask distributes them evenly without correlating
    // with the Fibonacci partitioning hash used in histogram/scatter.
    std::uint32_t slot_of(std::uint64_t key) const noexcept {
        return static_cast<std::uint32_t>(key) & mask;
    }

    // Build phase: count occurrences of each R key
    void increment(std::uint64_t key) noexcept {
        std::uint32_t h = slot_of(key);
        while (slots[h].key != ~0ULL && slots[h].key != key)
            h = (h + 1) & mask;
        if (slots[h].key == ~0ULL) slots[h].key = key;
        ++slots[h].cnt;
    }

    // Probe phase: return count for key (0 if absent)
    std::uint32_t count(std::uint64_t key) const noexcept {
        std::uint32_t h = slot_of(key);
        while (slots[h].key != ~0ULL && slots[h].key != key)
            h = (h + 1) & mask;
        return (slots[h].key == key) ? slots[h].cnt : 0u;
    }
};


// Local join on one partition (build + probe)
// Build:  scan R_p -> FlatCountMap[key] = multiplicity
// Probe:  scan S_p -> for each key, add FlatCountMap[key] matches
static inline JoinResult join_one_partition(const PartitionedRelation& Rpart,
                                            const PartitionedRelation& Spart,
                                            std::uint32_t pid) {
    JoinResult result{};

    const std::size_t r_begin = Rpart.begin[pid];
    const std::size_t r_end   = Rpart.end[pid];
    const std::size_t s_begin = Spart.begin[pid];
    const std::size_t s_end   = Spart.end[pid];

    if (r_begin == r_end || s_begin == s_end) return result;

    // Build phase
    FlatCountMap countR(r_end - r_begin);
    for (std::size_t i = r_begin; i < r_end; ++i)
        countR.increment(Rpart.data[i].key);

    // Probe phase
    for (std::size_t i = s_begin; i < s_end; ++i) {
        const std::uint64_t key = Spart.data[i].key;
        const std::uint32_t m   = countR.count(key);
        if (m) {
            result.join_count += m;
            result.checksum1  += splitmix64(key) * m;
            result.checksum2  += splitmix64(key ^ 0x9e3779b97f4a7c15ULL) * m;
        }
    }
    return result;
}

#endif // JOIN_PHASES_HPP
