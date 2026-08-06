#include "try_routing.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

bool same_point(const Point& lhs, const Point& rhs) {
    return lhs.first == rhs.first && lhs.second == rhs.second;
}

double polyline_length(const std::vector<Point>& path) {
    double length = 0.0;
    for (std::size_t index = 1U; index < path.size(); ++index) {
        const double delta_x = std::abs(path[index].first - path[index - 1U].first);
        const double delta_y = std::abs(path[index].second - path[index - 1U].second);
        if (ROUTING_STYLE == RoutingStyle::DEG135) {
            length = length + std::sqrt(2.0) * std::min(delta_x, delta_y) +
                     std::abs(delta_x - delta_y);
        } else {
            length = length +
                     std::sqrt(delta_x * delta_x + delta_y * delta_y);
        }
    }
    return length;
}

double interpolated_y(const std::vector<Point>& path, std::size_t index) {
    const Point& previous = path[index - 1U];
    const Point& current = path[index];
    const Point& next = path[index + 1U];
    return ((next.second - previous.second) *
            (current.first - previous.first)) /
               (next.first - previous.first) +
           previous.second;
}

}  // namespace

std::vector<std::uint32_t> Escaper::output(const std::string& path,
                                           std::vector<Net>& nets) {
    Timer timer("output");

    if (optimal_order.empty()) {
        return optimal_order;
    }

    Solution replay{};
    replay.contour.resize(resource_table.size());

    std::ofstream output_file(path);
    std::vector<std::vector<Point>> routes;
    routes.reserve(optimal_order.size());
    std::vector<std::vector<std::uint32_t>> occupied_intervals;
    occupied_intervals.reserve(optimal_order.size());
    std::vector<std::uint32_t> first_columns;
    first_columns.reserve(optimal_order.size());

    (void)route(replay);
    std::vector<ContourItem> previous_contour = replay.contour;

    for (const std::uint32_t net : optimal_order) {
        replay.order.emplace_back(static_cast<std::uint16_t>(net));
        routes.emplace_back(route(replay));

        occupied_intervals.emplace_back();
        std::vector<std::uint32_t>& intervals = occupied_intervals.back();
        intervals.reserve(replay.contour.size());
        for (std::size_t column = 0U; column < replay.contour.size();
             ++column) {
            const ContourItem& current = replay.contour[column];
            const ContourItem& previous = previous_contour[column];
            if (current.interval_index == previous.interval_index &&
                current.point_index == previous.point_index) {
                intervals.emplace_back(UINT32_MAX);
            } else {
                intervals.emplace_back(current.interval_index);
            }
        }

        first_columns.emplace_back(0U);
        while (intervals[first_columns.back()] == UINT32_MAX) {
            ++first_columns.back();
        }
        previous_contour = replay.contour;
    }

    const double pitch = NET_WIDTH + MIN_SPACING;

    // Separate consecutive routes sharing a point in the same column.
    for (std::size_t column = 0U; column < resource_table.size(); ++column) {
        std::size_t route_index = 0U;
        double ceiling = 1e100;

        while (route_index < routes.size()) {
            while (route_index < routes.size() &&
                   occupied_intervals[route_index][column] == UINT32_MAX) {
                ++route_index;
            }
            if (route_index == routes.size()) {
                break;
            }

            const std::size_t run_begin = route_index;
            Point& first_point =
                routes[run_begin][column - first_columns[run_begin]];
            std::size_t run_end = run_begin + 1U;
            while (run_end < routes.size() &&
                   occupied_intervals[run_end][column] != UINT32_MAX &&
                   same_point(first_point,
                              routes[run_end]
                                    [column - first_columns[run_end]])) {
                ++run_end;
            }

            const std::size_t run_size = run_end - run_begin;
            if (run_size == 1U) {
                if (ceiling < first_point.second) {
                    first_point.second = ceiling;
                }
                ceiling = first_point.second - pitch;
                route_index = run_end;
                continue;
            }

            double top_y = first_point.second;
            const std::uint32_t interval_index =
                occupied_intervals[run_begin][column];
            if (interval_index == 0U) {
                top_y = top_y + static_cast<double>(run_size) * pitch;
            } else if (interval_index !=
                       resource_table[column].size() - 1U) {
                const std::vector<Point>& samples =
                    resource_table[column][interval_index].points;
                if (samples.size() == 1U) {
                    top_y = top_y +
                            static_cast<double>(run_size >> 1U) * pitch;
                } else if (top_y == samples.front().second) {
                    // No pre-offset at the upper endpoint.
                } else if (top_y == samples.back().second) {
                    top_y = top_y + static_cast<double>(run_size) * pitch;
                } else {
                    top_y = top_y +
                            static_cast<double>(run_size >> 1U) * pitch;
                }
            }

            if (ceiling <= top_y) {
                top_y = ceiling;
            }
            ceiling = top_y;
            const double common_x = first_point.first;
            for (std::size_t index = run_begin; index < run_end; ++index) {
                Point& point =
                    routes[index][column - first_columns[index]];
                point.first = common_x;
                point.second = top_y;
                ceiling = top_y - pitch;
                top_y = ceiling;
            }
            route_index = run_end;
        }
    }

    // Ten top-to-bottom relaxation passes.
    for (std::uint32_t pass = 0U; pass < 10U; ++pass) {
        std::vector<double> limits;
        for (const std::vector<Interval>& column : resource_table) {
            limits.emplace_back(column.front().points.front().second);
        }

        std::vector<std::vector<Point>> adjusted_routes;
        for (std::size_t route_index = 0U; route_index < routes.size();
             ++route_index) {
            const std::uint32_t first_column = first_columns[route_index];
            const std::vector<Point>& source = routes[route_index];
            adjusted_routes.emplace_back();
            std::vector<Point>& adjusted = adjusted_routes.back();

            for (std::size_t point_index = 0U;
                 point_index < source.size(); ++point_index) {
                if (point_index == 0U ||
                    point_index + 1U == source.size()) {
                    adjusted.emplace_back(source[point_index]);
                    continue;
                }

                const double line_y = interpolated_y(source, point_index);
                const double current_y = source[point_index].second;
                const double limit =
                    limits[first_column + point_index];
                double selected_y;
                if (line_y <= current_y || limit <= line_y) {
                    if (line_y <= limit || limit <= current_y) {
                        selected_y = current_y;
                    } else {
                        selected_y = limit;
                    }
                } else {
                    selected_y = line_y;
                }
                adjusted.emplace_back(source[point_index].first, selected_y);
            }

            for (std::size_t point_index = 0U;
                 point_index < adjusted.size(); ++point_index) {
                const bool endpoint =
                    point_index == 0U ||
                    point_index + 1U == adjusted.size();
                limits[first_column + point_index] =
                    adjusted[point_index].second -
                    (endpoint
                         ? BUMP_SIZE * 0.5 + NET_WIDTH * 0.5 + MIN_SPACING
                         : pitch);
            }
        }
        routes = adjusted_routes;
    }

    // Ten bottom-to-top relaxation passes.
    for (std::uint32_t pass = 0U; pass < 10U; ++pass) {
        std::vector<double> limits;
        for (const std::vector<Interval>& column : resource_table) {
            limits.emplace_back(column.back().points.back().second);
        }

        std::vector<std::vector<Point>> reverse_adjusted;
        for (std::size_t reverse_index = routes.size(); reverse_index != 0U;) {
            --reverse_index;
            const std::uint32_t first_column = first_columns[reverse_index];
            const std::vector<Point>& source = routes[reverse_index];
            reverse_adjusted.emplace_back();
            std::vector<Point>& adjusted = reverse_adjusted.back();

            for (std::size_t point_index = 0U;
                 point_index < source.size(); ++point_index) {
                if (point_index == 0U ||
                    point_index + 1U == source.size()) {
                    adjusted.emplace_back(source[point_index]);
                    continue;
                }

                const double current_y = source[point_index].second;
                const double limit =
                    limits[first_column + point_index];
                double selected_y;
                if (limit <= current_y) {
                    const double line_y =
                        interpolated_y(source, point_index);
                    if (current_y <= line_y || line_y <= limit) {
                        if (line_y < limit && limit < current_y) {
                            selected_y = limit;
                        } else {
                            selected_y = current_y;
                        }
                    } else {
                        selected_y = line_y;
                    }
                } else {
                    selected_y = limit;
                }
                adjusted.emplace_back(source[point_index].first, selected_y);
            }

            for (std::size_t point_index = 0U;
                 point_index < adjusted.size(); ++point_index) {
                const bool endpoint =
                    point_index == 0U ||
                    point_index + 1U == adjusted.size();
                limits[first_column + point_index] =
                    adjusted[point_index].second +
                    (endpoint
                         ? BUMP_SIZE * 0.5 + NET_WIDTH * 0.5 + MIN_SPACING
                         : pitch);
            }
        }
        std::reverse(reverse_adjusted.begin(), reverse_adjusted.end());
        routes = reverse_adjusted;
    }

    double total_length = 0.0;
    double critical_length = 0.0;
    for (std::size_t route_index = 0U; route_index < routes.size();
         ++route_index) {
        const double length = polyline_length(routes[route_index]);
        total_length = total_length + length;
        if (length <= critical_length) {
        } else {
            critical_length = length;
        }

        output_file << nets[optimal_order[route_index]].name << " " << length;
        for (const Point& point : routes[route_index]) {
            output_file << " " << point.first << " " << point.second;
        }
        output_file << std::endl;
    }

    std::cout << "\nCritical length   " << critical_length << std::endl;
    std::cout << "Total wirelength " << total_length << std::endl;
    output_file.close();
    return optimal_order;
}
