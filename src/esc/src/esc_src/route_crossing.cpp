#include "try_routing.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

double point_distance(const Point& lhs, const Point& rhs) {
    const double dx = std::abs(lhs.first - rhs.first);
    const double dy = std::abs(lhs.second - rhs.second);
    if (ROUTING_STYLE == RoutingStyle::DEG135) {
        return std::sqrt(2.0) * std::min(dx, dy) + std::abs(dx - dy);
    }
    return std::sqrt(dx * dx + dy * dy);
}

Point bump_point(const Bump* bump) {
    return Point(bump->x, bump->y);
}

// Search downward from contour interval for net's bump.
// Returns true if found.  Sets found_interval and at_contour.
bool find_bump_in_col(const Escaper& esc, std::uint32_t col,
                      const ContourItem& contour_item,
                      std::uint32_t net_id,
                      std::uint32_t& found_intv, bool& at_contour) {
    const auto& col_data = esc.resource_table[col];
    const std::uint32_t start = contour_item.interval_index;
    for (std::uint32_t iv = start; iv < col_data.size(); ++iv) {
        const Interval& intv = col_data[iv];
        if (intv.second != nullptr && intv.second->id == net_id) {
            found_intv = iv;
            at_contour = (iv == start);
            return true;
        }
    }
    return false;
}

}  // namespace

bool Escaper::is_net_crossing(std::uint32_t net_id) const {
    for (const auto& group : crossing_groups) {
        if (std::find(group.begin(), group.end(), net_id) != group.end()) {
            return true;
        }
    }
    return false;
}

void Escaper::find_successors_crossing(
    const std::vector<ContourItem>& contour,
    std::vector<std::uint16_t>& successors) {
    Timer timer("find_successors_crossing");

    // Start with standard monotonic successors
    find_successors(contour, successors);

    if (crossing_groups.empty()) return;

    const std::uint32_t split_u = static_cast<std::uint32_t>(split_column);
    const std::uint32_t reserve = split_u + 1U;

    // Find exhausted column bounds (same logic as find_successors)
    std::uint32_t lower = split_u;
    while (contour[lower].point_index !=
               resource_table[lower][contour[lower].interval_index].capacity &&
           lower != 0U) {
        --lower;
    }
    std::uint32_t upper = reserve;
    while (contour[upper].point_index !=
               resource_table[upper][contour[upper].interval_index].capacity &&
           upper != contour.size() - 1U) {
        ++upper;
    }

    // Collect contour bump IDs
    std::vector<std::uint32_t> die_contour_ids, sub_contour_ids;
    for (std::uint32_t c = lower; c <= upper; ++c) {
        const Interval& intv = resource_table[c][contour[c].interval_index];
        if (intv.second != nullptr) {
            if (c <= split_u) die_contour_ids.push_back(intv.second->id);
            else sub_contour_ids.push_back(intv.second->id);
        }
    }

    // Crossing net successors: nets in crossing groups where at least
    // one bump is a contour bump
    for (const auto& group : crossing_groups) {
        for (std::uint32_t net_id : group) {
            // Skip if already in standard successors
            if (std::find(successors.begin(), successors.end(),
                          static_cast<std::uint16_t>(net_id)) !=
                successors.end()) {
                continue;
            }

            bool die_contour = std::find(die_contour_ids.begin(),
                                         die_contour_ids.end(),
                                         net_id) != die_contour_ids.end();
            bool sub_contour = std::find(sub_contour_ids.begin(),
                                         sub_contour_ids.end(),
                                         net_id) != sub_contour_ids.end();

            if (die_contour || sub_contour) {
                successors.emplace_back(static_cast<std::uint16_t>(net_id));
            }
        }
    }
}

