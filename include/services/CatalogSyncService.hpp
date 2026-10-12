#pragma once

#include "events/Event.hpp"
#include "network/NetworkManager.hpp"
#include "services/GameServices.hpp"

#include <cstdint>
#include <memory>
#include <spdlog/logger.h>

class CharacterEventHandler;
class CombatEventHandler;
class EmoteEventHandler;
class EquipmentEventHandler;
class SkillEventHandler;

/**
 * @brief Game→chunk catalog sync (extracted from EventHandler).
 *
 * One-way SET_* pushes from the game server (JOIN catalog + runtime
 * refreshes): exp table, game config, active effects, cooldowns, inventory,
 * attributes refresh, status templates, pity, bestiary, reputations,
 * masteries, zone events, learned skills, titles, mastery defs, skill bar,
 * emotes. Pure manager syncs; a few send ack/snapshot responses via
 * NetworkManager (same as before the move).
 *
 * catalogVersion_ counts processed syncs (observability + future diff hook).
 */
class CatalogSyncService
{
  public:
    CatalogSyncService(NetworkManager &networkManager, GameServices &gameServices);

    // Cross-handler refs (nullable, set by EventHandler after construction —
    // same pattern as CharacterEventHandler setters). Syncs degrade to
    // error logs when a peer is missing, never crash.
    void setCharacterEventHandler(CharacterEventHandler *h) { characterEventHandler_ = h; }
    void setCombatEventHandler(CombatEventHandler *h) { combatEventHandler_ = h; }
    void setEmoteEventHandler(EmoteEventHandler *h) { emoteEventHandler_ = h; }
    void setEquipmentEventHandler(EquipmentEventHandler *h) { equipmentEventHandler_ = h; }
    void setSkillEventHandler(SkillEventHandler *h) { skillEventHandler_ = h; }

    void handleSetExpLevelTableEvent(const Event &event);
    void handleSetGameConfigEvent(const Event &event);
    void handleSetPlayerActiveEffectsEvent(const Event &event);
    void handleSetPlayerSkillCooldownsEvent(const Event &event);
    void handleSetPlayerInventoryEvent(const Event &event);
    void handleSetCharacterAttributesRefreshEvent(const Event &event);
    void handleSetStatusEffectTemplatesEvent(const Event &event);
    void handleSetPlayerPityEvent(const Event &event);
    void handleSetPlayerBestiaryEvent(const Event &event);
    void handleSetPlayerReputationsEvent(const Event &event);
    void handleSetPlayerMasteriesEvent(const Event &event);
    void handleSetZoneEventTemplatesEvent(const Event &event);
    void handleSetLearnedSkillEvent(const Event &event);
    void handleSetTitleDefinitionsEvent(const Event &event);
    void handleSetMasteryDefinitionsEvent(const Event &event);
    void handleSetPlayerTitlesEvent(const Event &event);
    void handleSetSkillBarSlotEvent(const Event &event);
    void handleSetEmoteDefinitionsEvent(const Event &event);
    void handleSetPlayerEmotesEvent(const Event &event);

    uint64_t catalogVersion() const { return catalogVersion_; }

  private:
    void bumpVersion() { ++catalogVersion_; }

    NetworkManager &networkManager_;
    GameServices &gameServices_;
    std::shared_ptr<spdlog::logger> log_;
    uint64_t catalogVersion_ = 0;

    CharacterEventHandler *characterEventHandler_ = nullptr;
    CombatEventHandler *combatEventHandler_ = nullptr;
    EmoteEventHandler *emoteEventHandler_ = nullptr;
    EquipmentEventHandler *equipmentEventHandler_ = nullptr;
    SkillEventHandler *skillEventHandler_ = nullptr;
};
