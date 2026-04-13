v0.2.1
13.04.2026
================
New:
Emote System — реализована полная система анимаций-эмоций. Игрок получает список разблокированных эмоций при входе в мир и может воспроизводить их; анимация транслируется всем клиентам зоны. Доступные эмоции хранятся в БД, серверная сторона валидирует что эмоция разблокирована перед трансляцией (anti-cheat).

Migration 053 (`emote_definitions`, `character_emotes`) — таблица `emote_definitions` (id, slug, display_name, animation_name, category, is_default, sort_order, created_at); таблица `character_emotes` (character_id FK→characters, emote_slug FK→emote_definitions, unlocked_at, UNIQUE per character). Индекс `idx_char_emotes_character`. Seed: 13 эмоций (6 дефолтных `sit/wave/bow/laugh/cry/point`, 4 social, 3 dance). Дефолтные эмоции выданы всем существующим персонажам через CROSS JOIN INSERT ON CONFLICT DO NOTHING. Миграция применена; дамп обновлён.

`EmoteDefinitionStruct` / `UseEmoteRequestStruct` — два новых struct в `DataStructs.hpp`. `EmoteDefinitionStruct`: id, slug, displayName, animationName, category, isDefault, sortOrder. `UseEmoteRequestStruct`: characterId, clientId, emoteSlug, timestamps.

`SET_EMOTE_DEFINITIONS`, `SET_PLAYER_EMOTES`, `USE_EMOTE` — три новых типа событий в `Event.hpp` (chunk-server). `UseEmoteRequestStruct` и `std::vector<EmoteDefinitionStruct>` добавлены в `EventData` variant.

`EmoteManager` (новый сервис) — потокобезопасный runtime-кэш с `std::shared_mutex`. Хранит глобальный каталог определений (`unordered_map<string, EmoteDefinitionStruct>`) и per-player unlocks (`unordered_map<int, vector<string>>`). Методы: `loadEmoteDefinitions()`, `getEmoteDefinition(slug)`, `getAllDefinitions()` (сортировка по sortOrder), `loadPlayerEmotes(charId, slugs)`, `unloadPlayerEmotes(charId)`, `isUnlocked(charId, slug)`, `getPlayerEmotes(charId)`. Подключён в `GameServices` как `emoteManager_`.

`EmoteEventHandler` (новый обработчик) — расширяет `BaseEventHandler`. `handleSetEmoteDefinitionsEvent`: парсит JSON `body.emotes[]` → `EmoteManager::loadEmoteDefinitions`. `handleSetPlayerEmotesEvent`: парсит `characterId` + `emotes[]` slugs → `loadPlayerEmotes` → немедленно отправляет `player_emotes` пакет клиенту через `ClientManager`. `handleUseEmoteEvent`: валидирует `isUnlocked(charId, slug)`, при успехе транслирует `emoteAction` всем клиентам зоны (characterId, emoteSlug, animationName, serverTimestamp). `onPlayerDisconnect(charId)`: вызывает `unloadPlayerEmotes`.

`EventHandler` (chunk-server) — добавлен `emoteEventHandler_` (`unique_ptr<EmoteEventHandler>`), dispatch по `SET_EMOTE_DEFINITIONS`, `SET_PLAYER_EMOTES`, `USE_EMOTE`, методы-форвардеры.

`EventDispatcher` (chunk-server) — роутинг `eventType == "useEmote"` → `handleUseEmote()`: извлекает `emoteSlug` из `body`, создаёт `UseEmoteRequestStruct`, пушит `USE_EMOTE` событие.

`CharacterEventHandler` — при завершении pending-join запрашивает `getPlayerEmotesData` у game-server (аналогично `getPlayerTitlesData`).

`ClientEventHandler` — при отключении персонажа вызывает `EmoteManager::unloadPlayerEmotes(characterId)` в блоке unload (Stage 4, рядом с Reputation/Mastery).

`GameServerWorker` — добавлена маршрутизация входящих пакетов `setEmoteDefinitionsData` и `setPlayerEmotesData` от game-server.

`CMakeLists.txt` — добавлены `src/services/EmoteManager.cpp` и `src/events/handlers/EmoteEventHandler.cpp`.

