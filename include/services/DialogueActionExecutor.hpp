#pragma once
#include "data/DataStructs.hpp"
#include "utils/Logger.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// Forward declare to break circular dependency
class GameServices;

/**
 * @brief Executes action_group JSON against the player state.
 *
 * Called by DialogueEventHandler when:
 *  - Traversing action-type nodes automatically
 *  - Executing edge.action_group when the player makes a choice
 *
 * Supported action types (TZ section 3.5):
 *   set_flag          – set a player flag (bool or int increment)
 *   offer_quest       – put a quest in "offered" / "active" state
 *   turn_in_quest     – complete and reward a quest
 *   fail_quest        – fail/abandon a quest
 *   advance_quest_step – manually advance quest step
 *   give_item         – add item to player inventory
 *   give_exp          – grant experience
 *   give_gold         – grant gold coins (item id resolved by slug "gold_coin")
 *   open_vendor_shop  – open the NPC vendor shop UI
 *   open_repair_shop  – open the NPC repair shop UI
 *   open_skill_shop   – open the skill trainer shop UI (lists all teachable skills with costs)
 *   learn_skill       – validate and execute a single skill purchase
 */
class DialogueActionExecutor
{
  public:
    DialogueActionExecutor(GameServices &services, Logger &logger);

    struct ObjectStateBroadcast
    {
        int objectId;
        std::string state;
        int respawnSec{0};
    };

    struct ActionResult
    {
        /// JSON notifications to forward to the client (DIALOGUE_ACTION_RESULT body)
        std::vector<nlohmann::json> clientNotifications;
        /// JSON packet strings to send to the game server (e.g. saveLearnedSkill)
        std::vector<std::string> pendingGameServerPackets;
        /// World-object state broadcasts to dispatch after dialogue processing
        std::vector<ObjectStateBroadcast> pendingObjectStateBroadcasts;
    };

    /**
     * @brief Execute an action_group JSON.
     *
     * Mutates ctx in-place (flags, quest states) and schedules DB persistence.
     *
     * @param actionGroup     The action_group JSON object/array.
     * @param characterId     Character performing the action.
     * @param clientId        Client socket identifier for responses.
     * @param ctx             Player context – modified in place.
     * @return ActionResult with notifications to send to client.
     */
    ActionResult execute(const nlohmann::json &actionGroup,
        int characterId,
        int clientId,
        PlayerContextStruct &ctx);

  private:
    void executeDispatch(const nlohmann::json &action, const std::string &type, int characterId, int clientId, PlayerContextStruct &ctx, ActionResult &result);

    void executeSetFlag(const nlohmann::json &action,
        int characterId,
        PlayerContextStruct &ctx,
        ActionResult &result);

    void executeOfferQuest(const nlohmann::json &action,
        int characterId,
        int clientId,
        PlayerContextStruct &ctx,
        ActionResult &result);

    void executeTurnInQuest(const nlohmann::json &action,
        int characterId,
        int clientId,
        ActionResult &result);

    void executeFailQuest(const nlohmann::json &action,
        int characterId,
        ActionResult &result);

    void executeAdvanceQuestStep(const nlohmann::json &action,
        int characterId,
        ActionResult &result);

    void executeGiveItem(const nlohmann::json &action,
        int characterId,
        int clientId,
        ActionResult &result);

    void executeGiveExp(const nlohmann::json &action,
        int characterId,
        int clientId,
        ActionResult &result);

    void executeGiveGold(const nlohmann::json &action,
        int characterId,
        int clientId,
        ActionResult &result);

    void executeOpenVendorShop(const nlohmann::json &action,
        int characterId,
        int clientId,
        ActionResult &result);

    void executeOpenRepairShop(const nlohmann::json &action,
        int characterId,
        int clientId,
        ActionResult &result);

    // Skill trainer: open skill shop UI showing all learnable skills with costs
    // {"type":"open_skill_shop"}  — NPC resolved from active dialogue session
    void executeOpenSkillShop(const nlohmann::json &action,
        int characterId,
        int clientId,
        PlayerContextStruct &ctx,
        ActionResult &result);

    // Stage 4: change faction reputation via dialogue action
    // {"type":"change_reputation", "faction":"bandits", "delta":50}
    void executeChangeReputation(const nlohmann::json &action,
        int characterId,
        PlayerContextStruct &ctx,
        ActionResult &result);

    // Skill system: learn a skill by spending SP, gold, and optionally a skill book
    // {"type":"learn_skill","skill_slug":"shield_bash","sp_cost":1,"gold_cost":500,
    //  "requires_book":true,"book_item_id":18}
    void executeLearnSkill(const nlohmann::json &action,
        int characterId,
        int clientId,
        PlayerContextStruct &ctx,
        ActionResult &result);

    /// set_object_state: {"type":"set_object_state","object_id":N,"state":"depleted|active|disabled"}
    void executeSetObjectState(const nlohmann::json &action,
        int characterId,
        int clientId,
        PlayerContextStruct &ctx,
        ActionResult &result);

    GameServices &services_;
    Logger &logger_;
    std::shared_ptr<spdlog::logger> log_;
};
