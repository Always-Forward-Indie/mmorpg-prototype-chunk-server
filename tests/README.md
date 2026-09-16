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

## Sanitizers (on demand, no CI)

ASan is on by default in Debug builds (`-fsanitize=address` in the root
`CMakeLists.txt`). UBSan/TSan use throwaway build dirs in a scratch
container — never add sanitizer flags to `CMakeLists.txt`, and never run a
second `make` inside the watched `/usr/src/app/build`.

```bash
# UBSan+ASan unit run (separate build dir, same sources via bind mounts):
cmake -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
  -S /usr/src/app -B /usr/src/app/build-ubsan
cmake --build /usr/src/app/build-ubsan --target unit_tests -j8
/usr/src/app/build-ubsan/tests/unit_tests   # must be silent (264 green, 0 reports)

# TSan unit run. Two environment quirks, both documented:
#  1. TSan is incompatible with ASan: use a custom build type (e.g. -DCMAKE_BUILD_TYPE=TSan)
#     so the hardcoded Debug ASan flags do not apply.
#  2. This toolchain+Docker needs non-PIE binaries (-fno-pie -no-pie) plus
#     setarch/run under seccomp=unconfined, otherwise TSan dies at startup
#     with "unexpected memory mapping".
cmake -DCMAKE_BUILD_TYPE=TSan \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g -fno-pie" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread -no-pie" \
  -S /usr/src/app -B /usr/src/app/build-tsan-nopie
cmake --build /usr/src/app/build-tsan-nopie --target unit_tests -j8
TSAN_OPTIONS="suppressions=/usr/src/app/tests/TSanSuppressions.txt" \
  ./build-tsan-nopie/tests/unit_tests
```

Gate: zero non-suppressed reports. Suppressions (`tests/TSanSuppressions.txt`)
cover only the triaged spdlog-teardown artifact. The flaky Scheduler
same-mutex reports are deliberately NOT suppressed (see the file header);
any new stack shape is guilty until proven otherwise.
