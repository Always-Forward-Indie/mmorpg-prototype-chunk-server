// Unit tests for chunk CharacterManager vitals (Logger only).
#include "services/CharacterManager.hpp"

#include <gtest/gtest.h>

namespace
{

CharacterDataStruct makeChar(int id, int hp = 100, int mp = 50)
{
    CharacterDataStruct c;
    c.characterId = id;
    c.characterLevel = 10;
    c.characterMaxHealth = hp;
    c.characterCurrentHealth = hp;
    c.characterMaxMana = mp;
    c.characterCurrentMana = mp;
    return c;
}

struct CharFixture : ::testing::Test
{
    Logger logger{"test"};
    CharacterManager chars{logger};

    void SetUp() override
    {
        chars.addCharacter(makeChar(1));
    }
};

} // namespace

TEST_F(CharFixture, AddGetRemove)
{
    auto got = chars.getCharacterData(1);
    EXPECT_EQ(got.characterId, 1);
    EXPECT_EQ(got.characterLevel, 10);
    EXPECT_EQ(chars.getCharacterData(424242).characterId, 0); // miss sentinel
    chars.removeCharacter(1);
    EXPECT_EQ(chars.getCharacterData(1).characterId, 0);
}

TEST_F(CharFixture, JoinSeedsMovementValidation)
{
    // Regression (L4 quest swarm): lastValidatedPosition stayed (0,0,0) after
    // join, so the first legal move was falsely rejected as a teleport and
    // the client/server positions desynced permanently (OUT_OF_RANGE).
    CharacterDataStruct c = makeChar(9);
    c.characterPosition.positionX = 585.0f;
    c.characterPosition.positionY = -3300.0f;
    chars.addCharacter(c);
    auto got = chars.getCharacterData(9);
    EXPECT_FLOAT_EQ(got.lastValidatedPosition.positionX, 585.0f);
    EXPECT_FLOAT_EQ(got.lastValidatedPosition.positionY, -3300.0f);
    EXPECT_EQ(got.lastMoveSrvMs, 0);
}

TEST_F(CharFixture, DamageHealMana)
{
    auto dmg = chars.applyDamageToCharacter(1, 30);
    EXPECT_FALSE(dmg.died);
    EXPECT_EQ(dmg.newHealth, 70);
    auto kill = chars.applyDamageToCharacter(1, 1000);
    EXPECT_TRUE(kill.died);
    EXPECT_EQ(kill.newHealth, 0);

    chars.addCharacter(makeChar(2));
    chars.applyDamageToCharacter(2, 50);
    chars.applyHealToCharacter(2, 1000);
    EXPECT_EQ(chars.getCharacterData(2).characterCurrentHealth, 100); // clamped
    EXPECT_TRUE(chars.trySpendMana(2, 20));
    EXPECT_EQ(chars.getCharacterData(2).characterCurrentMana, 30);
    EXPECT_FALSE(chars.trySpendMana(2, 1000));
}

TEST_F(CharFixture, PositionAndZoneQuery)
{
    PositionStruct p;
    p.positionX = 10;
    p.positionY = 10;
    chars.setCharacterPosition(1, p);
    auto got = chars.getCharacterPosition(1);
    EXPECT_FLOAT_EQ(got.positionX, 10.0f);
    chars.addCharacter(makeChar(2));
    PositionStruct far;
    far.positionX = 100000;
    far.positionY = 100000;
    chars.setCharacterPosition(2, far);
    auto near = chars.getCharactersInZone(10, 10, 500);
    EXPECT_EQ(near.size(), 1u);
    EXPECT_EQ(near[0].characterId, 1);
}

TEST_F(CharFixture, ActiveEffectsAddRemove)
{
    ActiveEffectStruct eff;
    eff.effectSlug = "might";
    eff.attributeSlug = "strength";
    eff.value = 5.0f;
    eff.expiresAt = 0;
    chars.addActiveEffect(1, eff);
    chars.removeActiveEffectBySlug(1, "might");
    chars.removeActiveEffectBySlug(1, "nope"); // safe no-op
    SUCCEED();
}

TEST_F(CharFixture, AddCharacterSkillDedupsBySlug)
{
    // Sparse optimistic inserts (learn paths) must never duplicate: a late
    // game-driven setLearnedSkill only replaces the same slug.
    SkillStruct a;
    a.skillSlug = "power_slash";
    a.skillName = "Short";
    chars.addCharacterSkill(1, a);
    SkillStruct b;
    b.skillSlug = "power_slash";
    b.skillName = "Full Name From DB";
    chars.addCharacterSkill(1, b);
    auto got = chars.getCharacterData(1);
    ASSERT_EQ(got.skills.size(), 1u);
    EXPECT_EQ(got.skills[0].skillName, "Full Name From DB");
}
