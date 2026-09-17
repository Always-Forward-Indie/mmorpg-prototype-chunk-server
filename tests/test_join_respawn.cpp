// Unit tests for JoinFlushPolicy + RespawnResolver (B2 pure extracts).
//
// Pins the join-evict gate order and the respawn destination/vitals math
// 1-1 with the CharacterEventHandler code they were extracted from.
#include "services/JoinFlushPolicy.hpp"
#include "services/RespawnResolver.hpp"

#include <gtest/gtest.h>

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

ActiveEffectStruct sickness(const std::string &attr, float value, int64_t expiresAt)
{
    ActiveEffectStruct e;
    e.effectSlug = "resurrection_sickness";
    e.effectTypeSlug = "debuff";
    e.attributeSlug = attr;
    e.value = value;
    e.expiresAt = expiresAt;
    return e;
}

} // namespace

TEST(JoinFlushPolicy, FourGatesInHandlerOrder)
{
    // Nothing loaded: nothing to evict (even with stale ids around).
    EXPECT_EQ(JoinFlushPolicy::decide(false, 55, 77), JoinEvictDecision::NothingToEvict);
    // Loaded but no client mapped: async join preload, not a stale session.
    EXPECT_EQ(JoinFlushPolicy::decide(true, 0, 77), JoinEvictDecision::PreloadedSkip);
    // Same client reconnecting: duplicate joinGameCharacter packet.
    EXPECT_EQ(JoinFlushPolicy::decide(true, 77, 77), JoinEvictDecision::DuplicateSkip);
    // Different live client: genuine stale session, full evict.
    EXPECT_EQ(JoinFlushPolicy::decide(true, 55, 77), JoinEvictDecision::FullEvict);
}

TEST(RespawnResolver, PositionSourcePriority)
{
    const PositionStruct bind = pos(1000, 2000);
    const PositionStruct sampled = pos(300, 400);
    const PositionStruct death = pos(7000, 8000);

    // Bind point wins over everything (x!=0 || y!=0).
    auto rBind = RespawnResolver::resolvePosition(bind, 7, sampled, death);
    EXPECT_EQ(rBind.source, RespawnResolver::PositionSource::BindPoint);
    EXPECT_FLOAT_EQ(rBind.pos.positionX, 1000.0f);

    // No bind + zone present: sampled point.
    auto rZone = RespawnResolver::resolvePosition(pos(0, 0), 7, sampled, death);
    EXPECT_EQ(rZone.source, RespawnResolver::PositionSource::ZoneSample);
    EXPECT_FLOAT_EQ(rZone.pos.positionX, 300.0f);

    // No bind + no zones: death fallback.
    auto rDeath = RespawnResolver::resolvePosition(pos(0, 0), 0, sampled, death);
    EXPECT_EQ(rDeath.source, RespawnResolver::PositionSource::DeathFallback);
    EXPECT_FLOAT_EQ(rDeath.pos.positionX, 7000.0f);

    // Partial bind (y-only) still counts as a bind.
    auto rPartial = RespawnResolver::resolvePosition(pos(0, 5), 7, sampled, death);
    EXPECT_EQ(rPartial.source, RespawnResolver::PositionSource::BindPoint);
}

TEST(RespawnResolver, VitalsWithoutEffects)
{
    // 30% of base max, HP at least 1, mana at least 0.
    auto v = RespawnResolver::computeVitals(100, 50, {}, 1000, 0.30f, 0.30f);
    EXPECT_EQ(v.effectiveMaxHealth, 100);
    EXPECT_EQ(v.effectiveMaxMana, 50);
    EXPECT_EQ(v.newHp, 30);
    EXPECT_EQ(v.newMana, 15);
}

TEST(RespawnResolver, VitalsWithSicknessPenalty)
{
    // -20 max_health (rounded) then 30%: eff 80 → 24 HP.
    auto v = RespawnResolver::computeVitals(100, 50, {sickness("max_health", -20.4f, 2000)}, 1000, 0.30f, 0.30f);
    EXPECT_EQ(v.effectiveMaxHealth, 80);
    EXPECT_EQ(v.newHp, 24);
    // Expired effects are ignored.
    auto vExp = RespawnResolver::computeVitals(100, 50, {sickness("max_health", -20.0f, 500)}, 1000, 0.30f, 0.30f);
    EXPECT_EQ(vExp.effectiveMaxHealth, 100);
    // DoT/HoT never touch the max.
    ActiveEffectStruct dot = sickness("max_health", -50.0f, 2000);
    dot.effectTypeSlug = "dot";
    auto vDot = RespawnResolver::computeVitals(100, 50, {dot}, 1000, 0.30f, 0.30f);
    EXPECT_EQ(vDot.effectiveMaxHealth, 100);
}

TEST(RespawnResolver, VitalsFloors)
{
    // Base 1 HP stays 1 even with a debuff; mana floors at 0.
    auto v = RespawnResolver::computeVitals(1, 0, {sickness("max_health", -50.0f, 2000)}, 1000, 0.30f, 0.30f);
    EXPECT_EQ(v.effectiveMaxHealth, 1);
    EXPECT_EQ(v.newHp, 1);
    EXPECT_EQ(v.newMana, 0);
}
