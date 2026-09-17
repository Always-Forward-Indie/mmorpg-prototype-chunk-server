#include "services/DialogueActionExecutor.hpp"
#include "services/DialogueEnvelopeParser.hpp"
#include "services/DialogueNotificationBuilders.hpp"
#include "services/GameServices.hpp"
#include "services/ItemManager.hpp"
#include "services/LearnSkillValidator.hpp"
#include "services/QuestManager.hpp"
#include "services/RepairCostCalculator.hpp"
#include "services/TrainerManager.hpp"
#include "services/VendorManager.hpp"
#include <spdlog/logger.h>

DialogueActionExecutor::DialogueActionExecutor(GameServices &services, Logger &logger)
    : services_(services), logger_(logger)
{
    log_ = logger.getSystem("dialogue");
}

void
DialogueActionExecutor::sendDialogueAnalytics(const std::string &analyticsType,
    int characterId,
    const nlohmann::json &payload)
{
    // Best-effort: analytics must never break dialogue rewards. Debug-level
    // so a failing analytics path stays traceable without spamming.
    try
    {
        auto charData = services_.getCharacterManager().getCharacterData(characterId);
        if (charData.sessionId.empty())
            return;
        nlohmann::json ap;
        ap["header"]["eventType"] = "analyticsEvent";
        ap["body"]["analyticsType"] = analyticsType;
        ap["body"]["characterId"] = characterId;
        ap["body"]["sessionId"] = charData.sessionId;
        ap["body"]["level"] = charData.characterLevel;
        ap["body"]["zoneId"] = 0;
        ap["body"]["payload"] = payload;
        services_.sendAnalytics(ap.dump() + "\n");
    }
    catch (const std::exception &e)
    {
        log_->debug("[DialogueAction] analytics '{}' for char={} skipped ({})",
            analyticsType, characterId, e.what());
    }
    catch (...)
    {
        log_->debug("[DialogueAction] analytics '{}' for char={} skipped (unknown)",
            analyticsType, characterId);
    }
}

DialogueActionExecutor::ActionResult
DialogueActionExecutor::execute(const nlohmann::json &actionGroup,
    int characterId,
    int clientId,
    PlayerContextStruct &ctx)
{
    ActionResult result;

    for (const auto &parsed : parseDialogueActionGroup(actionGroup))
        executeDispatch(parsed.action, parsed.type, characterId, clientId, ctx, result);

    return result;
}

// Helper to dispatch a single action by type
void
DialogueActionExecutor::executeDispatch(const nlohmann::json &action, const std::string &type, int characterId, int clientId, PlayerContextStruct &ctx, ActionResult &result)
{
    if (type == "set_flag")
        executeSetFlag(action, characterId, ctx, result);
    else if (type == "offer_quest")
        executeOfferQuest(action, characterId, clientId, ctx, result);
    else if (type == "turn_in_quest")
        executeTurnInQuest(action, characterId, clientId, result);
    else if (type == "fail_quest")
        executeFailQuest(action, characterId, result);
    else if (type == "advance_quest_step")
        executeAdvanceQuestStep(action, characterId, result);
    else if (type == "give_item")
        executeGiveItem(action, characterId, clientId, result);
    else if (type == "give_exp")
        executeGiveExp(action, characterId, clientId, result);
    else if (type == "give_gold")
        executeGiveGold(action, characterId, clientId, result);
    else if (type == "open_vendor_shop")
        executeOpenVendorShop(action, characterId, clientId, result);
    else if (type == "open_repair_shop")
        executeOpenRepairShop(action, characterId, clientId, result);
    else if (type == "open_skill_shop")
        executeOpenSkillShop(action, characterId, clientId, ctx, result);
    else if (type == "change_reputation")
        executeChangeReputation(action, characterId, ctx, result);
    else if (type == "learn_skill")
        executeLearnSkill(action, characterId, clientId, ctx, result);
    else if (type == "set_object_state")
        executeSetObjectState(action, characterId, clientId, ctx, result);
    else
        log_->info("[DialogueAction] Unknown action type: " + type);
}