`docs/client-emote-protocol.md` — новый документ: DB-схема, диаграммы потоков (chunk connect, character join, useEmote, disconnect), полные примеры JSON-пакетов (`useEmote`, `emoteAction`, `player_emotes`, межсерверные пакеты), таблица методов EmoteManager и EmoteEventHandler, инструкция добавления новых эмоций.

---

v0.2.0
13.04.2026
================
New:
Skill Bar Persistence — реализована система сохранения скилл-бара. Игрок может привязать выученный скилл к любому из 12 слотов (0–11). Привязки хранятся в новой таблице `character_skill_bar` (migration 051). При входе персонажа в зону слоты загружаются из БД и отправляются клиенту пакетом `skillBarState`. При изменении слота клиент отправляет `setSkillBarSlot`, сервер валидирует (скилл должен быть выучен), обновляет состояние in-memory через `CharacterManager::updateSkillBarSlot()` и персистирует fire-and-forget пакетом `saveSkillBarSlot` на game-server.

Migration 051 (`character_skill_bar`) — новая таблица хранения скилл-бара с PK (character_id, slot_index), CHECK slot_index 0-11, CASCADE DELETE.

`SkillBarSlotStruct` — новая структура данных в `DataStructs.hpp` (chunk-server и game-server): `slotIndex`, `skillSlug`. Game-server: `CharacterDataStruct` дополнен полем `skillBarSlots`. Chunk-server: аналогично + `SetSkillBarSlotRequestStruct` для запросов от клиента.

`SAVE_SKILL_BAR_SLOT` (game-server Event) — новый тип события для персистенции слота. `SET_SKILL_BAR_SLOT` (chunk-server Event) — новый тип события для обработки запроса клиента. `SetSkillBarSlotRequestStruct` добавлен в `EventData` variant.

Game-server DB — три новых prepared statements: `get_character_skill_bar`, `save_skill_bar_slot` (INSERT...ON CONFLICT DO UPDATE), `clear_skill_bar_slot` (DELETE).

`CharacterManager::updateSkillBarSlot()` (chunk-server) — thread-safe мутация in-memory вектора `skillBarSlots`: replace если слот занят, erase если `skillSlug` пуст, push_back иначе.

`setCharacterData` расширен полем `skillBarData` — массив объектов `{slotIndex, skillSlug}`. Пустые слоты не включаются.

`docs/api/08-progression-stats-protocol.md` — добавлен раздел 8.8 «Скилл-бар»: жизненный цикл, схема БД, все пакеты (`skillBarState`, `setSkillBarSlot`, `skillBarSlotUpdated`, `saveSkillBarSlot`), коды ошибок, prepared statements.

---

v0.1.9
11.04.2026
================
New:
`corpseRemoved` broadcast — новый пакет `corpseRemoved` рассылается всем клиентам при удалении трупа (полностью разграблен или истёк TTL). Тело: `{ type: "CORPSE_REMOVED", corpseUID, timestamp }`. До этого трупы накапливались на клиенте бесконечно — сервер удалял их молча, без уведомления.

Bug Fixes:
Move speed effects в positionCorrection — активные эффекты с `attributeSlug == "move_speed"` (не-тик модификаторы) теперь суммируются при валидации позиции игрока. Ранее скоростные баффы/дебаффы от скилов, предметов и квестов игнорировались, вызывая ложные `positionCorrection`-отклонения на забаффованных клиентах.
Cast-time skill false cooldown — `dispatchSkillAction` теперь вызывает `executeSkillUsage` с флагом `cooldownAlreadySet=true`. Без него `useSkill` повторно проверяла кулдаун (уже выставленный в `initiateSkillUsage`), считала скил на кд и прерывала выполнение — скилы с castTime никогда не наносили урон после завершения каста.
Corpse removal on full loot — полностью разграбленный труп теперь немедленно удаляется из реестра (`harvestableCorpses_`) и рассылает `corpseRemoved` всем клиентам прямо в `pickupCorpseLoot()`. Ранее запись оставалась до следующего прохода `cleanupOldCorpses`.
Mutex ordering deadlock prevention — в `pickupCorpseLoot()` `lootMutex_` освобождается до захвата `corpsesMutex_`. Ранее оба мьютекса могли удерживаться одновременно в разном порядке, создавая потенциальный дедлок с `cleanupOldCorpses`.
Corpse TTL cleanup broadcast — `cleanupOldCorpses()` теперь рассылает `corpseRemoved` для каждого истёкшего трупа после освобождения всех мьютексов.

