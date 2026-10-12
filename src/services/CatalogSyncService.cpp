#include "services/CatalogSyncService.hpp"
#include "services/ReputationManager.hpp"
#include "events/handlers/CharacterEventHandler.hpp"
#include "events/handlers/CombatEventHandler.hpp"
#include "events/handlers/EmoteEventHandler.hpp"
#include "events/handlers/EquipmentEventHandler.hpp"
#include "events/handlers/SkillEventHandler.hpp"
#include "services/CombatSystem.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TerminalColors.hpp"
#include <spdlog/logger.h>

CatalogSyncService::CatalogSyncService(
    NetworkManager &networkManager,
    GameServices &gameServices)
    : networkManager_(networkManager), gameServices_(gameServices)
{
    log_ = gameServices_.getLogger().getSystem("catalog");
}

void
CatalogSyncService::handleSetExpLevelTableEvent(const Event &event)
{
    try
    {
        log_->info("Processing SET_EXP_LEVEL_TABLE event");

        const auto &data = event.getData();

        if (std::holds_alternative<std::vector<ExperienceLevelEntry>>(data))
        {
            std::vector<ExperienceLevelEntry> expLevelTable = std::get<std::vector<ExperienceLevelEntry>>(data);

            gameServices_.getLogger().log("Received experience level table with " +
                                              std::to_string(expLevelTable.size()) + " entries",
                GREEN);

            // Передаем данные в ExperienceCacheManager
            gameServices_.getExperienceCacheManager().setExperienceTable(expLevelTable);

            log_->info("Experience level table successfully loaded into cache");
        }
        else
        {
            log_->error("Invalid data type in SET_EXP_LEVEL_TABLE event");
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_EXP_LEVEL_TABLE event: " + std::string(e.what()));
    }
    bumpVersion();
}

void
CatalogSyncService::handleSetGameConfigEvent(const Event &event)
{
    try
    {
        log_->info("Processing SET_GAME_CONFIG event");

        const auto &data = event.getData();

        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_GAME_CONFIG: unexpected data type");
            return;
        }

        const auto &body = std::get<nlohmann::json>(data);

        if (!body.contains("configList") || !body["configList"].is_array())
        {
            log_->error("SET_GAME_CONFIG: missing or invalid 'configList' in body");
            return;
        }

        std::unordered_map<std::string, std::string> configMap;
        for (const auto &entry : body["configList"])
        {
            if (entry.contains("key") && entry.contains("value") &&
                entry["key"].is_string() && entry["value"].is_string())
            {
                configMap[entry["key"].get<std::string>()] = entry["value"].get<std::string>();
            }
        }

        gameServices_.getGameConfigService().setConfig(configMap);

        // Push bestiary tier thresholds to BestiaryManager
        const auto &cfg = gameServices_.getGameConfigService();
        gameServices_.getBestiaryManager().setThresholds({cfg.getInt("bestiary.tier1_kills", 1),
            cfg.getInt("bestiary.tier2_kills", 5),
            cfg.getInt("bestiary.tier3_kills", 15),
            cfg.getInt("bestiary.tier4_kills", 30),
            cfg.getInt("bestiary.tier5_kills", 75),
            cfg.getInt("bestiary.tier6_kills", 150)});

        // Push reputation tier thresholds (single tunable ladder)
        ReputationManager::setThresholds(cfg.getInt("reputation.enemy_threshold", -500),
            cfg.getInt("reputation.neutral_threshold", 0),
            cfg.getInt("reputation.friendly_threshold", 200),
            cfg.getInt("reputation.ally_threshold", 500));

        gameServices_.getLogger().log("Game config loaded: " +
                                          std::to_string(configMap.size()) + " entries.",
            GREEN);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_GAME_CONFIG event: " + std::string(e.what()));
    }
    bumpVersion();
}

