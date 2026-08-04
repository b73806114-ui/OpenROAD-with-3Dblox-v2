#include "try_routing.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

void Escaper::baseline_dp() {
    Timer timer("baseline_dp");

    // Algorithm 1 from the paper: Baseline Escape Order Finding.
    // DP that keeps only the single best solution per Tx (min wc, break ties
    // with wt). This produces a near-optimal baseline for pruning the BFS stage.

    // Build initial bitstream-indexed table with the empty solution.
    SolutionTable dp_table;
    const boost::dynamic_bitset<> empty_subset(net_count);
    Solution root{};
    root.last_net = net_count;
    root.contour.resize(resource_table.size());
    dp_table[empty_subset] = std::vector<Solution>{root};

    for (std::uint32_t level = 0U; level < net_count; ++level) {
        // Snapshot current subsets at this level.
        std::vector<boost::dynamic_bitset<>> current_subsets;
        for (const auto& entry : dp_table) {
            if (entry.first.count() == level) {
                current_subsets.push_back(entry.first);
            }
        }

        for (const boost::dynamic_bitset<>& subset : current_subsets) {
            std::vector<Solution>& solutions = dp_table[subset];

            // Route and evaluate each solution.
            for (Solution& solution : solutions) {
                (void)route(solution);
            }

            // Line 12-13: Keep only δ* with minimum wc (break ties with wt).
            Solution* best = &solutions.front();
            for (std::size_t i = 1U; i < solutions.size(); ++i) {
                Solution& candidate = solutions[i];
                if (candidate.critical_length < best->critical_length ||
                    (candidate.critical_length == best->critical_length &&
                     candidate.total_length < best->total_length)) {
                    best = &candidate;
                }
            }

            // Remove all other solutions (paper line 13).
            // We do this by keeping only best and wrapping up below.
            Solution best_copy = *best;

            // Line 14: Find Nδ* — feasible next nets.
            std::vector<std::uint16_t> feasible;
            find_successors(best_copy.contour, feasible);

            // Lines 15-21: Create new solutions for the next level.
            for (const std::uint16_t next_net : feasible) {
                boost::dynamic_bitset<> next_subset(subset);
                next_subset.set(next_net);

                if (dp_table.find(next_subset) == dp_table.end()) {
                    dp_table[next_subset] = std::vector<Solution>();
                }

                Solution child = best_copy;
                child.last_net = next_net;
                child.order.push_back(next_net);
                child.order.shrink_to_fit();
                dp_table[next_subset].push_back(std::move(child));
            }
        }

        // Erase processed subsets of this level (paper line 13: remove all
        // except δ*, which we already propagated).
        for (const boost::dynamic_bitset<>& subset : current_subsets) {
            dp_table.erase(subset);
        }
    }

    // Backtrack: find the complete solution (all nets routed) and record the
    // baseline escape order and critical length.
    const boost::dynamic_bitset<> full_subset = ~empty_subset;
    auto it = dp_table.find(full_subset);
    if (it == dp_table.end() || it->second.empty()) {
        // No complete solution found — baseline is the full-subset solution.
        // This shouldn't happen if the problem is routable.
        baseline_found = false;
        return;
    }

    // Route all complete solutions and pick the best.
    for (Solution& solution : it->second) {
        (void)route(solution);
    }

    Solution* best = &it->second.front();
    for (std::size_t i = 1U; i < it->second.size(); ++i) {
        Solution& candidate = it->second[i];
        if (candidate.critical_length < best->critical_length ||
            (candidate.critical_length == best->critical_length &&
             candidate.total_length < best->total_length)) {
            best = &candidate;
        }
    }

    baseline_critical_length = best->critical_length;
    baseline_found = true;

    std::cout << "Baseline DP critical length: " << baseline_critical_length
              << std::endl;
}
