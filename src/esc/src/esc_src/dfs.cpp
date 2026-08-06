#include "try_routing.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

void Escaper::DFS(std::vector<Bump>& die_bumps,
                  const std::string& die_direction_in,
                  std::vector<Bump>& substrate_bumps,
                  const std::string& substrate_direction_in,
                  const std::vector<Net>& nets) {
    Timer timer("DFS");

    die_direction = die_direction_in;
    substrate_direction = substrate_direction_in;
    create_resource_table(die_bumps, substrate_bumps);

    net_count = static_cast<std::uint16_t>(nets.size());
    best_solution = nullptr;

    using SearchNode =
        std::pair<Solution, std::vector<std::uint16_t>>;
    std::deque<SearchNode> stack;

    std::vector<std::uint16_t> remaining;
    for (std::uint32_t net = 0U; net < net_count; ++net) {
        remaining.emplace_back(static_cast<std::uint16_t>(net));
    }

    Solution root{};
    root.last_net = net_count;
    root.contour.resize(resource_table.size());
    stack.emplace_back(std::move(root), remaining);

    bool first_node = true;
    std::uint32_t node_count = 0U;
    double best_critical = 1e100;
    Solution local_best{};

    while (!stack.empty()) {
        Solution current = stack.back().first;
        std::vector<std::uint16_t> current_remaining = stack.back().second;
        stack.pop_back();
        ++node_count;

        std::vector<std::uint16_t> feasible_successors;
        find_successors(current.contour, feasible_successors);
        if (!first_node &&
            std::find(feasible_successors.begin(), feasible_successors.end(),
                      current.last_net) == feasible_successors.end()) {
            first_node = false;
            continue;
        }

        (void)route(current);
        if (current.critical_length >= best_critical) {
            first_node = false;
            continue;
        }

        if (current.order.size() == net_count) {
            if (current.critical_length < best_critical) {
                local_best = current;
                best_critical = current.critical_length;
            }
        } else if (!current_remaining.empty()) {
            for (std::size_t index = 0U; index < current_remaining.size();
                 ++index) {
                Solution child = current;
                std::vector<std::uint16_t> child_remaining =
                    current_remaining;

                const std::uint16_t next_net = current_remaining[index];
                child.last_net = next_net;
                child.order.push_back(next_net);
                child.order.shrink_to_fit();
                child_remaining.erase(child_remaining.begin() +
                                      static_cast<std::ptrdiff_t>(index));

                stack.emplace_back(std::move(child),
                                   std::move(child_remaining));
            }
        }

        first_node = false;
    }

    std::cout << "\nNode count: "
              << static_cast<unsigned long>(node_count) << std::endl;
    std::cout << "Best critical wirelength: " << best_critical << std::endl;
}
