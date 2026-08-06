#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio/thread_pool.hpp>
#include <boost/dynamic_bitset.hpp>

extern double MIN_SPACING;
extern double NET_WIDTH;
struct NamedPoint
{
    std::string name;
    double x;
    double y;
};

// Construct the algorithm input structures from in-memory data.

extern double BUMP_SIZE;

enum class RoutingStyle { ANY_OBTUSE, DEG135 };
extern RoutingStyle ROUTING_STYLE;

using Point = std::pair<double, double>;

struct Bump {
    std::uint32_t id;
    std::uint32_t _reserved;
    double x;
    double y;

    Bump(std::uint32_t id_in, double x_in, double y_in)
        : id(id_in), _reserved(0), x(x_in), y(y_in) {}
};

struct Net {
    std::uint32_t id;
    std::uint32_t _reserved;
    std::string name;

    Net(std::uint32_t id_in, const std::string& name_in)
        : id(id_in), _reserved(0), name(name_in) {}
};

void makeInput(const std::vector<NamedPoint>& die_pins,
               const std::vector<NamedPoint>& substrate_pins,
               const std::vector<std::pair<std::string, std::string>>& net_pairs,
               double net_width,
               double min_spacing,
               double die_radius,
               double substrate_radius,
               std::vector<Bump>& die_bumps,
               std::vector<Bump>& substrate_bumps,
               std::vector<Net>& nets);

struct Interval {
    const Bump* first;
    const Bump* second;
    std::uint32_t capacity;
    std::uint32_t _reserved;
    std::vector<Point> points;

    Interval(const Bump& first_in, const Bump& second_in,
             const std::string& direction);

    Interval(const Bump& bump, std::string endpoint, double boundary);
};

struct ContourItem {
    std::uint16_t interval_index;
    std::uint16_t point_index;
};

struct Solution {
    double critical_length;
    double total_length;
    std::uint16_t last_net;
    std::uint8_t _reserved[6];
    std::vector<ContourItem> contour;
    std::vector<std::uint16_t> order;
};

struct BitsetHash {
private:
    static std::size_t mix(std::size_t value) noexcept {
        value ^= value >> 32U;
        value *= static_cast<std::size_t>(UINT64_C(0x0e9846af9b1a615d));
        value ^= value >> 32U;
        value *= static_cast<std::size_t>(UINT64_C(0x0e9846af9b1a615d));
        value ^= value >> 28U;
        return value;
    }

    struct BlockOutputIterator {
        using iterator_category = std::output_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = void;
        using pointer = void;
        using reference = void;

        std::size_t* seed;

        BlockOutputIterator& operator*() noexcept { return *this; }
        BlockOutputIterator& operator++() noexcept { return *this; }
        BlockOutputIterator operator++(int) noexcept { return *this; }
        BlockOutputIterator& operator=(unsigned long block) noexcept {
            *seed = mix(*seed + static_cast<std::size_t>(block) +
                        static_cast<std::size_t>(0x9e3779b9U));
            return *this;
        }
    };

public:
    std::size_t operator()(const boost::dynamic_bitset<>& bits) const noexcept {
        static_assert(sizeof(std::size_t) == sizeof(std::uint64_t));
        std::size_t seed = 0U;
        boost::to_block_range(bits, BlockOutputIterator{&seed});
        return mix(seed + bits.size() +
                   static_cast<std::size_t>(0x9e3779b9U));
    }
};

using SolutionTable = std::unordered_map<
    boost::dynamic_bitset<>, std::vector<Solution>, BitsetHash>;
using SuccessorTable = std::unordered_map<
    boost::dynamic_bitset<>, std::vector<std::vector<std::uint16_t>>,
    BitsetHash>;

class Timer {
public:
    using DurationRecord = std::pair<int, long>;

    explicit Timer(const std::string& function_name);
    ~Timer();

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    static std::unordered_map<std::string, DurationRecord> _func_times;

private:
    std::string function_name_;
    std::chrono::system_clock::time_point start_;
};

class Escaper {
public:
    Escaper();
    ~Escaper();

    bool compare(const Solution& candidate, Solution& incumbent,
                 bool compare_contours);
    bool assess(const Solution& candidate, unsigned int count,
                std::vector<Solution>& incumbents,
                bool compare_contours);
    std::vector<Point> route(Solution& solution);
    void trim(std::vector<Solution>& solutions,
              std::vector<std::vector<std::uint16_t>>& successors);
    void group(std::vector<Bump>& bumps,
               std::vector<std::vector<std::uint32_t>>& groups,
               const std::string& direction,
               const std::string& other_direction,
               bool reverse);
    void backtrack();
    void find_successors(const std::vector<ContourItem>& contour,
                         std::vector<std::uint16_t>& successors);
    void create_subsolutions(SolutionTable& solutions,
                             SuccessorTable& successors);
    void create_resource_table(std::vector<Bump>& die_bumps,
                               std::vector<Bump>& substrate_bumps);
    void detect_crossing_nets(const std::vector<Bump>& die_bumps,
                              const std::vector<Bump>& substrate_bumps);
    void baseline_dp();
    void DFS(std::vector<Bump>& die_bumps,
             const std::string& die_direction_in,
             std::vector<Bump>& substrate_bumps,
             const std::string& substrate_direction_in,
             const std::vector<Net>& nets);
    void operator()(std::vector<Bump>& die_bumps,
                    const std::string& die_direction_in,
                    std::vector<Bump>& substrate_bumps,
                    const std::string& substrate_direction_in,
                    const std::vector<Net>& nets);
    std::vector<std::uint32_t> output(const std::string& path,
                                      std::vector<Net>& nets);

    std::uint16_t net_count;
    std::uint8_t _reserved_002[6];
    std::vector<std::vector<Interval>> resource_table;
    std::string die_direction;
    std::string substrate_direction;
    std::int32_t split_column;
    std::uint32_t _reserved_064;
    SolutionTable solution_table;
    std::vector<std::uint32_t> optimal_order;
    boost::asio::thread_pool pool;
    std::uint8_t _reserved_pool[8];
    std::mutex assess_mutex;
    bool dominated;
    std::uint8_t _reserved_101[7];
    Solution* best_solution;
    double best_critical_length;
    double baseline_critical_length;
    bool baseline_found;
    std::vector<std::vector<std::uint32_t>> crossing_groups;
    std::vector<double> routed_lengths;
};

void parse(std::ifstream& input,
           std::vector<Bump>& die_bumps,
           std::vector<Bump>& substrate_bumps,
           std::vector<Net>& nets);

// Equation 1: diagonal capacity of a routing tile (width × height).
std::uint32_t diagonal_capacity(double width, double height, double pitch);
