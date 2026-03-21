v0.1.1
21.03.2026
================
New:
ChatEventHandler — new chat message handler (zone/whisper channels); ChatEventHandler.cpp and ChatEventHandler.hpp registered in CMakeLists.txt.
ChatChannel enum and ChatMessageStruct added to DataStructs.hpp.
MobArchetype enum (MELEE, CASTER, RANGED, SUPPORT) — stores AI archetype as an enum for cheap hot-path comparison instead of a string aiArchetype field.
EventDispatcher — handleGetBestiaryOverview(): handles client request for a list of all known mobs.
EventHandler — handleGetBestiaryOverviewEvent(): sends the client the current bestiary overview (mob slug + kill count list).

Improvements:
MobMovementManager — unified tick `runMobTick()`: single method covering all states (patrol, chase, flee, return). Replaces two separate timers (moveMobInZoneTask@1000ms + aggroMobMovementTask@50ms) with one task using internal timing guards.
MobMovementManager — `updateLastBroadcastMs()`: per-mob last-broadcast timestamp; patrol updates capped at 200 ms, combat updates capped at 100 ms. Removes the global lock that serialized all zones.
MobMovementData — new fields for client Dead Reckoning: dirX, dirY, speed (units/sec), combatState, lastStepTimestampMs; waitingForMeleeSlot for queuing mobs when melee slots are occupied; lastBroadcastMs for rate-limiting.
Mob patrol timing — significantly reduced: inter-step pause 2-6 s (was 10-40 s), inter-patrol pause 2-5 s (was 12-28 s), initial spawn delay ≤1 s (was ≤5 s), random cooldown 1-3 s (was 5-15 s). Mobs are visibly active immediately after server start.
MobAIController — `countMobsEngagingTarget()`: counts mobs attacking the same player within a given radius; used for melee slot management and crowd jitter prevention.
MobAIController — `executeMobAttack()`: extracted as a dedicated method for mob attack execution (internal refactoring).
CombatSystem — `clearOngoingAction()`: removes the ongoing action entry after a skill is executed immediately, preventing a second execution by updateOngoingActions().
CombatSystem — `processAIAttack()` accepts an optional `forcedSkillSlug` parameter to force a specific skill for AI attacks.
CombatResult and CombatInitiationResult — new `serverTimestamp` field (unix ms) for client-side network latency compensation during animations.
SkillSystem — `isGCDActive()`: read-only Global Cooldown check without consuming it (used for pre-cast validation).
BestiaryManager — `getKnownMobs()`: returns a list of (mobTemplateId, killCount) pairs for all mobs killed by a character.
BestiaryManager — `setNotifyCallback()`: callback with 6-tier information unlock system (thresholds 1/10/25/50/100/300 kills: T1 — basic info, T2 — lore, T3 — combat info, T4 — loot table, T5 — drop rates, T6 — hunter mastery).
BestiaryManager — `setKillUpdateCallback()`: callback fired on every kill; pushes updated kill count to the client without a full re-request.
BestiaryManager kill update wired in ChunkServer: updated kill count is automatically sent to the client on every mob kill.
CharacterStatsNotificationService — `setDirectSendCallback()`: directed notification delivery to a specific character instead of broadcasting to the whole zone.
CharacterStatsNotificationService — new notification methods with `priority` (critical|high|medium|low|ambient) and `channel` (screen_center|toast|float_text|atmosphere|chat_log) parameters; auto-incrementing `notifSeq_` for unique notificationId.
CharacterStatsNotificationService — `sendWorldNotification()`, `sendGameZoneNotification()`: send notifications to all players in a zone.
CombatEventHandler — calls `clearOngoingAction()` after immediate skill execution.
docs/ updated: client-combat-protocol.md, client-progression-protocol.md, client-server-protocol.md, client-world-systems-protocol.md — brought in line with current implementation.

Removed:
docs/death-respawn-plan.md — removed (superseded by the implemented protocol in client-death-respawn-protocol.md).

Bug Fixes:
Fixed double skill execution: updateOngoingActions() could re-execute a skill that was already executed immediately in the combat handler — fixed via clearOngoingAction().
Fixed visual jitter when multiple mobs attack the same player: melee slot queuing (waitingForMeleeSlot) parks excess mobs just outside the ring until a slot is free.