void
DialogueActionExecutor::executeSetFlag(const nlohmann::json &action,
    int characterId,
    PlayerContextStruct &ctx,
    ActionResult &result)
{
    if (!action.contains("key"))
        return;

    const std::string key = action["key"].get<std::string>();

    if (action.contains("bool_value"))
    {
        bool val = action["bool_value"].get<bool>();
        ctx.flagsBool[key] = val;
        services_.getQuestManager().setFlagBool(characterId, key, val);
    }
    else if (action.contains("int_value"))
    {
        int val = action["int_value"].get<int>();
        ctx.flagsInt[key] = val;
        services_.getQuestManager().setFlagInt(characterId, key, val);
    }
    else if (action.contains("inc"))
    {
        int delta = action["inc"].get<int>();
        int newVal = ctx.flagsInt[key] + delta;
        ctx.flagsInt[key] = newVal;
        services_.getQuestManager().setFlagInt(characterId, key, newVal);
    }
}

void
DialogueActionExecutor::executeOfferQuest(const nlohmann::json &action,
    int characterId,
    int clientId,
    PlayerContextStruct &ctx,
    ActionResult &result)
{
    if (!action.contains("slug"))
        return;

    const std::string slug = action["slug"].get<std::string>();
    auto &questManager = services_.getQuestManager();

    if (questManager.offerQuest(characterId, slug))
    {
        ctx.questStates[slug] = "active";

        // Analytics: quest_accept (best-effort, never throws)
        sendDialogueAnalytics("quest_accept", characterId, {{"questSlug", slug}});

        // Build client notification
        const QuestStruct *quest = questManager.getQuestBySlug(slug);
        if (quest)
        {
            // Enrich: first step with resolved slugs + rewards
            nlohmann::json firstStep;
            bool hasStep = false;
            if (!quest->steps.empty())
            {
                firstStep = questManager.resolveStepForClient(quest->steps[0]);
                firstStep["current"] = 0;
                hasStep = true;
            }

            result.clientNotifications.push_back(DialogueNotificationBuilders::questOffered(
                quest->id,
                quest->clientQuestKey,
                firstStep,
                hasStep,
                questManager.resolveRewardsForClient(quest->rewards)));
        }

        log_->info("[DialogueAction] Offered quest '" + slug + "' to character " +
                   std::to_string(characterId));
    }
}

void
DialogueActionExecutor::executeTurnInQuest(const nlohmann::json &action,
    int characterId,
    int clientId,
    ActionResult &result)
{
    if (!action.contains("slug"))
        return;

    const std::string slug = action["slug"].get<std::string>();
    auto &questManager = services_.getQuestManager();

    auto notifications = questManager.turnInQuest(characterId, slug, clientId);
    for (auto &n : notifications)
        result.clientNotifications.push_back(std::move(n));

    // Analytics: quest_complete (best-effort, never throws)
    sendDialogueAnalytics("quest_complete", characterId, {{"questSlug", slug}});
}

void
DialogueActionExecutor::executeAdvanceQuestStep(const nlohmann::json &action,
    int characterId,
    ActionResult &result)
{
    if (!action.contains("slug"))
        return;

    const std::string slug = action["slug"].get<std::string>();
    services_.getQuestManager().advanceQuestStepBySlug(characterId, slug);
}

void
DialogueActionExecutor::executeFailQuest(const nlohmann::json &action,
    int characterId,
    ActionResult &result)
{
    if (!action.contains("slug"))
        return;

    const std::string slug = action["slug"].get<std::string>();
    auto &questManager = services_.getQuestManager();

    if (questManager.failQuest(characterId, slug))
    {
        const QuestStruct *quest = questManager.getQuestBySlug(slug);
        if (quest)
        {
            result.clientNotifications.push_back(
                DialogueNotificationBuilders::questFailed(quest->id, quest->clientQuestKey));

            // Notify client of reputation change if the quest has auto-rep on fail
            // (the actual rep change was already applied inside QuestManager::failQuest)
            if (!quest->reputationFactionSlug.empty() && quest->reputationOnFail != 0)
            {
                result.clientNotifications.push_back(DialogueNotificationBuilders::reputationChanged(
                    quest->reputationFactionSlug, quest->reputationOnFail));
            }
        }

        log_->info("[DialogueAction] Failed quest '" + slug + "' for character " +
                   std::to_string(characterId));

        // Analytics: quest_abandon (best-effort, never throws)
        sendDialogueAnalytics("quest_abandon", characterId, {{"questSlug", slug}});
    }
}

