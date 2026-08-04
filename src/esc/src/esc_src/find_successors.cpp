#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "try_routing.hpp"

void Escaper::find_successors(const std::vector<ContourItem>& contour,
                              std::vector<std::uint16_t>& successors)
{
  Timer timer("find_successors");

  successors.clear();
  const std::uint32_t reserve_count
      = static_cast<std::uint32_t>(split_column) + 1U;
  successors.reserve(reserve_count);

  std::uint32_t lower = static_cast<std::uint32_t>(split_column);
  while (contour[lower].point_index
             != resource_table[lower][contour[lower].interval_index].capacity
         && lower != 0U) {
    --lower;
  }

  std::uint32_t upper = reserve_count;
  while (contour[upper].point_index
             != resource_table[upper][contour[upper].interval_index].capacity
         && upper != contour.size() - 1U) {
    ++upper;
  }

  std::vector<std::uint32_t> encountered;
  encountered.reserve(reserve_count);
  for (std::uint32_t column = lower; column <= upper; ++column) {
    const Interval& interval
        = resource_table[column][contour[column].interval_index];
    if (interval.second == nullptr) {
      continue;
    }

    const std::uint32_t id = interval.second->id;
    if (std::find(encountered.begin(), encountered.end(), id)
        != encountered.end()) {
      successors.emplace_back(static_cast<std::uint16_t>(id));
    } else {
      encountered.push_back(id);
    }
  }
  successors.shrink_to_fit();
}
