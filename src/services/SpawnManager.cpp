#include "services/SpawnManager.hpp"

SpawnManager::SpawnManager(Logger& logger)
    : logger_(logger)
{
    log_ = logger.getSystem("spawn");

}