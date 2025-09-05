#include "services/ExperienceManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameServices.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TimestampUtils.hpp"
#include <algorithm>
#include <cmath>

ExperienceManager::ExperienceManager(GameServices *gameServices)
    : gameServices_(gameServices), experiencePacketCallback_(nullptr), statsUpdatePacketCallback_(nullptr)
{
}

ExperienceGrantResult
ExperienceManager::grantExperience(int characterId, int experienceAmount, const std::string &reason, int sourceId)
{
    ExperienceGrantResult result;
    result.success = false;

    try
    {
        // Получаем данные персонажа
        auto characterData = gameServices_->getCharacterManager().getCharacterData(characterId);

        // Сохраняем старые значения
        int oldExperience = characterData.characterExperiencePoints;
        int oldLevel = characterData.characterLevel;

        // Вычисляем новые значения
        int newExperience = oldExperience + experienceAmount;
        newExperience = std::max(0, newExperience); // Не даем опыту стать отрицательным

        int newLevel = getLevelFromExperience(newExperience);

        // Проверяем максимальный уровень
        if (newLevel > MAX_LEVEL)
        {
            newLevel = MAX_LEVEL;
            newExperience = getExperienceForLevel(MAX_LEVEL);
        }

        // Заполняем структуру события
        result.experienceEvent.characterId = characterId;
        result.experienceEvent.experienceChange = newExperience - oldExperience;
        result.experienceEvent.oldExperience = oldExperience;
        result.experienceEvent.newExperience = newExperience;
        result.experienceEvent.oldLevel = oldLevel;
        result.experienceEvent.newLevel = newLevel;
        result.experienceEvent.expForCurrentLevel = getExperienceForLevelFromGameServer(newLevel);
        result.experienceEvent.expForNextLevel = getExperienceForLevelFromGameServer(newLevel + 1);
        result.experienceEvent.reason = reason;
        result.experienceEvent.sourceId = sourceId;
        result.experienceEvent.timestamps = TimestampUtils::createReceiveTimestamp(0, "");

        // Проверяем повышение уровня
        result.levelUp = (newLevel > oldLevel);

        // Обновляем данные персонажа в CharacterManager
        // Обновляем опыт
        characterData.characterExperiencePoints = newExperience;
        characterData.characterLevel = newLevel;
        characterData.expForNextLevel = result.experienceEvent.expForNextLevel;

        // Тестируем новый метод получения опыта из гейм-сервера
        gameServices_->getLogger().log("Testing getExperienceForLevelFromGameServer for level " + std::to_string(newLevel + 1), YELLOW);
        int expFromGameServer = getExperienceForLevelFromGameServer(newLevel + 1);
        gameServices_->getLogger().log("Experience for level " + std::to_string(newLevel + 1) +
                                           " from game server: " + std::to_string(expFromGameServer),
            GREEN);

        // Если произошло повышение уровня, обновляем статы
        if (result.levelUp)
        {
            // Сохраняем старые значения для события
            int oldMaxHealth = characterData.characterMaxHealth;
            int oldMaxMana = characterData.characterMaxMana;

            // Убеждаемся, что у нас правильные базовые значения из атрибутов
            if (characterData.characterMaxHealth <= 0)
            {
                for (const auto &attr : characterData.attributes)
                {
                    if (attr.slug == "max_health")
                    {
                        characterData.characterMaxHealth = attr.value;
                        break;
                    }
                }
            }

            if (characterData.characterMaxMana <= 0)
            {
                for (const auto &attr : characterData.attributes)
                {
                    if (attr.slug == "max_mana")
                    {
                        characterData.characterMaxMana = attr.value;
                        break;
                    }
                }
            }

            handleLevelUp(characterId, oldLevel, newLevel, result);

            // Обновляем здоровье и ману при повышении уровня
            int healthIncrease = (newLevel - oldLevel) * 10; // +10 HP за уровень
            int manaIncrease = (newLevel - oldLevel) * 5;    // +5 MP за уровень

            characterData.characterMaxHealth += healthIncrease;
            characterData.characterMaxMana += manaIncrease;
            characterData.characterCurrentHealth = characterData.characterMaxHealth; // Полное восстановление при уровне
            characterData.characterCurrentMana = characterData.characterMaxMana;
        }

        // Сохраняем обновленные данные персонажа
        gameServices_->getCharacterManager().loadCharacterData(characterData);

        // Отправляем пакет об изменении опыта
        sendExperiencePacket(result.experienceEvent);

        // Если произошло повышение уровня, отправляем дополнительные пакеты
        if (result.levelUp)
        {
            sendStatsUpdatePacket(characterId);
        }

        result.success = true;

        gameServices_->getLogger().log("Granted " + std::to_string(experienceAmount) +
                                           " experience to character " + std::to_string(characterId) +
                                           " (reason: " + reason + ")",
            GREEN);

        if (result.levelUp)
        {
            gameServices_->getLogger().log("Character " + std::to_string(characterId) +
                                               " leveled up from " + std::to_string(oldLevel) +
                                               " to " + std::to_string(newLevel),
                CYAN);
        }
    }
    catch (const std::exception &e)
    {
        result.errorMessage = "Error granting experience: " + std::string(e.what());
        gameServices_->getLogger().logError("ExperienceManager::grantExperience error: " + std::string(e.what()));
    }

    return result;
}