---

v0.1.0
15.03.2026
================
New:
13 new service managers added: EquipmentManager (equipment slots, carry weight), VendorManager (vendor shop data), TradeSessionManager (player-to-player trading), RespawnZoneManager, GameZoneManager (PvP/safe zones), StatusEffectTemplateManager (DoT/HoT/buff templates), RegenManager (HP/MP regeneration ticks), PityManager (drop pity counters), BestiaryManager (enemy kill tracker), ChampionManager (timed champion spawns), ReputationManager (faction reputation), MasteryManager (weapon/skill mastery), ZoneEventManager (zone event templates).
New event handlers: VendorEventHandler (buy/sell/repair/restock), EquipmentEventHandler (equip/unequip/get equipment).
New data structures: EquipSlot enum, ItemUseEffectStruct, Vendor* structs (VendorInventoryItemStruct, VendorNPCDataStruct, OpenVendorShopRequestStruct, BuyItemRequestStruct, SellItemRequestStruct, BuyBatchRequestStruct, SellBatchRequestStruct, VendorStockUpdateStruct), Repair* structs (OpenRepairShopRequestStruct, RepairItemRequestStruct, RepairAllRequestStruct), Trade* structs (TradeOfferItemStruct, TradeSessionStruct, TradeRequestStruct, TradeOfferUpdateStruct, TradeConfirmCancelStruct), Equipment* structs (EquipItemRequestStruct, EquipmentSlotItemStruct, CharacterEquipmentStruct, SaveEquipmentChangeStruct), DurabilityEntryStruct / DurabilityUpdateStruct, SaveCurrencyTransactionStruct / SaveDurabilityChangeStruct, RespawnZoneStruct, GameZoneStruct, TimedChampionTemplate, StatusEffectModifierDef, StatusEffectTemplate, RespawnRequestStruct, TimedChampionKilledStruct.
New event types dispatched: PLAYER_RESPAWN, SET_MOB_WEAKNESSES_RESISTANCES, INVENTORY_UPDATE, ITEM_DROP_BY_PLAYER, ITEM_REMOVE, USE_ITEM, SET_RESPAWN_ZONES, SET_STATUS_EFFECT_TEMPLATES, SET_GAME_ZONES, SET_TIMED_CHAMPION_TEMPLATES, SET_PLAYER_INVENTORY, SET_PLAYER_PITY, SET_PLAYER_BESTIARY, GET_BESTIARY_ENTRY, SET_PLAYER_REPUTATIONS, SET_PLAYER_MASTERIES, SET_ZONE_EVENT_TEMPLATES, SAVE_REPUTATION, SAVE_MASTERY, INVENTORY_ITEM_ID_SYNC, all vendor/trade/equipment events (BUY_ITEM, SELL_ITEM, EQUIP_ITEM, UNEQUIP_ITEM, GET_EQUIPMENT, TRADE_REQUEST, TRADE_ACCEPT, TRADE_DECLINE, TRADE_OFFER_UPDATE, TRADE_CONFIRM, TRADE_CANCEL, REPAIR_ITEM, REPAIR_ALL, etc.).
CombatSystem — applySkillEffects(): applies DoT/HoT/buff/debuff active effects from skills onto characters.
CharacterManager — new methods: addActiveEffect(), restoreManaToCharacter(), markCharacterInCombat(), setCharacterFlags(), processEffectTicks(), setLastValidatedMovement().
InventoryManager — removeItemFromInventoryById(): remove item by inventory row ID (for vendor sell / trade flows).
InventoryManager — setSaveInventoryCallback(): immediate DB persistence on every item add/remove.
docs/ folder added with protocol documentation.