void
DialogueActionExecutor::executeGiveItem(const nlohmann::json &action,
    int characterId,
    int clientId,
    ActionResult &result)
{
    if (!action.contains("item_id"))
        return;

    int itemId = action["item_id"].get<int>();
    int quantity = action.value("quantity", 1);

    bool ok = services_.getInventoryManager().addItemToInventory(characterId, itemId, quantity);
    if (ok)
    {
        ItemDataStruct item = services_.getItemManager().getItemById(itemId);
        result.clientNotifications.push_back(
            DialogueNotificationBuilders::itemReceived(itemId, item.slug, quantity));

        // Analytics: item_acquired (best-effort, never throws)
        sendDialogueAnalytics("item_acquired", characterId,
            {{"source", "dialogue"}, {"itemSlug", item.slug}, {"quantity", quantity}});
    }
}

void
DialogueActionExecutor::executeGiveExp(const nlohmann::json &action,
    int characterId,
    int clientId,
    ActionResult &result)
{
    if (!action.contains("amount"))
        return;

    int64_t amount = action["amount"].get<int64_t>();
    auto expResult = services_.getExperienceManager().grantExperience(
        characterId, static_cast<int>(amount), "quest_reward", 0);

    if (expResult.success)
    {
        result.clientNotifications.push_back(DialogueNotificationBuilders::expReceived(amount));
    }
}

void
DialogueActionExecutor::executeGiveGold(const nlohmann::json &action,
    int characterId,
    int clientId,
    ActionResult &result)
{
    if (!action.contains("amount"))
        return;

    int64_t amount = action["amount"].get<int64_t>();
    if (amount <= 0)
        return;

    // Resolve gold item by slug "gold_coin"
    const ItemDataStruct *goldItem = services_.getItemManager().getItemBySlug("gold_coin");
    if (!goldItem)
    {
        log_->error("[DialogueAction] give_gold: item 'gold_coin' not found in ItemManager");
        return;
    }

    bool ok = services_.getInventoryManager().addItemToInventory(
        characterId, goldItem->id, static_cast<int>(amount));

    if (ok)
    {
        result.clientNotifications.push_back(DialogueNotificationBuilders::goldReceived(amount));

        // Analytics: gold_change (best-effort, never throws)
        sendDialogueAnalytics("gold_change", characterId,
            {{"source", "dialogue_give_gold"}, {"delta", static_cast<int>(amount)}});
    }
}

void
DialogueActionExecutor::executeOpenVendorShop(const nlohmann::json &action,
    int characterId,
    int clientId,
    ActionResult &result)
{
    // Get NPC id from the player's active dialogue session
    auto *session = services_.getDialogueSessionManager().getSessionByCharacter(characterId);
    if (!session)
    {
        log_->error("[DialogueAction] open_vendor_shop: no active dialogue session for character " +
                    std::to_string(characterId));
        return;
    }
    int npcId = session->npcId;

    float markupPct = static_cast<float>(
        services_.getGameConfigService().getFloat("economy.vendor_buy_markup_pct", VendorManager::kDefaultBuyMarkupPct));

    nlohmann::json shopData = services_.getVendorManager().buildShopJson(npcId, markupPct);
    if (shopData.is_null())
    {
        log_->warn("[DialogueAction] open_vendor_shop: no shop data for npc " + std::to_string(npcId));
        return;
    }

    const auto &npc = services_.getNPCManager().getNPCById(npcId);

    result.clientNotifications.push_back(DialogueNotificationBuilders::openVendorShop(
        action.value("mode", "shop"), npcId, npc.slug, std::move(shopData)));
}

