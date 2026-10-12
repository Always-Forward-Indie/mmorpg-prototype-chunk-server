#include "services/ExperienceManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/ExperienceCacheManager.hpp"
#include "services/IStatsNotifier.hpp"
#include "services/StatsResolver.hpp"
#include "services/TitleManager.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TimestampUtils.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <spdlog/logger.h>

ExperienceManager::ExperienceManager(CharacterManager &characters,
    ExperienceCacheManager &expCache,
    TitleManager *titles,
    IStatsNotifier *statsNotify,
    Logger &logger)
    : characters_(characters),
      expCache_(expCache),
      titles_(titles),
      statsNotify_(statsNotify),
      logger_(logger),
      experiencePacketCallback_(nullptr),
      saveProgressCallback_(nullptr)
{
    log_ = logger_.getSystem("experience");
}

ExperienceGrantResult
ExperienceManager::grantExperience(int characterId, int experienceAmount, const std::string &reason, int sourceId)
{
    ExperienceGrantResult result;
    result.success = false;

    try
    {
        // ── Atomic mutation under the character-map lock ────────────────────
        // The old copy-modify-write across the whole grant lost XP on
        // concurrent grants (AoE multi-kill) and wiped HP interleaved with
        // damage/heal via the stale-copy overwrite in loadCharacterData.
        // Everything below runs on the LIVE struct; packets, titles and
        // callbacks stay outside the lock. Lock order: character map is
        // innermost (see applyUnderLock); exp-cache reads are its own lock.
        struct Applied
        {
            bool present = false;
            int oldExp = 0, oldLvl = 0, oldDebt = 0;
            int newExp = 0, newLvl = 0, debtAfter = 0;
            bool leveled = false;
        } applied;
        const int amountIn = experienceAmount;
        const bool present = characters_.applyUnderLock(characterId,
            [&](CharacterDataStruct &ch) {
                applied.present = true;
                applied.oldExp = ch.characterExperiencePoints;
                applied.oldLvl = ch.characterLevel;
                applied.oldDebt = ch.experienceDebt;

                // ── Experience Debt: redirect XP to pay debt first ──────
                int amount = amountIn;
                if (amount > 0 && ch.experienceDebt > 0)
                {
                    const int toDebt = std::min(amount, ch.experienceDebt);
                    ch.experienceDebt -= toDebt;
                    amount -= toDebt;
                }

                int newExp = std::max(0, ch.characterExperiencePoints + amount);
                int newLvl = getLevelFromExperience(newExp);
                if (newLvl > MAX_LEVEL)
                {
                    newLvl = MAX_LEVEL;
                    newExp = getExperienceForLevel(MAX_LEVEL);
                }
                applied.newExp = newExp;
                applied.newLvl = newLvl;
                applied.debtAfter = ch.experienceDebt;
                applied.leveled = (newLvl > applied.oldLvl);

                ch.characterExperiencePoints = newExp;
                ch.characterLevel = newLvl;
                ch.expForNextLevel = getExperienceForLevelFromGameServer(newLvl + 1);
                // SP grant mirrors game set_character_exp_level (+1 per level).
                ch.freeSkillPoints += (newLvl - applied.oldLvl);

                if (applied.leveled)
                {
                    // Repair base max from attributes when unset.
                    if (ch.characterMaxHealth <= 0)
                        for (const auto &attr : ch.attributes)
                            if (attr.slug == "max_health")
                            {
                                ch.characterMaxHealth = attr.value;
                                break;
                            }
                    if (ch.characterMaxMana <= 0)
                        for (const auto &attr : ch.attributes)
                            if (attr.slug == "max_mana")
                            {
                                ch.characterMaxMana = attr.value;
                                break;
                            }

                    ch.characterMaxHealth += (newLvl - applied.oldLvl) * 10; // +10 HP/level
                    ch.characterMaxMana += (newLvl - applied.oldLvl) * 5;    // +5 MP/level

                    // Full restore capped to effective max: bumped struct max +
                    // effect deltas from StatsResolver (flat, percent-of-base,
                    // debuffs). Resolver totals are relative to the stored
                    // attribute vector (pre-bump), so take only the DELTA —
                    // the bump itself lives in the struct fields. Current never
                    // exceeds max on the client HUD.
                    const int64_t nowSecLvl =
                        std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
                    const auto eff = StatsResolver::effectiveMap(
                        ch.attributes, ch.activeEffects, nowSecLvl);
                    const auto effDelta = [&](const char *slug) {
                        int base = 0;
                        for (const auto &a : ch.attributes)
                            if (a.slug == slug)
                            {
                                base = a.value;
                                break;
                            }
                        const auto it = eff.find(slug);
                        const float total =
                            (it != eff.end()) ? it->second : static_cast<float>(base);
                        return static_cast<int>(std::round(total)) - base;
                    };
                    ch.characterCurrentHealth = std::max(1,
                        ch.characterMaxHealth + effDelta("max_health"));
                    ch.characterCurrentMana = std::max(0,
                        ch.characterMaxMana + effDelta("max_mana"));
                }
            });
        if (!present)
        {
            log_->error("grantExperience: character " + std::to_string(characterId) + " not in map");
            result.success = false;
            return result;
        }

        const int oldExperience = applied.oldExp;
        const int oldLevel = applied.oldLvl;
        const int oldDebt = applied.oldDebt;
        const int newExperience = applied.newExp;
        const int newLevel = applied.newLvl;

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
        result.levelUp = applied.leveled;

        if (result.levelUp)
            handleLevelUp(characterId, oldLevel, newLevel, result);

        // Немедленно сохраняем exp/level на гейм-сервере
        sendSaveProgressToGameServer(characterId, newExperience, newLevel);

        // Сохраняем долг, если он изменился
        if (applied.debtAfter != oldDebt && saveProgressCallback_)
        {
            nlohmann::json debtPacket;
            debtPacket["header"]["eventType"] = "saveExperienceDebt";
            debtPacket["header"]["clientId"] = 0;
            debtPacket["header"]["hash"] = "";
            debtPacket["body"]["characterId"] = characterId;
            debtPacket["body"]["experienceDebt"] = applied.debtAfter;
            saveProgressCallback_(debtPacket.dump() + "\n");
        }

        // Отправляем пакет об изменении опыта
        sendExperiencePacket(result.experienceEvent);

        // Если произошло повышение уровня, отправляем дополнительные пакеты
        if (result.levelUp && statsNotify_ != nullptr)
        {
            statsNotify_->sendStatsUpdate(characterId);
        }

        // Level-up growth (mult*level^exp) lives in DB: ask game to recompute
        // now so the new base values apply immediately, not at the next
        // incidental refresh. Best-effort like the durability path.
        if (result.levelUp && refreshAttributesCallback_)
        {
            try
            {
                refreshAttributesCallback_(characterId);
            }
            catch (const std::exception &e)
            {
                logger_.logError(std::string("Level-up refresh trigger failed: ") + e.what());
            }
            catch (...)
            {
            }
        }

        result.success = true;

        logger_.log("Granted " + std::to_string(experienceAmount) +
                        " experience to character " + std::to_string(characterId) +
                        " (reason: " + reason + ")",
            GREEN);

        if (result.levelUp)
        {
            logger_.log("Character " + std::to_string(characterId) +
                            " leveled up from " + std::to_string(oldLevel) +
                            " to " + std::to_string(newLevel),
                CYAN);
        }
    }
    catch (const std::exception &e)
    {
        result.errorMessage = "Error granting experience: " + std::string(e.what());
        logger_.logError("ExperienceManager::grantExperience error: " + std::string(e.what()));
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

    return static_cast<int>(std::lround(baseExp * modifier));
}

int
ExperienceManager::calculateDeathPenalty(int characterLevel, int currentExperience, int expForCurrentLevel)
{
    (void)characterLevel; // kept for call-site readability; the floor is fully described by expForCurrentLevel
    // Штраф составляет 10% от текущего опыта, но не меньше 0
    int penalty = static_cast<int>(std::lround(currentExperience * DEATH_PENALTY_PERCENT));

    // Но не забираем опыт ниже начала текущего уровня
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
        totalExp += static_cast<int>(std::lround(BASE_EXP_PER_LEVEL * std::pow(EXP_MULTIPLIER, i - 2)));
    }

    return totalExp;
}

