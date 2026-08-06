#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "try_routing.hpp"

namespace {

double point_distance(const Point& lhs, const Point& rhs)
{
  const double delta_x = lhs.first - rhs.first;
  const double delta_y = lhs.second - rhs.second;
  return std::sqrt(delta_x * delta_x + delta_y * delta_y);
}

Point bump_point(const Bump* bump)
{
  return Point(bump->x, bump->y);
}

}  // namespace

std::vector<Point> Escaper::route(Solution& solution)
{
  Timer timer("route");

  if (solution.order.empty()) {
    std::fill(
        solution.contour.begin(), solution.contour.end(), ContourItem{0U, 0U});
    return {};
  }

  const std::uint32_t net = solution.order.back();

  std::int32_t left = split_column;
  while (left >= 0) {
    const ContourItem& item = solution.contour[static_cast<std::size_t>(left)];
    const Interval& interval
        = resource_table[static_cast<std::size_t>(left)][item.interval_index];
    if (interval.second != nullptr && interval.second->id == net) {
      break;
    }
    --left;
  }

  std::uint32_t right = static_cast<std::uint32_t>(split_column) + 1U;
  while (right < solution.contour.size()) {
    const ContourItem& item = solution.contour[right];
    const Interval& interval = resource_table[right][item.interval_index];
    if (interval.second != nullptr && interval.second->id == net) {
      break;
    }
    ++right;
  }

  const std::uint32_t gap = right - static_cast<std::uint32_t>(left);

  if (gap == 1U) {
    ContourItem& left_item = solution.contour[static_cast<std::size_t>(left)];
    left_item.point_index = 0U;
    left_item.interval_index
        = static_cast<std::uint16_t>(left_item.interval_index + 1U);

    ContourItem& right_item = solution.contour[right];
    right_item.interval_index
        = static_cast<std::uint16_t>(right_item.interval_index + 1U);
    right_item.point_index = 0U;

    std::vector<Point> result(2U);
    result[0] = bump_point(
        resource_table[static_cast<std::size_t>(left)][left_item.interval_index]
            .first);
    result[1]
        = bump_point(resource_table[right][right_item.interval_index].first);
    return result;
  }

  constexpr std::size_t candidate_limit = 16U;
  const std::uint32_t intermediate_count = gap - 1U;
  std::vector<std::vector<double>> distances(
      intermediate_count, std::vector<double>(candidate_limit, 0.0));
  std::vector<std::vector<std::uint32_t>> predecessors(
      intermediate_count, std::vector<std::uint32_t>(candidate_limit, 0U));

  const std::uint32_t left_column = static_cast<std::uint32_t>(left);
  const ContourItem& old_left_item = solution.contour[left_column];
  const Interval& old_left_interval
      = resource_table[left_column][old_left_item.interval_index];
  const Point left_endpoint = bump_point(old_left_interval.second);

  const std::uint32_t first_column = left_column + 1U;
  const ContourItem& first_item = solution.contour[first_column];
  const Interval& first_interval
      = resource_table[first_column][first_item.interval_index];
  for (std::size_t point_index = 0U; point_index < first_interval.points.size();
       ++point_index) {
    distances[0][point_index]
        = point_distance(first_interval.points[point_index], left_endpoint);
  }

  ContourItem& mutable_left_item = solution.contour[left_column];
  mutable_left_item.interval_index
      = static_cast<std::uint16_t>(mutable_left_item.interval_index + 1U);
  mutable_left_item.point_index = 0U;
  solution.contour[first_column].point_index = static_cast<std::uint16_t>(
      solution.contour[first_column].point_index + 1U);

  for (std::uint32_t column = left_column + 2U; column < right; ++column) {
    const std::uint32_t stage = column - first_column;
    const ContourItem& previous_item = solution.contour[column - 1U];
    const Interval& previous_interval
        = resource_table[column - 1U][previous_item.interval_index];
    const ContourItem& current_item = solution.contour[column];
    const Interval& current_interval
        = resource_table[column][current_item.interval_index];

    for (std::size_t current_index = 0U;
         current_index < current_interval.points.size();
         ++current_index) {
      double best = distances[stage - 1U][0]
                    + point_distance(current_interval.points[current_index],
                                     previous_interval.points[0]);
      distances[stage][current_index] = best;

      for (std::size_t previous_index = 1U;
           previous_index < previous_interval.points.size();
           ++previous_index) {
        const double candidate
            = distances[stage - 1U][previous_index]
              + point_distance(current_interval.points[current_index],
                               previous_interval.points[previous_index]);
        if (candidate < distances[stage][current_index]) {
          distances[stage][current_index] = candidate;
          predecessors[stage][current_index]
              = static_cast<std::uint32_t>(previous_index);
        }
      }
    }

    solution.contour[column].point_index
        = static_cast<std::uint16_t>(solution.contour[column].point_index + 1U);
  }

  const ContourItem& old_right_item = solution.contour[right];
  const Interval& old_right_interval
      = resource_table[right][old_right_item.interval_index];
  const Point right_endpoint = bump_point(old_right_interval.second);

  const std::uint32_t last_column = right - 1U;
  const std::uint32_t last_stage = intermediate_count - 1U;
  const ContourItem& last_item = solution.contour[last_column];
  const Interval& last_interval
      = resource_table[last_column][last_item.interval_index];

  std::uint32_t selected = 0U;
  double route_length
      = distances[last_stage][0]
        + point_distance(right_endpoint, last_interval.points[0]);
  for (std::size_t point_index = 1U; point_index < last_interval.points.size();
       ++point_index) {
    const double candidate
        = distances[last_stage][point_index]
          + point_distance(right_endpoint, last_interval.points[point_index]);
    if (candidate < route_length) {
      selected = static_cast<std::uint32_t>(point_index);
      route_length = candidate;
    }
  }

  ContourItem& mutable_right_item = solution.contour[right];
  mutable_right_item.interval_index
      = static_cast<std::uint16_t>(mutable_right_item.interval_index + 1U);
  mutable_right_item.point_index = 0U;

  if (solution.critical_length < route_length) {
    solution.critical_length = route_length;
  }
  solution.total_length = solution.total_length + route_length;

  std::vector<Point> result(gap + 1U);
  result[gap] = right_endpoint;

  for (std::int32_t column = static_cast<std::int32_t>(right) - 1;
       column > left;
       --column) {
    const std::uint32_t unsigned_column = static_cast<std::uint32_t>(column);
    const ContourItem& item = solution.contour[unsigned_column];
    result[unsigned_column - left_column]
        = resource_table[unsigned_column][item.interval_index].points[selected];
    const std::uint32_t stage = unsigned_column - first_column;
    selected = predecessors[stage][selected];
  }

  result[0] = bump_point(
      resource_table[left_column][solution.contour[left_column].interval_index]
          .first);
  return result;
}
