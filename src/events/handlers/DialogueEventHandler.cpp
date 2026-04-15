#include "events/handlers/DialogueEventHandler.hpp"
#include "events/EventData.hpp"
#include "services/DialogueActionExecutor.hpp"
#include "services/DialogueConditionEvaluator.hpp"
#include "services/GameServices.hpp"
#include "utils/ResponseBuilder.hpp"
#include "utils/TerminalColors.hpp"
#include <cmath>
#include <spdlog/logger.h>

DialogueEventHandler::DialogueEventHandler(
    NetworkManager &networkManager,
    GameServerWorker &gameServerWorker,
    GameServices &gameServices)
    : BaseEventHandler(networkManager, gameServerWorker, gameServices, "dialogue")
{
    log_ = gameServices_.getLogger().getSystem("dialogue");
}

// =============================================================================
// Static data events (game-server → chunk-server)
// =============================================================================

void
DialogueEventHandler::handleSetAllDialoguesEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<DialogueGraphStruct>>(data))
        {
            log_->error("[DialogueEventHandler] SET_ALL_DIALOGUES: unexpected data type");
            return;
        }

        const auto &dialogues = std::get<std::vector<DialogueGraphStruct>>(data);
        gameServices_.getDialogueManager().setDialogues(dialogues);
        gameServices_.getLogger().log("[DialogueEventHandler] Received " +
                                          std::to_string(dialogues.size()) + " dialogues",
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[DialogueEventHandler] handleSetAllDialoguesEvent: " + std::string(ex.what()));
    }
}

void
DialogueEventHandler::handleSetNPCDialogueMappingsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<NPCDialogueMappingStruct>>(data))
        {
            log_->error("[DialogueEventHandler] SET_NPC_DIALOGUE_MAPPINGS: unexpected data type");
            return;
        }

        const auto &mappings = std::get<std::vector<NPCDialogueMappingStruct>>(data);
        gameServices_.getDialogueManager().setNPCDialogueMappings(mappings);
        gameServices_.getLogger().log("[DialogueEventHandler] Received " +
                                          std::to_string(mappings.size()) + " NPC dialogue mappings",
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[DialogueEventHandler] handleSetNPCDialogueMappingsEvent: " + std::string(ex.what()));
    }
}

void
DialogueEventHandler::handleSetAllQuestsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<QuestStruct>>(data))
        {
            log_->error("[DialogueEventHandler] SET_ALL_QUESTS: unexpected data type");
            return;
        }

        const auto &quests = std::get<std::vector<QuestStruct>>(data);
        gameServices_.getQuestManager().setQuests(quests);
        gameServices_.getLogger().log("[DialogueEventHandler] Received " +
                                          std::to_string(quests.size()) + " quest definitions",
            GREEN);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[DialogueEventHandler] handleSetAllQuestsEvent: " + std::string(ex.what()));
    }
}

// =============================================================================
// Per-character runtime data events
// =============================================================================

void
DialogueEventHandler::handleSetPlayerQuestsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<PlayerQuestProgressStruct>>(data))
        {
            log_->error("[DialogueEventHandler] SET_PLAYER_QUESTS: unexpected data type");
            return;
        }

        const auto &quests = std::get<std::vector<PlayerQuestProgressStruct>>(data);
        if (quests.empty())
            return;

        int characterId = quests[0].characterId;
        gameServices_.getQuestManager().loadPlayerQuests(characterId, quests);
        gameServices_.getLogger().log("[DialogueEventHandler] Loaded " +
                                      std::to_string(quests.size()) + " quests for character " +
                                      std::to_string(characterId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[DialogueEventHandler] handleSetPlayerQuestsEvent: " + std::string(ex.what()));
    }
}

