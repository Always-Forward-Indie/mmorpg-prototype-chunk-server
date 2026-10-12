# Надёжная доставка фактов chunk → game (factKey / factAck)

~22 семейства фактов (`savePositions`, `saveReputation`, `saveLearnedSkill`, …)
шлются чанком на game. Базовая семантика — **at-least-once доставка,
exactly-once эффект**: потерянный факт ретраится, дубликат применяется один раз.

## Формат ключа

`factKey` — строка `{characterId}:{factType}:{bootId}:{seq}`, где `seq` —
глобальный монотонный счётчик процесса (отсюда per-character монотонность
как подпоследовательность), а `bootId` — случайный id процесса.
`bootId` обязателен: без него факты после рестарта чанка collide'ились бы
со старыми ключами в `fact_keys_applied` и ложно дедуплицировались
(потеря данных). High-watermark в БД **не персистится** (открыто, см. ниже).

## Отправка (chunk): `FactOutbox`

Все seam-лямбды кладут факты в outbox вместо прямой отправки. Флаш каждые
5с (`kFlushMs`) + при накоплении; неподтверждённые ретраятся с
экспоненциальным backoff 5с→60s cap; per-character FIFO; cap 20000;
DLQ-expiry с логом через 1ч. Счётчики `pending/sent/acked/expired` — в
10s-саммари чанка (`Outbox game: ...`). Только 17 типов фактов ходят через
outbox (allowlist: learn/reputation/inventory + durability/currency/
equipment/debt/activeEffect/killCount/pity/bestiary/timedKilled/mastery/
skillbar/titles/cooldown/analytics); остальное — legacy fire-and-forget.

## Приём (game): claim + ack

Каждый save-хендлер из покрытых 17 сначала делает
`claim_fact_key(key)` **в той же транзакции**, что и бизнес-запись:

| claim | Значение | Поведение |
|-------|----------|-----------|
| `1` | applied | Применить бизнес-логику, закоммитить, ответить `factAck{applied}` |
| `0` | duplicate | Пропустить бизнес-логику, ответить `factAck{duplicate}` |
| `-1` | transport error | Без ack вообще — чанк ретраит |

`factAck` — `header.eventType == "factAck"`, `body{key, status}` в
`status:success`-конверте (квитанция, не ошибка — `errorCode` тут нет).

Пример (репутация — дельта, поэтому двойное применение наблюдаемо):

```json
// chunk → game (дважды, ретрай):
{"header": {"eventType": "saveReputation", ...},
  "body": {"characterId": 10, "factionSlug": "merchants", "delta": 3,
           "factKey": "10:saveReputation:42"}}
// game → chunk, первый раз:
{"header": {"eventType": "factAck", "status": "success", ...},
 "body": {"key": "10:saveReputation:42", "status": "applied"}}
// game → chunk, дубликат:
{"header": {"eventType": "factAck", "status": "success", ...},
 "body": {"key": "10:saveReputation:42", "status": "duplicate"}}
```

Итог в БД: `merchants` вырос ровно на +3. Пин: `test_factkey_idempotency.py`
(hermetic-бот `bot_05` — он никогда не онлайн на чанке, поэтому 60s
абсолютный снапшот не затирает дельты mid-test).

## Вторая линия обороны: 60s абсолютный снапшот репутации

Чанк каждые 60с (`scheduler.rep_snapshot_ms`) шлёт legacy-absolute
`saveReputation{value}` (без `factKey`/дельты) по онлайн-персонажам.
Ограничивает staleness дельт минутой при краше чанка (outbox in-memory).
Побочка для тестов: дельта-тесты на онлайн-персонаже флапают —
отсюда hermetic-фикстура выше.

## High-watermark (фаза B, построено)

