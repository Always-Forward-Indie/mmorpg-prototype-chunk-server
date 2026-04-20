#pragma once
#include "services/AmbientSpeechManager.hpp"
#include "services/BestiaryManager.hpp"
#include "services/ChampionManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/CharacterStatsNotificationService.hpp"
#include "services/ChunkManager.hpp"
#include "services/ClientManager.hpp"
#include "services/DialogueConditionEvaluator.hpp"
#include "services/DialogueManager.hpp"
#include "services/DialogueSessionManager.hpp"
#include "services/EmoteManager.hpp"
#include "services/EquipmentManager.hpp"
#include "services/ExperienceCacheManager.hpp"
#include "services/ExperienceManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameZoneManager.hpp"
#include "services/HarvestManager.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"
#include "services/LootManager.hpp"
#include "services/MasteryManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/MobMovementManager.hpp"
#include "services/NPCManager.hpp"
#include "services/PityManager.hpp"
#include "services/QuestManager.hpp"
#include "services/RegenManager.hpp"
#include "services/ReputationManager.hpp"
#include "services/RespawnZoneManager.hpp"
#include "services/SkillManager.hpp"
#include "services/SpawnZoneManager.hpp"
#include "services/StatusEffectTemplateManager.hpp"
#include "services/TitleManager.hpp"
#include "services/TradeSessionManager.hpp"
#include "services/TrainerManager.hpp"
#include "services/VendorManager.hpp"
#include "services/WorldObjectManager.hpp"
#include "services/ZoneEventManager.hpp"
#include "utils/Logger.hpp"
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

class GameServices
{
  public:
    // Constructor
    // Initializes all managers with the logger
    GameServices(Logger &logger)
        : logger_(logger),
          mobManager_(logger_),
          itemManager_(logger_),
          mobInstanceManager_(logger_),
          mobMovementManager_(logger_),
          spawnZoneManager_(mobManager_, logger_),
          characterManager_(logger_),
          clientManager_(logger_),
          chunkManager_(logger_),
          lootManager_(itemManager_, logger_),
          inventoryManager_(itemManager_, logger_),
          harvestManager_(itemManager_, logger_),
          npcManager_(logger_),
          dialogueManager_(logger_),
          dialogueSessionManager_(logger_),
          questManager_(this, logger_),
          skillManager_(this),
          experienceManager_(this),
          experienceCacheManager_(this),
          statsNotificationService_(this),
          gameConfigService_(logger_),
          vendorManager_(itemManager_, logger_),
          trainerManager_(itemManager_, logger_),
          tradeSessionManager_(logger_),
          equipmentManager_(inventoryManager_, itemManager_, characterManager_, logger_),
          respawnZoneManager_(logger_),
          gameZoneManager_(logger_),
          statusEffectTemplateManager_(logger_),
          regenManager_(this),
          pityManager_(logger_),
          bestiaryManager_(logger_),
          championManager_(this),
          reputationManager_(logger_),
          masteryManager_(this),
          titleManager_(this),
          zoneEventManager_(this),
          emoteManager_(this),
          ambientSpeechManager_(),
          worldObjectManager_(logger_)
    {
        // Set up manager dependencies
        spawnZoneManager_.setMobInstanceManager(&mobInstanceManager_);
        mobMovementManager_.setMobInstanceManager(&mobInstanceManager_);
        mobMovementManager_.setSpawnZoneManager(&spawnZoneManager_);
        mobMovementManager_.setCharacterManager(&characterManager_);

        // Set up harvest manager dependencies
        harvestManager_.setInventoryManager(&inventoryManager_);

        // Set up equipment manager dependencies
        equipmentManager_.setGameConfigService(&gameConfigService_);

        // Wire loot manager dependencies (pity)
        lootManager_.setPityManager(&pityManager_);
        lootManager_.setGameServices(this);
    }