void
DialogueEventHandler::handleSetPlayerFlagsEvent(const Event &event)
{
    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<std::vector<PlayerFlagStruct>>(data))
        {
            log_->error("[DialogueEventHandler] SET_PLAYER_FLAGS: unexpected data type");
            return;
        }

        const auto &flags = std::get<std::vector<PlayerFlagStruct>>(data);

        // Flags are attached to CharacterDataStruct in CharacterManager
        // The clientId stored in the event allows us to find the character
        int clientId = event.getClientID();
        if (clientId <= 0)
            return;

        auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
        if (!clientSocket)
            return;

        // Find characterId by clientId - iterate characters
        auto characters = gameServices_.getCharacterManager().getCharactersList();
        int characterId = 0;
        for (const auto &c : characters)
        {
            if (c.clientId == clientId)
            {
                characterId = c.characterId;
                break;
            }
        }

        if (characterId <= 0)
        {
            log_->error("[DialogueEventHandler] SET_PLAYER_FLAGS: no character for clientId " + std::to_string(clientId));
            return;
        }

        // Store flags in CharacterDataStruct so buildPlayerContext can use them.
        // Only replace if non-empty — an empty response means the player has no DB
        // flags yet; we must not wipe any flags already set in-memory this session.
        if (!flags.empty())
        {
            gameServices_.getCharacterManager().setCharacterFlags(characterId, flags);
        }

        gameServices_.getLogger().log("[DialogueEventHandler] SET_PLAYER_FLAGS: " +
                                      std::to_string(flags.size()) + " flags for character " +
                                      std::to_string(characterId));

        // Mark flags as ready regardless of whether the array was empty.
        // An empty array is a valid response: the character simply has no saved flags
        // (fresh player). The deferred exploration check below still needs to run.
        gameServices_.getQuestManager().markFlagsLoaded(characterId);

        auto charData = gameServices_.getCharacterManager().getCharacterData(characterId);
        auto zoneOpt = gameServices_.getGameZoneManager().getZoneForPosition(charData.characterPosition);
        if (zoneOpt.has_value())
        {
            const std::string exploredKey = "explored_" + zoneOpt->slug;
            const bool alreadyExplored = gameServices_.getQuestManager().getFlagBool(characterId, exploredKey);
            if (!alreadyExplored)
            {
                gameServices_.getQuestManager().setFlagBool(characterId, exploredKey, true);
                if (zoneOpt->explorationXpReward > 0)
                {
                    gameServices_.getExperienceManager().grantExperience(
                        characterId, zoneOpt->explorationXpReward, "zone_explored", 0);
                }
                nlohmann::json notifData = {{"zoneSlug", zoneOpt->slug}};
                gameServices_.getStatsNotificationService().sendWorldNotification(
                    characterId, "zone_explored", notifData, "medium", "toast");
            }
        }
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[DialogueEventHandler] handleSetPlayerFlagsEvent: " + std::string(ex.what()));
    }
}

// =============================================================================
// Client-driven interaction events
// =============================================================================

