#pragma once

#include "events/handlers/BaseEventHandler.hpp"
#include <spdlog/logger.h>

/**
 * @brief Handles emote / animation events.
 *
 * Responsibilities:
 *  - SET_EMOTE_DEFINITIONS  — load global emote catalog from game-server payload
 *  - SET_PLAYER_EMOTES      — load per-character unlocked emotes and push list to client
 *  - USE_EMOTE              — validate unlock, broadcast emoteAction to all zone clients
 */
class EmoteEventHandler : public BaseEventHandler
{
  public:
    EmoteEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    /// Handles SET_EMOTE_DEFINITIONS (game-server → chunk-server on connect)
    void handleSetEmoteDefinitionsEvent(const Event &event);

    /// Handles SET_PLAYER_EMOTES (game-server → chunk-server on character join)
    void handleSetPlayerEmotesEvent(const Event &event);

    /// Handles USE_EMOTE (client → chunk-server)
    void handleUseEmoteEvent(const Event &event);

    /// Called by CharacterEventHandler on disconnect to clean up per-character state.
    void onPlayerDisconnect(int characterId);

  private:
    /// Build and return the player_emotes JSON packet for a character.
    nlohmann::json buildPlayerEmotesPacket(int characterId) const;

    std::shared_ptr<spdlog::logger> log_;
};