Improvements:
Corpse TTL — уменьшен с 600 с (10 мин) до 30 с. Трупы убираются из мира значительно быстрее.

---

v0.1.8
08.04.2026
================
Bug Fixes:
Cast-time skill dispatch — `dispatchSkillAction` теперь корректно разделяет каст-скилы и мгновенные. При `castTime > 0` функция отправляет только `combatInitiation` и выходит; `updateOngoingActions()` вызывает `executeSkillUsage` по таймеру через `castMs` мс. Мгновенные скилы (`castMs == 0`) выполняются в `dispatchSkillAction` немедленно, как прежде. До фикса оба типа всегда выполнялись немедленно — каст-таймер фактически не работал.
Combat result timing — `action->endTime` теперь вычисляется как `startTime + castMs - swingMs`, а не `startTime + castMs`. Это означает, что `combatResult` прилетает клиенту в начале swing-фазы анимации (удар наносится визуально корректно) вместо того, чтобы прилетать уже после окончания всего каста. `animationDuration` упрощён до `castTime + swingTime` без предыдущего cap-логики с циклом и margin.

Improvements:
Reputation live updates — `ReputationManager::setClientNotifyCallback` и `setTierChangeCallback` теперь подключены в `ChunkServer`. При любом изменении репутации клиент получает пакет `reputation_update` (`characterId`, `factionSlug`, `value`, `tier`). При смене тира — `world_notification` типа `reputation_tier_change` (toast).
Mastery live updates — `MasteryManager::setClientNotifyCallback` теперь подключён в `ChunkServer`. При флаше прогресса мастерства клиент получает пакет `mastery_update` (`characterId`, `masterySlug`, `value`). Ранее обновления мастерства отправлялись клиенту только на логине.
Title callbacks — `TitleManager::setSaveCallback` и `setNotifyClientCallback` подключены в `ChunkServer`. Сохранение данных тайтлов на game-сервер и прямая отправка пакетов клиенту теперь работают в реальном времени.

---

v0.1.7
08.04.2026
================
Bug Fixes:
Cooldown starts at cast initiation — кулдаун скила теперь начинает отсчёт в момент нажатия кнопки (`initiateSkillUsage`), а не в момент прилёта результата (`executeSkillUsage`). Ранее при касте 4 с + кулдауне 5 с игрок вынужден был ждать суммарно ~9 с вместо ожидаемых 5 с. Изменение: `initiateSkillUsage` вызывает `trySetCooldown` атомарно (проверка + установка под одним `unique_lock`), заменяя отдельные вызовы `isOnCooldown` + `isGCDActive`. `CombatActionStruct` получил поле `cooldownPreset`; `updateOngoingActions` передаёт флаг в `executeSkillUsage` → `useSkill`, которая пропускает повторную проверку кулдауна.
TCP head-of-line blocking — пакеты позиций мобов больше не блокируют доставку боевых пакетов. Добавлена приоритетная очередь записи (`SocketWriteQueue`) с двумя уровнями: CRITICAL (бой, статы, все существующие `sendResponse`) и BULK (позиции мобов). Каждый сокет получает собственный ASIO `strand`, что также устраняет потенциальный UB от конкурентных `async_write` на одном сокете в многопоточном `io_context`. Новый метод `sendResponseBulk` используется в `MobEventHandler::handleMobMoveUpdateEvent`.

Improvements:
Mob update batching — тик мобов (50 мс) теперь собирает обновления из всех зон в единый `clientId → vector<MobMoveUpdateStruct>` и пушит **одно** событие `MOB_MOVE_UPDATE` на клиента вместо одного события на зону на клиента. При 5 зонах это снижает число `async_write` вызовов в 5 раз — меньше давления на буфер TCP-сокета.

---

