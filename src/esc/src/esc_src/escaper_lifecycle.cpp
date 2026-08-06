#include "try_routing.hpp"

Escaper::Escaper()
    : resource_table(),
      die_direction(),
      substrate_direction(),
      solution_table(),
      optimal_order(),
      pool(8),
      assess_mutex(),
      dominated(false),
      best_solution(nullptr),
      best_critical_length(0.0),
      baseline_critical_length(0.0),
      baseline_found(false),
      crossing_groups(),
      routed_lengths() {}

Escaper::~Escaper() {
    Timer timer("~Escaper");
}

void Escaper::trim(std::vector<Solution>&,
                   std::vector<std::vector<std::uint16_t>>&) {}
