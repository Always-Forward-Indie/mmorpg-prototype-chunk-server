#pragma once

#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"
#include "network/GameServerWorker.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"

/**
 * @brief Обработчик событий опыта
 */
class ExperienceEventHandler : public BaseEventHandler
{
  public:
    ExperienceEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /**
     * @brief Обработать событие начисления опыта
     */
    void handleExperienceGrantEvent(const Event &event);

    /**
     * @brief Обработать событие снятия опыта
     */
    void handleExperienceRemoveEvent(const Event &event);

    /**
     * @brief Обработать событие обновления опыта (отправка клиентам)
     */
    void handleExperienceUpdateEvent(const Event &event);

    /**
     * @brief Обработать событие повышения уровня
     */
    void handleLevelUpEvent(const Event &event);
};
