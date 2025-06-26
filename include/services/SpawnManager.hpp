#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"

class SpawnManager
{
public:
    SpawnManager(Logger& logger);

private:
    Logger& logger_;
};