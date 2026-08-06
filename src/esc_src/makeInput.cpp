#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "try_routing.hpp"

void makeInput(
    const std::vector<NamedPoint>& die_pins,
    const std::vector<NamedPoint>& substrate_pins,
    const std::vector<std::pair<std::string, std::string>>& net_pairs,
    double net_width,
    double min_spacing,
    double die_radius,
    double substrate_radius,
    std::vector<Bump>& die_bumps,
    std::vector<Bump>& substrate_bumps,
    std::vector<Net>& nets)
{
  NET_WIDTH = net_width;
  MIN_SPACING = min_spacing;

  if (substrate_radius <= die_radius) {
    BUMP_SIZE = substrate_radius + substrate_radius;
  } else {
    BUMP_SIZE = die_radius + die_radius;
  }

  // Build lookup tables: name → (x, y)
  std::unordered_map<std::string, std::pair<double, double>> die_map;
  for (const auto& pin : die_pins) {
    die_map[pin.name] = {pin.x, pin.y};
  }
  std::unordered_map<std::string, std::pair<double, double>> substrate_map;
  for (const auto& pin : substrate_pins) {
    substrate_map[pin.name] = {pin.x, pin.y};
  }

  // Each net pair creates one entry in all three vectors.
  // die_bumps[i], substrate_bumps[i], nets[i] all represent the same
  // connection — the algorithm relies on this 1:1:1 index alignment.
  std::uint32_t id = 0;
  for (const auto& pair : net_pairs) {
    const std::string& die_name = pair.first;
    const std::string& sub_name = pair.second;

    auto die_it = die_map.find(die_name);
    if (die_it == die_map.end()) {
      throw std::invalid_argument("makeInput: die pin '" + die_name
                                  + "' not found in die_pins");
    }
    auto sub_it = substrate_map.find(sub_name);
    if (sub_it == substrate_map.end()) {
      throw std::invalid_argument("makeInput: substrate pin '" + sub_name
                                  + "' not found in substrate_pins");
    }

    die_bumps.emplace_back(id, die_it->second.first, die_it->second.second);
    substrate_bumps.emplace_back(
        id, sub_it->second.first, sub_it->second.second);
    nets.emplace_back(id, die_name);
    ++id;
  }

  // Consistency sort: substrate bumps are sorted by id, matching parse().
  std::sort(substrate_bumps.begin(),
            substrate_bumps.end(),
            [](const Bump& lhs, const Bump& rhs) { return lhs.id < rhs.id; });
}
