#include "utils/TimeUtils.hpp"

float
getCurrentGameTime()
{
    using namespace std::chrono;
    // Use process-relative time so the float value stays small and retains
    // sub-second precision even after months of server uptime.
    // (steady_clock::time_since_epoch() can exceed 2^23 seconds on a long-running
    // host, making float deltas < 1 s unrepresentable and stalling AI timers.)
    static const auto startTime = steady_clock::now();
    return duration<float>(steady_clock::now() - startTime).count();
}