void
DialogueEventHandler::handleNPCInteractEvent(const Event &event)
{
    int clientId = event.getClientID();
    auto clientSocket = getClientSocket(event);

    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<NPCInteractRequestStruct>(data))
        {
            log_->error("[DialogueEventHandler] NPC_INTERACT: unexpected data type");
            return;
        }

        const auto &request = std::get<NPCInteractRequestStruct>(data);
        int characterId = request.characterId;

        if (characterId <= 0 || clientId <= 0)
        {
            log_->error("[DialogueEventHandler] NPC_INTERACT: invalid characterId or clientId");
            return;
        }

        if (!isPlayerAlive(characterId))
        {
            log_->warn("[DialogueEventHandler] Dead character {} attempted NPC interaction", characterId);
            return;
        }

        // Get character data (throws if not found)
        CharacterDataStruct charData = gameServices_.getCharacterManager().getCharacterData(characterId);

        // Get NPC data (returns empty struct if not found)
        NPCDataStruct npc = gameServices_.getNPCManager().getNPCById(request.npcId);
        if (npc.id <= 0)
        {
            log_->error("[DialogueEventHandler] NPC_INTERACT: NPC " + std::to_string(request.npcId) + " not found");
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(clientId);
                nlohmann::json resp = ResponseBuilder()
                                          .setHeader("message", "NPC not found")
                                          .setHeader("hash", clientData.hash)
                                          .setHeader("clientId", clientId)
                                          .setHeader("eventType", "dialogueError")
                                          .setBody("errorCode", "NPC_NOT_FOUND")
                                          .build();
                networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("error", resp));
            }
            return;
        }

        if (!npc.isInteractable)
        {
            log_->info("[DialogueEventHandler] NPC " + std::to_string(request.npcId) + " not interactable");
            return;
        }

        // Range check
        if (!isPlayerInRange(charData.characterPosition, npc.position, static_cast<float>(npc.radius)))
        {
            gameServices_.getLogger().log("[DialogueEventHandler] Character " + std::to_string(characterId) + " out of range of NPC " + std::to_string(request.npcId));
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(clientId);
                nlohmann::json resp = ResponseBuilder()
                                          .setHeader("message", "Too far from NPC")
                                          .setHeader("hash", clientData.hash)
                                          .setHeader("clientId", clientId)
                                          .setHeader("eventType", "dialogueError")
                                          .setBody("errorCode", "OUT_OF_RANGE")
                                          .build();
                networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("error", resp));
            }
            return;
        }

        // Reputation check: block dialogue if player is enemy (rep < -500) with NPC's faction
        if (!npc.factionSlug.empty())
        {
            const int rep = gameServices_.getReputationManager().getReputation(characterId, npc.factionSlug);
            if (rep < -500)
            {
                log_->info("[DialogueEventHandler] NPC_INTERACT: char {} blocked by reputation (faction={}, rep={})",
                    characterId,
                    npc.factionSlug,
                    rep);
                if (clientSocket)
                {
                    auto clientData = gameServices_.getClientManager().getClientData(clientId);
                    nlohmann::json resp = ResponseBuilder()
                                              .setHeader("message", "Blocked by reputation")
                                              .setHeader("hash", clientData.hash)
                                              .setHeader("clientId", clientId)
                                              .setHeader("eventType", "dialogueError")
                                              .setBody("errorCode", "BLOCKED_BY_REPUTATION")
                                              .setBody("factionSlug", npc.factionSlug)
                                              .build();
                    networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("error", resp));
                }
                return;
            }
        }

        // Fire onNPCTalked BEFORE building ctx so talk-step quests advance
        // and the updated state is visible in condition evaluation for this interaction.
        gameServices_.getQuestManager().onNPCTalked(characterId, request.npcId);

        // Build player context (after onNPCTalked so quest step/state is up to date)
        PlayerContextStruct ctx = buildPlayerContext(charData);

        // Select best dialogue for this NPC
        int dialogueId = gameServices_.getDialogueManager().selectDialogueForNPC(request.npcId, ctx);
        if (dialogueId < 0)
        {
            log_->info("[DialogueEventHandler] No matching dialogue for NPC " + std::to_string(request.npcId));
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(clientId);
                nlohmann::json resp = ResponseBuilder()
                                          .setHeader("message", "No dialogue available")
                                          .setHeader("hash", clientData.hash)
                                          .setHeader("clientId", clientId)
                                          .setHeader("eventType", "dialogueError")
                                          .setBody("errorCode", "NO_DIALOGUE")
                                          .build();
                networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("error", resp));
            }
            return;
        }

        const DialogueGraphStruct *dialogue = gameServices_.getDialogueManager().getDialogueById(dialogueId);
        if (!dialogue)
            return;

        // Close any existing session for this character
        gameServices_.getDialogueSessionManager().closeSessionByCharacter(characterId);

        // Create new session
        DialogueSessionStruct &session = gameServices_.getDialogueSessionManager().createSession(
            clientId, characterId, request.npcId, dialogueId, dialogue->startNodeId);

        // Traverse to first interactive node
        int interactiveNodeId = traverseToInteractiveNode(*dialogue, dialogue->startNodeId, ctx, characterId, clientId);

        if (interactiveNodeId < 0)
        {
            // Dialogue ended immediately (all auto-nodes with end)
            gameServices_.getDialogueSessionManager().closeSession(session.sessionId);
            sendDialogueClose(clientId, session.sessionId);
            return;
        }

        // Update session's current node
        session.currentNodeId = interactiveNodeId;

        const DialogueNodeStruct &node = dialogue->nodes.at(interactiveNodeId);
        sendDialogueNode(clientId, session, node, *dialogue, ctx);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[DialogueEventHandler] handleNPCInteractEvent: " + std::string(ex.what()));
    }
}

