#ifndef JOIN_PHASES_HPP
#define JOIN_PHASES_HPP

#include <cassert>
#include <cstdint>
#include <cstddef>
#include <vector>
#include "common.hpp"
#include "common_structs.hpp"
#include "generator.hpp"


// shared building blocks used by both the sequential and parallel paths

// exclusive prefix sum, O(P), inherently sequential (small P-sized arrays)
static inline std::vector<std::size_t> exclusive_prefix_sum(const std::vector<std::size_t>& hist) {
    std::vector<std::size_t> begin(hist.size(), 0);
    std::size_t running = 0;
    for (std::size_t i = 0; i < hist.size(); ++i) {
        begin[i] = running;
        running += hist[i];
    }
    return begin;
}

// in-place variant: no heap allocation, safe inside a noexcept barrier completion fn
static inline void exclusive_prefix_sum_inplace(const std::vector<std::size_t>& hist,
                                                 std::vector<std::size_t>& out) noexcept {
    std::size_t running = 0;
    for (std::size_t i = 0; i < hist.size(); ++i) {
        out[i] = running;
        running += hist[i];
    }
}


/* FlatCountMap: open-addressing hash table (linear probing) that counts
per-partition key occurrences instead of std::unordered_map.
• single contiguous vector: no per-node allocs, no pointer chasing, 16-byte
  slots (4 per cache line) keep probing cache-friendly
• empty sentinel UINT64_MAX (safe: keys < max_key <= 2^30)
• size = next pow2 >= 2*r_count -> load factor < 50%, short probe chains
• slot = key & mask (identity hash): keys have uniform low bits, and this
  avoids reusing the Fibonacci hash, which would cluster keys here */
struct FlatCountMap {
    struct Slot {
        std::uint64_t key = ~0ULL; // sentinel: empty
        std::uint32_t cnt = 0;
        std::uint32_t _p  = 0; // padding -> 16-byte slot (4 per cache line)
    };

    std::vector<Slot> slots;
    std::uint32_t     mask; // slots.size() - 1 (power-of-two mask)

    explicit FlatCountMap(std::size_t r_count) {
        std::size_t n = 1;
        while (n < r_count * 2) n <<= 1;
        slots.assign(n, Slot{});
        mask  = static_cast<std::uint32_t>(n - 1);
    }

    // initial slot index: key & mask (identity hash), as explained above
    std::uint32_t slot_of(std::uint64_t key) const noexcept {
        return static_cast<std::uint32_t>(key) & mask;
    }

    // build phase: count occurrences of each R key
    void increment(std::uint64_t key) noexcept {
        assert(key != ~0ULL); // sentinel value, would corrupt the table
        std::uint32_t h = slot_of(key);
        while (slots[h].key != ~0ULL && slots[h].key != key)
            h = (h + 1) & mask;
        if (slots[h].key == ~0ULL) slots[h].key = key;
        ++slots[h].cnt;
    }

    // probe phase: return count for key (0 if absent)
    std::uint32_t count(std::uint64_t key) const noexcept {
        std::uint32_t h = slot_of(key);
        while (slots[h].key != ~0ULL && slots[h].key != key)
            h = (h + 1) & mask;
        return (slots[h].key == key) ? slots[h].cnt : 0u;
    }
};


// build the per-partition count table from R_p, returned by value (NRVO)
static inline FlatCountMap build_table(const PartitionedRelation& Rpart,
                                       std::uint32_t pid) {
    const std::size_t r_begin = Rpart.begin[pid];
    const std::size_t r_end   = Rpart.end[pid];
    FlatCountMap countR(r_end - r_begin);
    for (std::size_t i = r_begin; i < r_end; ++i)
        countR.increment(Rpart.data[i].key);
    return countR;
}

/* probe a contiguous sub-range [s_lo, s_hi) of S_p against an already-built
   table, with a software prefetch on the random-access slot read */
static inline JoinResult probe_chunk(const FlatCountMap& tbl,
                                     const PartitionedRelation& Spart,
                                     std::size_t s_lo, std::size_t s_hi) {
    JoinResult result{};
    constexpr std::size_t PF_DIST = 8;
    for (std::size_t i = s_lo; i < s_hi; ++i) {
        if (i + PF_DIST < s_hi) {
            const std::uint64_t k_next = Spart.data[i + PF_DIST].key;
            __builtin_prefetch(&tbl.slots[tbl.slot_of(k_next)], 0, 0);
        }
        const std::uint64_t key = Spart.data[i].key;
        const std::uint32_t m   = tbl.count(key);
        if (m) {
            result.join_count += m;
            result.checksum1  += splitmix64(key) * m;
            result.checksum2  += splitmix64(key ^ 0x9e3779b97f4a7c15ULL) * m;
        }
    }
    return result;
}

// local join on one partition (build + probe)
static inline JoinResult join_one_partition(const PartitionedRelation& Rpart,
                                            const PartitionedRelation& Spart,
                                            std::uint32_t pid) {
    const std::size_t r_begin = Rpart.begin[pid];
    const std::size_t r_end   = Rpart.end[pid];
    const std::size_t s_begin = Spart.begin[pid];
    const std::size_t s_end   = Spart.end[pid];

    if (r_begin == r_end || s_begin == s_end) return JoinResult{};

    FlatCountMap countR = build_table(Rpart, pid);
    return probe_chunk(countR, Spart, s_begin, s_end);
}

#endif // JOIN_PHASES_HPP
