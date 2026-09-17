// Unit tests for MovementValidation (pure move-speed math extracted from
// CharacterEventHandler::handleMoveCharacterEvent, Wave 3.5).
#include "services/MovementValidation.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace
{

CharacterAttributeStruct attr(const std::string &slug, int value)
{
    CharacterAttributeStruct a;
    a.slug = slug;
    a.value = value;
    return a;
}

ActiveEffectStruct speedFx(float value, int64_t expiresAt = 0, int tickMs = 0)
{
    ActiveEffectStruct e;
    e.attributeSlug = "move_speed";
    e.value = value;
    e.expiresAt = expiresAt;
    e.tickMs = tickMs;
    return e;
}

PositionStruct pos(float x, float y)
{
    PositionStruct p;
    p.positionX = x;
    p.positionY = y;
    return p;
}

} // namespace

TEST(MovementValidation, ResolveDefaultsWithoutAttribute)
{
    auto r = MovementValidation::resolveMoveSpeed({}, {}, 1000);
    EXPECT_FLOAT_EQ(r.speed, 7.0f);
    EXPECT_FALSE(r.fromAttributes);
}

TEST(MovementValidation, ResolveReadsFirstAttributeAndStacksEffects)
{
    // attr 5 + live buff 2 + permanent buff 1 => 8; expired/tick/other ignored.
    ActiveEffectStruct expired = speedFx(100.0f, 500); // expired at 500 < now 1000
    ActiveEffectStruct ticker = speedFx(100.0f, 0, 1000);
    ActiveEffectStruct other;
    other.attributeSlug = "strength";
    other.value = 100.0f;
    auto r = MovementValidation::resolveMoveSpeed(
        {attr("strength", 10), attr("move_speed", 5), attr("move_speed", 9)},
        {speedFx(2.0f), speedFx(1.0f), expired, ticker, other}, 1000);
    EXPECT_FLOAT_EQ(r.speed, 8.0f);
    EXPECT_TRUE(r.fromAttributes);
}

TEST(MovementValidation, MaxDistanceClampsDeltaAndScales)
{
    // 200 units/s * 16ms floor * 1.0 buffer = 3.2 even for delta 5ms.
    EXPECT_FLOAT_EQ(MovementValidation::maxDistanceFor(200.0f, 5.0f, 1.0f), 3.2f);
    // 200 * 100ms * 1.3 buffer = 26.
    EXPECT_FLOAT_EQ(MovementValidation::maxDistanceFor(200.0f, 100.0f, 1.3f), 26.0f);
}

TEST(MovementValidation, WindowNeedsTwoSamplesAndPositiveDelta)
{
    MovementSample w[5] = {};
    PositionStruct np = pos(10.0f, 0.0f);
    // count < 2 => fail, no crash on empty window.
    auto v = MovementValidation::checkWindow(w, 0, 0, 5, np, 1000, 200.0f, 1.0f);
    EXPECT_FALSE(v.passed);
    v = MovementValidation::checkWindow(w, 1, 0, 5, np, 1000, 200.0f, 1.0f);
    EXPECT_FALSE(v.passed);
    // Zero window delta => fail (no time passed).
    w[0].position = pos(0.0f, 0.0f);
    w[0].srvMs = 1000;
    w[1].position = pos(1.0f, 0.0f);
    w[1].srvMs = 1000;
    v = MovementValidation::checkWindow(w, 2, 0, 5, np, 1000, 200.0f, 1.0f);
    EXPECT_FALSE(v.passed);
}

TEST(MovementValidation, WindowUsesOldestSample)
{
    // Partial window (count < capacity): oldest is index 0.
    MovementSample w[5] = {};
    w[0].position = pos(0.0f, 0.0f);
    w[0].srvMs = 900; // 100ms ago, 10 units away => 100 u/s
    w[1].position = pos(9.0f, 0.0f);
    w[1].srvMs = 990;
    auto v = MovementValidation::checkWindow(
        w, 2, 0, 5, pos(10.0f, 0.0f), 1000, 200.0f, 1.0f);
    EXPECT_TRUE(v.passed);
    EXPECT_FLOAT_EQ(v.dist, 10.0f);
    EXPECT_FLOAT_EQ(v.deltaMs, 100.0f);
    EXPECT_EQ(v.samples, 2);
    // Same geometry at half the speed budget => fail.
    v = MovementValidation::checkWindow(w, 2, 0, 5, pos(10.0f, 0.0f), 1000, 50.0f, 1.0f);
    EXPECT_FALSE(v.passed);
}

TEST(MovementValidation, WindowWrapsAroundRingHead)
{
    // Full ring (count == capacity): oldest is at head.
    MovementSample w[5] = {};
    w[3].position = pos(0.0f, 0.0f);
    w[3].srvMs = 800; // oldest: 200ms ago, 10 units => 50 u/s
    auto v = MovementValidation::checkWindow(
        w, 5, 3, 5, pos(10.0f, 0.0f), 1000, 200.0f, 1.0f);
    EXPECT_TRUE(v.passed);
    EXPECT_FLOAT_EQ(v.deltaMs, 200.0f);
    v = MovementValidation::checkWindow(w, 5, 3, 5, pos(10.0f, 0.0f), 1000, 40.0f, 1.0f);
    EXPECT_FALSE(v.passed);
}
