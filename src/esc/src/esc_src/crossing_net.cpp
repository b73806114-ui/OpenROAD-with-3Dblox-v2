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

    // Build per-column sorted bump lists (top-to-bottom by y-position).
    // Each bump appears once per column — extract from interval.second of
    // all non-head intervals + interval.first of tail.
    std::vector<std::vector<std::uint32_t>> die_column_bumps(die_cols);
    for (std::uint32_t col = 0U; col < die_cols; ++col) {
        die_column_bumps[col].reserve(resource_table[col].size());
        for (const Interval& interval : resource_table[col]) {
            if (interval.second != nullptr) {
                die_column_bumps[col].push_back(interval.second->id);
            }
        }
        // Already top-to-bottom from interval order (head→body→...→tail).
    }

    std::vector<std::vector<std::uint32_t>> sub_column_bumps(total_cols -
                                                              die_cols);
    for (std::uint32_t col = die_cols; col < total_cols; ++col) {
        const std::size_t sub_idx = col - die_cols;
        sub_column_bumps[sub_idx].reserve(resource_table[col].size());
        for (const Interval& interval : resource_table[col]) {
            if (interval.second != nullptr) {
                sub_column_bumps[sub_idx].push_back(interval.second->id);
            }
        }
    }

    // Build position map: for each bump ID, record its position in its column.
    auto build_pos = [](const std::vector<std::vector<std::uint32_t>>& cols) {
        std::unordered_map<std::uint32_t,
                           std::pair<std::uint32_t, std::uint32_t>> map;
        for (std::uint32_t ci = 0U; ci < cols.size(); ++ci) {
            for (std::uint32_t pos = 0U; pos < cols[ci].size(); ++pos) {
                map[cols[ci][pos]] = {ci, pos};
            }
        }
        return map;
    };

    const auto die_pos_map = build_pos(die_column_bumps);
    const auto sub_pos_map = build_pos(sub_column_bumps);

    const std::uint32_t net_count_32 =
        static_cast<std::uint32_t>(net_count);

    // Detect crossing pairs: for each pair of nets that both appear in a die
    // column AND a substrate column, check if their vertical ordering differs.
    std::vector<bool> crossing(net_count_32 * net_count_32, false);
    const auto set_cross = [&](std::uint32_t a, std::uint32_t b) {
        crossing[a * net_count_32 + b] = true;
        crossing[b * net_count_32 + a] = true;
    };

    // Only check nets within each die column against the same nets in substrate
    // columns.
    for (std::uint32_t dc = 0U; dc < die_column_bumps.size(); ++dc) {
        const auto& dlist = die_column_bumps[dc];
        const std::size_t n = dlist.size();
        if (n < 2U) continue;

        for (std::size_t i = 0U; i < n; ++i) {
            const std::uint32_t ni = dlist[i];
            auto si_it = sub_pos_map.find(ni);
            if (si_it == sub_pos_map.end()) continue;

            for (std::size_t j = i + 1U; j < n; ++j) {
                const std::uint32_t nj = dlist[j];
                auto sj_it = sub_pos_map.find(nj);
                if (sj_it == sub_pos_map.end()) continue;

                // ni is above nj in die column (i < j, top-to-bottom).
                // Check if the same order holds in the substrate column.
                // If in same sub column: compare positions.
                // If in different sub columns: compare column indices (left=die,
                // right=sub, but cols in sub possess natural left-right order).
                const bool sub_ni_above =
                    (si_it->second.first == sj_it->second.first)
                        ? si_it->second.second < sj_it->second.second
                        : si_it->second.first < sj_it->second.first;
                const bool die_ni_above = (i < j);

                if (die_ni_above != sub_ni_above) {
                    set_cross(ni, nj);
                }
            }
        }
    }

    // Union-find to form transitive crossing groups.
    std::vector<std::uint32_t> parent(net_count_32);
    for (std::uint32_t i = 0U; i < net_count_32; ++i) {
        parent[i] = i;
    }
    const auto find_root = [&parent](std::uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };

    for (std::uint32_t i = 0U; i < net_count_32; ++i) {
        for (std::uint32_t j = i + 1U; j < net_count_32; ++j) {
            if (crossing[i * net_count_32 + j]) {
                const std::uint32_t ri = find_root(i);
                const std::uint32_t rj = find_root(j);
                if (ri != rj) parent[rj] = ri;
            }
        }
    }

    // Collect groups.
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> group_map;
    for (std::uint32_t i = 0U; i < net_count_32; ++i) {
        group_map[find_root(i)].push_back(i);
    }
    for (auto& entry : group_map) {
        if (entry.second.size() > 1U) {
            crossing_groups.push_back(std::move(entry.second));
        }
    }
}