void
CatalogSyncService::handleSetPlayerActiveEffectsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<ActiveEffectStruct>>(data))
        {
            log_->error("SET_PLAYER_ACTIVE_EFFECTS: unexpected data type");
            return;
        }

        auto effects = std::get<std::vector<ActiveEffectStruct>>(data);
        // Game server sets header clientId = characterId (same convention as inventory)
        int characterId = event.getClientID();

        if (characterId <= 0)
        {
            log_->error("[EH] SET_PLAYER_ACTIVE_EFFECTS: invalid characterId " +
                        std::to_string(characterId));
            return;
        }

        gameServices_.getCharacterManager().setCharacterActiveEffects(characterId, std::move(effects));

        // Re-apply permanent passive-skill effects so they survive every reload.
        const auto skills = gameServices_.getCharacterManager().getCharacterSkills(characterId);
        for (const auto &skill : skills)
        {
            if (!skill.isPassive)
                continue;
            for (const auto &ed : skill.effects)
            {
                ActiveEffectStruct eff;
                eff.effectSlug = ed.effectSlug;
                eff.effectTypeSlug = ed.effectTypeSlug;
                eff.attributeSlug = ed.attributeSlug;
                eff.value = ed.value;
                eff.modifierType = ed.modifierType;
                eff.sourceType = "skill_passive";
                eff.expiresAt = 0; // permanent
                eff.tickMs = 0;    // passives never tick
                gameServices_.getCharacterManager().addActiveEffect(characterId, eff);
            }
        }

        // Re-apply equipped title bonuses — may have been wiped by the setCharacterActiveEffects() call above.
        gameServices_.getTitleManager().reapplyEquippedBonuses(characterId);

        // Re-apply permanent mastery milestone effects — mastery values are loaded
        // from DB via SET_PLAYER_MASTERIES but the corresponding ActiveEffects (damage
        // bonus, crit, parry) are computed in-memory and not persisted to the
        // active_effects table.  Re-apply them on every login so they survive server
        // restarts and player reconnects.
        gameServices_.getMasteryManager().reapplyMilestoneEffects(characterId);

        // Refresh HUD: active effects may modify attributes (stat buffs/debuffs).
        gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);

        log_->info("[EH] SET_PLAYER_ACTIVE_EFFECTS: stored effects for character " +
                   std::to_string(characterId));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_ACTIVE_EFFECTS event: " + std::string(e.what()));
    }
    bumpVersion();
}

void
CatalogSyncService::handleSetPlayerSkillCooldownsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_PLAYER_SKILL_COOLDOWNS: unexpected data type");
            return;
        }

        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("[EH] SET_PLAYER_SKILL_COOLDOWNS: invalid characterId " + std::to_string(characterId));
            return;
        }

        const auto &cooldownsArr = body["cooldowns"];
        if (!cooldownsArr.is_array() || cooldownsArr.empty())
        {
            // No active cooldowns — nothing to restore.
            return;
        }

        // Restore each cooldown into SkillSystem so the server correctly rejects
        // skill-use attempts while the cooldown is still active.
        if (!combatEventHandler_)
        {
            log_->error("[EH] SET_PLAYER_SKILL_COOLDOWNS: CombatEventHandler not wired");
            return;
        }
        auto *combatSystem = combatEventHandler_->getCombatSystem();
        if (!combatSystem)
        {
            log_->error("[EH] SET_PLAYER_SKILL_COOLDOWNS: CombatSystem not available");
            return;
        }

        nlohmann::json cdJson = nlohmann::json::array();
        for (const auto &cd : cooldownsArr)
        {
            std::string skillSlug = cd.value("skillSlug", std::string(""));
            int64_t remainingMs = cd.value("remainingMs", int64_t(0));
            if (skillSlug.empty() || remainingMs <= 0)
                continue;

            combatSystem->restoreSkillCooldown(characterId, skillSlug, remainingMs);

            nlohmann::json entry;
            entry["skillSlug"] = skillSlug;
            entry["remainingMs"] = remainingMs;
            cdJson.push_back(std::move(entry));
        }

        // Notify the game client about active cooldowns so it can grey out skill buttons.
        if (!cdJson.empty())
        {
            auto clientData = gameServices_.getClientManager().getClientDataByCharacterId(characterId);
            if (clientData.clientId > 0)
            {
                auto clientSocket = gameServices_.getClientManager().getClientSocket(clientData.clientId);
                if (clientSocket)
                {
                    nlohmann::json packet;
                    packet["header"]["eventType"] = "setSkillCooldowns";
                    packet["header"]["clientId"] = clientData.clientId;
                    packet["header"]["message"] = "active skill cooldowns";
                    packet["body"]["characterId"] = characterId;
                    packet["body"]["cooldowns"] = cdJson;
                    std::string responseData = networkManager_.generateResponseMessage("success", packet);
                    networkManager_.sendResponse(clientSocket, responseData);
                }
            }
        }

        log_->info("[EH] SET_PLAYER_SKILL_COOLDOWNS: restored " + std::to_string(cdJson.size()) +
                   " cooldowns for character " + std::to_string(characterId));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_SKILL_COOLDOWNS event: " + std::string(e.what()));
    }
    bumpVersion();
}

