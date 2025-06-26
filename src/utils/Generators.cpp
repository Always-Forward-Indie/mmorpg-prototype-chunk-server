#include "utils/Generators.hpp"

long long Generators::generateUniqueTimeBasedKey(int keyId) {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    long long now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    long long key = now_ms * 100 + keyId + (rand() % 100000);

    return key;
}

int Generators::generateSimpleRandomNumber(int min, int max) {
    return rand() % (max - min + 1) + min;
}