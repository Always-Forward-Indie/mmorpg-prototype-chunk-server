#ifndef CHARACTER_STATS_NOTIFICATION_SERVICE_HPP
#define CHARACTER_STATS_NOTIFICATION_SERVICE_HPP

#include <functional>
#include <nlohmann/json.hpp>

// Forward declarations
class GameServices;

/**
 * @brief Service responsible for sending character stats update notifications
 *
 * This service handles sending stats_update packets to clients when character
 * statistics (health, mana, experience, etc.) change. It can be used by any
 * system that needs to notify clients about stat changes.
 */
class CharacterStatsNotificationService
{
  public:
    explicit CharacterStatsNotificationService(GameServices *gameServices);
    ~CharacterStatsNotificationService() = default;

    /**
     * @brief Send a stats update packet for the specified character
     * @param characterId The ID of the character whose stats changed
     */
    void sendStatsUpdate(int characterId);

    /**
     * @brief Set the callback function for sending stats update packets
     * @param callback Function to call when sending stats updates
     */
    void setStatsUpdateCallback(std::function<void(const nlohmann::json &)> callback);

  private:
    GameServices *gameServices_;
    std::function<void(const nlohmann::json &)> statsUpdateCallback_;

    /**
     * @brief Build the stats update packet JSON
     * @param characterId The character ID
     * @return JSON packet with stats data
     */
    nlohmann::json buildStatsUpdatePacket(int characterId);
};

#endif // CHARACTER_STATS_NOTIFICATION_SERVICE_HPP