void
CatalogSyncService::handleSetPlayerInventoryEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<PlayerInventoryItemStruct>>(data))
        {
            log_->error("SET_PLAYER_INVENTORY: unexpected data type");
            return;
        }
        const auto &items = std::get<std::vector<PlayerInventoryItemStruct>>(data);
        int characterId = event.getClientID(); // game server sets clientId = characterId

        gameServices_.getInventoryManager().loadPlayerInventory(characterId, items);

        // Build equipment state from the freshly loaded inventory
        gameServices_.getEquipmentManager().buildFromInventory(characterId);

        log_->info("[EH] SET_PLAYER_INVENTORY: loaded " + std::to_string(items.size()) +
                   " items for character " + std::to_string(characterId));

        // Immediately push inventory to client to avoid race: client may have already
        // requested inventory before this DB response arrived (gets empty list).
        auto characters = gameServices_.getCharacterManager().getCharactersList();
        int clientId = 0;
        for (const auto &c : characters)
        {
            if (c.characterId == characterId)
            {
                clientId = c.clientId;
                break;
            }
        }

        if (clientId <= 0)
        {
            log_->info("[EH] SET_PLAYER_INVENTORY: character " + std::to_string(characterId) +
                       " has no active client yet, skipping push");
            return;
        }

        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!clientSocket || !clientSocket->is_open())
        {
            log_->error("[EH] SET_PLAYER_INVENTORY: socket not available for clientId " +
                        std::to_string(clientId));
            return;
        }

        nlohmann::json itemsArray = nlohmann::json::array();
        for (const auto &item : items)
        {
            itemsArray.push_back(gameServices_.getInventoryManager().inventoryItemToJson(item));
        }

        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Inventory retrieved successfully!")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientId)
                                      .setHeader("eventType", "getPlayerInventory")
                                      .setBody("characterId", characterId)
                                      .setBody("items", itemsArray)
                                      .build();

        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        log_->info("[EH] SET_PLAYER_INVENTORY: pushed " + std::to_string(items.size()) +
                   " items to clientId=" + std::to_string(clientId) +
                   " (characterId=" + std::to_string(characterId) + ")");

        // Send current equipment state alongside inventory so the client
        // can display equipped items immediately on character join.
        // (Peer wired by EventHandler; missing peer degrades to inventory-only.)
        if (equipmentEventHandler_)
        {
            equipmentEventHandler_->sendEquipmentState(clientId, characterId, TimestampStruct{});

            // Send carry weight so the client can display currentWeight/weightLimit
            // in the inventory UI immediately on connect.
            equipmentEventHandler_->sendWeightStatus(clientId, characterId);
        }
        else
        {
            log_->error("[EH] SET_PLAYER_INVENTORY: EquipmentEventHandler not wired");
        }

        // Send full stats snapshot: inventory is now loaded so effective attributes
        // (base + equipment bonuses) and weight are calculated correctly.
        gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);

        // Notify all OTHER clients about this character's equipment so they can
        // render the correct gear visuals on this character's model.
        // Only broadcast once the joining client has sent playerReady — otherwise
        // the other clients haven't received the joinGameCharacter packet yet and
        // would try to render equipment on a character that doesn't exist in their
        // scene. If playerReady hasn't arrived yet, handlePlayerReadyEvent performs
        // this broadcast as a catch-up step (step 5, after pushConnectedCharactersToClient).
        if (gameServices_.getClientManager().isClientWorldReady(clientId))
        {
            if (equipmentEventHandler_)
                equipmentEventHandler_->broadcastEquipmentUpdate(characterId, clientId);
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_INVENTORY event: " + std::string(e.what()));
    }
    bumpVersion();
}

