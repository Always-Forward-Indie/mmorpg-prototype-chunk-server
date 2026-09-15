# Chunk unit tests (no gtest, no CMake changes)

One file per manager (`test_<name>.cpp`), `main` returns 0/1, asserts with
messages, deterministic, milliseconds. The toolchain (g++, spdlog, boost
headers) lives in the dev container, so tests compile and run there.

`tests/` is deliberately NOT mounted into the container
(`docker-compose.dev.yml` mounts only `src/`, `include/`, `CMakeLists.txt`),
so watchexec never rebuilds the server because of a test edit.

## Run (from the WSL repo root)

```bash
C=mmorpg-prototype-chunk-server-new-chunk-server-1
docker cp mmorpg-prototype-chunk-server-new/tests/test_interest.cpp $C:/tmp/test_interest.cpp
docker exec $C g++ -std=c++17 -I/usr/src/app/include \
  /tmp/test_interest.cpp \
  /usr/src/app/src/services/InterestManager.cpp \
  /usr/src/app/src/utils/Logger.cpp \
  -o /tmp/test_interest -lspdlog -lfmt -pthread \
  && docker exec $C /tmp/test_interest
```

Expected: `ALL OK`.

## Rules for new tests

- Unit-test managers/services only (pure logic + Logger). Anything needing
  asio sockets, DB, or game state belongs to contract tests (`Tests/Contract`)
  or bots (`Tools/Bots`), not here.
- No new third-party deps. No changes to the server build (no CMake edits).
