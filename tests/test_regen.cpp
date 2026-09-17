// Unit tests for RegenManager tick (DI: explicit managers + Logger,
// IStatsNotifier fake; statsNotify=nullptr path covered separately).
#include "services/RegenManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/EquipmentManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/IStatsNotifier.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"

#include <chrono>
#include <gtest/gtest.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

struct FakeNotifier : IStatsNotifier
{
    int calls = 0;
    int lastCid = 0;
    std::string lastSource;
    void sendStatsUpdate(int characterId) override
    {
        ++calls;
        lastCid = characterId;
        lastSource = "";
    }
    void sendStatsUpdate(int characterId, const std::string &source) override
    {
        ++calls;
        lastCid = characterId;
        lastSource = source;
    }
    void sendWorldNotification(int, const std::string &, const nlohmann::json &, const std::string &, const std::string &) override
    {
    }
    void sendWorldNotificationToGameZone(int, const std::string &, const nlohmann::json &, const std::string &, const std::string &) override
    {
    }
};

CharacterAttributeStruct attr(const std::string &slug, int value)
{
    CharacterAttributeStruct a;
    a.slug = slug;
    a.value = value;
    return a;
}

struct RegenFixture : ::testing::Test
{
    Logger logger{"test"};
    ItemManager items{logger};
    InventoryManager inv{items, logger};
    CharacterManager chars{logger};
    GameConfigService cfg{logger};
    EquipmentManager eq{inv, items, chars, logger};
    FakeNotifier notifier;
    RegenManager regen{chars, cfg, &notifier, eq, items, logger};

    void SetUp() override
    {
        cfg.setConfig({
            {"regen.baseHpRegen", "2"},
            {"regen.baseMpRegen", "1"},
            {"regen.hpRegenConCoeff", "0.3"},
            {"regen.mpRegenWisCoeff", "0.5"},
            {"regen.disableInCombatMs", "8000"},
            {"regen.tickIntervalMs", "4000"},
        });
        eq.setGameConfigService(&cfg);
    }

    CharacterDataStruct baseChar(int id)
    {
        CharacterDataStruct c;
        c.characterId = id;
        c.characterLevel = 10;
        c.characterMaxHealth = 100;
        c.characterCurrentHealth = 50;
        c.characterMaxMana = 50;
        c.characterCurrentMana = 10;
        c.attributes = {attr("constitution", 10), attr("wisdom", 4)};
        return c;
    }

    int giveEquipped(int charId, int itemId, int64_t rowId)
    {
        EXPECT_TRUE(inv.addItemToInventory(charId, itemId, 1));
        inv.updateInventoryItemId(charId, itemId, rowId);
        auto r = eq.equipItem(charId, static_cast<int>(rowId));
        EXPECT_EQ(r.error, EquipmentManager::EquipError::NONE);
        return static_cast<int>(rowId);
    }
};

} // namespace

// con=10 -> hpFromStats = 2 + int(10*0.3) = 5
// wis=4  -> mpFromStats = 1 + int(4*0.5)  = 3
TEST_F(RegenFixture, GainFormulaAndNotify)
{
    chars.addCharacter(baseChar(1));
    regen.tickRegen();
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentHealth, 55);
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentMana, 13);
    EXPECT_EQ(notifier.calls, 1);
    EXPECT_EQ(notifier.lastCid, 1);
    EXPECT_EQ(notifier.lastSource, "regen");
}

TEST_F(RegenFixture, DeadSkipped)
{
    auto c = baseChar(1);
    c.isDead = true;
    c.characterCurrentHealth = 0;
    chars.addCharacter(c);
    regen.tickRegen();
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentHealth, 0);
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentMana, 10); // untouched
    EXPECT_EQ(notifier.calls, 0);
}