void
CatalogSyncService::handleSetCharacterAttributesRefreshEvent(const Event &event)
{
    using PairType = std::pair<int, std::vector<CharacterAttributeStruct>>;
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<PairType>(data))
        {
            log_->error("SET_CHARACTER_ATTRIBUTES_REFRESH: unexpected data type");
            return;
        }

        auto [characterId, attributes] = std::get<PairType>(data);

        if (characterId <= 0)
        {
            log_->error("[EH] SET_CHARACTER_ATTRIBUTES_REFRESH: invalid characterId");
            return;
        }

        // Replace stored attributes with fresh values from DB (includes equip bonuses & perm mods)
        gameServices_.getCharacterManager().replaceCharacterAttributes(characterId, attributes);

        // Find the client socket for this character so we can notify it
        auto characters = gameServices_.getCharacterManager().getCharactersList();
        int clientId = 0;
        for (const auto &c : characters)
        {
            if (c.characterId == characterId)
            {
                clientId = c.clientId;
                break;
            }
        }

        if (clientId <= 0)
        {
            log_->info("[EH] SET_CHARACTER_ATTRIBUTES_REFRESH: character " +
                       std::to_string(characterId) + " has no active client, skipping push");
            return;
        }

        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!clientSocket || !clientSocket->is_open())
        {
            log_->error("[EH] SET_CHARACTER_ATTRIBUTES_REFRESH: socket not available for clientId " +
                        std::to_string(clientId));
            return;
        }

        // Build charAttributesUpdate packet for the game client
        nlohmann::json attrsJson = nlohmann::json::array();
        for (const auto &attr : attributes)
        {
            nlohmann::json entry;
            entry["id"] = attr.id;
            entry["name"] = attr.name;
            entry["slug"] = attr.slug;
            entry["value"] = attr.value;
            attrsJson.push_back(std::move(entry));
        }

        nlohmann::json response = ResponseBuilder()
                                      .setHeader("message", "Attributes updated")
                                      .setHeader("hash", "")
                                      .setHeader("clientId", clientId)
                                      .setHeader("eventType", "charAttributesUpdate")
                                      .setBody("characterId", characterId)
                                      .setBody("attributesData", attrsJson)
                                      .build();

        networkManager_.sendResponse(clientSocket,
            networkManager_.generateResponseMessage("success", response));

        gameServices_.getLogger().log("[EH] Pushed " + std::to_string(attrsJson.size()) +
                                          " refreshed attributes to clientId=" + std::to_string(clientId) +
                                          " (characterId=" + std::to_string(characterId) + ")",
            GREEN);

        // The recomputed base values change effective stats (equip/set
        // bonuses, durability tiers): push a fresh stats_update so the
        // client (and its UI) learns the new effective values promptly
        // instead of waiting for the next incidental stats trigger.
        gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_CHARACTER_ATTRIBUTES_REFRESH event: " + std::string(e.what()));
    }
    bumpVersion();
}

void
CatalogSyncService::handleSetStatusEffectTemplatesEvent(const Event &event)
{
    try
    {
        log_->info("Processing SET_STATUS_EFFECT_TEMPLATES event");

        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<StatusEffectTemplate>>(data))
        {
            log_->error("SET_STATUS_EFFECT_TEMPLATES: unexpected data type");
            return;
        }

        const auto &templates = std::get<std::vector<StatusEffectTemplate>>(data);
        gameServices_.getStatusEffectTemplateManager().loadTemplates(templates);

        gameServices_.getLogger().log(
            "Status effect templates loaded: " + std::to_string(templates.size()) + " entries.", GREEN);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError(
            "Error processing SET_STATUS_EFFECT_TEMPLATES event: " + std::string(e.what()));
    }
    bumpVersion();
}

// ── SET_PLAYER_PITY ────────────────────────────────────────────────────────

void
CatalogSyncService::handleSetPlayerPityEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_PLAYER_PITY: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("SET_PLAYER_PITY: missing characterId");
            return;
        }

        std::vector<std::pair<int, int>> counters;
        if (body.contains("entries") && body["entries"].is_array())
        {
            for (const auto &e : body["entries"])
                counters.emplace_back(e.value("itemId", 0), e.value("killCount", 0));
        }

        gameServices_.getPityManager().loadPityData(characterId, counters);
        log_->info("[EH] SET_PLAYER_PITY: loaded " + std::to_string(counters.size()) +
                   " counters for character " + std::to_string(characterId));
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_PITY: " + std::string(e.what()));
    }
    bumpVersion();
}

// ── SET_PLAYER_BESTIARY ────────────────────────────────────────────────────

