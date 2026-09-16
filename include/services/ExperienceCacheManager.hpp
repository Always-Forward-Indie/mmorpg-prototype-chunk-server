#pragma once

#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <memory>
#include <mutex>
#include <shared_mutex>

// Forward declarations (none needed: no external services).

/**
 * @brief Менеджер кеша таблицы опыта
 * Загружает и кеширует таблицу опыта с гейм-сервера при инициализации.
 * Explicit dependencies (no GameServices): requests to the game-server are
 * sent by the caller through GameServerWorker, never from here.
 */
class ExperienceCacheManager
{
  public:
    explicit ExperienceCacheManager(Logger &logger);

    /**
     * @brief Инициализация менеджера - запрос таблицы опыта с гейм-сервера
     */
    void initialize();

    /**
     * @brief Загрузить таблицу опыта с гейм-сервера
     */
    void loadExperienceTableFromGameServer();

    /**
     * @brief Установить данные таблицы опыта (вызывается при получении ответа от гейм-сервера)
     */
    void setExperienceTable(const std::vector<ExperienceLevelEntry> &entries);

    /**
     * @brief Получить опыт для указанного уровня
     */
    int getExperienceForLevel(int level) const;

    /**
     * @brief Получить максимальный доступный уровень
     */
    int getMaxLevel() const;

    /**
     * @brief Проверить, загружена ли таблица опыта
     */
    bool isTableLoaded() const;

    /**
     * @brief Получить количество записей в таблице
     */
    size_t getTableSize() const;

    /**
     * @brief Обновить таблицу с гейм-сервера (если нужно периодическое обновление)
     */
    void refreshFromGameServer();

    /**
     * @brief Очистить кеш
     */
    void clearCache();

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
    mutable std::shared_mutex mutex_;
    ExperienceLevelTable experienceTable_;

    /**
     * @brief Создать запрос к гейм-серверу для получения таблицы опыта
     */
    std::string createGetExpTableRequest() const;
};
