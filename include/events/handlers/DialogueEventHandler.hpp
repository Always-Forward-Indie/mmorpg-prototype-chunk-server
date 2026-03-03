#pragma once

#include "data/DataStructs.hpp"
#include "events/Event.hpp"
#include "events/handlers/BaseEventHandler.hpp"

/**
 * @brief Handler for dialogue and quest data events.
 *
 * Handles:
 *  - Static data loaded from game-server (dialogues, quests)
 *  - Per-character runtime data (player quests, player flags)
 *  - Client-driven interactions (NPC_INTERACT, DIALOGUE_CHOICE, DIALOGUE_CLOSE)
 */
class DialogueEventHandler : public BaseEventHandler
{
  public:
    DialogueEventHandler(
        NetworkManager &networkManager,
        GameServerWorker &gameServerWorker,
        GameServices &gameServices);

    // === Static data events (game-server → chunk-server) ===

    /** SET_ALL_DIALOGUES: receive all dialogue graphs from game-server. */
    void handleSetAllDialoguesEvent(const Event &event);

    /** SET_NPC_DIALOGUE_MAPPINGS: receive NPC→dialogue priority mappings. */
    void handleSetNPCDialogueMappingsEvent(const Event &event);

    /** SET_ALL_QUESTS: receive all quest static definitions. */
    void handleSetAllQuestsEvent(const Event &event);

    // === Per-character runtime data events ===

    /** SET_PLAYER_QUESTS: load active quest progress for a joining character. */
    void handleSetPlayerQuestsEvent(const Event &event);

    /** SET_PLAYER_FLAGS: load saved flags for a joining character. */
    void handleSetPlayerFlagsEvent(const Event &event);

    // === Client-driven interaction events ===

    /** NPC_INTERACT: player requests dialogue with an NPC. */
    void handleNPCInteractEvent(const Event &event);

    /** DIALOGUE_CHOICE: player selects a dialogue edge/choice. */
    void handleDialogueChoiceEvent(const Event &event);

    /** DIALOGUE_CLOSE: player closes the dialogue window. */
    void handleDialogueCloseEvent(const Event &event);

  private:
    /**
     * @brief Build a PlayerContextStruct for condition evaluation.
     */
    PlayerContextStruct buildPlayerContext(const CharacterDataStruct &charData) const;

    /**
     * @brief Traverse auto-nodes (action/jump) until reaching an interactive node.
     * Executes action groups along the way.
     * @return The id of the first interactive node, or -1 if end reached.
     */
    int traverseToInteractiveNode(
        const DialogueGraphStruct &dialogue,
        int startNodeId,
        PlayerContextStruct &ctx,
        int characterId,
        int clientId);

    /**
     * @brief Build the choices JSON array for a choice_hub node.
     * Evaluates edge conditions; hides locked edges if hideIfLocked is true.
     */
    nlohmann::json buildChoicesJson(
        const DialogueGraphStruct &dialogue,
        int nodeId,
        const PlayerContextStruct &ctx) const;

    /**
     * @brief Send DIALOGUE_NODE packet to the client.
     */
    void sendDialogueNode(
        int clientId,
        const DialogueSessionStruct &session,
        const DialogueNodeStruct &node,
        const DialogueGraphStruct &dialogue,
        const PlayerContextStruct &ctx,
        const std::string &hash = "");

    /**
     * @brief Send DIALOGUE_CLOSE packet to the client.
     */
    void sendDialogueClose(int clientId, const std::string &sessionId, const std::string &hash = "");

    /**
     * @brief Check if player is within interaction range of the NPC.
     */
    bool isPlayerInRange(const PositionStruct &playerPos,
        const PositionStruct &npcPos,
        float radius) const;
};
