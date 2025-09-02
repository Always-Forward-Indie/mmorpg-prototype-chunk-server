#include "services/ExperienceCacheManager.hpp"
#include "services/GameServices.hpp"
#include "utils/TimestampUtils.hpp"
#include <chrono>

ExperienceCacheManager::ExperienceCacheManager(GameServices *gameServices)
    : gameServices_(gameServices)
{
    initialize();
}

void
ExperienceCacheManager::initialize()
{
    gameServices_->getLogger().log("ExperienceCacheManager initialized (ready for experience table loading)", CYAN);

    // Просто инициализируем пустой кэш, данные будут загружены позже через событие
    std::unique_lock<std::shared_mutex> lock(mutex_);
    experienceTable_.clear();
    experienceTable_.lastUpdated = std::chrono::system_clock::now();
}

void
ExperienceCacheManager::loadExperienceTableFromGameServer()
{
    gameServices_->getLogger().log("Manual request for experience level table from game server", CYAN);

    try
    {
        // Этот метод может быть вызван вручную для перезагрузки кэша
        // Запрос будет отправлен через GameServerWorker вызывающим кодом
        gameServices_->getLogger().log("Experience table manual reload requested", BLUE);
    }
    catch (const std::exception &e)
    {
        gameServices_->getLogger().logError("Error in manual experience table reload: " + std::string(e.what()));
    }
}

void
ExperienceCacheManager::setExperienceTable(const std::vector<ExperienceLevelEntry> &entries)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);

    experienceTable_.levels = entries;
    experienceTable_.isLoaded = true;
    experienceTable_.lastUpdated = std::chrono::system_clock::now();

    gameServices_->getLogger().log("Experience table loaded successfully with " +
                                       std::to_string(entries.size()) + " level entries",
        GREEN);

    // Логируем несколько первых записей для отладки
    if (!entries.empty())
    {
        gameServices_->getLogger().log("Sample entries:", BLUE);
        for (size_t i = 0; i < std::min(size_t(5), entries.size()); ++i)
        {
            const auto &entry = entries[i];
            gameServices_->getLogger().log("  Level " + std::to_string(entry.level) +
                                               ": " + std::to_string(entry.experiencePoints) + " exp",
                BLUE);
        }
    }
}

int
ExperienceCacheManager::getExperienceForLevel(int level) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (!experienceTable_.isLoaded)
    {
        gameServices_->getLogger().logError("Experience table not loaded, returning 0 for level " + std::to_string(level));
        return 0;
    }

    return experienceTable_.getExperienceForLevel(level);
}

int
ExperienceCacheManager::getMaxLevel() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (!experienceTable_.isLoaded)
    {
        return 0;
    }

    return experienceTable_.getMaxLevel();
}

bool
ExperienceCacheManager::isTableLoaded() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return experienceTable_.isLoaded;
}

size_t
ExperienceCacheManager::getTableSize() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return experienceTable_.levels.size();
}

void
ExperienceCacheManager::refreshFromGameServer()
{
    gameServices_->getLogger().log("Refreshing experience table from game server", CYAN);
    loadExperienceTableFromGameServer();
}

void
ExperienceCacheManager::clearCache()
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    experienceTable_.clear();
    gameServices_->getLogger().log("Experience table cache cleared", YELLOW);
}

std::string
ExperienceCacheManager::createGetExpTableRequest() const
{
    nlohmann::json requestPacket;

    // Header
    requestPacket["header"]["event"] = "getExpLevelTable";
    requestPacket["header"]["status"] = "request";
    requestPacket["header"]["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                                               .count();
    requestPacket["header"]["requestId"] = "exp_table_request_" +
                                           std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                                   .count());

    // Body - пустое тело для запроса всей таблицы
    requestPacket["body"]["requestType"] = "getAllLevels";

    return requestPacket.dump();
}
