# Chunk ↔ game: reconnect, heartbeat, outbox ops

## Reconnect (chunk → game)

- Обрыв/недоступность game при старте или в рантайме: **бесконечный**
  capped backoff `5с × 2^n`, cap 60с (`RETRY_TIMEOUT=5`,
  `MAX_RETRY_WAIT_SEC=60`, `GameServerWorker.hpp:47-50`). Процесс **никогда**
  не выходит (раньше был `exit(1)` после 5 попыток с вайпом in-memory мира).
- При переподключении: heartbeat перерегистрирует чанк, game делает полный
  каталожный пуш заново (`entries=24 failed=0`). In-memory мир и outbox
  переживают разрыв.
- Пин: `test_flap.py` (рестарт game mid-test, ~74с, standalone/nightly).

## Heartbeat (chunk → game, каждые 60с)

Перерегистрация + liveness. Game считает чанк мёртвым после 180с тишины
(`scheduler.chunk_silent_threshold_ms`, свип каждые
`scheduler.chunk_sweep_sec`). Heartbeat с того же сокета — пропуск
повторного пуша; с нового сокета — полный пуш.

## Outbox flush (chunk → game, каждые 5с)

`FactOutbox::kFlushMs=5000`: acked-факты дропаются, unacked ретраятся
(backoff до `kBackoffCapMs=60000`), cap 20000 фактов, expiry-лог через 1ч
(`sweepExpired(...,3600000)` = DLQ-сигнал, не удаление молча).
Детали формата ключей и claim-протокола — в `protocol/13-reliability-delivery.md`.

## Game → chunk liveness (наблюдаемость)

- Лог game при пуше: `[join] full catalog push chunk=1 entries=24 failed=N tookMs=M`
  (`failed>0` → регистрация роняется, ретрай на следующем heartbeat).
- Лог чанка каждые 10с: `Outbox game: pending/sent/acked/expired` +
  `Interest: ...`. `pending`, стареющий >60с — смотреть как FATAL
  (потеря ack'ов / умер game).