void
DialogueActionExecutor::executeOpenRepairShop(const nlohmann::json &action,
    int characterId,
    int clientId,
    ActionResult &result)
{
    auto *session = services_.getDialogueSessionManager().getSessionByCharacter(characterId);
    if (!session)
    {
        log_->error("[DialogueAction] open_repair_shop: no active dialogue session for character " +
                    std::to_string(characterId));
        return;
    }

    // Collect ALL durable items (equipped and non-equipped) with repair cost
    auto inventory = services_.getInventoryManager().getPlayerInventory(characterId);

    std::vector<RepairCostInput> repairInputs;
    repairInputs.reserve(inventory.size());
    for (const auto &invSlot : inventory)
    {
        const auto &iData = services_.getItemManager().getItemById(invSlot.itemId);
        RepairCostInput in;
        in.inventoryItemId = invSlot.id;
        in.itemId = invSlot.itemId;
        in.itemSlug = iData.slug;
        in.durabilityCurrent = invSlot.durabilityCurrent;
        in.isDurable = iData.isDurable;
        in.durabilityMax = iData.durabilityMax;
        in.vendorPriceBuy = iData.vendorPriceBuy;
        repairInputs.push_back(std::move(in));
    }

    nlohmann::json items = nlohmann::json::array();
    for (const auto &entry : computeRepairEntries(repairInputs))
    {
        nlohmann::json jsonEntry;
        jsonEntry["inventoryItemId"] = entry.inventoryItemId;
        jsonEntry["itemId"] = entry.itemId;
        jsonEntry["itemName"] = entry.itemName;
        jsonEntry["durabilityCurrent"] = entry.durabilityCurrent;
        jsonEntry["durabilityMax"] = entry.durabilityMax;
        jsonEntry["repairCost"] = entry.repairCost;
        items.push_back(std::move(jsonEntry));
    }

    result.clientNotifications.push_back(DialogueNotificationBuilders::openRepairShop(
        session->npcId,
        services_.getInventoryManager().getGoldAmount(characterId),
        std::move(items)));
}

// ── change_reputation ──────────────────────────────────────────────────────
void
DialogueActionExecutor::executeChangeReputation(const nlohmann::json &action,
    int characterId,
    PlayerContextStruct &ctx,
    ActionResult &result)
{
    if (!action.contains("faction") || !action.contains("delta"))
        return;

    const std::string faction = action["faction"].get<std::string>();
    int delta = action["delta"].get<int>();

    services_.getReputationManager().changeReputation(characterId, faction, delta);

    // Update in-context snapshot so subsequent conditions in the same node see the change
    auto &rep = ctx.reputations[faction];
    rep += delta;

    log_->info("[DialogueAction] change_reputation: char=" + std::to_string(characterId) +
               " faction=" + faction + " delta=" + std::to_string(delta));

    result.clientNotifications.push_back(DialogueNotificationBuilders::reputationChanged(faction, delta));
}

// ── open_skill_shop ───────────────────────────────────────────────────────
// Action JSON: {"type":"open_skill_shop"}
// Resolves the NPC from the active dialogue session, queries TrainerManager
// for the full skill list with per-skill affordability flags and sends
// an openSkillShop notification to the client.
void
DialogueActionExecutor::executeOpenSkillShop(const nlohmann::json & /*action*/,
    int characterId,
    int /*clientId*/,
    PlayerContextStruct &ctx,
    ActionResult &result)
{
    auto *session = services_.getDialogueSessionManager().getSessionByCharacter(characterId);
    if (!session)
    {
        log_->error("[DialogueAction] open_skill_shop: no active session for char {}", characterId);
        return;
    }
    int npcId = session->npcId;

    const CharacterDataStruct &charData = services_.getCharacterManager().getCharacterData(characterId);
    ctx.characterLevel = charData.characterLevel;

    nlohmann::json skillsJson = services_.getTrainerManager().buildSkillShopJson(
        npcId, ctx, services_.getInventoryManager());

    if (skillsJson.is_null())
    {
        log_->warn("[DialogueAction] open_skill_shop: npc {} is not a registered trainer", npcId);
        return;
    }

    const auto &npc = services_.getNPCManager().getNPCById(npcId);

    result.clientNotifications.push_back(DialogueNotificationBuilders::openSkillShop(npcId,
        npc.slug,
        ctx.freeSkillPoints,
        services_.getInventoryManager().getGoldAmount(characterId),
        std::move(skillsJson)));

    log_->info("[DialogueAction] open_skill_shop: char={} npc={}", characterId, npcId);
}

