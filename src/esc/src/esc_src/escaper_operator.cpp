#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "try_routing.hpp"

void Escaper::operator()(std::vector<Bump>& die_bumps,
                         const std::string& die_direction_in,
                         std::vector<Bump>& substrate_bumps,
                         const std::string& substrate_direction_in,
                         const std::vector<Net>& nets)
{
  Timer timer("operator()");

  die_direction = die_direction_in;
  substrate_direction = substrate_direction_in;
  create_resource_table(die_bumps, substrate_bumps);

  net_count = static_cast<std::uint16_t>(nets.size());
  best_solution = nullptr;
  solution_table.clear();

  Solution root{};
  root.last_net = net_count;
  root.contour.resize(resource_table.size());

  const boost::dynamic_bitset<> empty_subset(net_count);
  solution_table[empty_subset] = std::vector<Solution>{root};

  for (std::uint32_t level = 0U; level < net_count; ++level) {
    SuccessorTable successor_table;

    for (auto& entry : solution_table) {
      const boost::dynamic_bitset<>& subset = entry.first;
      std::vector<Solution>& solutions = entry.second;

      successor_table[subset]
          = std::vector<std::vector<std::uint16_t>>(solutions.size());
      std::vector<std::vector<std::uint16_t>>& successor_lists
          = successor_table[subset];

      std::uint32_t index = 0U;
      auto successor_it = successor_lists.begin();
      for (Solution& solution : solutions) {
        (void) route(solution);
        if (assess(solution, index, solutions, false)) {
          find_successors(solution.contour, *successor_it);
        }
        ++index;
        ++successor_it;
      }
    }

    create_subsolutions(solution_table, successor_table);
  }

  if (!solution_table.empty()) {
    std::vector<Solution>& complete_solutions = solution_table.begin()->second;
    for (Solution& solution : complete_solutions) {
      (void) route(solution);
    }
    backtrack();
  }
}
