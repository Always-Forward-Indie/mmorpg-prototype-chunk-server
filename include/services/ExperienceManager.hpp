#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <cmath>
#include <functional>
#include <vector>

// Forward declarations
class CharacterManager;
class ExperienceCacheManager;
class TitleManager;
class IStatsNotifier;

/**
 * @brief Менеджер опыта, отвечающий за начисление/снятие опыта и управление уровнями
 */
class ExperienceManager
{
  public:
    /// Explicit dependencies (no GameServices). titles and statsNotify are
    /// optional and may be null (level-up titles / stats_update then skipped)
    /// — same pattern as ChampionManager::statsNotify_. Tests pass nullptr;
    /// production passes &titleManager_ / &statsNotificationService_.
    /// Packet/save callbacks stay std::function setters (wired by CombatSystem
    /// and ChunkServer, null-guarded).
    ExperienceManager(CharacterManager &characters,
        ExperienceCacheManager &expCache,
        TitleManager *titles,
        IStatsNotifier *statsNotify,
        Logger &logger);

    /**
     * @brief Начислить опыт персонажу
     * @param characterId ID персонажа
     * @param experienceAmount Количество опыта для начисления
     * @param reason Причина начисления опыта
     * @param sourceId ID источника опыта (например, ID убитого моба)
     * @return Результат начисления опыта
     */
    ExperienceGrantResult grantExperience(int characterId, int experienceAmount, const std::string &reason, int sourceId = 0);

    /**
     * @brief Снять опыт у персонажа (например, при смерти)
     * @param characterId ID персонажа
     * @param experienceAmount Количество опыта для снятия
     * @param reason Причина снятия опыта
     * @return Результат снятия опыта
     */
    ExperienceGrantResult removeExperience(int characterId, int experienceAmount, const std::string &reason);

    /**
     * @brief Вычислить количество опыта за убийство моба
     * Static/pure: зависит только от аргументов (можно тестировать без мира).
     * @param mobLevel Уровень моба
     * @param characterLevel Уровень персонажа
     * @param baseExperience Базовый опыт моба
     * @return Количество опыта
     */
    static int calculateMobExperience(int mobLevel, int characterLevel, int baseExperience);

    /**
     * @brief Вычислить штраф опыта при смерти
     * Static/pure: 10% от текущего опыта, но не ниже начала текущего уровня
     * (можно тестировать без мира — expForCurrentLevel инжектится).
     * @param characterLevel Уровень персонажа
     * @param currentExperience Текущий опыт персонажа
     * @param expForCurrentLevel Опыт начала текущего уровня
     * @return Количество опыта к снятию
     */
    static int calculateDeathPenalty(int characterLevel, int currentExperience, int expForCurrentLevel);

    /**
     * @brief Получить количество опыта, требуемое для достижения определенного уровня
     * Static/pure: BASE_EXP_PER_LEVEL=100, EXP_MULTIPLIER=1.2 (можно тестировать без мира).
     * @param level Целевой уровень
     * @return Общее количество опыта для достижения уровня
     */
    static int getExperienceForLevel(int level);

    /**
     * @brief Запросить опыт для уровня с гейм-сервера
     * @param level Уровень
     * @return Опыт, требуемый для достижения уровня с гейм-сервера
     */
    int getExperienceForLevelFromGameServer(int level);

    /**
     * @brief Получить уровень по количеству опыта
     * @param experience Количество опыта
     * @return Уровень персонажа
     */
    int getLevelFromExperience(int experience);

    /**
     * @brief Получить опыт, требуемый для следующего уровня
     * @param currentLevel Текущий уровень
     * @return Количество опыта для следующего уровня
     */
    int getExperienceForNextLevel(int currentLevel);

    /**
     * @brief Установить callback для отправки пакетов опыта
     * @param callback Функция для отправки пакетов
     */
    void setExperiencePacketCallback(std::function<void(const nlohmann::json &)> callback);

    /**
     * @brief Установить callback для немедленного сохранения exp/level на гейм-сервере
     * @param callback Функция отправки данных на гейм-сервер (принимает строку)
     */
    void setSaveProgressCallback(std::function<void(const std::string &)> callback);

  private:
    CharacterManager &characters_;
    ExperienceCacheManager &expCache_;
    TitleManager *titles_; // optional, may be null
    IStatsNotifier *statsNotify_; // optional, may be null
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    std::function<void(const nlohmann::json &)> experiencePacketCallback_;
    std::function<void(const std::string &)> saveProgressCallback_;

    /**
     * @brief Отправить пакет об изменении опыта
     * @param experienceEvent Событие изменения опыта
     */
    void sendExperiencePacket(const ExperienceEventStruct &experienceEvent);

    /**
     * @brief Построить JSON пакет с данными об опыте
     * @param experienceEvent Событие изменения опыта
     * @return JSON пакет
     */
    nlohmann::json buildExperiencePacket(const ExperienceEventStruct &experienceEvent);

    /**
     * @brief Отправить saveCharacterProgress на гейм-сервер для немедленной записи в БД
     * @param characterId ID персонажа
     * @param experience Новый опыт
     * @param level Новый уровень
     */
    void sendSaveProgressToGameServer(int characterId, int experience, int level);

    /**
     * @brief Проверить и обработать повышение уровня
     * @param characterId ID персонажа
     * @param oldLevel Старый уровень
     * @param newLevel Новый уровень
     * @param result Результат для заполнения данными о повышении уровня
     */
    void handleLevelUp(int characterId, int oldLevel, int newLevel, ExperienceGrantResult &result);

    /**
     * @brief Константы для расчета опыта
     */
    static constexpr int BASE_EXP_PER_LEVEL = 100;       // Базовый опыт для 1 уровня
    static constexpr double EXP_MULTIPLIER = 1.2;        // Множитель роста опыта за уровень
    static constexpr double DEATH_PENALTY_PERCENT = 0.1; // 10% штраф при смерти
    static constexpr int MAX_LEVEL = 100;                // Максимальный уровень
};
