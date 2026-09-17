// Unit tests for MobMovementMath (A5 pure extract).
//
// Pins the movement predicates 1-1 with the inline manager code this
// replaced: containment + separation, chase rules, combat-state action
// table, leash thresholds, patrol timing bounds, return-destination
// sampling. No managers, no sockets — only RandomUtils (seeded).
#include "services/MobMovementMath.hpp"
#include "utils/RandomUtils.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace
{

PositionStruct pos(float x, float y)
{
    PositionStruct p;
    p.positionX = x;
    p.positionY = y;
    return p;
}

SpawnZoneStruct rectZone()
{
    SpawnZoneStruct z;
    z.zoneId = 7;
    z.minX = 0;
    z.maxX = 1000;
    z.minY = 0;
    z.maxY = 1000;
    return z;
}

using Neighbours = std::vector<std::pair<int, PositionStruct>>;

} // namespace

TEST(MobMovementMath, ValidPositionContainmentAndSeparation)
{
    const SpawnZoneStruct z = rectZone();
    // Inside, alone: valid.
    EXPECT_TRUE(MobMovementMath::isValidPosition(500, 500, z, {}, 1, 0.0f, 140.0f));
    // Outside the RECT: invalid even when alone.
    EXPECT_FALSE(MobMovementMath::isValidPosition(1500, 500, z, {}, 1, 0.0f, 140.0f));
    // Neighbour inside fallback separation (140): invalid.
    Neighbours close{{{2, pos(560, 500)}}};
    EXPECT_FALSE(MobMovementMath::isValidPosition(500, 500, z, close, 1, 0.0f, 140.0f));
    // Neighbour beyond it: valid.
    Neighbours far{{{2, pos(800, 500)}}};
    EXPECT_TRUE(MobMovementMath::isValidPosition(500, 500, z, far, 1, 0.0f, 140.0f));
    // Self is always skipped.
    Neighbours self{{{1, pos(500, 500)}}};
    EXPECT_TRUE(MobMovementMath::isValidPosition(500, 500, z, self, 1, 0.0f, 140.0f));
}

TEST(MobMovementMath, ValidPositionRadiusBeatsFallback)
{
    const SpawnZoneStruct z = rectZone();
    // Own collision diameter (2*100=200) wins over the 140 fallback.
    Neighbours at150{{{2, pos(650, 500)}}};
    EXPECT_FALSE(MobMovementMath::isValidPosition(500, 500, z, at150, 1, 100.0f, 140.0f));
    Neighbours at250{{{2, pos(750, 500)}}};
    EXPECT_TRUE(MobMovementMath::isValidPosition(500, 500, z, at250, 1, 100.0f, 140.0f));
}

TEST(MobMovementMath, ChaseIgnoresZoneBounds)
{
    // Far outside the zone but separated: chase placement allows it.
    Neighbours none{};
    EXPECT_TRUE(MobMovementMath::isValidPositionForChase(5000, 5000, none, 1, 0.0f, 140.0f));
    // Separation still enforced (self skipped).
    Neighbours close{{{2, pos(5050, 5000)}}};
    EXPECT_FALSE(MobMovementMath::isValidPositionForChase(5000, 5000, close, 1, 0.0f, 140.0f));
    Neighbours self{{{1, pos(5000, 5000)}}};
    EXPECT_TRUE(MobMovementMath::isValidPositionForChase(5000, 5000, self, 1, 0.0f, 140.0f));
}

