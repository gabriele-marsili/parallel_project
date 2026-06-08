#ifndef COMMON_STRUCTS_HPP
#define COMMON_STRUCTS_HPP

#include <cstdint>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

struct Record {
    std::uint64_t key;
};

/* allocator that default-initialises elements:
-> uses default-constructible T => makes vector::resize skip the 
per-element write, so the first parallel write to a page is the one that fixes
its NUMA placement under Linux first-touch */
template <class T>
struct default_init_allocator : std::allocator<T> {
    using std::allocator<T>::allocator;

    template <class U>
    struct rebind { using other = default_init_allocator<U>; };

    template <class U>
    void construct(U* p) noexcept(std::is_nothrow_default_constructible_v<U>) {
        ::new (static_cast<void*>(p)) U;
    }

    template <class U, class... Args>
    void construct(U* p, Args&&... args) {
        ::new (static_cast<void*>(p)) U(std::forward<Args>(args)...);
    }
};

struct PartitionedRelation {
    std::vector<Record, default_init_allocator<Record>> data;
    std::vector<std::size_t> begin;
    std::vector<std::size_t> end;
};

// join result
struct JoinResult {
    std::uint64_t join_count = 0;
    std::uint64_t checksum1 = 0;
    std::uint64_t checksum2 = 0;
};

// phase timing breakdown
struct PhaseTiming {
    double histogram_R  = 0.0;
    double scatter_R    = 0.0;
    double histogram_S  = 0.0;
    double scatter_S    = 0.0;
    double join_local   = 0.0;
    double accumulation = 0.0;
    double total        = 0.0;

    void print() const {
        auto pct = [&](double v) { return (total > 0) ? 100.0 * v / total : 0.0; };
        /* format "  LABEL : VALUE ms  (PERC%)", the parse script greps by label
           and reads awk field $3 as the numeric value */
        std::fprintf(stderr,
            "--- Phase Breakdown ---\n"
            "  Histogram_R : %10.3f ms  (%5.1f%%)\n"
            "  Scatter_R   : %10.3f ms  (%5.1f%%)\n"
            "  Histogram_S : %10.3f ms  (%5.1f%%)\n"
            "  Scatter_S   : %10.3f ms  (%5.1f%%)\n"
            "  Join_local  : %10.3f ms  (%5.1f%%)\n"
            "  Accumulation: %10.3f ms  (%5.1f%%)\n"
            "  TOTAL       : %10.3f ms\n",
            histogram_R  * 1e3, pct(histogram_R),
            scatter_R    * 1e3, pct(scatter_R),
            histogram_S  * 1e3, pct(histogram_S),
            scatter_S    * 1e3, pct(scatter_S),
            join_local   * 1e3, pct(join_local),
            accumulation * 1e3, pct(accumulation),
            total        * 1e3);
    }
};

#endif // COMMON_STRUCTS_HPP
