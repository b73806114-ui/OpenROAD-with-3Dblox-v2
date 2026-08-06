#include "try_routing.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

std::vector<std::uint16_t> normalized_profile(
    const Escaper& escaper, const Solution& solution) {
    const auto column_count =
        static_cast<std::uint32_t>(escaper.split_column) + 1U;
    std::vector<std::uint16_t> profile(column_count, 0U);
    if (column_count == 0U) {
        return profile;
    }

    const auto available_at = [&](std::uint32_t column) {
        const ContourItem& item = solution.contour[column];
        const Interval& interval =
            escaper.resource_table[column][item.interval_index];
        return interval.capacity - item.point_index;
    };

    std::uint16_t suffix_min =
        static_cast<std::uint16_t>(available_at(column_count - 1U));
    for (std::uint32_t column = column_count; column != 0U;) {
        --column;
        const std::uint32_t available = available_at(column);
        if (suffix_min <= available) {
            profile[column] = suffix_min;
        } else {
            profile[column] = static_cast<std::uint16_t>(available);
            suffix_min = static_cast<std::uint16_t>(available);
        }
    }

    std::uint16_t prefix_limit = 0U;
    for (std::uint16_t& value : profile) {
        if (prefix_limit < value) {
            value = prefix_limit;
        }
        prefix_limit = static_cast<std::uint16_t>(value + 1U);
    }
    return profile;
}

bool elementwise_less_equal(const std::vector<std::uint16_t>& lhs,
                            const std::vector<std::uint16_t>& rhs) {
    return std::equal(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                      [](std::uint16_t left, std::uint16_t right) {
                          return left <= right;
                      });
}

}  // namespace

// Pareto dominance over critical length, total length, and optionally a
// normalized residual-capacity contour. Returns true when the existing solution
// dominates the candidate. When the reverse dominance holds, invalidates the
// existing solution in place with a 1e100 sentinel.
bool Escaper::compare(const Solution& candidate, Solution& incumbent,
                      bool compare_contours) {
    Timer timer("compare");

    const std::vector<std::uint16_t> incumbent_profile =
        normalized_profile(*this, incumbent);
    const std::vector<std::uint16_t> candidate_profile =
        normalized_profile(*this, candidate);

    const bool incumbent_contour_no_worse =
        !compare_contours ||
        elementwise_less_equal(candidate_profile, incumbent_profile);
    const bool candidate_contour_no_worse =
        !compare_contours ||
        elementwise_less_equal(incumbent_profile, candidate_profile);

    // Equal solutions retain the incumbent rather than invalidating it.
    if (candidate.critical_length >= incumbent.critical_length &&
        candidate.total_length >= incumbent.total_length &&
        incumbent_contour_no_worse) {
        return true;
    }

    if (candidate.critical_length <= incumbent.critical_length &&
        candidate.total_length <= incumbent.total_length &&
        candidate_contour_no_worse) {
        incumbent.critical_length = 1.0e100;
        incumbent.last_net = static_cast<std::uint16_t>(net_count + 1U);
        incumbent.contour.clear();
        incumbent.order.clear();
    }
    return false;
}