void
DialogueEventHandler::handleDialogueChoiceEvent(const Event &event)
{
    int clientId = event.getClientID();

    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<DialogueChoiceRequestStruct>(data))
        {
            log_->error("[DialogueEventHandler] DIALOGUE_CHOICE: unexpected data type");
            return;
        }

        const auto &request = std::get<DialogueChoiceRequestStruct>(data);
        int characterId = request.characterId;

        if (characterId <= 0 || clientId <= 0)
            return;

        auto clientSocket = getClientSocket(event);

        // Get active session
        auto *session = gameServices_.getDialogueSessionManager().getSession(request.sessionId);
        if (!session || session->characterId != characterId)
        {
            log_->error("[DialogueEventHandler] DIALOGUE_CHOICE: session not found or mismatch for sessionId=" + request.sessionId);
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(clientId);
                nlohmann::json resp = ResponseBuilder()
                                          .setHeader("eventType", "dialogueError")
                                          .setHeader("hash", clientData.hash)
                                          .setHeader("clientId", clientId)
                                          .setBody("errorCode", "SESSION_EXPIRED")
                                          .build();
                networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("error", resp));
            }
            return;
        }

        const DialogueGraphStruct *dialogue = gameServices_.getDialogueManager().getDialogueById(session->dialogueId);
        if (!dialogue)
        {
            log_->error("[DialogueEventHandler] DIALOGUE_CHOICE: dialogue not found for id=" + std::to_string(session->dialogueId));
            gameServices_.getDialogueSessionManager().closeSession(session->sessionId);
            sendDialogueClose(clientId, request.sessionId);
            return;
        }

        // Find current node's edges and locate chosen edge
        auto edgeIt = dialogue->edges.find(session->currentNodeId);
        if (edgeIt == dialogue->edges.end())
        {
            log_->error("[DialogueEventHandler] DIALOGUE_CHOICE: no edges from node " + std::to_string(session->currentNodeId));
            gameServices_.getDialogueSessionManager().closeSession(session->sessionId);
            sendDialogueClose(clientId, request.sessionId);
            return;
        }

        const DialogueEdgeStruct *chosenEdge = nullptr;
        for (const auto &edge : edgeIt->second)
        {
            if (edge.id == request.edgeId)
            {
                chosenEdge = &edge;
                break;
            }
        }

        if (!chosenEdge)
        {
            gameServices_.getLogger().logError("[DialogueEventHandler] DIALOGUE_CHOICE: edge " + std::to_string(request.edgeId) + " not found in node " + std::to_string(session->currentNodeId));
            gameServices_.getDialogueSessionManager().closeSession(session->sessionId);
            sendDialogueClose(clientId, request.sessionId);
            return;
        }

        // Get character data for context
        CharacterDataStruct charData = gameServices_.getCharacterManager().getCharacterData(characterId);
        PlayerContextStruct ctx = buildPlayerContext(charData);

        // Evaluate edge condition
        if (!chosenEdge->conditionGroup.is_null() && !chosenEdge->conditionGroup.empty())
        {
            if (!DialogueConditionEvaluator::evaluate(chosenEdge->conditionGroup, ctx))
            {
                log_->info("[DialogueEventHandler] DIALOGUE_CHOICE: edge condition not met for edge " + std::to_string(request.edgeId));
                if (clientSocket)
                {
                    auto clientData = gameServices_.getClientManager().getClientData(clientId);
                    nlohmann::json resp = ResponseBuilder()
                                              .setHeader("eventType", "dialogueError")
                                              .setHeader("hash", clientData.hash)
                                              .setHeader("clientId", clientId)
                                              .setBody("errorCode", "CHOICE_LOCKED")
                                              .build();
                    networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("error", resp));
                }
                return;
            }
        }

        // Execute edge action group
        if (!chosenEdge->actionGroup.is_null() && !chosenEdge->actionGroup.empty())
        {
            DialogueActionExecutor executor(gameServices_, gameServices_.getLogger());
            auto result = executor.execute(chosenEdge->actionGroup, characterId, clientId, ctx);

            // Send any notifications from actions to client
            if (clientSocket)
            {
                for (const auto &notif : result.clientNotifications)
                {
                    const std::string eventType = notif.value("type", "notification");
                    nlohmann::json resp = ResponseBuilder()
                                              .setHeader("eventType", eventType)
                                              .build();
                    resp["body"] = notif;
                    networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", resp));
                }
            }

            // Forward any pending game-server packets (e.g. saveLearnedSkill)
            for (const auto &pkt : result.pendingGameServerPackets)
                gameServerWorker_.sendDataToGameServer(pkt);
        }

        // Move to next node
        int nextNodeId = chosenEdge->toNodeId;
        int interactiveNodeId = traverseToInteractiveNode(*dialogue, nextNodeId, ctx, characterId, clientId);

        if (interactiveNodeId < 0)
        {
            // End of dialogue
            gameServices_.getDialogueSessionManager().closeSession(session->sessionId);
            sendDialogueClose(clientId, request.sessionId);
            return;
        }

        // Update session's current node
        session->currentNodeId = interactiveNodeId;

        const DialogueNodeStruct &node = dialogue->nodes.at(interactiveNodeId);
        sendDialogueNode(clientId, *session, node, *dialogue, ctx);
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[DialogueEventHandler] handleDialogueChoiceEvent: " + std::string(ex.what()));
    }
}

