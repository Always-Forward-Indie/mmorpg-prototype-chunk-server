#include "events/handlers/SkillEventHandler.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"
#include "services/TrainerManager.hpp"
#include "utils/Logger.hpp"
#include "utils/ResponseBuilder.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/logger.h>

SkillEventHandler::SkillEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "skill")
{
    log_ = gameServices_.getLogger().getSystem("skill");
    log_->info("SkillEventHandler initialized");
}

void
SkillEventHandler::handleInitializePlayerSkills(const Event &event)
{
    const auto &data = event.getData();
    int clientID = event.getClientID();
    std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket = getClientSocket(event);

    try
    {
        if (!std::holds_alternative<PlayerSkillInitStruct>(data))
        {
            log_->error("Invalid data format for INITIALIZE_PLAYER_SKILLS event");
            sendErrorResponse("Invalid skill data format", clientID, clientSocket);
            return;
        }

        auto skillInitData = std::get<PlayerSkillInitStruct>(data);
        handleInitializePlayerSkillsDirect(skillInitData, clientID, clientSocket);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleInitializePlayerSkills: " + std::string(ex.what()));
        sendErrorResponse("Internal server error", clientID, clientSocket);
    }
}

void
SkillEventHandler::handleInitializePlayerSkillsDirect(const PlayerSkillInitStruct &skillInitData, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    try
    {
        gameServices_.getLogger().log("Initializing skills for character " +
                                          std::to_string(skillInitData.characterId) +
                                          " with " + std::to_string(skillInitData.skills.size()) + " skills",
            GREEN);

        // Build skills response
        nlohmann::json response = buildSkillsResponse(skillInitData);

        // Send response to client
        sendSkillsResponse(response, clientID, clientSocket);

        log_->info("Player skills initialized successfully for client " +
                   std::to_string(clientID));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in handleInitializePlayerSkillsDirect: " + std::string(ex.what()));
        sendErrorResponse("Failed to initialize player skills", clientID, clientSocket);
    }
}

void
SkillEventHandler::initializeFromCharacterData(const CharacterDataStruct &characterData, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    try
    {
        gameServices_.getLogger().log("Initializing skills from character data for character " +
                                          std::to_string(characterData.characterId) +
                                          " (client " + std::to_string(clientID) + ")",
            GREEN);

        // Create skill initialization structure
        PlayerSkillInitStruct skillInitData;
        skillInitData.characterId = characterData.characterId;
        skillInitData.skills = characterData.skills;

        // Use direct method to avoid event overhead
        handleInitializePlayerSkillsDirect(skillInitData, clientID, clientSocket);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("Error in initializeFromCharacterData: " + std::string(ex.what()));
        sendErrorResponse("Failed to initialize skills from character data", clientID, clientSocket);
    }
}

nlohmann::json
SkillEventHandler::buildSkillsResponse(const PlayerSkillInitStruct &skillInitData)
{
    nlohmann::json response;

    // Create header structure
    response["header"]["eventType"] = "initializePlayerSkills";
    response["header"]["message"] = "Player skills initialized successfully";

    // Create body structure
    response["body"]["characterId"] = skillInitData.characterId;

    nlohmann::json skillsArray = nlohmann::json::array();

    gameServices_.getLogger().log("Building skills response for " + std::to_string(skillInitData.skills.size()) + " skills", GREEN);

    for (size_t i = 0; i < skillInitData.skills.size(); ++i)
    {
        const auto &skill = skillInitData.skills[i];

        gameServices_.getLogger().log("Processing skill " + std::to_string(i) + ": " + skill.skillName + " (" + skill.skillSlug + ")", GREEN);

        try
        {
            nlohmann::json skillData;
            skillData["skillSlug"] = skill.skillSlug;
            skillData["skillLevel"] = skill.skillLevel;
            skillData["coeff"] = skill.coeff;
            skillData["flatAdd"] = skill.flatAdd;
            skillData["cooldownMs"] = skill.cooldownMs;
            skillData["gcdMs"] = skill.gcdMs;
            skillData["castMs"] = skill.castMs;
            skillData["costMp"] = skill.costMp;
            skillData["maxRange"] = skill.maxRange;
            skillData["isPassive"] = skill.isPassive;

            skillsArray.push_back(skillData);
            log_->info("Successfully processed skill " + std::to_string(i));
        }
        catch (const std::exception &ex)
        {
            gameServices_.getLogger().logError("Error processing skill " + std::to_string(i) + ": " + std::string(ex.what()));
            continue; // Skip this skill and continue with the next one
        }
    }

    response["body"]["skills"] = skillsArray;
    gameServices_.getLogger().log("Skills response built with " + std::to_string(skillsArray.size()) + " skills", GREEN);
    return response;
}