v0.1.6
08.04.2026
================
Bug Fixes:
Combat cast blocking — во время активного каста (`CombatActionState::CASTING`) игрок больше не может использовать ни другой каст, ни мгновенный скил. Ранее проверка `initiateSkillUsage` применялась только к скилам с `castMs > 0`, позволяя мгновенным скилам обходить блокировку. Теперь проверка наличия CASTING-записи в `ongoingActions_` выполняется безусловно для любого скила.
Cast-time result timing — скилы с ненулевым `castTime` больше не выполняются немедленно. `dispatchSkillAction` теперь при `castTime > 0` отправляет только `combatInitiation` и возвращает управление; `updateOngoingActions()` пробуждает действие по таймеру через `castMs` мс и рассылает `combatResult` факт — что соответствует реальному времени каста.

Improvements:
Skill properties — всем активным скилам в БД установлены `cast_ms ≥ 1000 мс` и `swing_ms > cast_ms` (swing_ms ≥ 1000 мс). Для скилов `basic_attack`, `shield_bash`, `whirlwind` исправлены занулённые/слишком малые значения; для `power_slash`, `fireball`, `frost_bolt`, `arcane_blast`, `chain_lightning` добавлены ранее отсутствующие записи `swing_ms`. Итоговые значения cast → swing: basic_attack 1000→1200, shield_bash 1000→1100, whirlwind 1000→1200, power_slash 2000→2300, fireball 4000→4500, frost_bolt 2000→2300, arcane_blast 3000→3500, chain_lightning 2500→3000.

---

v0.1.5
07.04.2026
================
New:
Title System — `TitleManager` (новый сервис): хранит каталог определений титулов (`TitleDefinitionStruct`) и персональное состояние персонажей (`PlayerTitleStateStruct`). Методы: `setTitleDefinitions()`, `setPlayerTitles()`, `getAllTitles()`, `equipTitle()`, `grantTitle()`. При экипировке применяет бонусы как `ActiveEffectStruct` с `sourceType="title"`, `expiresAt=0`; при снятии — удаляет эффекты по slug через `CharacterManager::removeActiveEffectBySlug()`. `grantTitle()` отправляет `world_notification` типа `title_granted` и вызывает `ClientNotifyCallback` для отправки `player_titles_update`.
`TitleBonusStruct`, `TitleDefinitionStruct`, `PlayerTitleStateStruct`, `EquipTitleRequestStruct` — четыре новых структуры данных в `DataStructs.hpp`.
`SET_TITLE_DEFINITIONS_DATA`, `SET_PLAYER_TITLES_DATA`, `GET_TITLES`, `EQUIP_TITLE` — четыре новых типа событий в `Event.hpp`. `EquipTitleRequestStruct` добавлена в `EventData.hpp` variant.
`EventHandler` — 4 новых обработчика: `handleSetTitleDefinitionsDataEvent`, `handleSetPlayerTitlesDataEvent`, `handleGetTitlesEvent`, `handleEquipTitleEvent`. `handleEquipTitleEvent` после успешного экипирования отправляет `stats_update` с пересчитанными эффективными атрибутами.
`EventDispatcher` — маршрутизация клиентских пакетов `getTitles` и `equipTitle`.
`GameServerWorker` — маршрутизация входящих пакетов `setTitleDefinitionsData` и `setPlayerTitlesData` от game-сервера.
`CharacterManager::removeActiveEffectBySlug()` — удаление активного эффекта по slug (используется при смене надетого титула).
`CharacterEventHandler` — при `joinGameCharacter` отправляет `getPlayerTitlesData` на game-сервер вслед за запросами мастерства и репутации (оба login flow: нормальный и stale-eviction повторный вход).
`docs/migrations/049_title_system.sql` — миграция создаёт таблицы `title_definitions` и `character_titles`, seed из 4 тестовых титулов (wolf_slayer, first_blood, dungeon_delver, merchant). Миграция применена; дамп обновлён.