ExperienceGrantResult
ExperienceManager::removeExperience(int characterId, int experienceAmount, const std::string &reason)
{
    // Используем grantExperience с отрицательным значением
    return grantExperience(characterId, -experienceAmount, reason, 0);
}

int
ExperienceManager::calculateMobExperience(int mobLevel, int characterLevel, int baseExperience)
{
    // Используем базовый опыт моба как основу, если он больше 0
    int baseExp = (baseExperience > 0) ? baseExperience : (mobLevel * 10);

    // Применяем модификатор в зависимости от разности уровней
    int levelDifference = mobLevel - characterLevel;
    double modifier = 1.0;

    if (levelDifference < -5)
    {
        // Моб слишком слабый - очень мало опыта
        modifier = 0.1;
    }
    else if (levelDifference < -2)
    {
        // Моб слабый - меньше опыта
        modifier = 0.5;
    }
    else if (levelDifference <= 2)
    {
        // Моб подходящего уровня - нормальный опыт
        modifier = 1.0;
    }
    else if (levelDifference <= 5)
    {
        // Моб сильный - больше опыта
        modifier = 1.5;
    }
    else
    {
        // Моб очень сильный - максимальный опыт
        modifier = 2.0;
    }

    return static_cast<int>(baseExp * modifier);
}

int
ExperienceManager::calculateDeathPenalty(int characterLevel, int currentExperience)
{
    // Штраф составляет 10% от текущего опыта, но не меньше 0
    int penalty = static_cast<int>(currentExperience * DEATH_PENALTY_PERCENT);

    // Но не забираем опыт ниже начала текущего уровня
    int expForCurrentLevel = (characterLevel > 1) ? getExperienceForLevelFromGameServer(characterLevel - 1) : 0;
    int maxPenalty = currentExperience - expForCurrentLevel;

    return std::min(penalty, maxPenalty);
}

int
ExperienceManager::getExperienceForLevel(int level)
{
    if (level <= 1)
        return 0;

    int totalExp = 0;
    for (int i = 2; i <= level; i++)
    {
        // Экспоненциальный рост: baseExp * (multiplier ^ (level-2))
        totalExp += static_cast<int>(BASE_EXP_PER_LEVEL * std::pow(EXP_MULTIPLIER, i - 2));
    }

    return totalExp;
}

int
ExperienceManager::getLevelFromExperience(int experience)
{
    if (experience <= 0)
        return 1;

    // Используем кешированные данные если доступны
    auto &cacheManager = gameServices_->getExperienceCacheManager();

    if (cacheManager.isTableLoaded())
    {
        // Ищем подходящий уровень в кешированных данных
        int level = 1;
        int maxLevel = cacheManager.getMaxLevel();

        for (int i = 1; i <= maxLevel; i++)
        {
            int expRequired = cacheManager.getExperienceForLevel(i);
            if (experience < expRequired)
            {
                break;
            }
            level = i;
        }

        gameServices_->getLogger().log("Level calculation from cache: " + std::to_string(experience) +
                                           " exp = level " + std::to_string(level),
            GREEN);
        return level;
    }
    else
    {
        // Fallback на локальный расчет
        int level = 1;
        int totalExpForLevel = 0;

        while (level < MAX_LEVEL)
        {
            int expForNextLevel = getExperienceForLevel(level + 1);
            if (experience < expForNextLevel)
            {
                break;
            }
            level++;
        }

        return level;
    }
}

int
ExperienceManager::getExperienceForNextLevel(int currentLevel)
{
    if (currentLevel >= MAX_LEVEL)
        return getExperienceForLevelFromGameServer(MAX_LEVEL);
    return getExperienceForLevelFromGameServer(currentLevel + 1);
}

void
ExperienceManager::setExperiencePacketCallback(std::function<void(const nlohmann::json &)> callback)
{
    experiencePacketCallback_ = callback;
}