Improvements:
CombatSystem — cast time anti-spam guard (blocks new skill cast while in cast time); animation duration capped to attack cycle; weapon durability deduction on hit; mastery experience gain on mob hit; experience debt on player death (instead of immediate XP loss); equipment durability penalty on death; effect ticks processing each game loop iteration.
SkillSystem — Global Cooldown (GCD) enforcement; trySetCooldown() extracted as separate method; mana consumed atomically before cooldown check (refunded if on cooldown); mob-as-caster now uses full player-equivalent damage pipeline.
MobAIController — combat initiation packet sent before the result packet so client can play cast/swing animation; hit delay includes cast + swing time; skill range check converted to correct world-unit scale.
MobManager — loadWeaknessesResistances(), getWeaknessesForMob(), getResistancesForMob() added.
MobMovementManager — always sends mob update on combat-state transitions even if the mob did not move (fixes mob appearing frozen on CHASING→PREPARING_ATTACK).
SpawnZoneManager — rare mob spawn support; social mob group support; zone AABB bounds loaded from DB.
LootManager — champion/rare mob loot multiplier; zone-event loot multiplier; pity system (soft pity at 300 kills, hard pity at 800, hint at 500); item reservation with expiry; instanced item ownership transfer; periodic cleanup of expired ground drops (5 min).
CharacterEventHandler (join) — sends ground items snapshot; sends all spawn zones with live mob state; sends zone_entered notification with zone slug/level range/PvP/safe flags.
ItemEventHandler — dead player cannot pick up items; carry weight update after pickup; item JSON now includes reservedForCharacterId / reservationSecondsLeft / droppedByCharacterId; removed redundant name/description fields from item JSON (slug is the canonical identifier).
QuestManager — loadPlayerQuests() sends full quest journal to client immediately on login; fillQuestContext() now populates questCurrentStep alongside questStates; offerQuest() pre-fills collect progress when player already holds required items; offerQuest() runs checkStepCompletion immediately; turnInQuest() rewritten with snapshot-before-release pattern to eliminate mutex deadlock.
InventoryManager — fixed same-thread deadlock: write lock released before calling QuestManager.onItemObtained(); logging now uses item slug instead of name.
CharacterStatsNotificationService — stats packet now includes: carry weight (current / limit / overweight flag); effective attributes (base + equipment bonuses + active-effect stat modifiers + passive skill modifiers + item soul tier bonus); active effects display list.
ChunkServer (main loop) — wired up persistence callbacks for inventory, durability, item kill count (soul), attribute refresh trigger on durability warning, pity, bestiary, champion, reputation, mastery; periodic task to save all online player HP/MP every 10 s; HP/MP regeneration task (every 4 s driven by config keys); periodic ground-item cleanup task (every 60 s); main loop tick set to 100 ms.
CMakeLists.txt — new source files registered.

Bug Fixes:
Fixed mob state update not being sent to client when mob transitioned states without moving.
Fixed InventoryManager → QuestManager deadlock caused by holding exclusive inventory mutex while calling onItemObtained.
Fixed QuestManager → InventoryManager deadlock in turnInQuest caused by holding quest mutex while granting item rewards.

---

v0.0.3
07.03.2026
================
New:
Per-subsystem logging via spdlog — each component (combat, mob, skill, character, item, harvest, dialogue, npc, chunk, client, zone, experience, spawn, network, events, gameloop, config) now logs to its own named channel. Each subsystem level can be set independently via environment variables (LOG_LEVEL_<NAME>).
Added JSONParser utility for convenient JSON data handling.
Added TimestampUtils utility.

Improvements:
MobMovementManager — deep refactor of movement and AI logic.
CombatSystem — major refactor of combat calculations and flow.
CombatCalculator — extended and improved.
CharacterManager — extended, improved character data handling.
SpawnZoneManager — reworked.
EventHandler / EventDispatcher — significantly extended.
GameServerWorker — refactored.
Scheduler — refactored.
Dockerfile — minor update.
docker-compose: LOG_LEVEL=info by default; network, gameloop, events set to warn to reduce log noise.

Removed:
Removed outdated .md documents (AGGRO_SYSTEM_GUIDE, ATTACK_SYSTEM_GUIDE, COMBAT_SKILL_PACKETS, etc.).
Removed test script test_request_id.py.

---

v0.0.2
28.02.2026
================
New:
Add changelog file to track changes and updates in the project.
Save current experience and level to game server DB immediately upon grant.
Save current player position to game server DB periodically and upon player logout.

Bug Fixes:
Fixed incorrect experience thresholds for current and next levels in character data responses.