void
CatalogSyncService::handleSetPlayerBestiaryEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_PLAYER_BESTIARY: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("SET_PLAYER_BESTIARY: missing characterId");
            return;
        }

        std::vector<std::pair<int, int>> kills;
        if (body.contains("entries") && body["entries"].is_array())
        {
            for (const auto &e : body["entries"])
                kills.emplace_back(e.value("mobTemplateId", 0), e.value("killCount", 0));
        }

        gameServices_.getBestiaryManager().loadBestiaryData(characterId, kills);
        log_->info("[EH] SET_PLAYER_BESTIARY: loaded " + std::to_string(kills.size()) +
                   " bestiary entries for character " + std::to_string(characterId));

        // Auto-push overview to the client so it knows which mobs are discovered
        int clientId = event.getClientID();
        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (clientSocket && clientSocket->is_open())
        {
            auto knownMobs = gameServices_.getBestiaryManager().getKnownMobs(characterId);
            nlohmann::json entries = nlohmann::json::array();
            for (const auto &[mobTemplateId, killCount] : knownMobs)
            {
                const std::string slug = gameServices_.getMobManager().getMobById(mobTemplateId).slug;
                if (slug.empty())
                    continue;
                nlohmann::json e;
                e["mobSlug"] = slug;
                e["killCount"] = killCount;
                entries.push_back(std::move(e));
            }

            nlohmann::json response = ResponseBuilder()
                                          .setHeader("message", "Bestiary overview")
                                          .setHeader("hash", "")
                                          .setHeader("clientId", clientId)
                                          .setHeader("eventType", "getBestiaryOverview")
                                          .setBody("characterId", characterId)
                                          .setBody("entries", entries)
                                          .build();

            networkManager_.sendResponse(clientSocket,
                networkManager_.generateResponseMessage("success", response));

            log_->info("[EH] SET_PLAYER_BESTIARY: pushed overview (" +
                       std::to_string(entries.size()) + " mobs) to clientId=" + std::to_string(clientId));
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_BESTIARY: " + std::string(e.what()));
    }
    bumpVersion();
}

// ── GET_BESTIARY_OVERVIEW ──────────────────────────────────────────────────

void
CatalogSyncService::handleSetPlayerReputationsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_PLAYER_REPUTATIONS: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("SET_PLAYER_REPUTATIONS: missing characterId");
            return;
        }

        std::unordered_map<std::string, int> reps;
        if (body.contains("entries") && body["entries"].is_array())
        {
            for (const auto &e : body["entries"])
                reps[e.value("factionSlug", "")] = e.value("value", 0);
        }

        gameServices_.getReputationManager().loadCharacterReputations(characterId, reps);
        log_->info("[EH] SET_PLAYER_REPUTATIONS: loaded " + std::to_string(reps.size()) +
                   " entries for character " + std::to_string(characterId));

        // Push reputation state to the client immediately (same pattern as SET_PLAYER_INVENTORY)
        int clientId = 0;
        {
            auto characters = gameServices_.getCharacterManager().getCharactersList();
            for (const auto &c : characters)
            {
                if (c.characterId == characterId)
                {
                    clientId = c.clientId;
                    break;
                }
            }
        }
        if (clientId > 0)
        {
            auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
            if (clientSocket && clientSocket->is_open())
            {
                nlohmann::json entriesArr = nlohmann::json::array();
                for (const auto &[faction, value] : reps)
                    entriesArr.push_back({{"factionSlug", faction}, {"value", value}, {"tier", ReputationManager::getTier(value)}});

                nlohmann::json response;
                response["header"]["eventType"] = "player_reputations";
                response["header"]["status"] = "success";
                response["body"]["characterId"] = characterId;
                response["body"]["entries"] = entriesArr;
                networkManager_.sendResponse(clientSocket,
                    networkManager_.generateResponseMessage("success", response));
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_REPUTATIONS: " + std::string(e.what()));
    }
    bumpVersion();
}

// ── SET_PLAYER_MASTERIES ───────────────────────────────────────────────────

void
CatalogSyncService::handleSetPlayerMasteriesEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_PLAYER_MASTERIES: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("SET_PLAYER_MASTERIES: missing characterId");
            return;
        }

        std::unordered_map<std::string, float> masteries;
        if (body.contains("entries") && body["entries"].is_array())
        {
            for (const auto &e : body["entries"])
                masteries[e.value("masterySlug", "")] = e.value("value", 0.0f);
        }

        gameServices_.getMasteryManager().loadCharacterMasteries(characterId, masteries);
        log_->info("[EH] SET_PLAYER_MASTERIES: loaded " + std::to_string(masteries.size()) +
                   " entries for character " + std::to_string(characterId));

        // Re-apply permanent mastery milestone effects from the freshly loaded values.
        // Handles both the initial login path (masteries arrive first) and the late-arrival
        // race (masteries arrive AFTER SET_PLAYER_ACTIVE_EFFECTS).  The effects deduplicate
        // via addActiveEffect (same effectSlug = refresh), so double-application is safe.
        if (!masteries.empty())
        {
            gameServices_.getMasteryManager().reapplyMilestoneEffects(characterId);
            gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);
        }

        // Push mastery state to the client immediately
        int clientId = 0;
        {
            auto characters = gameServices_.getCharacterManager().getCharactersList();
            for (const auto &c : characters)
            {
                if (c.characterId == characterId)
                {
                    clientId = c.clientId;
                    break;
                }
            }
        }
        if (clientId > 0)
        {
            auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
            if (clientSocket && clientSocket->is_open())
            {
                nlohmann::json entriesArr = nlohmann::json::array();
                for (const auto &[slug, value] : masteries)
                    entriesArr.push_back({{"masterySlug", slug}, {"value", value}});

                nlohmann::json response;
                response["header"]["eventType"] = "player_masteries";
                response["header"]["status"] = "success";
                response["body"]["characterId"] = characterId;
                response["body"]["entries"] = entriesArr;
                networkManager_.sendResponse(clientSocket,
                    networkManager_.generateResponseMessage("success", response));
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_MASTERIES: " + std::string(e.what()));
    }
    bumpVersion();
}