Improvements:
`ReputationManager` — `getAllReputations()` + `ClientNotifyCallback`: push `player_reputations` клиенту сразу после загрузки данных от game-сервера при логине.
`MasteryManager` — `getAllMasteries()` + `ClientNotifyCallback`: push `player_masteries` клиенту сразу после загрузки данных от game-сервера при логине.
`ChunkServer` — подключены callback'и `TitleManager`, `ReputationManager`, `MasteryManager` для уведомления клиента при логине.
`docs/api/08-progression-stats-protocol.md` — добавлены разделы 8.7 (система титулов): жизненный цикл на сервере, таблица мест хранения, формат бонусов как ActiveEffects, пакеты `getTitles`, `player_titles_update`, `equipTitle`, `title_granted`; уточнена последовательность событий при логине для репутации и мастерства; `freeSkillPoints` добавлен в JSON-пример `stats_update` и сопроводительную заметку.

---

v0.1.4
06.04.2026
================
Bug Fixes:
`requestLearnSkill` — `npc_not_found` при открытии магазина через диалог: клиент может не передавать `npcId` в теле пакета, если магазин был открыт через `open_skill_shop` action диалога. `handleRequestLearnSkillEvent` теперь, при `npcId == 0`, получает `npcId` из активной диалоговой сессии персонажа (`DialogueSessionManager::getSessionByCharacter`). Оба сценария работают корректно: прямое открытие (с `npcId`) и через диалог (без `npcId`).

Improvements:
`stats_update` packet — добавлено поле `freeSkillPoints` (int). Ранее свободные очки навыков приходили клиенту только при входе на сервер и в пакете `skill_learned`. Теперь актуальное значение отправляется при каждой генерации `stats_update` (level-up, equip-change, xp-gain и т.д.), что гарантирует синхронизацию HUD после получения уровня.
docs/api/05-npc-dialogue-quests-protocol.md — полная актуализация: исправлены форматы пакетов `spawnNPCs` (поле `npcsSpawn`, добавлены `spawnRadius`, `npcCount`, поля NPC), `npcInteract` (убраны лишние поля тела), `dialogueChoice` / `dialogueClose` (убраны `characterId`, `playerId`), формат нотификаций от actions, условная опциональность полей `QUEST_UPDATE`; добавлено поведение авто-нод при невыполненном условии.
docs/stats-update-protocol.md — добавлено поле `freeSkillPoints` в пример пакета и таблицу полей.

---

v0.1.3
06.04.2026
================
New:
Skill Trainer Shop UI — новое окно с полным списком навыков тренера и флагами доступности. Клиент отправляет прямой пакет `openSkillShop {npcId}` без прохождения диалога; chunk-сервер отвечает пакетом `skillShop` с массивом навыков, где каждый элемент содержит флаги `isLearned`, `canLearn`, `prereqMet`, `levelMet`, `spMet`, `goldMet`, `bookMet`. Параллельно добавлена диалоговая экшн-команда `open_skill_shop` в `DialogueActionExecutor` — отправляет тот же пакет прямо из узла диалога.
`requestLearnSkill` packet — прямой пакет обучения из UI тренера (без диалога). Проходит ту же цепочку валидаций, что и `learn_skill` в диалоге: уровень персонажа, пресет навыков, SP, золото, наличие книги навыка. При успехе потребляет ресурсы и отправляет `saveLearnedSkill` на game-сервер; ответ приходит стандартным `skill_learned` / `learn_skill_failed`. Новые коды ошибок: `insufficient_level`, `missing_prerequisite`, `npc_not_found`, `out_of_range`, `skill_not_available`.
TrainerManager — новый сервис (аналог `VendorManager`): хранит в памяти списки навыков для каждого NPC-тренера. Методы: `setTrainerData()`, `getTrainerByNpcId()`, `getSkillEntry()`, `buildSkillShopJson()`. Потокобезопасен (`shared_mutex`). Зарегистрирован в `GameServices`.
`SET_TRAINER_DATA` / `OPEN_SKILL_SHOP` / `REQUEST_LEARN_SKILL` — три новых типа событий. `GameServerWorker` маршрутизирует входящий пакет `setTrainerData` от game-сервера в `SET_TRAINER_DATA`; `EventDispatcher` парсит `openSkillShop` и `requestLearnSkill` от клиента. Все три случая обрабатываются `SkillEventHandler`.
`ClassSkillTreeEntryStruct` / `TrainerNPCDataStruct` / `OpenSkillShopRequestStruct` / `RequestLearnSkillRequestStruct` — четыре новых структуры данных в `DataStructs.hpp`.
DB Migration 048 — таблица `npc_trainer_class`: связывает NPC-тренера с классом, навыки которого он преподаёт. Seed: Theron (id=4) → Warrior (class_id=2), Sylara (id=5) → Mage (class_id=1). Дамп обновлён.
docs/skill-learning-system.md — новый раздел «Skill Trainer Shop UI»: описание пакетов `openSkillShop` / `skillShop` / `requestLearnSkill`, таблица флагов, таблица кодов ошибок.
docs/api/05-npc-dialogue-quests-protocol.md — добавлен блок `open_skill_shop` с примерами пакетов.

