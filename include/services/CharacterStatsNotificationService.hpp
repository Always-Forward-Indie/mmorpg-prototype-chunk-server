#ifndef CHARACTER_STATS_NOTIFICATION_SERVICE_HPP
#define CHARACTER_STATS_NOTIFICATION_SERVICE_HPP

#include <functional>
#include <nlohmann/json.hpp>

// Forward declarations
namespace spdlog
{
class logger;
}
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
     * @brief Send a world notification to a specific character.
     *
     * Broadcasts a `world_notification` packet via the shared stats callback.
     * Clients filter by `characterId` in the body.
     *
     * @param characterId     Target character
     * @param notificationType Short tag identifying the notification (e.g. "fellowship_bonus")
     * @param text            Human-readable text to display
     * @param data            Optional extra JSON payload (default: empty object)
     */
    void sendWorldNotification(int characterId,
        const std::string &notificationType,
        const std::string &text,
        const nlohmann::json &data = nlohmann::json::object());

    /**
     * @brief Send a world notification to all players currently inside a game zone.
     *
     * Iterates all connected characters, checks their position against the
     * GameZoneManager AABB, and sends a personal notification to each match.
     * O(n_players) — acceptable for prototype scale.
     *
     * @param gameZoneId      Target game zone (zones.id)
     * @param notificationType Short machine-readable type string
     * @param text            Human-readable announcement text
     * @param data            Optional extra JSON payload
     */
    void sendWorldNotificationToGameZone(int gameZoneId,
        const std::string &notificationType,
        const std::string &text,
        const nlohmann::json &data = nlohmann::json::object());

    /**
     * @brief Set the callback function for sending stats update packets
     * @param callback Function to call when sending stats updates
     */
    void setStatsUpdateCallback(std::function<void(const nlohmann::json &)> callback);

  private:
    GameServices *gameServices_;
    std::shared_ptr<spdlog::logger> log_;
    std::function<void(const nlohmann::json &)> statsUpdateCallback_;

    /**
     * @brief Build the stats update packet JSON
     * @param characterId The character ID
     * @return JSON packet with stats data
     */
    nlohmann::json buildStatsUpdatePacket(int characterId);
};

#endif // CHARACTER_STATS_NOTIFICATION_SERVICE_HPP
