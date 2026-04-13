#ifndef CHARACTER_STATS_NOTIFICATION_SERVICE_HPP
#define CHARACTER_STATS_NOTIFICATION_SERVICE_HPP

#include <atomic>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

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
     * @brief Send a stats update packet with an optional source tag.
     * @param characterId The ID of the character whose stats changed
     * @param source      Free-form string identifying the cause (e.g. "regen")
     */
    void sendStatsUpdate(int characterId, const std::string &source);

    /**
     * @brief Send a world notification to a specific character.
     *
     * Packet fields: notificationType, priority, channel, notificationId (auto),
     * data. The `text` field in the packet is always empty — clients localise
     * all displayed text via the notificationType + data + locale files.
     *
     * @param characterId      Target character
     * @param notificationType Machine-readable type (e.g. "fellowship_bonus")
     * @param data             Extra JSON payload (default: empty object)
     * @param priority         Display priority: critical|high|medium|low|ambient
     * @param channel          Render target: screen_center|toast|float_text|atmosphere|chat_log
     */
    void sendWorldNotification(int characterId,
        const std::string &notificationType,
        const nlohmann::json &data = nlohmann::json::object(),
        const std::string &priority = "medium",
        const std::string &channel = "toast");

    /**
     * @brief Send a world notification to all players currently inside a game zone.
     *
     * Iterates all connected characters, checks their position against the
     * GameZoneManager AABB, and sends a personal notification to each match.
     * O(n_players) — acceptable for prototype scale.
     *
     * @param gameZoneId       Target game zone (zones.id)
     * @param notificationType Machine-readable type string
     * @param data             Optional extra JSON payload
     * @param priority         Display priority
     * @param channel          Render target
     */
    void sendWorldNotificationToGameZone(int gameZoneId,
        const std::string &notificationType,
        const nlohmann::json &data = nlohmann::json::object(),
        const std::string &priority = "medium",
        const std::string &channel = "toast");

    /**
     * @brief Set the callback function for sending stats update packets
     * @param callback Function to call when sending stats updates
     */
    void setStatsUpdateCallback(std::function<void(const nlohmann::json &)> callback);

    /**
     * @brief Set a direct per-character send callback used by sendWorldNotification.
     *
     * When set, world notifications are sent only to the target character's socket
     * instead of being broadcast to every client in the zone.
     *
     * @param callback  (characterId, packet) → sends packet to that character only
     */
    void setDirectSendCallback(std::function<void(int, const nlohmann::json &)> callback);

  private:
    GameServices *gameServices_;
    std::shared_ptr<spdlog::logger> log_;
    std::function<void(const nlohmann::json &)> statsUpdateCallback_;
    std::function<void(int, const nlohmann::json &)> directSendCallback_;

    /// Auto-incrementing sequence for notificationId (per server lifetime).
    std::atomic<uint64_t> notifSeq_{0};

    /**
     * @brief Build the stats update packet JSON
     * @param characterId The character ID
     * @return JSON packet with stats data
     */
    nlohmann::json buildStatsUpdatePacket(int characterId);
};

#endif // CHARACTER_STATS_NOTIFICATION_SERVICE_HPP