---

v0.1.2
05.04.2026
================
New:
Skill Learning System — `DialogueActionExecutor`: new `learn_skill` action type. Validates SP cost, gold cost, and optional skill book requirement before teaching a skill. Consumes the book (removed from inventory) and gold. On failure sends typed `learn_skill_failed` notification to client (`already_learned` / `insufficient_sp` / `insufficient_gold` / `missing_skill_book`). `PlayerContextStruct` extended with `freeSkillPoints` and `learnedSkillSlugs` for validation.
CharacterManager — `addCharacterSkill()`: upsert skill by slug into the character's in-memory skill list. `modifyFreeSkillPoints(delta)`: thread-safe increment/decrement. `getCharacterFreeSkillPoints()`: read-only SP query.
SET_LEARNED_SKILL event — `EventHandler::handleSetLearnedSkillEvent()`: receives full `SkillStruct` from game server after DB persistence, adds it to `CharacterManager`, decrements SP, and forwards `setLearnedSkill` packet to the owning client.
CharacterDataStruct — new field `freeSkillPoints` (int, default 0). Included in the character join response and character data updates.
playerReady two-phase join protocol — world-state push (mobs, NPCs, ground items, connected players, equipment) is deferred until the client sends `playerReady` (scene finished loading). Phase 1+2 (character private data: stats/quests/flags/inventory/effects/masteries/reputations) still sent immediately on join. `ClientManager`: `setClientWorldReady()` / `isClientWorldReady()` flag per client. New `CharacterEventHandler::handlePlayerReadyEvent()` executes Phase 4.
Stale session eviction — `CharacterEventHandler::evictStaleSession(characterId, newClientId)`: if a character is already loaded in `CharacterManager` when a new join arrives (crash / reconnect without clean disconnect), the stale session is cleanly flushed: position, HP/Mana, quests, flags, reputation, mastery and ItemSoul `killCount` are persisted, the old client/character entries are removed, and a `disconnectClient` broadcast is sent to all other connected clients. Three guard conditions prevent false evictions: pre-loaded data (no client mapped), duplicate own reconnect (same clientId), and already-absent entries.
`pendingJoinRequestsMutex_` — mutex added to `CharacterEventHandler` to serialise concurrent access to `pendingJoinRequests_` map.
Equipment broadcast to other clients — `EquipmentEventHandler::broadcastEquipmentUpdate()`: sends `PLAYER_EQUIPMENT_UPDATE` to all clients except the owner whenever gear changes (equip/unequip). Broadcast is guarded by `isClientWorldReady` to avoid sending equipment for characters that are not yet in other clients' scenes. Phase 4 (`handlePlayerReadyEvent`) also pushes all existing characters' equipment to the newly ready client via `pushConnectedCharactersToClient()`.
AoE skill execution — `CombatSystem::executeAoESkillUsage()`: iterates nearby mobs and players, applies damage with per-target crit roll, marks caster in-combat, sends batched `combatAoeResult` packet via new `CombatResponseBuilder::buildAoESkillExecutionBroadcast()`. `AoESkillExecutionResult` and `AoETargetResultEntry` structs added.
ItemSoul kill-count flush on disconnect — `ClientEventHandler` and `evictStaleSession` both send `saveItemKillCount` packet to game server when disconnecting, so unsaved kills are not lost.
QuestManager: `markFlagsLoaded` / `areFlagsLoaded` / `clearFlagsLoaded` — tracks per-character flag-load state. Used to defer exploration XP awards until the character's persisted flags have arrived from the game server, preventing duplicate exploration grants.
QuestManager: `getQuestStateBySlug(characterId, questSlug)` — look up quest state by human-readable slug without exposing internal integer quest IDs.
NPC `questSlugs` field — `NPCDataStruct` and `NPCEventHandler` now expose the list of quest slugs for which an NPC is giver or turn-in target. Sent to clients in the NPC spawn packet.
DB Migrations (untracked, docs/migrations/): 042_npc_type_quest_binding.sql, 043_skill_system_schema.sql, 044_warrior_skills.sql, 045_mage_skills.sql, 046_skill_books.sql, 047_trainer_npcs_and_dialogues.sql.
docs/skill-learning-system.md — full specification: trainer NPCs, skill table, skill books, SP/gold/book flow, packet sequence, DB schema notes.
docs/api/ — new API directory (untracked).