TEST(MobMovementMath, CanPerformActionTable)
{
    EXPECT_TRUE(MobMovementMath::canPerformAction(MobCombatState::PATROLLING));
    EXPECT_TRUE(MobMovementMath::canPerformAction(MobCombatState::CHASING));
    EXPECT_TRUE(MobMovementMath::canPerformAction(MobCombatState::RETURNING));
    EXPECT_TRUE(MobMovementMath::canPerformAction(MobCombatState::FLEEING));
    EXPECT_FALSE(MobMovementMath::canPerformAction(MobCombatState::PREPARING_ATTACK));
    EXPECT_FALSE(MobMovementMath::canPerformAction(MobCombatState::ATTACKING));
    EXPECT_FALSE(MobMovementMath::canPerformAction(MobCombatState::ATTACK_COOLDOWN));
    EXPECT_FALSE(MobMovementMath::canPerformAction(MobCombatState::EVADING));
}

TEST(MobMovementMath, LeashPredicates)
{
    const SpawnZoneStruct z = rectZone();
    // Inside (distance 0): no return, no stop, can search.
    EXPECT_FALSE(MobMovementMath::shouldReturnToSpawn(pos(500, 500), z, 1000.0f));
    EXPECT_FALSE(MobMovementMath::shouldStopChasing(pos(500, 500), z, 1500.0f));
    EXPECT_TRUE(MobMovementMath::canSearchNewTargets(pos(500, 500), z, 150.0f));
    // 1100 outside the edge: return trips, chase continues, no new targets.
    EXPECT_TRUE(MobMovementMath::shouldReturnToSpawn(pos(2100, 500), z, 1000.0f));
    EXPECT_FALSE(MobMovementMath::shouldStopChasing(pos(2100, 500), z, 1500.0f));
    EXPECT_FALSE(MobMovementMath::canSearchNewTargets(pos(2100, 500), z, 150.0f));
    // 1600 outside: chase stops too.
    EXPECT_TRUE(MobMovementMath::shouldStopChasing(pos(2600, 500), z, 1500.0f));
}

TEST(MobMovementMath, NextMoveTimeBounds)
{
    // speedTime 2..5 at patrolSpeed 1: now+2 floor, occasional +cooldown/2.
    // Assert bounds over samples, never exact values (RNG inside).
    RandomUtils::seedForTests(7u);
    MobMovementParams params;
    for (int i = 0; i < 200; ++i)
    {
        const float t = MobMovementMath::calculateNextMoveTime(100.0f, 1.0f, 1.0f, params);
        EXPECT_GE(t, 102.0f);
        EXPECT_LE(t, 100.0f + 5.0f + 3.0f * 0.5f + 0.001f);
    }
    // Zero patrolSpeed falls back to the movement-data multiplier.
    RandomUtils::seedForTests(7u);
    const float t = MobMovementMath::calculateNextMoveTime(100.0f, 0.0f, 2.0f, params);
    EXPECT_GE(t, 102.0f);
}

TEST(MobMovementMath, PickReturnDestinationStaysInZone)
{
    RandomUtils::seedForTests(7u);
    const SpawnZoneStruct z = rectZone();
    // From the centre every candidate lands inside the big zone.
    for (int i = 0; i < 25; ++i)
    {
        auto dest = MobMovementMath::pickReturnDestination(pos(500, 500), z);
        ASSERT_TRUE(dest.has_value());
        EXPECT_GE(dest->positionX, 0.0f);
        EXPECT_LE(dest->positionX, 1000.0f);
        EXPECT_GE(dest->positionY, 0.0f);
        EXPECT_LE(dest->positionY, 1000.0f);
        // Within the 400 walk radius of the mob.
        const float dx = dest->positionX - 500.0f;
        const float dy = dest->positionY - 500.0f;
        EXPECT_LE(dx * dx + dy * dy, 400.0f * 400.0f + 1.0f);
    }
}

TEST(MobMovementMath, PickReturnDestinationFallsBackOutside)
{
    // A mob further than 400+margin from any zone point: all 20 attempts
    // fail deterministically regardless of seed → nullopt (caller falls
    // back to the original spawn position).
    const SpawnZoneStruct z = rectZone();
    auto dest = MobMovementMath::pickReturnDestination(pos(5000, 5000), z);
    EXPECT_FALSE(dest.has_value());
}