void
DialogueEventHandler::handleDialogueCloseEvent(const Event &event)
{
    int clientId = event.getClientID();

    try
    {
        const auto &data = event.getData();
        if (!std::holds_alternative<DialogueCloseRequestStruct>(data))
        {
            log_->error("[DialogueEventHandler] DIALOGUE_CLOSE: unexpected data type");
            return;
        }

        const auto &request = std::get<DialogueCloseRequestStruct>(data);
        int characterId = request.characterId;

        // Close session
        gameServices_.getDialogueSessionManager().closeSessionByCharacter(characterId);

        // Acknowledge close to client
        sendDialogueClose(clientId, request.sessionId);

        log_->info("[DialogueEventHandler] Session closed for character " + std::to_string(characterId));
    }
    catch (const std::exception &ex)
    {
        gameServices_.getLogger().logError("[DialogueEventHandler] handleDialogueCloseEvent: " + std::string(ex.what()));
    }
}

// =============================================================================
// Private helpers
// =============================================================================

PlayerContextStruct
DialogueEventHandler::buildPlayerContext(const CharacterDataStruct &charData) const
{
    PlayerContextStruct ctx;
    ctx.characterId = charData.characterId;
    ctx.characterLevel = charData.characterLevel;

    for (const auto &flag : charData.flags)
    {
        if (flag.boolValue.has_value())
            ctx.flagsBool[flag.flagKey] = flag.boolValue.value();
        if (flag.intValue.has_value())
            ctx.flagsInt[flag.flagKey] = flag.intValue.value();
    }

    // Fill quest states from QuestManager
    gameServices_.getQuestManager().fillQuestContext(charData.characterId, ctx);

    // Fill item quantities for inventory-based conditions (item_<id> keys)
    const auto &inventory = gameServices_.getInventoryManager().getPlayerInventory(charData.characterId);
    for (const auto &invItem : inventory)
    {
        std::string key = "item_" + std::to_string(invItem.itemId);
        ctx.flagsInt[key] = invItem.quantity;
    }

    // Stage 4: fill reputation and mastery for dialogue conditions
    gameServices_.getReputationManager().fillReputationContext(charData.characterId, ctx);
    gameServices_.getMasteryManager().fillMasteryContext(charData.characterId, ctx);

    // Skill system: fill free skill points and learned skill slugs
    ctx.freeSkillPoints = charData.freeSkillPoints;
    for (const auto &skill : charData.skills)
        ctx.learnedSkillSlugs.insert(skill.skillSlug);

    return ctx;
}