Improvements:
ClientEventHandler — disconnect handler: idempotency guard skips cleanup if client was already removed (prevents double-free on duplicate disconnect events). Character save path now checks `CharacterManager.getCharacterData()` before attempting position/HP-Mana persist; logs a warning and skips if character was never fully loaded (e.g., incomplete join). `clearFlagsLoaded` called on every character unload path.
CombatSystem — target re-validation before damage: checks target is still alive (not dead/absent) immediately before spending mana and applying effects. Prevents hitting a mob that died between action initiation and execution. In-combat marking (`markCharacterInCombat`) applied to caster on skill execution to suppress mana regeneration during fights. `finalCasterMana` captured after mana consumption and included in `SkillExecutionResult` and `CombatAoEResult` for client-side HUD updates without a separate stats packet.
CombatResponseBuilder — `buildSkillExecutionBroadcast()`: adds `cooldownMs`, `gcdMs`, and `finalCasterMana` fields to the response body.
CombatCalculator — `critMultiplier` now reads from the attacker's **effective** attributes (after skill/buff modifiers) instead of base attributes, fixing under-powered crits for buffed characters.
MobMovementManager — movement broadcast skip for stationary states: PREPARING_ATTACK / ATTACKING / ATTACK_COOLDOWN / EVADING mobs no longer acquire the write-lock and enqueue a broadcast every tick; the broadcast timer is advanced to preserve the rate-limit on the next tick. Uses `forceNextUpdate` flag to still deliver state-transition packets immediately (e.g., CHASING→PREPARING_ATTACK). Stat reset when mob hasn't moved: broadcast timer reset so rate-limit fires correctly on the next cycle.
MobMovementData — new fields: `returnSpeedUnitsPerSec` (200 units/sec, intentionally lower than chase speed), `lastDeflectionSign` (±1/0, prevents ±90° oscillation when mobs crowd the same path). `maxRetries` increased from 4 to 8 to give cornered/clustered mobs more attempts to find a free direction. `speed` and `deflectionSign` fields added for client-side interpolation.
CharacterEventHandler — `handleSetCharacterData()`: upserts character in `CharacterManager` (overwrites stale entry with fresh game-server data instead of silently ignoring it on reconnect).
ChunkServer — mob broadcast loop extended with state-aware guard: only enters broadcast path for mobs that are physically moving.
JSONParser — extended to handle new packet types (playerReady, learn_skill-related fields).

Bug Fixes:
Fixed exploration XP being awarded before persistent flags loaded: zone-entered XP grant now only fires after `areFlagsLoaded()` returns true; deferred check in `DialogueEventHandler::handleSetPlayerFlagsEvent` covers the race window.
Fixed equipment broadcast timing: `broadcastEquipmentUpdate` is now gated on `isClientWorldReady` so other clients don't try to render gear on characters that have not entered their scene yet.
Fixed pending join request race: `pendingJoinRequests_` map was accessed from multiple threads without synchronisation; protected by `pendingJoinRequestsMutex_`.
Fixed double-save on disconnect: HP/Mana now saved once via a single `charData` snapshot; previous code fetched `CharacterManager` twice and could save stale data on the second call if the character was removed between calls.

---

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