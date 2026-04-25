#ifndef VERIFIER_HPP
#define VERIFIER_HPP

#include <vector>
#include "common_structs.hpp"
#include "generator.hpp"

// ------------------------------------------------------------
// Naive join verifier for small inputs — O(|R| * |S|)
// ------------------------------------------------------------
static inline JoinResult naive_join_verifier(const std::vector<Record>& R,
                                             const std::vector<Record>& S) {
    JoinResult result{};
    for (const auto& r : R) {
        for (const auto& s : S) {
            if (r.key == s.key) {
                result.join_count += 1;
                result.checksum1 += splitmix64(r.key);
                result.checksum2 += splitmix64(r.key ^ 0x9e3779b97f4a7c15ULL);
            }
        }
    }
    return result;
}

#endif // VERIFIER_HPP
