// Unit tests for CrowdControl DR math (pure, header-only) and the mob
// effect store in MobInstanceManager (add/prune/CC/unregister purge/DR).
#include "services/CharacterManager.hpp"
#include "services/CrowdControl.hpp"
#include "services/MobInstanceManager.hpp"

#include <algorithm>
#include <climits>
#include <cstdint>
#include <ctime>
#include <gtest/gtest.h>

TEST(CrowdControl, StepsThenImmune)
{
    std::vector<int64_t> hist;
    EXPECT_FLOAT_EQ(CrowdControl::applyWithDR(hist, 1000, 4.0f), 4.0f);
    EXPECT_FLOAT_EQ(CrowdControl::applyWithDR(hist, 1001, 4.0f), 2.0f);
    EXPECT_FLOAT_EQ(CrowdControl::applyWithDR(hist, 1002, 4.0f), 1.0f);
    EXPECT_FLOAT_EQ(CrowdControl::applyWithDR(hist, 1003, 4.0f), -1.0f);
}

TEST(CrowdControl, WindowExpiryResetsChain)
{
    std::vector<int64_t> hist;
    EXPECT_FLOAT_EQ(CrowdControl::applyWithDR(hist, 1000, 4.0f, 15.0f), 4.0f);
    // 16s later the old application aged out: full duration again.
    EXPECT_FLOAT_EQ(CrowdControl::applyWithDR(hist, 1016, 4.0f, 15.0f), 4.0f);
    // Immune rejections do not extend the window.
    EXPECT_FLOAT_EQ(CrowdControl::applyWithDR(hist, 1017, 4.0f, 15.0f), 2.0f);
    EXPECT_FLOAT_EQ(CrowdControl::applyWithDR(hist, 1018, 4.0f, 15.0f), 1.0f);
    EXPECT_FLOAT_EQ(CrowdControl::applyWithDR(hist, 1019, 4.0f, 15.0f), -1.0f);
    EXPECT_FLOAT_EQ(CrowdControl::applyWithDR(hist, 1020, 4.0f, 15.0f), -1.0f);
    EXPECT_EQ(hist.size(), 3u);
}

namespace
{

ActiveEffectStruct ccEff(const std::string &type, int64_t expires)
{
    ActiveEffectStruct e;
    e.effectSlug = "test_" + type;
    e.effectTypeSlug = type;
    e.expiresAt = expires;
    return e;
}

MobDataStruct mob(int uid)
{
    MobDataStruct m;
    m.uid = uid;
    m.id = 4;
    return m;
}

} // namespace

TEST(MobEffects, AddGetPruneByExpiry)
{
    Logger logger{"test"};
    MobInstanceManager mgr(logger);
    EXPECT_TRUE(mgr.registerMobInstance(mob(11)));
    EXPECT_TRUE(mgr.getMobEffects(11, 1000).empty());
    mgr.addMobEffect(11, ccEff("stun", 1010));
    mgr.addMobEffect(11, ccEff("debuff", 0));
    auto live = mgr.getMobEffects(11, 1005);
    EXPECT_EQ(live.size(), 2u);
    // Stun expired at 1010: pruned on read.
    live = mgr.getMobEffects(11, 1011);
    ASSERT_EQ(live.size(), 1u);
    EXPECT_EQ(live[0].effectTypeSlug, "debuff");
    EXPECT_TRUE(mgr.hasMobCC(11, "debuff", 1011));
    EXPECT_FALSE(mgr.hasMobCC(11, "stun", 1011));
    EXPECT_FALSE(mgr.hasMobCC(11, "root", 1011));
}

TEST(MobEffects, DieWithInstance)
{
    Logger logger{"test"};
    MobInstanceManager mgr(logger);
    EXPECT_TRUE(mgr.registerMobInstance(mob(12)));
    mgr.addMobEffect(12, ccEff("stun", 9999));
    mgr.unregisterMobInstance(12);
    EXPECT_TRUE(mgr.getMobEffects(12, 1000).empty());
    // Re-registering the uid starts clean (no ghost effects).
    EXPECT_TRUE(mgr.registerMobInstance(mob(12)));
    EXPECT_TRUE(mgr.getMobEffects(12, 1000).empty());
}

TEST(MobEffects, UnknownUidIsSafeNoOp)
{
    Logger logger{"test"};
    MobInstanceManager mgr(logger);
    mgr.addMobEffect(424242, ccEff("stun", 9999)); // no crash
    EXPECT_TRUE(mgr.getMobEffects(424242, 1000).empty());
    EXPECT_FALSE(mgr.hasMobCC(424242, "stun", 1000));
    EXPECT_FLOAT_EQ(mgr.applyMobCC(424242, "stun", 2.0f, 1000), -1.0f);
}

