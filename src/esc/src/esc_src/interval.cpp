#include <cstddef>

#include "try_routing.hpp"

namespace {

std::size_t sample_count(double gap, double pitch)
{
  if (pitch * 15.0 <= gap) {
    return 16;
  }
  if (pitch * 7.0 <= gap) {
    return 8;
  }
  return 4;
}

void append_samples(std::vector<Point>& points,
                    double first_x,
                    double first_y,
                    double last_x,
                    double last_y,
                    std::size_t count)
{
  points.reserve(count);
  const double divisor = static_cast<double>(count - 1U);
  const double x_step = (first_x - last_x) / divisor;
  const double y_step = (first_y - last_y) / divisor;
  points.emplace_back(first_x, first_y);
  for (std::size_t index = 0; index < count - 2U; ++index) {
    first_x = first_x - x_step;
    first_y = first_y - y_step;
    points.emplace_back(first_x, first_y);
  }
  points.emplace_back(last_x, last_y);
}

}  // namespace

Interval::Interval(const Bump& first_in,
                   const Bump& second_in,
                   const std::string& direction)
    : first(&first_in), second(&second_in), capacity(0), _reserved(0), points()
{
  double span;
  if (direction == "left" || direction == "right") {
    span = first_in.y - second_in.y;
  } else {
    span = first_in.x - second_in.x;
  }

  const double pitch = NET_WIDTH + MIN_SPACING;
  capacity
      = static_cast<std::uint32_t>(((span - BUMP_SIZE) - MIN_SPACING) / pitch);

  const double first_x = first_in.x;
  const double last_x = second_in.x;
  const double first_y
      = ((first_in.y - BUMP_SIZE * 0.5) - MIN_SPACING) - NET_WIDTH * 0.5;
  const double last_y
      = second_in.y + BUMP_SIZE * 0.5 + MIN_SPACING + NET_WIDTH * 0.5;
  const double gap = first_y - last_y;

  if (gap < pitch * 3.0) {
    points.emplace_back((first_x + last_x) * 0.5,
                        (first_in.y + second_in.y) * 0.5);
    return;
  }
  append_samples(
      points, first_x, first_y, last_x, last_y, sample_count(gap, pitch));
}

Interval::Interval(const Bump& bump, std::string endpoint, double boundary)
    : first(endpoint == "head" ? nullptr : &bump),
      second(endpoint == "tail" ? nullptr : &bump),
      capacity(UINT32_MAX),
      _reserved(0),
      points()
{
  const double pitch = NET_WIDTH + MIN_SPACING;
  const bool is_head = endpoint == "head";

  const double first_x = bump.x;
  const double last_x = bump.x;
  const double first_y
      = is_head ? boundary
                : ((bump.y - BUMP_SIZE) - MIN_SPACING) - NET_WIDTH * 0.5;
  const double last_y
      = is_head ? bump.y + BUMP_SIZE + MIN_SPACING + NET_WIDTH * 0.5 : boundary;
  const double gap = first_y - last_y;

  if (pitch * 3.0 <= gap) {
    append_samples(
        points, first_x, first_y, last_x, last_y, sample_count(gap, pitch));
  } else {
    points.reserve(1);
    points.emplace_back(bump.x, is_head ? last_y : first_y);
  }
}
