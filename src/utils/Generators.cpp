#include "utils/Generators.hpp"

// Initialize static members
std::atomic<int> Generators::mobUIDCounter_(1000000); // Start from 1 million to avoid conflicts
std::mutex Generators::generatorMutex_;
std::mt19937 Generators::randomGenerator_(std::chrono::steady_clock::now().time_since_epoch().count());

long long
Generators::generateUniqueTimeBasedKey(int keyId)
{
    auto now = std::chrono::system_clock::now().time_since_epoch();
    long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    // Use safer approach to avoid overflow
    long long key = (now_ms % 1000000000LL) * 1000 + keyId + (randomGenerator_() % 1000);

    return key;
}

int
Generators::generateUniqueMobUID()
{
    // Thread-safe increment and return
    return mobUIDCounter_.fetch_add(1);
}

int
Generators::generateSimpleRandomNumber(int min, int max)
{
    std::lock_guard<std::mutex> lock(generatorMutex_);
    std::uniform_int_distribution<int> dist(min, max);
    return dist(randomGenerator_);
}