TEST(MobEffects, DiminishingReturns)
{
    Logger logger{"test"};
    MobInstanceManager mgr(logger);
    EXPECT_TRUE(mgr.registerMobInstance(mob(13)));
    EXPECT_FLOAT_EQ(mgr.applyMobCC(13, "stun", 2.0f, 1000), 2.0f);
    EXPECT_FLOAT_EQ(mgr.applyMobCC(13, "stun", 2.0f, 1001), 1.0f);
    EXPECT_FLOAT_EQ(mgr.applyMobCC(13, "stun", 2.0f, 1002), 0.5f);
    EXPECT_FLOAT_EQ(mgr.applyMobCC(13, "stun", 2.0f, 1003), -1.0f);
    // Root is a separate chain from stun.
    EXPECT_FLOAT_EQ(mgr.applyMobCC(13, "root", 2.0f, 1003), 2.0f);
}

namespace
{

ActiveEffectStruct dotEff(const std::string &slug, int64_t expires)
{
    ActiveEffectStruct e;
    e.effectSlug = slug;
    e.effectTypeSlug = "dot";
    e.attributeSlug = "max_health";
    e.value = -5.0f;
    e.expiresAt = expires;
    e.tickMs = 1000;
    return e;
}

CharacterDataStruct toon(int id)
{
    CharacterDataStruct c;
    c.characterId = id;
    return c;
}

} // namespace

TEST(CharacterDots, StackUpToThreeThenRefreshOldest)
{
    Logger logger{"test"};
    CharacterManager chars{logger};
    chars.addCharacter(toon(1));
    const int64_t base = static_cast<int64_t>(std::time(nullptr)) + 1000;
    chars.addActiveEffect(1, dotEff("poison", base));
    chars.addActiveEffect(1, dotEff("poison", base + 1));
    chars.addActiveEffect(1, dotEff("poison", base + 2));
    auto got = chars.getCharacterData(1);
    int n = 0;
    for (const auto &e : got.activeEffects)
        if (e.effectSlug == "poison")
            ++n;
    EXPECT_EQ(n, 3);
    // 4th does not add a 4th instance: oldest refreshed instead.
    chars.addActiveEffect(1, dotEff("poison", base + 1000));
    got = chars.getCharacterData(1);
    n = 0;
    int64_t earliest = INT64_MAX;
    for (const auto &e : got.activeEffects)
        if (e.effectSlug == "poison")
        {
            ++n;
            earliest = std::min(earliest, e.expiresAt);
        }
    EXPECT_EQ(n, 3);
    EXPECT_EQ(earliest, base + 1); // base refreshed to base+1000
}

TEST(CharacterDots, BuffsStillRefreshWithoutStacking)
{
    Logger logger{"test"};
    CharacterManager chars{logger};
    chars.addCharacter(toon(2));
    ActiveEffectStruct b;
    b.effectSlug = "might";
    b.effectTypeSlug = "buff";
    b.attributeSlug = "physical_attack";
    b.value = 5.0f;
    b.expiresAt = 2000;
    chars.addActiveEffect(2, b);
    b.expiresAt = 3000;
    chars.addActiveEffect(2, b);
    auto got = chars.getCharacterData(2);
    int n = 0;
    for (const auto &e : got.activeEffects)
        if (e.effectSlug == "might")
            ++n;
    EXPECT_EQ(n, 1);
}

TEST(CharacterCC, StunGateAndDiminishing)
{
    Logger logger{"test"};
    CharacterManager chars{logger};
    chars.addCharacter(toon(3));
    EXPECT_FALSE(chars.hasLiveCC(3, "stun", 1000));
    ActiveEffectStruct s;
    s.effectSlug = "shield_slam_stun";
    s.effectTypeSlug = "stun";
    s.expiresAt = 0;
    EXPECT_FLOAT_EQ(chars.applyCrowdControl(3, s, 2.0f, 1000), 2.0f);
    EXPECT_TRUE(chars.hasLiveCC(3, "stun", 1001));
    EXPECT_FLOAT_EQ(chars.applyCrowdControl(3, s, 2.0f, 1001), 1.0f);
    EXPECT_FLOAT_EQ(chars.applyCrowdControl(3, s, 2.0f, 1002), 0.5f);
    EXPECT_FLOAT_EQ(chars.applyCrowdControl(3, s, 2.0f, 1003), -1.0f);
    EXPECT_FALSE(chars.hasLiveCC(9, "stun", 1000)); // unknown char
    EXPECT_FLOAT_EQ(chars.applyCrowdControl(9, s, 2.0f, 1000), -1.0f);
}
