#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "try_routing.hpp"

void Escaper::create_subsolutions(SolutionTable& solutions,
                                  SuccessorTable& successors)
{
  Timer timer("create_subsolutions");

  // Snapshot only the keys present on entry. New one-bit-larger subsets are
  // deliberately not processed during this invocation.
  std::vector<boost::dynamic_bitset<>> current_subsets;
  for (const auto& entry : solutions) {
    current_subsets.push_back(entry.first);
  }

  for (const boost::dynamic_bitset<>& subset : current_subsets) {
    std::vector<Solution>& subset_solutions = solutions[subset];
    std::vector<std::vector<std::uint16_t>>& subset_successors
        = successors[subset];

    auto solution_it = subset_solutions.begin();
    auto successor_it = subset_successors.begin();
    while (solution_it != subset_solutions.end()
           || successor_it != subset_successors.end()) {
      if (solution_it->last_net != static_cast<std::uint32_t>(net_count) + 1U) {
        for (const std::uint16_t next_net : *successor_it) {
          boost::dynamic_bitset<> next_subset(subset);
          next_subset.set(next_net);

          if (solutions.find(next_subset) == solutions.end()) {
            solutions[next_subset] = std::vector<Solution>();
          }

          std::vector<Solution>& next_solutions = solutions[next_subset];
          next_solutions.push_back(*solution_it);

          Solution& appended = next_solutions.back();
          appended.last_net = next_net;
          appended.order.push_back(next_net);
          appended.order.shrink_to_fit();
        }
      }

      ++solution_it;
      ++successor_it;
    }

    solutions.erase(subset);
  }
}
