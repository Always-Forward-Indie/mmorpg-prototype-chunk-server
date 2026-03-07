#pragma once
#include "utils/Logger.hpp"
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

/**
 * @brief Сервис геймплейной конфигурации на стороне chunk-server.
 *
 * Получает конфиг от game-server при старте (setConfig) и при runtime-reload.
 * Предоставляет типизированные геттеры с дефолтным значением — никогда не бросает.
 *
 * Потокобезопасен: setConfig блокирует на запись,
 * get* блокируют на чтение (shared_mutex).
 *
 * Пример использования:
 *   float k   = config.getFloat("combat.defense_formula_k", 7.5f);
 *   int   cap = config.getInt("aggro.base_radius", 500);
 */
class GameConfigService
{
  public:
    explicit GameConfigService(Logger &logger);

    /**
     * @brief Принять конфиг, пришедший от game-server (setGameConfig event).
     * @param config key→value map (значения — строки, тип закодирован на DB стороне)
     */
    void setConfig(const std::unordered_map<std::string, std::string> &config);

    // ----------------------------------------------------------------
    // Типизированные геттеры. Все возвращают defaultValue при отсутствии ключа
    // или ошибке конвертации — сервер не должен падать из-за конфига.
    // ----------------------------------------------------------------

    float getFloat(const std::string &key, float defaultValue = 0.0f) const;
    int getInt(const std::string &key, int defaultValue = 0) const;
    bool getBool(const std::string &key, bool defaultValue = false) const;
    std::string getString(const std::string &key, const std::string &defaultValue = "") const;

    /** @brief Проверить, загружен ли конфиг (getAll вернёт непустой map). */
    bool isLoaded() const;

  private:
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> config_;
};
