#include "try_routing.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

void Escaper::detect_crossing_nets(
    const std::vector<Bump>& die_bumps,
    const std::vector<Bump>& substrate_bumps) {
    Timer timer("detect_crossing_nets");

    crossing_groups.clear();

    const std::uint32_t die_cols =
        static_cast<std::uint32_t>(split_column) + 1U;
    const std::uint32_t total_cols =
        static_cast<std::uint32_t>(resource_table.size());

    // Build per-column bump lists (top-to-bottom by interval order).
    std::vector<std::vector<std::uint32_t>> die_col_bumps(die_cols);
    for (std::uint32_t col = 0U; col < die_cols; ++col) {
        die_col_bumps[col].reserve(resource_table[col].size());
        for (const Interval& interval : resource_table[col]) {
            if (interval.second != nullptr) {
                die_col_bumps[col].push_back(interval.second->id);
            }
        }
    }

    std::vector<std::vector<std::uint32_t>> sub_col_bumps(total_cols -
                                                           die_cols);
    for (std::uint32_t col = die_cols; col < total_cols; ++col) {
        const std::size_t si = col - die_cols;
        sub_col_bumps[si].reserve(resource_table[col].size());
        for (const Interval& interval : resource_table[col]) {
            if (interval.second != nullptr) {
                sub_col_bumps[si].push_back(interval.second->id);
            }
        }
    }

    const std::uint32_t n32 = static_cast<std::uint32_t>(net_count);
    std::vector<bool> crossing(n32 * n32, false);

    // Conservative crossing check: only within same die-column and same
    // substrate-column.  If two nets share both columns and their vertical
    // order is reversed, they form a crossing pair.
    for (std::uint32_t dc = 0U; dc < die_col_bumps.size(); ++dc) {
        const auto& dlist = die_col_bumps[dc];
        if (dlist.size() < 2U) continue;

        // Build die position map for this column
        std::unordered_map<std::uint32_t, std::uint32_t> die_pos;
        for (std::uint32_t p = 0U; p < dlist.size(); ++p) {
            die_pos[dlist[p]] = p;
        }

        for (std::uint32_t sc = 0U; sc < sub_col_bumps.size(); ++sc) {
            const auto& slist = sub_col_bumps[sc];
            if (slist.size() < 2U) continue;

            // Build sub position map for this column
            std::unordered_map<std::uint32_t, std::uint32_t> sub_pos;
            for (std::uint32_t p = 0U; p < slist.size(); ++p) {
                sub_pos[slist[p]] = p;
            }

            // Check pairs that appear in BOTH this die column and this sub column
            for (std::uint32_t i = 0U; i < dlist.size(); ++i) {
                const std::uint32_t ni = dlist[i];
                auto si = sub_pos.find(ni);
                if (si == sub_pos.end()) continue;
                for (std::uint32_t j = i + 1U; j < dlist.size(); ++j) {
                    const std::uint32_t nj = dlist[j];
                    auto sj = sub_pos.find(nj);
                    if (sj == sub_pos.end()) continue;

                    // Both nets in same die column AND same sub column
                    // Crossing if vertical order is reversed
                    const bool die_order = i < j;  // ni above nj in die
                    const bool sub_order =
                        si->second < sj->second;   // ni above nj in sub
                    if (die_order != sub_order) {
                        crossing[ni * n32 + nj] = true;
                        crossing[nj * n32 + ni] = true;
                    }
                }
            }
        }
    }

    // Union-find for transitive groups
    std::vector<std::uint32_t> parent(n32);
    for (std::uint32_t i = 0U; i < n32; ++i) parent[i] = i;
    auto find = [&parent](std::uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };

    for (std::uint32_t i = 0U; i < n32; ++i) {
        for (std::uint32_t j = i + 1U; j < n32; ++j) {
            if (crossing[i * n32 + j]) {
                std::uint32_t ri = find(i), rj = find(j);
                if (ri != rj) parent[rj] = ri;
            }
        }
    }

    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> gm;
    for (std::uint32_t i = 0U; i < n32; ++i) {
        gm[find(i)].push_back(i);
    }
    for (auto& e : gm) {
        if (e.second.size() > 1U) {
            crossing_groups.push_back(std::move(e.second));
        }
    }
}