void
SkillEventHandler::sendSkillsResponse(const nlohmann::json &response, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    if (!clientSocket)
    {
        log_->info("Client socket not found for player skills initialization");
        return;
    }

    std::string responseData = networkManager_.generateResponseMessage("success", response);
    networkManager_.sendResponse(clientSocket, responseData);
}

void
SkillEventHandler::sendErrorResponse(const std::string &errorMessage, int clientID, std::shared_ptr<boost::asio::ip::tcp::socket> clientSocket)
{
    if (!clientSocket)
    {
        return;
    }

    nlohmann::json errorResponse;
    errorResponse["header"]["eventType"] = "initializePlayerSkills";
    errorResponse["header"]["message"] = errorMessage;
    errorResponse["body"]["error"] = errorMessage;

    std::string errorData = networkManager_.generateResponseMessage("error", errorResponse);
    networkManager_.sendResponse(clientSocket, errorData);
}

// ── handleSetTrainerDataEvent ─────────────────────────────────────────────────────────────────
// Game-server → chunk: populate TrainerManager with all trainer NPC skill lists.
void
SkillEventHandler::handleSetTrainerDataEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<nlohmann::json>(data))
        {
            log_->error("[SkillEventHandler] SET_TRAINER_DATA: unexpected data type");
            return;
        }
        const auto &body = std::get<nlohmann::json>(data);
        if (!body.contains("trainers") || !body["trainers"].is_array())
        {
            log_->error("[SkillEventHandler] SET_TRAINER_DATA: missing 'trainers' array");
            return;
        }

        std::vector<TrainerNPCDataStruct> trainers;
        for (const auto &tj : body["trainers"])
        {
            TrainerNPCDataStruct trainer;
            trainer.npcId = tj.value("npcId", 0);
            if (trainer.npcId <= 0)
                continue;

            if (tj.contains("skills") && tj["skills"].is_array())
            {
                for (const auto &sj : tj["skills"])
                {
                    ClassSkillTreeEntryStruct entry;
                    entry.skillId = sj.value("skillId", 0);
                    entry.skillSlug = sj.value("skillSlug", "");
                    entry.skillName = sj.value("skillName", "");
                    entry.isPassive = sj.value("isPassive", false);
                    entry.requiredLevel = sj.value("requiredLevel", 1);
                    entry.spCost = sj.value("spCost", 1);
                    entry.goldCost = sj.value("goldCost", 0);
                    entry.requiresBook = sj.value("requiresBook", false);
                    entry.bookItemId = sj.value("bookItemId", 0);
                    entry.prerequisiteSkillSlug = sj.value("prerequisiteSkillSlug", "");
                    if (!entry.skillSlug.empty())
                        trainer.skills.push_back(std::move(entry));
                }
            }
            trainers.push_back(std::move(trainer));
        }

        gameServices_.getTrainerManager().setTrainerData(trainers);
        log_->info("[SkillEventHandler] SET_TRAINER_DATA: loaded {} trainers", trainers.size());
    }
    catch (const std::exception &ex)
    {
        log_->error("[SkillEventHandler] handleSetTrainerDataEvent: {}", ex.what());
    }
}

