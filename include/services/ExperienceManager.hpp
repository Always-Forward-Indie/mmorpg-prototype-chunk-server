#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <cmath>
#include <functional>
#include <vector>

// Forward declaration
class GameServices;

/**
 * @brief Менеджер опыта, отвечающий за начисление/снятие опыта и управление уровнями
 */
class ExperienceManager
{
  public:
    // Constructor
    ExperienceManager(GameServices *gameServices);

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
     * @param mobLevel Уровень моба
     * @param characterLevel Уровень персонажа
     * @param baseExperience Базовый опыт моба
     * @return Количество опыта
     */
    int calculateMobExperience(int mobLevel, int characterLevel, int baseExperience);

    /**
     * @brief Вычислить штраф опыта при смерти
     * @param characterLevel Уровень персонажа
     * @param currentExperience Текущий опыт персонажа
     * @return Количество опыта к снятию
     */
    int calculateDeathPenalty(int characterLevel, int currentExperience);

    /**
     * @brief Получить количество опыта, требуемое для достижения определенного уровня
     * @param level Целевой уровень
     * @return Общее количество опыта для достижения уровня
     */
    int getExperienceForLevel(int level);

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
     * @brief Установить callback для отправки пакетов обновления статов
     * @param callback Функция для отправки пакетов
     */
    void setStatsUpdatePacketCallback(std::function<void(const nlohmann::json &)> callback);

  private:
    GameServices *gameServices_;
    std::function<void(const nlohmann::json &)> experiencePacketCallback_;
    std::function<void(const nlohmann::json &)> statsUpdatePacketCallback_;

    /**
     * @brief Отправить пакет об изменении опыта
     * @param experienceEvent Событие изменения опыта
     */
    void sendExperiencePacket(const ExperienceEventStruct &experienceEvent);

    /**
     * @brief Отправить пакет обновления статов
     * @param characterId ID персонажа
     */
    void sendStatsUpdatePacket(int characterId);

    /**
     * @brief Построить JSON пакет с данными об опыте
     * @param experienceEvent Событие изменения опыта
     * @return JSON пакет
     */
    nlohmann::json buildExperiencePacket(const ExperienceEventStruct &experienceEvent);

    /**
     * @brief Построить JSON пакет обновления статов
     * @param characterData Данные персонажа
     * @param requestId ID запроса
     * @return JSON пакет
     */
    nlohmann::json buildStatsUpdatePacket(const CharacterDataStruct &characterData, const std::string &requestId);

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
