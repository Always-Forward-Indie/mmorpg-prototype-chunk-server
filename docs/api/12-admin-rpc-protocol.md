# 12. Admin-RPC — тестовый крюк (DEV ONLY, никогда в проде)

> **Область:** chunk-server, `eventType: "adminCommand"`.
> Мгновенная установка тестового состояния (телепорт/спавн/гранты/чтение)
> вместо фарма ногами. Реализация: `EventDispatcher::handleAdminCommand`
> (+ queued `ADMIN_TELEPORT` / `ADMIN_SPAWN` / `ADMIN_KILL_MOB` на event-потоке),
> гейты — `include/services/AdminGate.hpp`, харнес — `Tools/Bots/admin.py`.

## 12.0. Защита (defense in depth — все слои, всегда)

| # | Слой | Где |
|---|------|-----|
| 1 | Allowlist GM `admin.gm_client_ids` (CSV clientId; у чанка нет DB — это и есть role-слой; боты никогда не в списке, только отдельный `gm_bot` с `users.role=1`) | `game_config`, пуш на handshake |
| 2 | Мастер-выключатель `admin.enabled` (default `false`) | `game_config` |
| 3 | Компиляция `#ifdef ADMIN_RPC` (в prod-сборках кода физически нет; попытка → `forbidden`, сессия жива) | `CMakeLists.txt` (`option` default OFF), `Dockerfile.dev` собирает `-DADMIN_RPC=ON`, prod `Dockerfile` — нет |
| 4 | Аудит: исполненная команда — warn `ADMIN char= op=` + строка в `gm_action_log` (fire-and-forget факт `saveGmAction`); отказ — error | видно в `bin/watch_logs.sh` (на DEV ожидаемо, не FATAL) |

Ни один слой не достаточен. Prod deploy: flag off + non-DEV build + zero `role>=1` аккаунтов (проверяется Preflight prod-gate перед apply).

Вызывающая сессия — отдельная GM-сессия, действующая на произвольные `characterId` (бот ходит сам, админ — вторым сокетом). Обычный бот (`role=0`) получает `forbidden`, сессия выживает.

## 12.1. Конверт ответа (единый для всех опов)

```json
{
  "header": {
    "eventType": "adminCommand",
    "event": "adminCommand",
    "clientId": 3809,
    "status": "success",
    "message": "ok",
    "op": "teleport",
    "serverRecvMs": 1789986397000,
    "serverSendMs": 1789986397001,
    "clientSendMsEcho": 1789986396990,
    "requestId": "sync_...",
    "version": "1.0"
  },
  "body": { "op": "teleport", "characterId": 12, "ok": true }
}
```

Харнес ждёт `header.eventType == "adminCommand"` + `op`. Запрос — обычное событие:

```json
{ "header": { "eventType": "adminCommand", "clientId": 3809, "hash": "..." },
  "body": { "op": "teleport", "characterId": 12, "x": 4738.0, "y": -925.0, "z": 300.0 } }
```

Коды ошибок (`status: "error"`, `body.ok: false`): `forbidden` (не GM / выключено / нет сборки / неаутентифицирован),
`parse error`, `invalid_op`, `invalid_params`, `invalid_template`, `unknown_character`,
`unknown_uid`, `already_dead`, `invalid_scope`, `grant_failed`. Ошибка никогда не рвёт сессию и не виснет — ответ всегда.

## 12.2. Опы (параметры → эффект; вся бизнес-логика переиспользует менеджеры)

