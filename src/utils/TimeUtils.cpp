#include "utils/TimeUtils.hpp"

float getCurrentGameTime() {
    using namespace std::chrono;
    return duration<float>(steady_clock::now().time_since_epoch()).count();
}
