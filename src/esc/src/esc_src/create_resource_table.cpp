#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "try_routing.hpp"

void Escaper::create_resource_table(std::vector<Bump>& die_bumps,
                                    std::vector<Bump>& substrate_bumps)
{
  Timer timer("create_resource_table");

  std::vector<std::vector<std::uint32_t>> die_groups;
  group(die_bumps, die_groups, die_direction, substrate_direction, true);

  std::vector<std::vector<std::uint32_t>> substrate_groups;
  group(substrate_bumps,
        substrate_groups,
        substrate_direction,
        die_direction,
        false);

  split_column = static_cast<std::int32_t>(die_groups.size() - 1U);

  double minimum_y = 1e100;
  double maximum_y = -1e100;
  const auto extend_bounds
      = [&minimum_y, &maximum_y](const std::vector<Bump>& bumps) {
          for (const Bump& bump : bumps) {
            const double value = bump.y;

            double selected_minimum = value;
            if (minimum_y <= value) {
              selected_minimum = minimum_y;
            }
            minimum_y = selected_minimum;

            double selected_maximum = value;
            if (value <= maximum_y) {
              selected_maximum = maximum_y;
            }
            maximum_y = selected_maximum;
          }
        };
  extend_bounds(die_bumps);
  extend_bounds(substrate_bumps);

  const double boundary_offset = BUMP_SIZE * 0.5 + MIN_SPACING + NET_WIDTH;
  const double upper_boundary = maximum_y + boundary_offset;
  const double lower_boundary = minimum_y - boundary_offset;

  const std::size_t column_count = die_groups.size() + substrate_groups.size();
  resource_table.resize(column_count);

  // Reserve group.size() per column; each column gets group.size() + 1
  // intervals.
  std::size_t column_index = 0U;
  for (const auto& ids : die_groups) {
    resource_table[column_index].reserve(ids.size());
    ++column_index;
  }
  for (const auto& ids : substrate_groups) {
    resource_table[column_index].reserve(ids.size());
    ++column_index;
  }

  column_index = 0U;
  const auto append_columns = [&](const std::vector<Bump>& bumps,
                                  const auto& groups) {
    for (const auto& ids : groups) {
      std::vector<Interval>& column = resource_table[column_index];

      column.emplace_back(
          bumps[ids.front()], std::string("head"), upper_boundary);
      for (std::size_t index = 1U; index < ids.size(); ++index) {
        column.emplace_back(
            bumps[ids[index - 1U]], bumps[ids[index]], substrate_direction);
      }
      column.emplace_back(
          bumps[ids.back()], std::string("tail"), lower_boundary);
      ++column_index;
    }
  };

  append_columns(die_bumps, die_groups);
  append_columns(substrate_bumps, substrate_groups);
}
