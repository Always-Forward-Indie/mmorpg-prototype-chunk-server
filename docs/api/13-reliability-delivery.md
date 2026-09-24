# Надёжная доставка фактов chunk → game (factKey / factAck)

~22 семейства фактов (`savePositions`, `saveReputation`, `saveLearnedSkill`, …)
шлются чанком на game. Базовая семантика — **at-least-once доставка,
exactly-once эффект**: потерянный факт ретраится, дубликат применяется один раз.

## Формат ключа

`factKey` — строка `{characterId}:{factType}:{bootId}:{seq}`, где `seq` —
монотонный счётчик на персонажа в пределах процесса, а `bootId` — случайный
id процесса.
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
 "body": {"characterId": 10, "factionSlug": "bandits", "delta": 3,
          "factKey": "10:saveReputation:42"}}
// game → chunk, первый раз:
{"header": {"eventType": "factAck", "status": "success", ...},
 "body": {"key": "10:saveReputation:42", "status": "applied"}}
// game → chunk, дубликат:
{"header": {"eventType": "factAck", "status": "success", ...},
 "body": {"key": "10:saveReputation:42", "status": "duplicate"}}
```

Итог в БД: `bandits` вырос ровно на +3. Пин: `test_factkey_idempotency.py`
(hermetic-бот `bot_05` — он никогда не онлайн на чанке, поэтому 60s
абсолютный снапшот не затирает дельты mid-test).

## Вторая линия обороны: 60s абсолютный снапшот репутации

Чанк каждые 60с (`scheduler.rep_snapshot_ms`) шлёт legacy-absolute
`saveReputation{value}` (без `factKey`/дельты) по онлайн-персонажам.
Ограничивает staleness дельт минутой при краше чанка (outbox in-memory).
Побочка для тестов: дельта-тесты на онлайн-персонаже флапают —
отсюда hermetic-фикстура выше.

## Открыто (не построено)

- High-watermark persist (seq рестартует с нуля каждый boot; спасает только `bootId`).
- Handshake reconcile при JOIN (чанк шлёт watermark, game отвечает diff'ами) + catch-up send.
- Live e2e retry-proof ботами (рестарт чанка mid-flow → unacked прилетает после реконнекта). Покрыта только идемпотентность, не crash-retry.