// ── SET_ZONE_EVENT_TEMPLATES ───────────────────────────────────────────────

void
CatalogSyncService::handleSetZoneEventTemplatesEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_ZONE_EVENT_TEMPLATES: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);

        std::vector<ZoneEventManager::ZoneEventTemplate> templates;
        if (body.contains("templates") && body["templates"].is_array())
        {
            for (const auto &t : body["templates"])
            {
                ZoneEventManager::ZoneEventTemplate tmpl;
                tmpl.id = t.value("id", 0);
                tmpl.slug = t.value("slug", "");
                tmpl.gameZoneId = t.value("gameZoneId", 0);
                tmpl.triggerType = t.value("triggerType", "manual");
                tmpl.durationSec = t.value("durationSec", 300);
                tmpl.lootMultiplier = t.value("lootMultiplier", 1.0f);
                tmpl.spawnRateMultiplier = t.value("spawnRateMultiplier", 1.0f);
                tmpl.mobSpeedMultiplier = t.value("mobSpeedMultiplier", 1.0f);
                tmpl.announceKey = t.value("announceKey", "");
                tmpl.intervalHours = t.value("intervalHours", 0);
                tmpl.randomChancePerHour = t.value("randomChancePerHour", 0.0f);
                tmpl.hasInvasionWave = t.value("hasInvasionWave", false);
                tmpl.invasionMobTemplateId = t.value("invasionMobTemplateId", 0);
                tmpl.invasionWaveCount = t.value("invasionWaveCount", 0);
                if (!tmpl.slug.empty())
                    templates.push_back(std::move(tmpl));
            }
        }

        gameServices_.getZoneEventManager().loadTemplates(templates);
        log_->info("[EH] SET_ZONE_EVENT_TEMPLATES: loaded " + std::to_string(templates.size()) + " templates");
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_ZONE_EVENT_TEMPLATES: " + std::string(e.what()));
    }
    bumpVersion();
}

// ── SET_LEARNED_SKILL ─────────────────────────────────────────────────────
// Game server responds with full SkillStruct data for the newly learned skill.
// We update CharacterManager and notify the client.

