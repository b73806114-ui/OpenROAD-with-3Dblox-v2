#include <boost/asio/post.hpp>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "try_routing.hpp"

bool Escaper::assess(const Solution& candidate,
                     unsigned int count,
                     std::vector<Solution>& incumbents,
                     bool compare_contours)
{
  Timer timer("assess");

  if (candidate.critical_length == 0.0) {
    return true;
  }

  if (compare_contours) {
    if (candidate.critical_length > best_critical_length) {
      return false;
    }
    if (candidate.critical_length == best_critical_length) {
      double routed_total = 0.0;
      for (const std::uint16_t net : candidate.order) {
        routed_total = routed_total + routed_lengths[net];
      }
      return candidate.total_length < routed_total;
    }
  }

  if (count == 0U) {
    return true;
  }

  if (count <= 63U) {
    for (unsigned int index = 0U; index < count; ++index) {
      if (compare(candidate, incumbents[index], compare_contours)) {
        return false;
      }
    }
    return true;
  }

  dominated = false;
  const unsigned int base_count = count >> 3U;
  const unsigned int remainder = count & 7U;
  unsigned int begin = 0U;
  for (unsigned int worker = 0U; worker != 8U; ++worker) {
    const unsigned int end
        = begin + base_count + static_cast<unsigned int>(worker < remainder);
    boost::asio::post(
        pool, [this, &candidate, &incumbents, compare_contours, begin, end]() {
          for (unsigned int index = begin; index < end; ++index) {
            if (compare(candidate, incumbents[index], compare_contours)) {
              std::lock_guard<std::mutex> lock(assess_mutex);
              dominated = true;
            }
          }
        });
    begin = end;
  }

  pool.join();
  std::lock_guard<std::mutex> lock(assess_mutex);
  if (!dominated) {
    dominated = true;
    return true;
  }
  return false;
}
