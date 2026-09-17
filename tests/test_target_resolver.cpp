// Unit tests for CombatTargetResolver (A4 pure extract).
//
// Pins the initiation range-resolution rules 1-1 with the inline block this
// replaced: AREA/NONE skip, character-first caster lookup, PLAYER/SELF
// direct target, MOB last-sent preference with instance fallback, unknown
// sides skip the check, XY-plane distance.
#include "services/CombatTargetResolver.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <optional>

namespace
{

using Lookup = CombatTargetResolver::PosLookup;

PositionStruct pos(float x, float y)
{
    PositionStruct p;
    p.positionX = x;
    p.positionY = y;
    return p;
}

Lookup fixed(std::optional<PositionStruct> v)
{
    return [v](int) { return v; };
}

Lookup empty()
{
    return [](int) -> std::optional<PositionStruct> { return std::nullopt; };
}

} // namespace

TEST(TargetResolver, AreaAndNoneSkipRange)
{
    auto r = CombatTargetResolver::resolveRange(
        1, 0, CombatTargetType::AREA, fixed(pos(0, 0)), empty(), empty());
    EXPECT_FALSE(r.checkRange);
    auto r2 = CombatTargetResolver::resolveRange(
        1, 0, CombatTargetType::NONE, fixed(pos(0, 0)), empty(), empty());
    EXPECT_FALSE(r2.checkRange);
}

TEST(TargetResolver, PlayerCasterPlayerTargetDistance)
{
    Lookup byId = [](int id) -> std::optional<PositionStruct>
    {
        if (id == 1)
            return pos(0, 0);
        if (id == 2)
            return pos(300, 0);
        return std::nullopt;
    };
    auto r2 = CombatTargetResolver::resolveRange(1, 2, CombatTargetType::PLAYER, byId, empty(), empty());
    EXPECT_TRUE(r2.checkRange);
    EXPECT_FLOAT_EQ(r2.distance, 300.0f);
}

TEST(TargetResolver, SelfTargetUsesCharacterPosition)
{
    Lookup byId = [](int id) -> std::optional<PositionStruct>
    {
        if (id == 1)
            return pos(10, 10);
        return std::nullopt;
    };
    auto r = CombatTargetResolver::resolveRange(1, 1, CombatTargetType::SELF, byId, empty(), empty());
    EXPECT_TRUE(r.checkRange);
    EXPECT_FLOAT_EQ(r.distance, 0.0f);
}

TEST(TargetResolver, MobCasterFallsBackToInstanceLookup)
{
    Lookup chars = [](int id) -> std::optional<PositionStruct>
    {
        if (id == 1)
            return pos(0, 0);
        return std::nullopt;
    };
    Lookup mobs = [](int id) -> std::optional<PositionStruct>
    {
        if (id == 7)
            return pos(30, 40);
        return std::nullopt;
    };
    auto r = CombatTargetResolver::resolveRange(7, 1, CombatTargetType::PLAYER, chars, empty(), mobs);
    EXPECT_TRUE(r.checkRange);
    EXPECT_FLOAT_EQ(r.distance, 50.0f); // 3-4-5 triangle
}

TEST(TargetResolver, UnknownCasterSkips)
{
    auto r = CombatTargetResolver::resolveRange(424242, 1, CombatTargetType::PLAYER, empty(), empty(), empty());
    EXPECT_FALSE(r.checkRange);
}

TEST(TargetResolver, UnknownTargetSkips)
{
    Lookup byId = [](int id) -> std::optional<PositionStruct>
    {
        if (id == 1)
            return pos(0, 0);
        return std::nullopt;
    };
    auto r = CombatTargetResolver::resolveRange(1, 424242, CombatTargetType::PLAYER, byId, empty(), empty());
    EXPECT_FALSE(r.checkRange);
}

TEST(TargetResolver, MobTargetPrefersSentPosition)
{
    Lookup chars = [](int id) -> std::optional<PositionStruct>
    {
        if (id == 1)
            return pos(0, 0);
        return std::nullopt;
    };
    // Movement layer renders the mob at (100,0); instance lags at (500,0).
    Lookup sent = [](int) -> std::optional<PositionStruct> { return pos(100, 0); };
    Lookup inst = [](int) -> std::optional<PositionStruct> { return pos(500, 0); };
    auto r = CombatTargetResolver::resolveRange(1, 9001, CombatTargetType::MOB, chars, sent, inst);
    EXPECT_TRUE(r.checkRange);
    EXPECT_FLOAT_EQ(r.distance, 100.0f);
}

TEST(TargetResolver, MobTargetFallsBackToInstance)
{
    Lookup chars = [](int id) -> std::optional<PositionStruct>
    {
        if (id == 1)
            return pos(0, 0);
        return std::nullopt;
    };
    Lookup inst = [](int) -> std::optional<PositionStruct> { return pos(500, 0); };
    auto r = CombatTargetResolver::resolveRange(1, 9001, CombatTargetType::MOB, chars, empty(), inst);
    EXPECT_TRUE(r.checkRange);
    EXPECT_FLOAT_EQ(r.distance, 500.0f);
}

TEST(TargetResolver, MissingMobEverywhereSkips)
{
    auto r = CombatTargetResolver::resolveRange(
        1, 9001, CombatTargetType::MOB, fixed(pos(0, 0)), empty(), empty());
    EXPECT_FALSE(r.checkRange);
}