void
CatalogSyncService::handleSetLearnedSkillEvent(const Event &event)
{
    const auto &data = event.getData();
    int clientId = event.getClientID();

    try
    {
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_LEARNED_SKILL: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);

        if (characterId <= 0)
        {
            log_->error("[EH] SET_LEARNED_SKILL: invalid characterId");
            return;
        }

        // Build SkillStruct from the skillData object
        if (!body.contains("skillData") || !body["skillData"].is_object())
        {
            log_->error("[EH] SET_LEARNED_SKILL: missing skillData");
            return;
        }
        const auto &sd = body["skillData"];

        SkillStruct skill;
        skill.skillName = sd.value("skillName", "");
        skill.skillSlug = sd.value("skillSlug", "");
        skill.scaleStat = sd.value("scaleStat", "");
        skill.school = sd.value("school", "");
        skill.skillEffectType = sd.value("skillEffectType", "");
        skill.skillLevel = sd.value("skillLevel", 1);
        skill.coeff = sd.value("coeff", 0.0);
        skill.flatAdd = sd.value("flatAdd", 0.0);
        skill.cooldownMs = sd.value("cooldownMs", 0);
        skill.gcdMs = sd.value("gcdMs", 0);
        skill.castMs = sd.value("castMs", 0);
        skill.costMp = sd.value("costMp", 0);
        skill.maxRange = sd.value("maxRange", 0);
        skill.areaRadius = sd.value("areaRadius", 0);
        skill.swingMs = sd.value("swingMs", 300);
        skill.animationName = sd.value("animationName", "");
        skill.isPassive = sd.value("isPassive", false);

        // Parse effect definitions so passive bonuses can be applied immediately
        if (sd.contains("effects") && sd["effects"].is_array())
        {
            for (const auto &eff : sd["effects"])
            {
                SkillEffectDefinitionStruct ed;
                if (eff.contains("effectSlug") && eff["effectSlug"].is_string())
                    ed.effectSlug = eff["effectSlug"].get<std::string>();
                if (eff.contains("effectTypeSlug") && eff["effectTypeSlug"].is_string())
                    ed.effectTypeSlug = eff["effectTypeSlug"].get<std::string>();
                if (eff.contains("attributeSlug") && eff["attributeSlug"].is_string())
                    ed.attributeSlug = eff["attributeSlug"].get<std::string>();
                if (eff.contains("value") && eff["value"].is_number())
                    ed.value = eff["value"].get<float>();
                if (eff.contains("durationSeconds") && eff["durationSeconds"].is_number_integer())
                    ed.durationSeconds = eff["durationSeconds"].get<int>();
                if (eff.contains("tickMs") && eff["tickMs"].is_number_integer())
                    ed.tickMs = eff["tickMs"].get<int>();
                if (eff.contains("modifierType") && eff["modifierType"].is_string())
                    ed.modifierType = eff["modifierType"].get<std::string>();
                skill.effects.push_back(ed);
            }
        }

        // Persist in-memory
        gameServices_.getCharacterManager().addCharacterSkill(characterId, skill);

        // If this is a passive skill, apply its permanent stat modifiers immediately
        // (same logic as handleSetPlayerActiveEffectsEvent does on login — both live here now)
        if (skill.isPassive)
        {
            for (const auto &ed : skill.effects)
            {
                ActiveEffectStruct eff;
                eff.effectSlug = ed.effectSlug;
                eff.effectTypeSlug = ed.effectTypeSlug;
                eff.attributeSlug = ed.attributeSlug;
                eff.value = ed.value;
                eff.modifierType = ed.modifierType;
                eff.sourceType = "skill_passive";
                eff.expiresAt = 0; // permanent
                eff.tickMs = 0;    // passives never tick
                gameServices_.getCharacterManager().addActiveEffect(characterId, eff);
            }
        }

        // Always send a full stats update: freeSkillPoints changed for every skill learned,
        // and passive effects (if any) must appear in activeEffects immediately.
        gameServices_.getStatsNotificationService().sendStatsUpdate(characterId);

        // Build and send skill_learned notification to client
        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!clientSocket)
        {
            log_->error("[EH] SET_LEARNED_SKILL: socket not available for clientId " + std::to_string(clientId));
            return;
        }

        int newSp = gameServices_.getCharacterManager().getCharacterFreeSkillPoints(characterId);

        nlohmann::json notif;
        notif["type"] = "skill_learned";
        notif["skillSlug"] = skill.skillSlug;
        notif["skillName"] = skill.skillName;
        notif["isPassive"] = skill.isPassive;
        notif["newFreeSkillPoints"] = newSp;
        notif["skillData"] = sd;

        nlohmann::json resp = ResponseBuilder()
                                  .setHeader("eventType", "skill_learned")
                                  .build();
        resp["body"] = notif;
        networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", resp));

        log_->info("[EH] SET_LEARNED_SKILL: char={} skill={} newSp={}", characterId, skill.skillSlug, newSp);
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_LEARNED_SKILL: " + std::string(e.what()));
    }
    bumpVersion();
}

// ── SET_TITLE_DEFINITIONS ──────────────────────────────────────────────────

void
CatalogSyncService::handleSetTitleDefinitionsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_TITLE_DEFINITIONS: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);

        std::vector<TitleDefinitionStruct> defs;
        if (body.contains("titles") && body["titles"].is_array())
        {
            for (const auto &t : body["titles"])
            {
                TitleDefinitionStruct td;
                td.id = t.value("id", 0);
                td.slug = t.value("slug", "");
                td.displayName = t.value("displayName", "");
                td.description = t.value("description", "");
                td.earnCondition = t.value("earnCondition", "");
                if (t.contains("conditionParams") && t["conditionParams"].is_object())
                    td.conditionParams = t["conditionParams"];
                if (t.contains("bonuses") && t["bonuses"].is_array())
                {
                    for (const auto &b : t["bonuses"])
                    {
                        TitleBonusStruct bonus;
                        bonus.attributeSlug = b.value("attributeSlug", "");
                        bonus.value = b.value("value", 0.0f);
                        if (!bonus.attributeSlug.empty())
                            td.bonuses.push_back(bonus);
                    }
                }
                if (!td.slug.empty())
                    defs.push_back(std::move(td));
            }
        }

        gameServices_.getTitleManager().loadTitleDefinitions(defs);
        log_->info("[EH] SET_TITLE_DEFINITIONS: loaded {} definitions", defs.size());
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_TITLE_DEFINITIONS: " + std::string(e.what()));
    }
    bumpVersion();
}

