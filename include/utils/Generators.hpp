#pragma once
#include <atomic>
#include <chrono>
#include <mutex>
#include <random>

class Generators
{
  public:
    // generate unique time based key
    static long long generateUniqueTimeBasedKey(int zoneId);
    // generate unique positive integer ID for mob instances
    static int generateUniqueMobUID();
    // generate ranmdom number between min and max
    static int generateSimpleRandomNumber(int min, int max);

  private:
    static std::atomic<int> mobUIDCounter_;
    static std::mutex generatorMutex_;
    static std::mt19937 randomGenerator_;
};