// ── handleOpenSkillShopEvent ──────────────────────────────────────────────────────────────────
// Client → chunk: player opens skill trainer shop window directly (without dialogue).
void
SkillEventHandler::handleOpenSkillShopEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<OpenSkillShopRequestStruct>(data))
            return;

        const auto &req = std::get<OpenSkillShopRequestStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        // Validate NPC exists and is a trainer
        const auto &npc = gameServices_.getNPCManager().getNPCById(req.npcId);
        if (npc.id == 0)
        {
            sendErrorResponseWithTimestamps(socket, "NPC_NOT_FOUND", "skillShop", req.clientId, req.timestamps);
            return;
        }

        // Proximity check (same tolerance as vendor shop)
        auto distSq = [](const PositionStruct &a, const PositionStruct &b)
        {
            float dx = a.positionX - b.positionX, dy = a.positionY - b.positionY;
            return dx * dx + dy * dy;
        };
        float rangeLimit = npc.radius + 2.0f;
        if (distSq(req.playerPosition, npc.position) > rangeLimit * rangeLimit)
        {
            sendErrorResponseWithTimestamps(socket, "OUT_OF_RANGE", "skillShop", req.clientId, req.timestamps);
            return;
        }

        const CharacterDataStruct &charData =
            gameServices_.getCharacterManager().getCharacterData(req.characterId);

        // Build PlayerContextStruct for affordability checks
        PlayerContextStruct ctx;
        ctx.characterId = req.characterId;
        ctx.characterLevel = charData.characterLevel;
        ctx.freeSkillPoints = charData.freeSkillPoints;
        for (const auto &s : charData.skills)
            ctx.learnedSkillSlugs.insert(s.skillSlug);

        nlohmann::json skillsJson = gameServices_.getTrainerManager().buildSkillShopJson(
            req.npcId, ctx, gameServices_.getInventoryManager());

        if (skillsJson.is_null())
        {
            sendErrorResponseWithTimestamps(socket, "NOT_A_TRAINER", "skillShop", req.clientId, req.timestamps);
            return;
        }

        nlohmann::json resp;
        resp["header"]["eventType"] = "skillShop";
        resp["header"]["status"] = "success";
        resp["header"]["clientId"] = req.clientId;
        resp["body"]["npcId"] = req.npcId;
        resp["body"]["npcSlug"] = npc.slug;
        resp["body"]["freeSkillPoints"] = charData.freeSkillPoints;
        resp["body"]["goldBalance"] = gameServices_.getInventoryManager().getGoldAmount(req.characterId);
        resp["body"]["skills"] = std::move(skillsJson);

        networkManager_.sendResponse(socket, resp.dump() + "\n");
        log_->info("[SkillEventHandler] OPEN_SKILL_SHOP: char={} npc={}", req.characterId, req.npcId);
    }
    catch (const std::exception &ex)
    {
        log_->error("[SkillEventHandler] handleOpenSkillShopEvent: {}", ex.what());
    }
}

