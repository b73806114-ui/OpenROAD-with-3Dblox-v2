#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "try_routing.hpp"

void Escaper::group(std::vector<Bump>& bumps,
                    std::vector<std::vector<std::uint32_t>>& groups,
                    const std::string& direction,
                    const std::string& other_direction,
                    bool reverse)
{
  Timer timer("group");
  (void) other_direction;

  if (direction == "left" || direction == "right") {
    const bool ascending = (direction == "left") != reverse;
    std::sort(bumps.begin(),
              bumps.end(),
              [ascending](const Bump& lhs, const Bump& rhs) {
                return ascending ? lhs.x < rhs.x : lhs.x > rhs.x;
              });
  }

  std::vector<std::uint32_t> current_group;
  current_group.push_back(0U);

  const auto emit_group = [&]() {
    std::sort(current_group.begin(),
              current_group.end(),
              [&bumps](std::uint32_t lhs, std::uint32_t rhs) {
                return bumps[lhs].y > bumps[rhs].y;
              });
    for (std::uint32_t& index : current_group) {
      index = bumps[index].id;
    }
    groups.push_back(current_group);
    current_group.clear();
  };

  constexpr double slope_limit = 3.0625;
  for (std::uint32_t index = 1U; index < bumps.size(); ++index) {
    bool begins_new_group = false;
    for (const std::uint32_t member_index : current_group) {
      const double delta_x = bumps[index].x - bumps[member_index].x;
      if (delta_x == 0.0) {
        continue;
      }
      const double slope = (bumps[index].y - bumps[member_index].y) / delta_x;
      if (slope >= slope_limit || slope <= -slope_limit) {
        continue;
      }
      begins_new_group = true;
      break;
    }

    if (begins_new_group) {
      emit_group();
    }
    current_group.push_back(index);
  }

  if (!current_group.empty()) {
    emit_group();
  }

  std::sort(bumps.begin(), bumps.end(), [](const Bump& lhs, const Bump& rhs) {
    return lhs.id < rhs.id;
  });
}