void
ExperienceManager::sendExperiencePacket(const ExperienceEventStruct &experienceEvent)
{
    if (experiencePacketCallback_)
    {
        auto packet = buildExperiencePacket(experienceEvent);
        experiencePacketCallback_(packet);
    }
}

nlohmann::json
ExperienceManager::buildExperiencePacket(const ExperienceEventStruct &experienceEvent)
{
    ResponseBuilder builder;

    builder.setHeader("eventType", "experience_update")
        .setHeader("status", "success")
        .setHeader("requestId", experienceEvent.timestamps.requestId)
        .setTimestamps(experienceEvent.timestamps);

    builder.setBody("characterId", experienceEvent.characterId)
        .setBody("experienceChange", experienceEvent.experienceChange)
        .setBody("oldExperience", experienceEvent.oldExperience)
        .setBody("newExperience", experienceEvent.newExperience)
        .setBody("oldLevel", experienceEvent.oldLevel)
        .setBody("newLevel", experienceEvent.newLevel)
        .setBody("expForCurrentLevel", experienceEvent.expForCurrentLevel)
        .setBody("expForNextLevel", experienceEvent.expForNextLevel)
        .setBody("reason", experienceEvent.reason)
        .setBody("sourceId", experienceEvent.sourceId)
        .setBody("levelUp", (experienceEvent.newLevel > experienceEvent.oldLevel));

    return builder.build();
}

void
ExperienceManager::handleLevelUp(int characterId, int oldLevel, int newLevel, ExperienceGrantResult &result)
{
    gameServices_->getLogger().log("Handling level up for character " + std::to_string(characterId) +
                                       " from level " + std::to_string(oldLevel) +
                                       " to level " + std::to_string(newLevel),
        CYAN);

    // Здесь можно добавить логику для получения новых способностей
    // Например, каждые 5 уровней игрок получает новую способность
    for (int level = oldLevel + 1; level <= newLevel; level++)
    {
        if (level % 5 == 0)
        {
            std::string newAbility = "ability_level_" + std::to_string(level);
            result.newAbilities.push_back(newAbility);
            gameServices_->getLogger().log("Character " + std::to_string(characterId) +
                                               " gained new ability: " + newAbility,
                YELLOW);
        }
    }
}

int
ExperienceManager::getExperienceForLevelFromGameServer(int level)
{
    // Проверяем, загружена ли таблица опыта в кеше
    auto &cacheManager = gameServices_->getExperienceCacheManager();

    if (cacheManager.isTableLoaded())
    {
        // Используем кешированные данные
        int cachedExp = cacheManager.getExperienceForLevel(level);
        gameServices_->getLogger().log("Retrieved experience for level " + std::to_string(level) +
                                           " from cache: " + std::to_string(cachedExp),
            GREEN);
        return cachedExp;
    }
    else
    {
        // Кеш не загружен, используем локальный расчет как fallback
        gameServices_->getLogger().log("Experience cache not loaded, using local calculation for level " +
                                           std::to_string(level),
            YELLOW);
        return getExperienceForLevel(level);
    }
}

void
ExperienceManager::setStatsUpdatePacketCallback(std::function<void(const nlohmann::json &)> callback)
{
    statsUpdatePacketCallback_ = callback;
}

void
ExperienceManager::sendStatsUpdatePacket(int characterId)
{
    if (statsUpdatePacketCallback_)
    {
        auto characterData = gameServices_->getCharacterManager().getCharacterData(characterId);
        std::string requestId = "stats_update_" + std::to_string(characterId);
        auto packet = buildStatsUpdatePacket(characterData, requestId);
        statsUpdatePacketCallback_(packet);
    }
}

nlohmann::json
ExperienceManager::buildStatsUpdatePacket(const CharacterDataStruct &characterData, const std::string &requestId)
{
    ResponseBuilder builder;

    TimestampStruct timestamps = TimestampUtils::createReceiveTimestamp(0, requestId);

    builder.setHeader("eventType", "stats_update")
        .setHeader("status", "success")
        .setHeader("requestId", requestId)
        .setTimestamps(timestamps);

    builder.setBody("characterId", characterData.characterId)
        .setBody("level", characterData.characterLevel)
        .setBody("experience", nlohmann::json{{"current", characterData.characterExperiencePoints}, {"nextLevel", characterData.expForNextLevel}})
        .setBody("health", nlohmann::json{{"current", characterData.characterCurrentHealth}, {"max", characterData.characterMaxHealth}})
        .setBody("mana", nlohmann::json{{"current", characterData.characterCurrentMana}, {"max", characterData.characterMaxMana}});

    return builder.build();
}
