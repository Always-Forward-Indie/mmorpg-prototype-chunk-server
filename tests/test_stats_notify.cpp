// Unit tests for CharacterStatsNotificationService with the REAL service
// (fully DI since phase 3: no GameServices, no stubs).
#include "services/CharacterStatsNotificationService.hpp"
#include "services/CharacterManager.hpp"
#include "services/EquipmentManager.hpp"
#include "services/ExperienceCacheManager.hpp"
#include "services/ExperienceManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameZoneManager.hpp"
#include "services/InventoryManager.hpp"
#include "services/ItemManager.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace
{

struct NotifyFixture : ::testing::Test
{
    Logger logger{"test"};
    ItemManager items{logger};
    InventoryManager inv{items, logger};
    CharacterManager chars{logger};
    ExperienceCacheManager expCache{logger};
    ExperienceManager experience{chars, expCache, nullptr, nullptr, logger};
    GameConfigService config{logger};
    EquipmentManager equipment{inv, items, chars, logger};
    GameZoneManager gameZones{logger};
    CharacterStatsNotificationService stats{chars,
        experience,
        inv,
        equipment,
        items,
        config,
        gameZones,
        logger};

    std::vector<nlohmann::json> broadcast;
    struct DirectRecord
    {
        int cid;
        nlohmann::json packet;
    };
    std::vector<DirectRecord> direct;

    void SetUp() override
    {
        equipment.setGameConfigService(&config);
        stats.setStatsUpdateCallback([this](const nlohmann::json &p) { broadcast.push_back(p); });
        stats.setDirectSendCallback(
            [this](int cid, const nlohmann::json &p) { direct.push_back({cid, p}); });
    }

    CharacterDataStruct baseChar(int id, float x = 0.0f, float y = 0.0f)
    {
        CharacterDataStruct c;
        c.characterId = id;
        c.characterLevel = 5;
        c.characterExperiencePoints = 500;
        c.characterMaxHealth = 100;
        c.characterCurrentHealth = 80;
        c.characterMaxMana = 50;
        c.characterCurrentMana = 20;
        c.characterPosition.positionX = x;
        c.characterPosition.positionY = y;
        return c;
    }

    void addZone(int id, float minX, float maxX, float minY, float maxY)
    {
        GameZoneStruct z;
        z.id = id;
        z.minX = minX;
        z.maxX = maxX;
        z.minY = minY;
        z.maxY = maxY;
        zones.push_back(z);
        gameZones.loadGameZones(zones); // replaces: reload the full set
    }

    std::vector<GameZoneStruct> zones;
};

} // namespace

TEST_F(NotifyFixture, StatsUpdatePacketShape)
{
    chars.addCharacter(baseChar(1));
    broadcast.clear();
    direct.clear();
    stats.sendStatsUpdate(1);
    ASSERT_EQ(broadcast.size(), 1u); // sendStatsUpdate uses the broadcast seam
    EXPECT_EQ(broadcast[0]["header"]["eventType"], "stats_update");
    EXPECT_EQ(broadcast[0]["body"]["characterId"], 1);
    EXPECT_EQ(broadcast[0]["body"]["level"], 5);
}

TEST_F(NotifyFixture, StatsUpdateSourceTag)
{
    chars.addCharacter(baseChar(1));
    broadcast.clear();
    direct.clear();
    stats.sendStatsUpdate(1, "regen");
    ASSERT_EQ(broadcast.size(), 1u);
    EXPECT_EQ(broadcast[0]["body"]["source"], "regen");
}

TEST_F(NotifyFixture, WorldNotificationDirectBeatsBroadcast)
{
    // directSendCallback_ set -> personal delivery, broadcast untouched.
    broadcast.clear();
    direct.clear();
    stats.sendWorldNotification(1, "fellowship_bonus", nlohmann::json{{"xpBonus", 5}}, "low", "float_text");
    ASSERT_EQ(direct.size(), 1u);
    EXPECT_EQ(direct[0].cid, 1);
    EXPECT_EQ(direct[0].packet["body"]["notificationType"], "fellowship_bonus");
    EXPECT_TRUE(broadcast.empty());
}

TEST_F(NotifyFixture, ZoneFiltering)
{
    addZone(7, 0.0f, 1000.0f, 0.0f, 1000.0f);
    addZone(8, 5000.0f, 6000.0f, 5000.0f, 6000.0f);
    chars.addCharacter(baseChar(1, 100.0f, 100.0f));   // zone 7
    chars.addCharacter(baseChar(2, 5100.0f, 5100.0f)); // zone 8
    direct.clear();
    stats.sendWorldNotificationToGameZone(7, "zone_event_start", nlohmann::json::object());
    ASSERT_EQ(direct.size(), 1u);
    EXPECT_EQ(direct[0].cid, 1);
}

TEST_F(NotifyFixture, NoCallbacksNoCrash)
{
    CharacterStatsNotificationService silent{chars,
        experience,
        inv,
        equipment,
        items,
        config,
        gameZones,
        logger};
    chars.addCharacter(baseChar(1));
    silent.sendStatsUpdate(1);
    silent.sendStatsUpdate(1, "regen");
    silent.sendWorldNotification(1, "x");
    silent.sendWorldNotificationToGameZone(7, "y");
    SUCCEED();
}
