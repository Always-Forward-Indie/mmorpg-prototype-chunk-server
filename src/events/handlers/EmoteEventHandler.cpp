#include "events/handlers/EmoteEventHandler.hpp"
#include "network/NetworkManager.hpp"
#include "services/EmoteManager.hpp"
#include "services/GameServices.hpp"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

EmoteEventHandler::EmoteEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "emote")
{
    log_ = gameServices_.getLogger().getSystem("emote");
    log_->info("EmoteEventHandler initialized");
}

// ── SET_EMOTE_DEFINITIONS ──────────────────────────────────────────────────

void
EmoteEventHandler::handleSetEmoteDefinitionsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("[Emote] SET_EMOTE_DEFINITIONS: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);

        std::vector<EmoteDefinitionStruct> defs;
        if (body.contains("emotes") && body["emotes"].is_array())
        {
            for (const auto &e : body["emotes"])
            {
                EmoteDefinitionStruct def;
                def.id = e.value("id", 0);
                def.slug = e.value("slug", "");
                def.displayName = e.value("displayName", "");
                def.animationName = e.value("animationName", "");
                def.category = e.value("category", "general");
                def.isDefault = e.value("isDefault", false);
                def.sortOrder = e.value("sortOrder", 0);
                if (!def.slug.empty())
                    defs.push_back(std::move(def));
            }
        }

        gameServices_.getEmoteManager().loadEmoteDefinitions(defs);
        log_->info("[Emote] SET_EMOTE_DEFINITIONS: loaded {} definitions", defs.size());
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError(
            "[Emote] Error processing SET_EMOTE_DEFINITIONS: " + std::string(ex.what()));
    }
}

// ── SET_PLAYER_EMOTES ──────────────────────────────────────────────────────

void
EmoteEventHandler::handleSetPlayerEmotesEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("[Emote] SET_PLAYER_EMOTES: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("[Emote] SET_PLAYER_EMOTES: missing characterId");
            return;
        }

        std::vector<std::string> unlocked;
        if (body.contains("emotes") && body["emotes"].is_array())
        {
            for (const auto &s : body["emotes"])
                if (s.is_string())
                    unlocked.push_back(s.get<std::string>());
        }

        gameServices_.getEmoteManager().loadPlayerEmotes(characterId, unlocked);
        log_->info("[Emote] SET_PLAYER_EMOTES: loaded {} emotes for char={}", unlocked.size(), characterId);

        // Push emote list to client immediately (same pattern as SET_PLAYER_INVENTORY)
        auto clientData = gameServices_.getClientManager().getClientDataByCharacterId(characterId);
        if (clientData.clientId <= 0)
            return;
        auto sock = gameServices_.getClientManager().getClientSocket(clientData.clientId);
        if (!sock || !sock->is_open())
            return;

        nlohmann::json packet = buildPlayerEmotesPacket(characterId);
        networkManager_.sendResponse(sock,
            networkManager_.generateResponseMessage("success", packet));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError(
            "[Emote] Error processing SET_PLAYER_EMOTES: " + std::string(ex.what()));
    }
}

// ── USE_EMOTE ──────────────────────────────────────────────────────────────

void
EmoteEventHandler::handleUseEmoteEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<UseEmoteRequestStruct>(data))
        {
            log_->error("[Emote] USE_EMOTE: unexpected data type");
            return;
        }
        const auto &req = std::get<UseEmoteRequestStruct>(data);

        if (req.characterId <= 0 || req.emoteSlug.empty())
        {
            log_->warn("[Emote] USE_EMOTE: invalid request char={} slug='{}'",
                req.characterId,
                req.emoteSlug);
            return;
        }

        // Server-authoritative validation: character must own the emote
        if (!gameServices_.getEmoteManager().isUnlocked(req.characterId, req.emoteSlug))
        {
            log_->warn("[Emote] USE_EMOTE: char={} attempted unlocked emote '{}'",
                req.characterId,
                req.emoteSlug);
            auto sock = gameServices_.getClientManager().getClientSocket(req.clientId);
            if (sock && sock->is_open())
            {
                nlohmann::json errPkt;
                errPkt["header"]["eventType"] = "emoteError";
                errPkt["header"]["status"] = "error";
                errPkt["body"]["message"] = "emote_not_unlocked";
                networkManager_.sendResponse(sock,
                    networkManager_.generateResponseMessage("error", errPkt));
            }
            return;
        }

        // Also validate the emote definition exists
        EmoteDefinitionStruct def =
            gameServices_.getEmoteManager().getEmoteDefinition(req.emoteSlug);
        if (def.slug.empty())
        {
            log_->warn("[Emote] USE_EMOTE: unknown emote slug '{}' for char={}",
                req.emoteSlug,
                req.characterId);
            return;
        }

        // Broadcast emote action to ALL clients in the zone (including the sender — client
        // can optimistically play locally, but authoritative broadcast confirms it)
        nlohmann::json broadcast;
        broadcast["header"]["eventType"] = "emoteAction";
        broadcast["header"]["status"] = "success";
        broadcast["body"]["characterId"] = req.characterId;
        broadcast["body"]["emoteSlug"] = req.emoteSlug;
        broadcast["body"]["animationName"] = def.animationName;
        broadcast["body"]["serverTimestamp"] = req.timestamps.serverRecvMs;

        broadcastToAllClients(
            networkManager_.generateResponseMessage("success", broadcast));

        log_->info("[Emote] char={} plays emote '{}'", req.characterId, req.emoteSlug);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError(
            "[Emote] Error processing USE_EMOTE: " + std::string(ex.what()));
    }
}

// ── Player disconnect ──────────────────────────────────────────────────────

void
EmoteEventHandler::onPlayerDisconnect(int characterId)
{
    gameServices_.getEmoteManager().unloadPlayerEmotes(characterId);
}

// ── Private helpers ────────────────────────────────────────────────────────

nlohmann::json
EmoteEventHandler::buildPlayerEmotesPacket(int characterId) const
{
    auto emotes = gameServices_.getEmoteManager().getPlayerEmotes(characterId);

    nlohmann::json emotesArr = nlohmann::json::array();
    for (const auto &e : emotes)
    {
        nlohmann::json entry;
        entry["slug"] = e.slug;
        entry["displayName"] = e.displayName;
        entry["animationName"] = e.animationName;
        entry["category"] = e.category;
        entry["isDefault"] = e.isDefault;
        entry["sortOrder"] = e.sortOrder;
        emotesArr.push_back(std::move(entry));
    }

    nlohmann::json pkt;
    pkt["header"]["eventType"] = "player_emotes";
    pkt["header"]["status"] = "success";
    pkt["body"]["characterId"] = characterId;
    pkt["body"]["emotes"] = emotesArr;
    return pkt;
}
