# MMOChunkServer — World simulation (in-memory)

Port: `27017`

## Prod (Release, без ASan, без hot-reload)

```bash
cd mmorpg-prototype-chunk-server-new
docker-compose up --build -d
```

Собирает с `CMAKE_BUILD_TYPE=Release`, **без** AddressSanitizer, стрипает бинарник. Не линкует `libpqxx` (чанк-сервер не ходит в БД).

## Dev (Debug, ASan, hot-reload)

```bash
cd mmorpg-prototype-chunk-server-new
docker-compose -f docker-compose.dev.yml up --build -d
```

Собирает с `CMAKE_BUILD_TYPE=Debug`, включает AddressSanitizer и watchexec.

## Порядок запуска

Запускать **последним** — после login-server и game-server. Чанк-сервер при старте получает все статические данные (зоны, NPC, предметы, квесты) от гейм-сервера через `JOIN_CHUNK_SERVER`.

## Конфигурация

`config.json`:
- `chunk_server.max_clients` — лимит одновременных подключений клиентов
- `game_server.host` / `game_server.port` — адрес вышестоящего гейм-сервера (в Docker: `game-server:27016`)

## Примечания

- Чанк-сервер **не использует PostgreSQL** — всё состояние в памяти
- В Release-сборке AddressSanitizer отключён (экономит ~2x RAM)
- `libpqxx` не линкуется (был мёртвым грузом)