// ── SET_MASTERY_DEFINITIONS ────────────────────────────────────────────────

void
CatalogSyncService::handleSetMasteryDefinitionsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_MASTERY_DEFINITIONS: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);

        std::vector<MasteryDefinitionStruct> defs;
        if (body.contains("definitions") && body["definitions"].is_array())
        {
            for (const auto &d : body["definitions"])
            {
                MasteryDefinitionStruct md;
                md.slug = d.value("slug", "");
                md.name = d.value("name", "");
                md.weaponTypeSlug = d.value("weaponTypeSlug", "");
                md.maxValue = d.value("maxValue", 100.0);
                md.targetAttributeSlug = d.value("targetAttributeSlug", "physical_attack");
                if (!md.slug.empty())
                    defs.push_back(std::move(md));
            }
        }

        gameServices_.getMasteryManager().loadMasteryDefinitions(defs);
        log_->info("[EH] SET_MASTERY_DEFINITIONS: loaded {} definitions", defs.size());
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_MASTERY_DEFINITIONS: " + std::string(e.what()));
    }
    bumpVersion();
}

// ── SET_PLAYER_TITLES ─────────────────────────────────────────────────────

void
CatalogSyncService::handleSetPlayerTitlesEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("SET_PLAYER_TITLES: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        int characterId = body.value("characterId", 0);
        if (characterId <= 0)
        {
            log_->error("SET_PLAYER_TITLES: missing characterId");
            return;
        }

        PlayerTitleStateStruct state;
        state.characterId = characterId;
        state.equippedSlug = body.value("equippedSlug", "");
        if (body.contains("earnedSlugs") && body["earnedSlugs"].is_array())
        {
            for (const auto &s : body["earnedSlugs"])
                if (s.is_string())
                    state.earnedSlugs.push_back(s.get<std::string>());
        }

        gameServices_.getTitleManager().loadPlayerTitles(characterId, state);
        log_->info("[EH] SET_PLAYER_TITLES: loaded {} titles for char={}", state.earnedSlugs.size(), characterId);

        // Push title state to client immediately (same pattern as SET_PLAYER_INVENTORY)
        // notifyClientCallback_ fires sendTitleUpdateToClient → already handles this
        // But we need it explicitly if the callback isn't wired yet during login ordering.
        // The callback wired in ChunkServer will re-send on any future change.

        // If the player is already world-ready (titles arrived late — after playerReady),
        // broadcast their equipped title to all other zone clients so nameplates update.
        // If not yet ready, handlePlayerReadyEvent (Phase 4, step 6) does it as catch-up.
        auto chars = gameServices_.getCharacterManager().getCharactersList();
        int clientId = 0;
        for (const auto &c : chars)
            if (c.characterId == characterId)
            {
                clientId = c.clientId;
                break;
            }

        if (clientId > 0 && gameServices_.getClientManager().isClientWorldReady(clientId) && characterEventHandler_)
        {
            characterEventHandler_->broadcastTitleChanged(characterId, clientId);
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Error processing SET_PLAYER_TITLES: " + std::string(e.what()));
    }
    bumpVersion();
}

// ── GET_PLAYER_TITLES ─────────────────────────────────────────────────────

void
CatalogSyncService::handleSetSkillBarSlotEvent(const Event &event)
{
    if (skillEventHandler_)
        skillEventHandler_->handleSetSkillBarSlotEvent(event);
    else
        log_->error("handleSetSkillBarSlotEvent: skillEventHandler_ is null");
    bumpVersion();
}

// ── Emote system ──────────────────────────────────────────────────────────

void
CatalogSyncService::handleSetEmoteDefinitionsEvent(const Event &event)
{
    if (emoteEventHandler_)
        emoteEventHandler_->handleSetEmoteDefinitionsEvent(event);
    else
        log_->error("handleSetEmoteDefinitionsEvent: emoteEventHandler_ is null");
    bumpVersion();
}

void
CatalogSyncService::handleSetPlayerEmotesEvent(const Event &event)
{
    if (emoteEventHandler_)
        emoteEventHandler_->handleSetPlayerEmotesEvent(event);
    else
        log_->error("handleSetPlayerEmotesEvent: emoteEventHandler_ is null");
    bumpVersion();
}

