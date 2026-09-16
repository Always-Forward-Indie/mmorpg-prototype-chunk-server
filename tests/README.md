# Chunk unit tests (gtest)

One file per manager (`test_<name>.cpp`). Toolchain (g++, gtest, spdlog,
boost headers) lives in the dev container, so tests compile and run there.

## Run (from inside any dev container)

```bash
ctest --test-dir /usr/src/app/build --output-on-failure
```

The `unit_tests` binary is built by the normal server build
(`cmake --build build` / watchexec `make -j8`); no separate steps.

## Add a test

1. Create `tests/test_<name>.cpp` with `TEST`/`TEST_F` cases.
2. Append the file plus the needed `../src/**/*.cpp` to `tests/CMakeLists.txt`
   (link only what the test needs; `Logger.cpp` is the usual companion).
3. Reconfigure happens automatically via watchexec; for a manual check:
   `cmake -S /usr/src/app -B /tmp/tbuild && cmake --build /tmp/tbuild --target unit_tests -j8`
   (separate build dir — never run a second `make` inside `/usr/src/app/build`
   while watchexec watches it).

## Rules

- Unit-test managers/services only (pure logic + Logger). Anything needing
  asio sockets, DB, or live game state belongs to contract tests
  (`Tests/Contract`) or bots (`Tools/Bots`), not here.
- RNG rolls: assert ranges/invariants over many samples, never exact values.
- `tests/` is mounted into the container but ignored by `watch_and_run.sh`,
  so editing tests never restarts the server.
