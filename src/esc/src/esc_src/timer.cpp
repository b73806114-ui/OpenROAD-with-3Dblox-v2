#include "try_routing.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <tuple>
#include <vector>

std::unordered_map<std::string, Timer::DurationRecord> Timer::_func_times;

Timer::Timer(const std::string& function_name)
    : function_name_(function_name),
      start_(std::chrono::system_clock::now()) {}

Timer::~Timer() {
    const auto end = std::chrono::system_clock::now();
    DurationRecord& record = _func_times[function_name_];
    ++record.first;
    record.second += std::chrono::duration_cast<std::chrono::nanoseconds>(
                         end - start_)
                         .count();

    if (function_name_ != "main") {
        return;
    }

    using Row = std::tuple<std::string, int, long>;
    std::vector<Row> rows;
    for (const auto& entry : _func_times) {
        rows.emplace_back(entry.first, entry.second.first, entry.second.second);
    }
    std::sort(rows.begin(), rows.end(), [](const Row& lhs, const Row& rhs) {
        return std::get<2>(lhs) > std::get<2>(rhs);
    });

    constexpr const char* separator =
        "====================================================================";
    std::cout << '\n' << separator << '\n';
    std::cout << std::left << std::setw(30) << "|  Function Name"
              << std::right << std::setw(15) << " Exec Count"
              << std::setw(23) << " Exec Time  |" << '\n';
    std::cout << separator << '\n';
    for (const Row& row : rows) {
        std::cout << "|  " << std::left << std::setw(27) << std::get<0>(row)
                  << std::right << std::setw(15) << std::get<1>(row)
                  << std::setw(17) << std::get<2>(row) << " ns  |" << '\n';
    }
    std::cout << separator << std::endl;
}
