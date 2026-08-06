#include "try_routing.hpp"

#include <cstdint>

void Escaper::backtrack() {
    Timer timer("backtrack");

    optimal_order.clear();

    std::vector<Solution>& complete_solutions =
        solution_table.begin()->second;
    best_solution = complete_solutions.data();
    if (best_solution == nullptr) {
        return;
    }

    double selected_critical = best_solution->critical_length;
    for (Solution& candidate : complete_solutions) {
        if (candidate.critical_length < selected_critical ||
            (candidate.critical_length == selected_critical &&
             candidate.total_length < best_solution->total_length)) {
            best_solution = &candidate;
            selected_critical = candidate.critical_length;
        }
    }
    best_critical_length = selected_critical;

    optimal_order.reserve(net_count);
    for (const std::uint16_t net : best_solution->order) {
        optimal_order.emplace_back(static_cast<std::uint32_t>(net));
    }

    routed_lengths.resize(net_count);

    // Initialise routing state with an empty solution before the main pass.
    Solution initial{};
    initial.contour.resize(resource_table.size());
    (void)route(initial);
}
