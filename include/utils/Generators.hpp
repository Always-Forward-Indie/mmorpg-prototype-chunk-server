#pragma once
#include <chrono>
#include <stdlib.h>

class Generators
{
public:
    // generate unique time based key
    static long long generateUniqueTimeBasedKey(int zoneId);
    // generate ranmdom number between min and max
    static int generateSimpleRandomNumber(int min, int max);
};