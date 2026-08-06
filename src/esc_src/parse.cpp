#include <algorithm>

#include "try_routing.hpp"

void parse(std::ifstream& input,
           std::vector<Bump>& die_bumps,
           std::vector<Bump>& substrate_bumps,
           std::vector<Net>& nets)
{
  double die_radius = 0.0;
  double substrate_radius = 0.0;
  input >> NET_WIDTH;
  input >> MIN_SPACING;
  input >> die_radius >> substrate_radius;

  if (substrate_radius <= die_radius) {
    BUMP_SIZE = substrate_radius + substrate_radius;
  } else {
    BUMP_SIZE = die_radius + die_radius;
  }

  std::string ignored;
  std::string name;
  double die_x = 0.0;
  double die_y = 0.0;
  double substrate_x = 0.0;
  double substrate_y = 0.0;
  double supplied_length = 0.0;
  std::uint32_t id = 0;

  while (input >> name) {
    if (name == "-1") {
      input >> ignored >> ignored;
      input >> substrate_x >> substrate_y >> supplied_length;
      continue;
    }

    input >> die_x >> die_y >> substrate_x >> substrate_y >> supplied_length;
    die_bumps.emplace_back(id, die_x, die_y);
    substrate_bumps.emplace_back(id, substrate_x, substrate_y);
    nets.emplace_back(id, name);
    ++id;
  }

  std::sort(substrate_bumps.begin(),
            substrate_bumps.end(),
            [](const Bump& lhs, const Bump& rhs) { return lhs.id < rhs.id; });
}