TEST_F(RegenFixture, CombatWindowSuppresses)
{
    auto c = baseChar(1);
    c.lastInCombatAt = std::chrono::steady_clock::now(); // just hit
    chars.addCharacter(c);
    regen.tickRegen();
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentHealth, 50);
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentMana, 10);
    EXPECT_EQ(notifier.calls, 0);
}

TEST_F(RegenFixture, ClampToMax)
{
    auto c = baseChar(1);
    c.characterCurrentHealth = 99; // gain 5 -> clamp 100
    c.characterCurrentMana = 50;   // full -> untouched
    chars.addCharacter(c);
    regen.tickRegen();
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentHealth, 100);
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentMana, 50);
    EXPECT_EQ(notifier.calls, 1);
}

TEST_F(RegenFixture, FullHealthNoNotify)
{
    auto c = baseChar(1);
    c.characterCurrentHealth = 100;
    c.characterCurrentMana = 50;
    chars.addCharacter(c);
    regen.tickRegen();
    EXPECT_EQ(notifier.calls, 0);
}

TEST_F(RegenFixture, ActiveEffectRegenBonus)
{
    auto c = baseChar(1);
    ActiveEffectStruct eff;
    eff.effectSlug = "regen_buff";
    eff.attributeSlug = "hp_regen_per_s";
    eff.value = 8.0f;
    eff.expiresAt = 0; // permanent
    eff.tickMs = 0;
    c.activeEffects.push_back(eff);
    chars.addCharacter(c);
    regen.tickRegen();
    // hpRegenBase=8, tickSec=4 -> 32 > hpFromStats 5
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentHealth, 82);
    EXPECT_EQ(notifier.calls, 1);
}

TEST_F(RegenFixture, ExpiredAndTickEffectsIgnored)
{
    auto c = baseChar(1);
    ActiveEffectStruct expired;
    expired.effectSlug = "old_buff";
    expired.attributeSlug = "hp_regen_per_s";
    expired.value = 100.0f;
    expired.expiresAt = 1; // long past
    expired.tickMs = 0;
    ActiveEffectStruct dot;
    dot.effectSlug = "poison";
    dot.attributeSlug = "hp_regen_per_s";
    dot.value = 100.0f;
    dot.expiresAt = 0;
    dot.tickMs = 1000; // DoT/HoT tick -> skipped
    c.activeEffects.push_back(expired);
    c.activeEffects.push_back(dot);
    chars.addCharacter(c);
    regen.tickRegen();
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentHealth, 55); // base formula only
}

TEST_F(RegenFixture, EquipmentRegenBonusAndEffectiveCap)
{
    ItemDataStruct ring;
    ring.id = 300;
    ring.slug = "regen_ring";
    ring.stackMax = 1;
    ring.isEquippable = true;
    ring.equipSlotSlug = "ring_1";
    ItemAttributeStruct regenAttr;
    regenAttr.slug = "hp_regen_per_s";
    regenAttr.value = 5;
    regenAttr.apply_on = "equip";
    ItemAttributeStruct maxHpAttr;
    maxHpAttr.slug = "max_health";
    maxHpAttr.value = 50;
    maxHpAttr.apply_on = "equip";
    ring.attributes = {regenAttr, maxHpAttr};
    items.setItemsList({ring});

    auto c = baseChar(1);
    c.characterCurrentHealth = 120; // above base max 100, below effective 150
    chars.addCharacter(c);
    giveEquipped(1, 300, 21);

    regen.tickRegen();
    // hpRegenBase=5*4=20 > 5; effectiveMax = 100 + 50 = 150
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentHealth, 140);
    EXPECT_EQ(notifier.calls, 1);
}

TEST_F(RegenFixture, NullNotifierStillApplies)
{
    RegenManager silent{chars, cfg, nullptr, eq, items, logger};
    chars.addCharacter(baseChar(1));
    silent.tickRegen(); // must not crash
    EXPECT_EQ(chars.getCharacterData(1).characterCurrentHealth, 55);
    EXPECT_EQ(notifier.calls, 0);
}