std::vector<Point> Escaper::route_crossing(Solution& solution) {
    Timer timer("route_crossing");

    if (solution.order.empty()) {
        std::fill(solution.contour.begin(), solution.contour.end(),
                  ContourItem{0U, 0U});
        return {};
    }

    const std::uint32_t net = solution.order.back();

    // ── Find bump columns: search from contour downward ──
    std::int32_t left = split_column;
    std::uint32_t left_intv = 0U;
    bool left_at_contour = false;
    bool left_found = false;
    while (left >= 0) {
        const std::uint32_t uc = static_cast<std::uint32_t>(left);
        if (find_bump_in_col(*this, uc, solution.contour[uc],
                             net, left_intv, left_at_contour)) {
            left_found = true;
            break;
        }
        --left;
    }

    std::uint32_t right = static_cast<std::uint32_t>(split_column) + 1U;
    std::uint32_t right_intv = 0U;
    bool right_at_contour = false;
    bool right_found = false;
    while (right < solution.contour.size()) {
        if (find_bump_in_col(*this, right, solution.contour[right],
                             net, right_intv, right_at_contour)) {
            right_found = true;
            break;
        }
        ++right;
    }

    if (!left_found || !right_found) {
        // Net not found — shouldn't happen for valid nets
        return {};
    }

    const std::uint32_t left_col = static_cast<std::uint32_t>(left);
    const std::uint32_t gap = right - left_col;

    // ── Both at contour → monotonic, delegate to standard route ──
    if (left_at_contour && right_at_contour) {
        // Use standard route logic inline for simplicity
    }

    // ── gap == 1: adjacent columns ──
    if (gap == 1U) {
        if (left_at_contour) {
            ContourItem& item = solution.contour[left_col];
            item.interval_index =
                static_cast<std::uint16_t>(left_intv + 1U);
            item.point_index = 0U;
        }
        if (right_at_contour) {
            ContourItem& item = solution.contour[right];
            item.interval_index =
                static_cast<std::uint16_t>(right_intv + 1U);
            item.point_index = 0U;
        }
        std::vector<Point> result(2U);
        result[0] = bump_point(
            resource_table[left_col][left_intv].second);
        result[1] = bump_point(
            resource_table[right][right_intv].second);
        // Compute length
        double len = point_distance(result[0], result[1]);
        if (solution.critical_length < len)
            solution.critical_length = len;
        solution.total_length += len;
        return result;
    }

    // ── gap >= 2: DP through intermediate columns ──
    const std::uint32_t intermediate_count = gap - 1U;
    constexpr std::size_t max_pts = 16U;
    std::vector<std::vector<double>> dist(
        intermediate_count, std::vector<double>(max_pts, 0.0));
    std::vector<std::vector<std::uint32_t>> pred(
        intermediate_count, std::vector<std::uint32_t>(max_pts, 0U));

    const Point left_pt =
        bump_point(resource_table[left_col][left_intv].second);

    const std::uint32_t first_col = left_col + 1U;
    const Interval& fi =
        resource_table[first_col][solution.contour[first_col].interval_index];
    for (std::size_t pi = 0U; pi < fi.points.size(); ++pi) {
        dist[0][pi] = point_distance(fi.points[pi], left_pt);
    }

    // Advance contour at left column only if bump was at contour
    if (left_at_contour) {
        ContourItem& m = solution.contour[left_col];
        m.interval_index = static_cast<std::uint16_t>(m.interval_index + 1U);
        m.point_index = 0U;
    }
    solution.contour[first_col].point_index =
        static_cast<std::uint16_t>(
            solution.contour[first_col].point_index + 1U);

    for (std::uint32_t col = left_col + 2U; col < right; ++col) {
        const std::uint32_t stage = col - first_col;
        const Interval& prev_iv =
            resource_table[col - 1U]
                          [solution.contour[col - 1U].interval_index];
        const Interval& cur_iv =
            resource_table[col][solution.contour[col].interval_index];
        for (std::size_t ci = 0U; ci < cur_iv.points.size(); ++ci) {
            double best = dist[stage - 1U][0] +
                          point_distance(cur_iv.points[ci], prev_iv.points[0]);
            dist[stage][ci] = best;
            for (std::size_t pi = 1U; pi < prev_iv.points.size(); ++pi) {
                double cand = dist[stage - 1U][pi] +
                              point_distance(cur_iv.points[ci],
                                             prev_iv.points[pi]);
                if (cand < dist[stage][ci]) {
                    dist[stage][ci] = cand;
                    pred[stage][ci] = static_cast<std::uint32_t>(pi);
                }
            }
        }
        solution.contour[col].point_index =
            static_cast<std::uint16_t>(
                solution.contour[col].point_index + 1U);
    }

    const Point right_pt =
        bump_point(resource_table[right][right_intv].second);
    const std::uint32_t last_col = right - 1U;
    const std::uint32_t last_stage = intermediate_count - 1U;
    const Interval& li =
        resource_table[last_col][solution.contour[last_col].interval_index];

    std::uint32_t selected = 0U;
    double route_len = dist[last_stage][0] +
                       point_distance(right_pt, li.points[0]);
    for (std::size_t pi = 1U; pi < li.points.size(); ++pi) {
        double cand = dist[last_stage][pi] +
                      point_distance(right_pt, li.points[pi]);
        if (cand < route_len) {
            selected = static_cast<std::uint32_t>(pi);
            route_len = cand;
        }
    }

    if (right_at_contour) {
        ContourItem& m = solution.contour[right];
        m.interval_index = static_cast<std::uint16_t>(m.interval_index + 1U);
        m.point_index = 0U;
    }

    if (solution.critical_length < route_len)
        solution.critical_length = route_len;
    solution.total_length += route_len;

    std::vector<Point> result(gap + 1U);
    result[gap] = right_pt;
    for (std::int32_t col = static_cast<std::int32_t>(right) - 1;
         col > left; --col) {
        const std::uint32_t uc = static_cast<std::uint32_t>(col);
        result[uc - left_col] =
            resource_table[uc][solution.contour[uc].interval_index]
                .points[selected];
        if (col > static_cast<std::int32_t>(first_col))
            selected = pred[uc - first_col][selected];
    }
    result[0] = left_pt;
    return result;
}