int
DialogueEventHandler::traverseToInteractiveNode(
    const DialogueGraphStruct &dialogue,
    int startNodeId,
    PlayerContextStruct &ctx,
    int characterId,
    int clientId)
{
    int nodeId = startNodeId;
    constexpr int MAX_AUTO_TRAVERSE = 50; // safety cap

    for (int i = 0; i < MAX_AUTO_TRAVERSE; ++i)
    {
        auto nit = dialogue.nodes.find(nodeId);
        if (nit == dialogue.nodes.end())
        {
            log_->error("[DialogueEventHandler] traverseToInteractiveNode: node " + std::to_string(nodeId) + " not found");
            return -1;
        }

        const DialogueNodeStruct &node = nit->second;

        // Interactive nodes - stop and return
        if (node.type == "line" || node.type == "choice_hub")
            return nodeId;

        // Terminal node
        if (node.type == "end")
            return -1;

        // Auto nodes: evaluate condition first
        if (!node.conditionGroup.is_null() && !node.conditionGroup.empty())
        {
            if (!DialogueConditionEvaluator::evaluate(node.conditionGroup, ctx))
            {
                // Condition not met for auto node - treat as end
                return -1;
            }
        }

        // Execute action group if present
        if (!node.actionGroup.is_null() && !node.actionGroup.empty())
        {
            DialogueActionExecutor executor(gameServices_, gameServices_.getLogger());
            auto result = executor.execute(node.actionGroup, characterId, clientId, ctx);

            // Send notifications to client
            auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
            if (clientSocket)
            {
                for (const auto &notif : result.clientNotifications)
                {
                    const std::string eventType = notif.value("type", "notification");
                    nlohmann::json resp = ResponseBuilder()
                                              .setHeader("eventType", eventType)
                                              .build();
                    resp["body"] = notif;
                    networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", resp));
                }
            }

            // Forward any pending game-server packets (e.g. saveLearnedSkill)
            for (const auto &pkt : result.pendingGameServerPackets)
                gameServerWorker_.sendDataToGameServer(pkt);
        }

        // Jump node: move to target
        if (node.type == "jump")
        {
            if (node.jumpTargetNodeId <= 0)
                return -1;
            nodeId = node.jumpTargetNodeId;
            continue;
        }

        // Action node: follow first edge
        if (node.type == "action")
        {
            auto eit = dialogue.edges.find(nodeId);
            if (eit == dialogue.edges.end() || eit->second.empty())
                return -1;
            nodeId = eit->second[0].toNodeId;
            continue;
        }

        // Unknown type — stop
        return -1;
    }

    log_->error("[DialogueEventHandler] traverseToInteractiveNode: max auto-traverse exceeded");
    return -1;
}