// ── handleRequestLearnSkillEvent ─────────────────────────────────────────────────────────────
// Client → chunk: player clicks "Learn" in skill shop UI.
// Mirrors DialogueActionExecutor::executeLearnSkill() but resolves costs from TrainerManager.
void
SkillEventHandler::handleRequestLearnSkillEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<RequestLearnSkillRequestStruct>(data))
            return;

        const auto &req = std::get<RequestLearnSkillRequestStruct>(data);
        auto socket = gameServices_.getClientManager().getClientSocket(req.clientId);
        if (!socket)
            return;

        // Helper lambdas for sending typed failures
        auto sendFailed = [&](const std::string &reason)
        {
            nlohmann::json resp;
            resp["header"]["eventType"] = "learn_skill_failed";
            resp["header"]["status"] = "error";
            resp["header"]["clientId"] = req.clientId;
            resp["body"]["skillSlug"] = req.skillSlug;
            resp["body"]["reason"] = reason;
            networkManager_.sendResponse(socket, resp.dump() + "\n");
        };

        // Resolve NPC id — client may omit npcId when opening shop through dialogue
        int effectiveNpcId = req.npcId;
        if (effectiveNpcId == 0)
        {
            auto *session = gameServices_.getDialogueSessionManager()
                                .getSessionByCharacter(req.characterId);
            if (session)
                effectiveNpcId = session->npcId;
        }

        // Validate NPC
        const auto &npc = gameServices_.getNPCManager().getNPCById(effectiveNpcId);
        if (npc.id == 0)
        {
            sendFailed("npc_not_found");
            return;
        }

        // Proximity check
        auto distSq = [](const PositionStruct &a, const PositionStruct &b)
        {
            float dx = a.positionX - b.positionX, dy = a.positionY - b.positionY;
            return dx * dx + dy * dy;
        };
        float rangeLimit = npc.radius + 2.0f;
        bool inRange = distSq(req.playerPosition, npc.position) <= rangeLimit * rangeLimit;

        // Also accept if an active dialogue session exists (player in dialogue)
        bool hasSession = (gameServices_.getDialogueSessionManager()
                               .getSessionByCharacter(req.characterId) != nullptr);

        if (!inRange && !hasSession)
        {
            sendFailed("out_of_range");
            return;
        }

        // Look up skill cost data from TrainerManager
        const ClassSkillTreeEntryStruct *entry =
            gameServices_.getTrainerManager().getSkillEntry(effectiveNpcId, req.skillSlug);
        if (!entry)
        {
            sendFailed("skill_not_available");
            return;
        }

        const CharacterDataStruct &charData =
            gameServices_.getCharacterManager().getCharacterData(req.characterId);

        // Build learned-set for validation
        std::unordered_set<std::string> learnedSlugs;
        for (const auto &s : charData.skills)
            learnedSlugs.insert(s.skillSlug);

        // Guard: already learned
        if (learnedSlugs.count(req.skillSlug))
        {
            sendFailed("already_learned");
            return;
        }

        // Guard: level requirement
        if (charData.characterLevel < entry->requiredLevel)
        {
            sendFailed("insufficient_level");
            return;
        }

        // Guard: prerequisite skill
        if (!entry->prerequisiteSkillSlug.empty() &&
            !learnedSlugs.count(entry->prerequisiteSkillSlug))
        {
            sendFailed("missing_prerequisite");
            return;
        }

        // Guard: SP
        if (charData.freeSkillPoints < entry->spCost)
        {
            sendFailed("insufficient_sp");
            return;
        }

        // Guard: gold
        if (entry->goldCost > 0)
        {
            int goldBalance = gameServices_.getInventoryManager().getGoldAmount(req.characterId);
            if (goldBalance < entry->goldCost)
            {
                sendFailed("insufficient_gold");
                return;
            }
        }

        // Guard: skill book
        if (entry->requiresBook && entry->bookItemId > 0)
        {
            const auto &inv = gameServices_.getInventoryManager().getPlayerInventory(req.characterId);
            bool hasBook = false;
            for (const auto &slot : inv)
                if (slot.itemId == entry->bookItemId && slot.quantity > 0)
                {
                    hasBook = true;
                    break;
                }
            if (!hasBook)
            {
                sendFailed("missing_skill_book");
                return;
            }
        }

        // ── All checks passed: consume resources ──────────────────────────────────

        // Consume skill book
        if (entry->requiresBook && entry->bookItemId > 0)
            gameServices_.getInventoryManager().removeItemFromInventory(req.characterId, entry->bookItemId, 1);

        // Consume gold
        if (entry->goldCost > 0)
        {
            const ItemDataStruct *goldItem =
                gameServices_.getItemManager().getItemBySlug("gold_coin");
            if (goldItem)
                gameServices_.getInventoryManager().removeItemFromInventory(
                    req.characterId, goldItem->id, entry->goldCost);
        }

        // Deduct SP
        gameServices_.getCharacterManager().modifyFreeSkillPoints(req.characterId, -entry->spCost);

        // Queue saveLearnedSkill to game server — game server will respond with
        // setLearnedSkill which calls EventHandler::handleSetLearnedSkillEvent() and
        // sends the skill_learned packet to the client.
        nlohmann::json packet;
        packet["header"]["eventType"] = "saveLearnedSkill";
        packet["header"]["clientId"] = req.clientId;
        packet["header"]["hash"] = "";
        packet["body"]["characterId"] = req.characterId;
        packet["body"]["clientId"] = req.clientId;
        packet["body"]["skillSlug"] = req.skillSlug;
        gameServerWorker_.sendDataToGameServer(packet.dump() + "\n");

        log_->info("[SkillEventHandler] REQUEST_LEARN_SKILL: char={} npc={} skill={}",
            req.characterId,
            effectiveNpcId,
            req.skillSlug);
    }
    catch (const std::exception &ex)
    {
        log_->error("[SkillEventHandler] handleRequestLearnSkillEvent: {}", ex.what());
    }
}