    Logger &getLogger()
    {
        return logger_;
    }
    MobManager &getMobManager()
    {
        return mobManager_;
    }
    ItemManager &getItemManager()
    {
        return itemManager_;
    }
    MobInstanceManager &getMobInstanceManager()
    {
        return mobInstanceManager_;
    }
    MobMovementManager &getMobMovementManager()
    {
        return mobMovementManager_;
    }
    SpawnZoneManager &getSpawnZoneManager()
    {
        return spawnZoneManager_;
    }
    CharacterManager &getCharacterManager()
    {
        return characterManager_;
    }
    ClientManager &getClientManager()
    {
        return clientManager_;
    }
    ChunkManager &getChunkManager()
    {
        return chunkManager_;
    }
    LootManager &getLootManager()
    {
        return lootManager_;
    }
    InventoryManager &getInventoryManager()
    {
        return inventoryManager_;
    }
    HarvestManager &getHarvestManager()
    {
        return harvestManager_;
    }
    SkillManager &getSkillManager()
    {
        return skillManager_;
    }
    ExperienceManager &getExperienceManager()
    {
        return experienceManager_;
    }
    ExperienceCacheManager &getExperienceCacheManager()
    {
        return experienceCacheManager_;
    }
    CharacterStatsNotificationService &getStatsNotificationService()
    {
        return statsNotificationService_;
    }
    NPCManager &getNPCManager()
    {
        return npcManager_;
    }
    DialogueManager &getDialogueManager()
    {
        return dialogueManager_;
    }
    DialogueSessionManager &getDialogueSessionManager()
    {
        return dialogueSessionManager_;
    }
    QuestManager &getQuestManager()
    {
        return questManager_;
    }
    GameConfigService &getGameConfigService()
    {
        return gameConfigService_;
    }
    VendorManager &getVendorManager()
    {
        return vendorManager_;
    }
    TrainerManager &getTrainerManager()
    {
        return trainerManager_;
    }
    TradeSessionManager &getTradeSessionManager()
    {
        return tradeSessionManager_;
    }
    EquipmentManager &getEquipmentManager()
    {
        return equipmentManager_;
    }
    RespawnZoneManager &getRespawnZoneManager()
    {
        return respawnZoneManager_;
    }
    GameZoneManager &getGameZoneManager()
    {
        return gameZoneManager_;
    }
    StatusEffectTemplateManager &getStatusEffectTemplateManager()
    {
        return statusEffectTemplateManager_;
    }
    RegenManager &getRegenManager()
    {
        return regenManager_;
    }
    PityManager &getPityManager()
    {
        return pityManager_;
    }
    BestiaryManager &getBestiaryManager()
    {
        return bestiaryManager_;
    }
    ChampionManager &getChampionManager()
    {
        return championManager_;
    }
    ReputationManager &getReputationManager()
    {
        return reputationManager_;
    }
    MasteryManager &getMasteryManager()
    {
        return masteryManager_;
    }
    TitleManager &getTitleManager()
    {
        return titleManager_;
    }
    ZoneEventManager &getZoneEventManager()
    {
        return zoneEventManager_;
    }
    EmoteManager &getEmoteManager()
    {
        return emoteManager_;
    }
    AmbientSpeechManager &getAmbientSpeechManager()
    {
        return ambientSpeechManager_;
    }
    WorldObjectManager &getWorldObjectManager()
    {
        return worldObjectManager_;
    }

    // Analytics sender: set by the EventHandler layer so that inner services can
    // fire analytics events to the game server without a direct dependency on
    // GameServerWorker.
    void setAnalyticsSender(std::function<void(const std::string &)> fn)
    {
        analyticsSender_ = std::move(fn);
    }
    void sendAnalytics(const std::string &data)
    {
        if (analyticsSender_)
            analyticsSender_(data);
    }

    // Skill cooldown persistence: called from CombatSystem when a player uses a skill.
    // Sends a fire-and-forget saveSkillCooldown message to the game server.
    void setSkillCooldownSender(std::function<void(const std::string &)> fn)
    {
        skillCooldownSender_ = std::move(fn);
    }
    void sendSkillCooldownPersist(int characterId, const std::string &skillSlug, int64_t cooldownEndsAtMs)
    {
        logger_.log("[GameServices] sendSkillCooldownPersist char=" + std::to_string(characterId) +
                        " skill=" + skillSlug + " senderSet=" + (skillCooldownSender_ ? "yes" : "no"),
            YELLOW);
        if (skillCooldownSender_)
        {
            nlohmann::json msg;
            msg["header"]["eventType"] = "saveSkillCooldown";
            msg["body"]["characterId"] = characterId;
            msg["body"]["skillSlug"] = skillSlug;
            msg["body"]["cooldownEndsAtMs"] = cooldownEndsAtMs;
            logger_.log("[GameServices] dispatching saveSkillCooldown: " + msg.dump(), YELLOW);
            skillCooldownSender_(msg.dump() + "\n");
        }
    }

  private:
    Logger &logger_;
    GameConfigService gameConfigService_; // FIRST: initialized before all managers
    MobManager mobManager_;
    ItemManager itemManager_;
    MobInstanceManager mobInstanceManager_;
    MobMovementManager mobMovementManager_;
    SpawnZoneManager spawnZoneManager_;
    CharacterManager characterManager_;
    ClientManager clientManager_;
    ChunkManager chunkManager_;
    LootManager lootManager_;
    InventoryManager inventoryManager_;
    HarvestManager harvestManager_;
    NPCManager npcManager_;
    DialogueManager dialogueManager_;
    DialogueSessionManager dialogueSessionManager_;
    QuestManager questManager_;
    SkillManager skillManager_;
    ExperienceManager experienceManager_;
    ExperienceCacheManager experienceCacheManager_;
    CharacterStatsNotificationService statsNotificationService_;
    VendorManager vendorManager_;
    TrainerManager trainerManager_;
    TradeSessionManager tradeSessionManager_;
    EquipmentManager equipmentManager_;
    RespawnZoneManager respawnZoneManager_;
    GameZoneManager gameZoneManager_;
    StatusEffectTemplateManager statusEffectTemplateManager_;
    RegenManager regenManager_;
    PityManager pityManager_;
    BestiaryManager bestiaryManager_;
    ChampionManager championManager_;
    ReputationManager reputationManager_;
    MasteryManager masteryManager_;
    TitleManager titleManager_;
    ZoneEventManager zoneEventManager_;
    EmoteManager emoteManager_;
    AmbientSpeechManager ambientSpeechManager_;
    WorldObjectManager worldObjectManager_;
    std::function<void(const std::string &)> analyticsSender_ = [](const std::string &) {};
    std::function<void(const std::string &)> skillCooldownSender_ = [](const std::string &) {};
};