int
ExperienceManager::getLevelFromExperience(int experience)
{
    if (experience <= 0)
        return 1;

    // Используем кешированные данные если доступны
    if (expCache_.isTableLoaded())
    {
        // Ищем подходящий уровень в кешированных данных
        int level = 1;
        int maxLevel = expCache_.getMaxLevel();

        for (int i = 1; i <= maxLevel; i++)
        {
            int expRequired = expCache_.getExperienceForLevel(i);
            if (experience < expRequired)
            {
                break;
            }
            level = i;
        }

        logger_.log("Level calculation from cache: " + std::to_string(experience) +
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
    logger_.log("Handling level up for character " + std::to_string(characterId) +
                    " from level " + std::to_string(oldLevel) +
                    " to level " + std::to_string(newLevel),
        CYAN);

    // Здесь можно добавить логику для получения новых способностей
    // Например, каждые 5 уровней игрок получает новую способность
    for (int level = oldLevel + 1; level <= newLevel; level++)
    {
        // Check for level-based title grants on every new level reached.
        if (titles_ != nullptr)
        {
            titles_->checkAndGrantTitles(characterId, "level",
                nlohmann::json{{"level", level}});
        }

        if (level % 5 == 0)
        {
            std::string newAbility = "ability_level_" + std::to_string(level);
            result.newAbilities.push_back(newAbility);
            log_->info("Character " + std::to_string(characterId) +
                       " gained new ability: " + newAbility);
        }
    }
}

int
ExperienceManager::getExperienceForLevelFromGameServer(int level)
{
    // Проверяем, загружена ли таблица опыта в кеше
    if (expCache_.isTableLoaded())
    {
        // Используем кешированные данные
        int cachedExp = expCache_.getExperienceForLevel(level);
        logger_.log("Retrieved experience for level " + std::to_string(level) +
                        " from cache: " + std::to_string(cachedExp),
            GREEN);
        return cachedExp;
    }
    else
    {
        // Кеш не загружен, используем локальный расчет как fallback
        log_->info("Experience cache not loaded, using local calculation for level " +
                   std::to_string(level));
        return getExperienceForLevel(level);
    }
}

void
ExperienceManager::setSaveProgressCallback(std::function<void(const std::string &)> callback)
{
    saveProgressCallback_ = callback;
}

void
ExperienceManager::setRefreshAttributesCallback(std::function<void(int)> callback)
{
    refreshAttributesCallback_ = std::move(callback);
}

void
ExperienceManager::sendSaveProgressToGameServer(int characterId, int experience, int level)
{
    if (!saveProgressCallback_)
        return;

    nlohmann::json packet;
    packet["header"]["eventType"] = "saveCharacterProgress";
    packet["header"]["clientId"] = 0;
    packet["header"]["hash"] = "";
    nlohmann::json entry;
    entry["characterId"] = characterId;
    entry["exp"] = experience;
    entry["level"] = level;
    packet["body"]["characters"] = nlohmann::json::array({entry});

    saveProgressCallback_(packet.dump() + "\n");
}