Каждый applied-факт атомарно пишет high-water в `outbox_seq_watermark`
(`chunk_id, character_id → last_seq`, GREATEST-wins) в той же транзакции,
что и claim + бизнес-запись — watermark никогда не впереди эффектов.
При новом линке game отдаёт глобальный `MAX(last_seq)` в `setChunkData`-ack
(`body.outboxGlobalSeq`); чанк fast-forward'ит свой счётчик (только вперёд).
`bootId` в ключах сохранён — stale watermark безвреден.
Heartbeat-ack'и поля не несут (иначе чанк reconcile'ился бы каждые 60с).

## Reconcile handshake (фаза C, построено)

- UP: `chunkServerConnection` несёт `header.bootId/nextSeq` (observability;
  heartbeat'ы тоже — game их игнорирует кроме логирования).
- DOWN: только новый линк получает `outboxGlobalSeq` в ack'е.
- Diff: чанк после такого ack'а шлёт `outboxUnacked{bootId, keys[]}`
  (батчи ~120 ключей — лимит 8KB game inbound); game отвечает
  `outboxDiff{applied[], unknown[]}` (point PK-lookups) на живой chunk-сокет.
  Чанк: applied → drop, unknown → poke + немедленный resend (флаш 5с — backstop).
  На DB-ошибку game молчит (claim −1 семантика) — чанк ретраит флашом.

## Ретеншн dedup-таблицы (построено)

Окно дедупликации покрывает только 1h DLQ/retry-горизонт; дальше
`fact_keys_applied` чистится game-свипом: `delete_old_fact_keys(days, limit)`
(30 дней, батчи по 1000, индекс `ix_fact_keys_applied(applied_at)`).
Без этого PK-btree рос бы вечно на units/sec трафика.

## Открыто (не построено)

- Chunk-crash амнезия: unacked-факты + породившие их in-memory дельты гибнут
  вместе с процессом — reconcile воскрешать не умеет (только convergence proof).
  Восстановление — регидрация из game-БД при JOIN'е. Persistent WAL отклонён
  сознательно: чанк stateless in-memory без Postgres (смена архитектуры ради
  5-секундного окна). Пин: `FactOutbox.CrashSimRekeyedResendNeverCollides`
  (unit) + `contracts/test_crash_reconverge_sim.py` (cross-boot diff,
  fast-батч).
- Live e2e retry-proof ботами (рестарт чанка mid-flow → unacked прилетает после реконнекта). Покрыта только идемпотентность, не crash-retry.

## Синхронизация id строк инвентаря (построено, S-батч)

Прямая сторона (`saveInventoryChange`) идёт через outbox надёжно; дыра была
в обратной — fire-and-forget `inventoryItemIdSync` (game → chunk).
Закрыта токеном (`syncToken`, эхо чинит ровно свой экземпляр — пин
`ShopFixture.SyncTokenDisambiguatesIdenticalPendings` + sync-sim на эхо),
ретраем (`dueSyncResend` + `scheduler.inv_sync_retry_ms`, миграция 095, тот же
токен и последнее абсолютное количество),
абсолютным upsert'ом game-стороны (`SET quantity = EXCLUDED.quantity`, было
аддитивное `+` — дрейфовало при повторах) и max-wins JOIN-аудитом
(`loadPlayerInventory`: пины `LoadPreservesPendingSyncRows`,
`LoadAbsorbsPendingWhenDbIsNewer`). Пины: unit (`ShopFixture.*Sync*`, `*Load*`) +
  `contracts/test_inventory_sync_sim.py` (fast). Честно открыто из того же
  семейства: read-your-write для equip-attr refresh чинён внутри equip-хендлера
  (гонка двух очередей), для durability — следующим заходом. A-batch (096) закрыл
  остаток: equip/unequip для id==0 строк деферятся до sync echo (game
  добивает по токену); строки владеют токеном, template-pair unique снят;
  мультистек-remove шлёт сейв на стек; flush oldest-first; ретрай с
  потолком 60. Ordered refresh чинён и для durability (внутри persist).
