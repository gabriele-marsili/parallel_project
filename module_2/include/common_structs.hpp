#ifndef COMMON_STRUCTS_HPP
#define COMMON_STRUCTS_HPP

#include <cstdint>
#include <cstddef>
#include <vector>

// ------------------------------------------------------------
// Record definition
// ------------------------------------------------------------
struct Record {
    std::uint64_t key{};
};

// ------------------------------------------------------------
// Partitioned relation metadata
// ------------------------------------------------------------
struct PartitionedRelation {
    std::vector<Record> data;
    std::vector<std::size_t> begin;
    std::vector<std::size_t> end;
};

// ------------------------------------------------------------
// Join result
// ------------------------------------------------------------
struct JoinResult {
    std::uint64_t join_count = 0;
    std::uint64_t checksum1 = 0;
    std::uint64_t checksum2 = 0;
};

// ------------------------------------------------------------
// Phase timing breakdown
// ------------------------------------------------------------
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
        // Format: "  LABEL : VALUE ms  (PERC%)"
        // The parse script greps by label and reads awk field $3 as the numeric value.
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