// ── learn_skill ───────────────────────────────────────────────────────────
//               "sp_cost":1,"gold_cost":500,
//               "requires_book":false,"book_item_id":0}
void
DialogueActionExecutor::executeLearnSkill(const nlohmann::json &action,
    int characterId,
    int clientId,
    PlayerContextStruct &ctx,
    ActionResult &result)
{
    LearnSkillRequest req;
    if (!parseLearnSkillRequest(action, req))
    {
        log_->error("[DialogueAction] learn_skill: missing skill_slug");
        return;
    }
    const std::string &skillSlug = req.skillSlug;

    // Resolve validator state from managers + ctx
    LearnSkillState st;
    st.learnedSlugs = ctx.learnedSkillSlugs;
    st.freeSkillPoints = ctx.freeSkillPoints;
    const auto &inv = services_.getInventoryManager().getPlayerInventory(characterId);
    if (req.goldCost > 0)
    {
        const ItemDataStruct *goldItem = services_.getItemManager().getItemBySlug("gold_coin");
        if (!goldItem)
        {
            log_->error("[DialogueAction] learn_skill: gold_coin item not found");
            return;
        }
        st.goldItemKnown = true;
        st.goldItemId = goldItem->id;
        for (const auto &slot : inv)
            if (slot.itemId == goldItem->id)
                st.totalGold += slot.quantity;
    }
    if (req.requiresBook && req.bookItemId > 0)
    {
        for (const auto &slot : inv)
            if (slot.itemId == req.bookItemId && slot.quantity > 0)
            {
                st.hasBook = true;
                break;
            }
    }

    const LearnSkillError validationError = validateLearnSkill(req, st);
    if (validationError != LearnSkillError::None)
    {
        result.clientNotifications.push_back(DialogueNotificationBuilders::learnSkillFailed(
            learnSkillErrorReason(validationError), skillSlug));
        return;
    }

    // Consume skill book
    if (req.requiresBook && req.bookItemId > 0)
    {
        services_.getInventoryManager().removeItemFromInventory(characterId, req.bookItemId, 1);
    }

    // Consume gold
    if (req.goldCost > 0)
    {
        services_.getInventoryManager().removeItemFromInventory(characterId, st.goldItemId, req.goldCost);
    }

    // Deduct SP in-memory
    services_.getCharacterManager().modifyFreeSkillPoints(characterId, -req.spCost);
    ctx.freeSkillPoints -= req.spCost;
    if (ctx.freeSkillPoints < 0)
        ctx.freeSkillPoints = 0;

    // Update ctx so subsequent conditions work
    ctx.learnedSkillSlugs.insert(skillSlug);

    // Queue saveLearnedSkill packet to game server
    nlohmann::json packet;
    packet["header"]["eventType"] = "saveLearnedSkill";
    packet["header"]["clientId"] = clientId;
    packet["header"]["hash"] = "";
    packet["body"]["characterId"] = characterId;
    packet["body"]["clientId"] = clientId;
    packet["body"]["skillSlug"] = skillSlug;
    result.pendingGameServerPackets.push_back(packet.dump() + "\n");

    log_->info("[DialogueAction] learn_skill: char={} skill={} sp={} gold={}",
        characterId,
        skillSlug,
        req.spCost,
        req.goldCost);
}

void
DialogueActionExecutor::executeSetObjectState(const nlohmann::json &action,
    int /*characterId*/,
    int /*clientId*/,
    PlayerContextStruct &ctx,
    ActionResult &result)
{
    int objectId = action.value("object_id", 0);
    if (objectId <= 0)
    {
        log_->warn("[DialogueAction] set_object_state: missing object_id");
        return;
    }

    const std::string newState = action.value("state", "active");

    // Update WorldObjectManager global state
    services_.getWorldObjectManager().setGlobalState(objectId, newState);

    // Encode state into the player context flagsInt so that subsequent
    // object_state conditions within the same dialogue can see the change.
    static const std::unordered_map<std::string, int> stateCode = {
        {"active", 0}, {"depleted", 1}, {"disabled", 2}};
    auto it = stateCode.find(newState);
    ctx.flagsInt["wio_state_" + std::to_string(objectId)] = (it != stateCode.end()) ? it->second : 0;

    log_->info("[DialogueAction] set_object_state: objectId={} → {}", objectId, newState);

    // Queue a broadcast so the calling DialogueEventHandler can push
    // worldObjectStateUpdate to all connected clients.
    result.pendingObjectStateBroadcasts.push_back({objectId, newState, 0});
}
