#pragma once
// Single source of truth for all server-side RNG (Wave 1.1).
//
// Rules (learned from two prior races: CombatCalculator shared mt19937 and
// TimestampUtils localtime, both fixed after TSan live runs):
// - Exactly ONE thread_local mt19937 per thread, seeded once from
//   random_device. No shared engines, ever.
// - Distributions are function-LOCAL (never static/thread_local-shared):
//   std::distribution objects are stateful, so sharing one across threads
//   is a data race even when the engine itself is thread-local.
// - Unit tests: assert ranges/invariants over many samples, never exact
//   values; use seedForTests() for deterministic sequences on one thread.
#include <cstdint>
#include <random>

class RandomUtils
{
  public:
    // Per-thread engine. First use on a thread seeds from random_device.
    static std::mt19937 &engine()
    {
        thread_local std::mt19937 gen{std::random_device{}()};
        return gen;
    }

    // Reseed the CALLING thread's engine. Test-only: makes the sequence
    // deterministic on this thread. Never call from production code.
    static void seedForTests(uint32_t seed) { engine().seed(seed); }

    // Uniform float in [0, 1).
    static float uniform01()
    {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(engine());
    }

    // Uniform float in [lo, hi).
    static float range(float lo, float hi)
    {
        std::uniform_real_distribution<float> dist(lo, hi);
        return dist(engine());
    }

    // Uniform int in [lo, hi] (closed on both ends, like uniform_int_distribution).
    static int rangeInt(int lo, int hi)
    {
        std::uniform_int_distribution<int> dist(lo, hi);
        return dist(engine());
    }

    // Uniform angle in [0, 2pi) for disc/annulus spawn sampling.
    // (Own constant: M_PI needs feature-test macros on some toolchains.)
    static float angle()
    {
        constexpr float kTwoPi = 2.0f * 3.14159265358979323846f;
        return range(0.0f, kTwoPi);
    }

    RandomUtils() = delete;
};