nlohmann::json
DialogueEventHandler::buildChoicesJson(
    const DialogueGraphStruct &dialogue,
    int nodeId,
    const PlayerContextStruct &ctx) const
{
    nlohmann::json choices = nlohmann::json::array();

    auto eit = dialogue.edges.find(nodeId);
    if (eit == dialogue.edges.end())
        return choices;

    // Sort edges by orderIndex
    std::vector<const DialogueEdgeStruct *> sortedEdges;
    for (const auto &edge : eit->second)
        sortedEdges.push_back(&edge);
    std::sort(sortedEdges.begin(), sortedEdges.end(), [](const DialogueEdgeStruct *a, const DialogueEdgeStruct *b)
        { return a->orderIndex < b->orderIndex; });

    for (const DialogueEdgeStruct *edge : sortedEdges)
    {
        bool conditionMet = true;
        if (!edge->conditionGroup.is_null() && !edge->conditionGroup.empty())
            conditionMet = DialogueConditionEvaluator::evaluate(edge->conditionGroup, ctx);

        if (!conditionMet && edge->hideIfLocked)
            continue; // Hidden locked choice

        nlohmann::json choice;
        choice["edgeId"] = edge->id;
        choice["clientChoiceKey"] = edge->clientChoiceKey;
        choice["conditionMet"] = conditionMet;

        // Enrich choice with quest preview / turn-in preview / gift preview from actionGroup
        if (!edge->actionGroup.is_null() && edge->actionGroup.is_array())
        {
            auto &questManager = gameServices_.getQuestManager();
            for (const auto &action : edge->actionGroup)
            {
                const std::string actionType = action.value("type", "");
                const std::string questSlug = action.value("slug", "");

                if (actionType == "offer_quest")
                {
                    if (questSlug.empty())
                        continue;
                    const QuestStruct *quest = questManager.getQuestBySlug(questSlug);
                    if (quest)
                    {
                        nlohmann::json preview;
                        preview["questId"] = quest->id;
                        preview["clientQuestKey"] = quest->clientQuestKey;
                        if (!quest->steps.empty())
                        {
                            nlohmann::json firstStep = questManager.resolveStepForClient(quest->steps[0]);
                            firstStep["current"] = 0;
                            preview["firstStep"] = std::move(firstStep);
                        }
                        preview["rewards"] = questManager.resolveRewardsForClient(quest->rewards);
                        choice["questPreview"] = std::move(preview);
                    }
                    break; // Only one offer_quest action per edge expected
                }
                else if (actionType == "turn_in_quest")
                {
                    if (questSlug.empty())
                        continue;
                    const QuestStruct *quest = questManager.getQuestBySlug(questSlug);
                    if (quest)
                    {
                        nlohmann::json preview;
                        preview["questId"] = quest->id;
                        preview["clientQuestKey"] = quest->clientQuestKey;
                        preview["rewards"] = questManager.resolveRewardsForClient(quest->rewards);
                        choice["turnInPreview"] = std::move(preview);
                    }
                    break;
                }
            }

            // Collect all give_item / give_gold / give_exp into giftPreview
            nlohmann::json giftItems = nlohmann::json::array();
            for (const auto &action : edge->actionGroup)
            {
                const std::string actionType = action.value("type", "");
                if (actionType == "give_item")
                {
                    int itemId = action.value("item_id", -1);
                    int quantity = action.value("quantity", 1);
                    if (itemId > 0)
                    {
                        ItemDataStruct item = gameServices_.getItemManager().getItemById(itemId);
                        nlohmann::json g;
                        g["giftType"] = "item";
                        g["item_slug"] = item.slug;
                        g["quantity"] = quantity;
                        giftItems.push_back(std::move(g));
                    }
                }
                else if (actionType == "give_gold")
                {
                    int amount = action.value("amount", 0);
                    if (amount > 0)
                    {
                        nlohmann::json g;
                        g["giftType"] = "gold";
                        g["amount"] = amount;
                        giftItems.push_back(std::move(g));
                    }
                }
                else if (actionType == "give_exp")
                {
                    int amount = action.value("amount", 0);
                    if (amount > 0)
                    {
                        nlohmann::json g;
                        g["giftType"] = "exp";
                        g["amount"] = amount;
                        giftItems.push_back(std::move(g));
                    }
                }
            }
            if (!giftItems.empty())
                choice["giftPreview"] = std::move(giftItems);
        }

        choices.push_back(std::move(choice));
    }

    return choices;
}

void
DialogueEventHandler::sendDialogueNode(
    int clientId,
    const DialogueSessionStruct &session,
    const DialogueNodeStruct &node,
    const DialogueGraphStruct &dialogue,
    const PlayerContextStruct &ctx,
    const std::string &hash)
{
    auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
    if (!clientSocket)
        return;

    nlohmann::json body;
    body["sessionId"] = session.sessionId;
    body["npcId"] = session.npcId;
    body["nodeId"] = node.id;
    body["clientNodeKey"] = node.clientNodeKey;
    body["type"] = node.type;
    body["speakerNpcId"] = node.speakerNpcId;

    // Always send outgoing choices for all interactive node types.
    // For 'line' nodes with no choices or a single no-key edge the client
    // shows a generic "Continue" button; for 'choice_hub' / 'line' with
    // multiple keyed edges the client renders explicit choice buttons.
    body["choices"] = buildChoicesJson(dialogue, node.id, ctx);

    nlohmann::json packet = ResponseBuilder()
                                .setHeader("eventType", "DIALOGUE_NODE")
                                .build();
    packet["body"] = body;

    networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", packet));
}

void
DialogueEventHandler::sendDialogueClose(int clientId, const std::string &sessionId, const std::string &hash)
{
    auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
    if (!clientSocket)
        return;

    nlohmann::json packet = ResponseBuilder()
                                .setHeader("eventType", "DIALOGUE_CLOSE")
                                .setBody("sessionId", sessionId)
                                .build();

    networkManager_.sendResponse(clientSocket, networkManager_.generateResponseMessage("success", packet));
}

bool
DialogueEventHandler::isPlayerInRange(
    const PositionStruct &playerPos,
    const PositionStruct &npcPos,
    float radius) const
{
    float dx = playerPos.positionX - npcPos.positionX;
    float dy = playerPos.positionY - npcPos.positionY;
    return std::sqrt(dx * dx + dy * dy) <= radius;
}
