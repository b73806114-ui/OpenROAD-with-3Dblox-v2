#include "try_routing.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

void Escaper::find_successors(
    const std::vector<ContourItem>& contour,
    std::vector<std::uint16_t>& successors) {
    Timer timer("find_successors");

    successors.clear();
    const std::uint32_t die_cols =
        static_cast<std::uint32_t>(split_column) + 1U;
    const std::uint32_t total_cols =
        static_cast<std::uint32_t>(contour.size());
    successors.reserve(die_cols);

    // ── Hybrid: paper's die∩sub intersection + range scan ──
    // 1. Find exhausted column boundaries (original logic).
    std::uint32_t low = static_cast<std::uint32_t>(split_column);
    while (low < total_cols &&
           contour[low].interval_index < resource_table[low].size() &&
           contour[low].point_index !=
               resource_table[low][contour[low].interval_index].capacity &&
           low != 0U) {
        --low;
    }

    std::uint32_t upp = die_cols;
    while (upp < total_cols &&
           contour[upp].interval_index < resource_table[upp].size() &&
           contour[upp].point_index !=
               resource_table[upp][contour[upp].interval_index].capacity &&
           upp != total_cols - 1U) {
        ++upp;
    }

    // 2. Collect die-side contour net IDs (columns right of exhausted).
    std::vector<std::uint32_t> die_ctr;
    for (std::uint32_t col = 0U; col < die_cols; ++col) {
        if (col > upp) break;
        const std::uint32_t ci = contour[col].interval_index;
        if (ci >= resource_table[col].size()) continue;
        const Interval& iv = resource_table[col][ci];
        if (iv.second != nullptr) die_ctr.push_back(iv.second->id);
    }

    // 3. Collect substrate-side contour net IDs (columns left of exhausted).
    std::vector<std::uint32_t> sub_ctr;
    for (std::uint32_t col = die_cols; col < total_cols; ++col) {
        if (col < low) continue;
        const std::uint32_t ci = contour[col].interval_index;
        if (ci >= resource_table[col].size()) continue;
        const Interval& iv = resource_table[col][ci];
        if (iv.second != nullptr) sub_ctr.push_back(iv.second->id);
    }

    // 4. Intersection = feasible next nets (both bumps at contour).
    successors.reserve(std::min(die_ctr.size(), sub_ctr.size()));
    for (const std::uint32_t nid : die_ctr) {
        if (std::find(sub_ctr.begin(), sub_ctr.end(), nid) != sub_ctr.end()) {
            successors.emplace_back(static_cast<std::uint16_t>(nid));
        }
    }

    // 5. Crossing-aware successors: nets in crossing groups where at least
    //    one bump is a contour bump.
    if (!crossing_groups.empty()) {
        std::unordered_set<std::uint32_t> dc2, sc2;
        for (std::uint32_t c = 0U; c < die_cols; ++c) {
            const std::uint32_t ci = contour[c].interval_index;
            if (ci >= resource_table[c].size()) continue;
            const Interval& iv = resource_table[c][ci];
            if (iv.second != nullptr) dc2.insert(iv.second->id);
        }
        for (std::uint32_t c = die_cols; c < total_cols; ++c) {
            const std::uint32_t ci = contour[c].interval_index;
            if (ci >= resource_table[c].size()) continue;
            const Interval& iv = resource_table[c][ci];
            if (iv.second != nullptr) sc2.insert(iv.second->id);
        }
        for (const auto& grp : crossing_groups) {
            for (const std::uint32_t nid : grp) {
                if (std::find(successors.begin(), successors.end(),
                              static_cast<std::uint16_t>(nid)) !=
                    successors.end()) {
                    continue;
                }
                if (dc2.count(nid) || sc2.count(nid)) {
                    successors.emplace_back(
                        static_cast<std::uint16_t>(nid));
                }
            }
        }
    }

    successors.shrink_to_fit();
}