| Оп | Параметры | Эффект |
|----|-----------|--------|
| `teleport` | `characterId`, `x`, `y`, `z` | Queued (`ADMIN_TELEPORT`): полный move-путь — позиция + `lastValidated` сброс (srvMs=0, иначе следующий шаг триггерит античит), resubscribe С enter-снапшотами (без них клиент видит только thin-дельты без slug/name), evict, немедленный `savePositions`, бродкаст. Ответ `accepted:true` сразу; применение — poll `getState` (память пишется на event-потоке за мс) |
| `getState` | `characterId` | READ (sync): `position`, `level`, `exp`, `hp/hpMax`, `mana/manaMax`, `freeSkillPoints`, `attributes[]`, `skills[]`, `inventory[]` (полный JSON), `gold`, `quests[]` (questSlug/state/currentStep), `flags[]`. Ассерты без парсинга логов |
| `grantXP` | `characterId`, `xp` (>0) | Через `grantExperience` (SP/статы/титулы — как обычно). Ответ: `grantedXp`, `level`, `exp` |
| `grantLevel` | `characterId`, `level` (2..100) | Добивка XP до уровня **по серверной таблице** (`getExperienceForLevelFromGameServer`, НЕ статическая формула — расходятся), далее genuine level-up путь. No-op если уровень уже >= |
| `grantItem` | `characterId`, `itemId`, `qty` | Через `InventoryManager::addItemToInventory` (хуки, вес). Квест-`collect` кредитуется через настоящий `onItemObtained`-хук. Ответ: `quantity` |
| `setHP` | `characterId`, `value` (>=0, clamp к max) | Raw setter + снятие death-флага при оживлении. Для regen/threshold-кейсов |
| `skipTime` | `characterId`, `seconds` (1..604800) | Сдвиг ОБОИХ инжектированных часов `ChampionManager` (epoch для timed/survival, steady для despawn) + немедленные тики. Wall-clock места не трогаются. Ответ: `skippedSec`. Внимание: skipped-время contaminates персистентный `next_spawn_at` (kill репортит fake `killedAt`) — цикл 1 timed-тестов идёт escalating-степами, цикл 2 самодокументирован |
| `spawnMob` | `characterId`, `zoneId` (>0, РЕАЛЬНАЯ зона — threshold атрибутирует по origin, -1 не кредитует), `x/y/z`, `count` (1..20), `mobSlug` или `mobTemplateId` | Queued (`ADMIN_SPAWN`): шаблон + uid + регистрация + **настоящий spawn-лист подписчикам ячейки** (дефолтный путь шлёт только thin-дельты). Uid генерируются в диспетчере — sync-ответ сразу несёт `uids` + `accepted:true`. Позиции — кольцом на стороне харнеса (стакинг ломает movement/STUCK-GUARD) |
| `killMob` | `characterId`, `uid`, `killerCharacterId` | Queued (`ADMIN_KILL_MOB`): летальный урон + `CombatSystem::handleMobDeath` (лут + XP/квест/бестиарий/чемпион/реп — те же вызовы, что в скилл-пути). Ответ `accepted:true`; тест ждёт `mobDeath`. **Setup-only** (трупы/harvest): механика убийства доказывается только через `playerAttack` |
| `resetWorld` | `characterId`, `scope="champion"`, `zoneId` | Сброс ГЛОБАЛЬНОГО состояния (персонажи решаются эфемерными ботами, не скрабом): счётчики + активные чемпионы + timed re-arm + cull лишних мобов зоны (безопасно для чужих uid: `mobDied` floors/no-op; амбиент добирает респаун-таск). Только между тестами (evict-бродкаста нет). Ответ: `scope`, `culled` |
| `reloadWorldContent` | `characterId` (любой загруженный: uniformity гейта) | Перезагрузка мирового контента БЕЗ рестартов: чанк просит game перепушить срезы NPC/vendors/world-objects/dialogues/quests/timed по живому линку (`requestCatalogReload`); чанк мержит как при реконнекте (вендоры/WIO/квесты — замена, timed — merge-by-slug, спавн-зоны — с preserve рантайма). Ответ `accepted` сразу; готовность — поллингом контента (пуши асинхронны). Только между тестами: рантайм-стоки вендоров и эфемерные WIO-состояния сбрасываются к контенту. Зеркало: харнес матчит ответы по паре `(op, requestId)` — op-only матчинг однажды вернул stale-успех (см. интеграционную 12-ю спеку) |
| `banUser` | `characterId`, `reason` (обязательно), `expiresAt` (ISO, пусто = навсегда) | Бан аккаунта-владельца с причиной (`user_bans`; вход показывает причину+срок). Синхронно с bounded-ожиданием факта (ответ `ack: applied/duplicate`, иначе `game_timeout`). Пин: `test_ban.py` |
| `unbanUser` | `characterId` | Снятие активных банов (`is_active=false`). Тот же sync-путь. Пин: `test_ban.py` |

## 12.3. `brief` join (быстрый вход для teleport-flow тестов)

Опциональный флаг в `playerReady`:

```json
{ "header": { "eventType": "playerReady", "clientId": 42, "hash": "..." },
  "body": { "characterId": 7, "brief": true } }
```

Фаза 4 режет тяжёлый mob/NPC flood, остальное intact (F2-набор, инвентарь/скиллы/квесты, `playerReady`-ack, предметы, игроки/экипировка/титулы/WIO). Вместо списков — один пустой `spawnMobsInZone` (shape-checks клиентов зелёные). Недостающее добирают teleport snapshots / spawn-рассылки / явные запросы (`getNearbyCorpses` и т.п.). Без флага — полный flood как раньше. Замер: ~5% (джойны — это раунды протокола + settles, не байты; главный рычаг — параллельные батчи).

## 12.4. Правила тестов (ранбук, коротко)

- Локомоция платится ровно один раз (dedicated locomotion/join-тесты); всё остальное телепортируется. Движение тестов — только внутри тестовой зоны (interest-anchor следует за движением — погоня за глобальными призраками отписывает арену).
- Spawning tests spawning, killing tests killing — не смешивать в одном вердикте. `adminKillMob` не проходит kill-тесты.
- Один `AdminClient` на поток (сокеты не thread-safe). Stop-event на первый pass (wall = первый, не cap).
- Новая механика ships with its admin command first (atom test via admin-RPC).
