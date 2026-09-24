# Scheduler cadences (chunk + game + login)

Все периоды — из конфига, в коде только фолбэки (= предыдущие литералы).
Изменение требует рестарта процесса (периоды семплируются один раз при
планировании; TTL/amount-ключи читаются живьём — см. пометки).

## Chunk (`ChunkServer.cpp`, `scheduler.*_ms`, источник — `game_config`)

| Ключ | Default | Что делает |
|------|---------|------------|
| `scheduler.mob_tick_ms` | 50 | Unified mob tick: движение всех зон + per-mob rate-limited fan-out (100/200/500/1000мс) |
| `scheduler.combat_ms` | 100 | `updateOngoingActions` (instant-skill/cast resolution) |
| `scheduler.harvest_ms` | 1000 | `updateHarvestProgress` + `cleanupOldCorpses(60)` |
| `scheduler.quest_flush_ms` | 5000 | `flushDirtyProgress` (квесты + флаги) |
| `scheduler.dead_mob_ms` | 5000 | Проверка corpse-expiry (сам TTL — живьём: `corpse.ttl_ms`, default 60000) |
| `scheduler.mem_monitor_ms` | 5000 | Лог VmRSS |
| `scheduler.cleanup_ms` | 10000 | Reap сокетов/сессий, `forceCleanupMemory`, лог очередей/outbox/interest |
| `scheduler.save_hp_mana_ms` | 10000 | Push `saveHpMana` онлайн-персонажей на game |
| `scheduler.ping_sweep_ms` | 15000 | Reap клиентов без пинга (`PING_TIMEOUT_SEC=30` в коде; grace для `!isWorldReady`) |
| `scheduler.respawn_ms` | 30000 | Respawn-добивка неполных зон (первый запуск +10с) |
| `scheduler.aggressive_cleanup_ms` | 30000 | `forceCleanupMemory` + `malloc_trim` |
| `scheduler.save_positions_ms` | 30000 | Push `savePositions` на game (первый +30с) |
| `scheduler.trade_cleanup_ms` | 30000 | `cleanupExpiredSessions` трейдов (первый +30с) |
| `scheduler.dead_sockets_ms` | 30000 | `cleanupDeadSockets` вне hot path (первый +30с) |
| `scheduler.timed_champion_ms` | 30000 | Окна timed-чемпионов + пре-анонсы (первый +30с) |
| `scheduler.zone_event_ms` | 30000 | `tickEventScheduler` (первый +30с) |
| `scheduler.ghost_cleanup_ms` | 30000 | Удаление orphan-персонажей (timeout 60с в коде, первый +60с) |
| `scheduler.drop_cleanup_ms` | 60000 | Чистка ground-айтема (сам TTL — живьём: `drop.ttl_sec`, default 300) |
| `scheduler.dialogue_cleanup_ms` | 60000 | `cleanupExpiredSessions` диалогов (первый +60с) |
| `scheduler.rep_snapshot_ms` | 60000 | Абсолютный снапшот репутации (delta-loss bound, первый +60с) |
| `scheduler.save_playtime_ms` | 60000 | Push дельт `savePlayTime` |
| `scheduler.spawn_safety_ms` | 300000 | Safety-sweep спавна (`alive<totalSpawnCount`, 5 мин) |
| `scheduler.survival_evo_ms` | 300000 | Эволюция long-lived мобов в чемпионов (первый +300с) |
| `regen.tickIntervalMs` | 4000 | `tickRegen` (amounts — живьём: `regen.baseHpRegen/baseMpRegen/...`) |

Сиды: миграции `085` (interest) + `086` (24 ключа) — `game_config` в дампе
и live совпадают. Game пушит весь `game_config` в чанк при JOIN
(`GET_GAME_CONFIG`).

## Game (`main.cpp`, `scheduler.*_sec`, источник — `game_config`)

| Ключ | Default | Что делает |
|------|---------|------------|
| `scheduler.chunk_sweep_sec` | 60 | Sweep чанков (порог — `scheduler.chunk_silent_threshold_ms`, default 180000 = 3 heartbeat'а) |
| `scheduler.client_sweep_sec` | 60 | Sweep клиентов (порог — `scheduler.client_silent_threshold_ms`, default 600000 = 10 мин) |

Пороги — миграция `087`. Ping обновляет `lastSeenMs`.

## Login (без Scheduler'а)

Периодика — таймаут `tryPopBatch` в `mainEventLoop`: `session_cleanup_ms`
(`config.json` + `SESSION_CLEANUP_MS`, default 60000, clamp 1с–1ч).
Каждый тик: `cleanup_expired_sessions` (DB — source of truth) + in-memory
sweep (>2× cadence) + `gcWriteQueues` + pool-health warn.
