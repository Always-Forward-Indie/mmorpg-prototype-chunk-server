// Unit tests for ChampionManager (DI: explicit managers + Logger;
// statsNotify=nullptr, sendToGameServer callback recorded).
#include "services/ChampionManager.hpp"
#include "services/CharacterManager.hpp"
#include "services/GameConfigService.hpp"
#include "services/GameZoneManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/MobManager.hpp"
#include "services/SpawnZoneManager.hpp"

#include <gtest/gtest.h>
#include <chrono>
#include <string>
#include <vector>

namespace
{

GameZoneStruct makeZone()
{
    GameZoneStruct z;
    z.id = 7;
    z.minX = 0;
    z.maxX = 10000;
    z.minY = 0;
    z.maxY = 10000;
    z.championThresholdKills = 3;
    return z;
}

MobDataStruct makeTemplate()
{
    MobDataStruct m;
    m.id = 5;
    m.slug = "wolf";
    m.name = "Wolf";
    m.level = 5;
    m.maxHealth = 100;
    m.currentHealth = 100;
    return m;
}

struct ChampFixture : ::testing::Test
{
    Logger logger{"test"};
    GameZoneManager zones{logger};
    GameConfigService config{logger};
    MobInstanceManager instances{logger};
    CharacterManager chars{logger};
    MobManager mobs{logger};
    SpawnZoneManager spawnZones{mobs, logger};
    ChampionManager champ{zones, config, instances, chars, mobs, spawnZones, nullptr, logger};

    void SetUp() override
    {
        zones.loadGameZones({makeZone()});
        mobs.setListOfMobs({makeTemplate()});
    }

    size_t liveChampions()
    {
        return instances.getAllLivingInstances().size();
    }
};

} // namespace

TEST_F(ChampFixture, ThresholdSpawnAndSuppression)
{
    EXPECT_EQ(liveChampions(), 0u);
    champ.recordMobKill(7, 5);
    champ.recordMobKill(7, 5);
    EXPECT_EQ(liveChampions(), 0u); // below threshold
    champ.recordMobKill(7, 5);      // 3rd kill -> spawn
    EXPECT_EQ(liveChampions(), 1u);
    champ.recordMobKill(7, 5);
    champ.recordMobKill(7, 5);
    champ.recordMobKill(7, 5);
    EXPECT_EQ(liveChampions(), 1u); // suppressed while active
}

TEST_F(ChampFixture, KillClearsAndCycleRepeats)
{
    champ.recordMobKill(7, 5);
    champ.recordMobKill(7, 5);
    champ.recordMobKill(7, 5);
    ASSERT_EQ(liveChampions(), 1u);
    int uid = instances.getAllLivingInstances()[0].uid;
    EXPECT_NE(uid, 0);

    std::vector<std::string> sent;
    champ.setSendToGameServerCallback([&](const std::string &pkt) { sent.push_back(pkt); });
    // Production order: the instance dies in combat first, then the kill is
    // reported. Kill it through the instance manager.
    instances.applyDamageToMob(uid, 1000000);
    champ.onChampionKilled(uid, 1);
    EXPECT_EQ(liveChampions(), 0u);

    // Counter reset (or halved): a fresh threshold run spawns again.
    for (int i = 0; i < 6; ++i)
        champ.recordMobKill(7, 5);
    EXPECT_GE(liveChampions(), 1u);
}

TEST_F(ChampFixture, UnknownTemplateNeverSpawns)
{
    for (int i = 0; i < 10; ++i)
        champ.recordMobKill(7, 424242);
    EXPECT_EQ(liveChampions(), 0u);
}

TEST_F(ChampFixture, ChampionZoneIdMatchesTargetGameZone)
{
    // Pin for the spawnChampion zone-search: the champion must take the
    // zoneId of a spawn zone INSIDE the target game zone, not the first
    // map entry. std::map iterates by key, so the outside zone (id 1)
    // comes first and exposes the unconditional-break bug.
    GameZoneStruct other = makeZone();
    other.id = 8;
    other.minX = 20000;
    other.maxX = 30000;
    other.minY = 20000;
    other.maxY = 30000;
    zones.loadGameZones({makeZone(), other});

    SpawnZoneStruct szOutside;
    szOutside.zoneId = 1;
    szOutside.centerX = 25000.0f;
    szOutside.centerY = 25000.0f;
    SpawnZoneStruct szInside;
    szInside.zoneId = 2;
    szInside.centerX = 5000.0f;
    szInside.centerY = 5000.0f;
    spawnZones.loadMobSpawnZones({szOutside, szInside});

    const int uid = champ.spawnChampion(5, 7, "Test ", 1.0f, "test_zone");
    ASSERT_NE(uid, 0);
    EXPECT_EQ(instances.getMobInstance(uid).zoneId, 2);
}

TEST_F(ChampFixture, ChampionSpawnPointRespectsCircleGameZone)
{
    // resolveChampionSpawnPoint must use shape-aware containment: a spawn
    // zone in the AABB corner but outside the CIRCLE game zone must not be
    // picked — with no valid candidates the fallback (zone centre) applies.
    GameZoneStruct circle;
    circle.id = 9;
    circle.shape = ZoneShape::CIRCLE;
    circle.centerX = 5000.0f;
    circle.centerY = 5000.0f;
    circle.outerRadius = 1000.0f;
    circle.minX = 4000;
    circle.maxX = 6000;
    circle.minY = 4000;
    circle.maxY = 6000;
    zones.loadGameZones({circle});

    SpawnZoneStruct corner;
    corner.zoneId = 1;
    corner.centerX = 4100.0f; // dist ~1273 from centre: inside AABB, outside disc
    corner.centerY = 4100.0f;
    corner.minX = 4050;
    corner.maxX = 4150;
    corner.minY = 4050;
    corner.maxY = 4150;
    corner.minZ = 0;
    corner.maxZ = 200;
    spawnZones.loadMobSpawnZones({corner});

    const int uid = champ.spawnChampion(5, 9, "Test ", 1.0f, "test_circle");
    ASSERT_NE(uid, 0);
    const auto inst = instances.getMobInstance(uid);
    const float dx = inst.position.positionX - 5000.0f;
    const float dy = inst.position.positionY - 5000.0f;
    EXPECT_LE(dx * dx + dy * dy, 1000.0f * 1000.0f);
}

TEST_F(ChampFixture, ChanceZeroNeverSpawns)
{
    // spawn_chance_pct=0: threshold reached, roll always fails, counter
    // resets (retry next threshold — still nothing).
    config.setConfig({{"champion.spawn_chance_pct", "0"}});
    for (int i = 0; i < 10; ++i)
        champ.recordMobKill(7, 5);
    EXPECT_EQ(liveChampions(), 0u);
}

TEST_F(ChampFixture, GarbagePushedIdFallsBackToContainment)
{
    // A pushed gameZoneId naming no known game zone is ignored (never
    // trusted blindly); the spawn zone center resolves live instead.
    // (Live incident: push carried 41 for a zone owning 9001.)
    SpawnZoneStruct sz;
    sz.zoneId = 9;
    sz.gameZoneId = 41;
    sz.centerX = 5000.0f;
    sz.centerY = 5000.0f;
    spawnZones.loadMobSpawnZones({sz});
    for (int i = 0; i < 3; ++i)
        champ.recordMobKill(0, 5, 9);
    EXPECT_EQ(liveChampions(), 1u);
}

TEST_F(ChampFixture, OriginAttributionBeatsDeathPosition)
{
    // Spawn zone 9 belongs to game zone 7: kills with an empty position
    // fallback (0 = unzoned death, e.g. fled far) still credit zone 7.
    SpawnZoneStruct sz;
    sz.zoneId = 9;
    sz.gameZoneId = 7;
    spawnZones.loadMobSpawnZones({sz});
    for (int i = 0; i < 3; ++i)
        champ.recordMobKill(0, 5, 9);
    EXPECT_EQ(liveChampions(), 1u);
}

TEST_F(ChampFixture, UnknownOriginAndFallbackSpawnsNothing)
{
    // No spawn mapping and no position zone: counter never starts.
    for (int i = 0; i < 3; ++i)
        champ.recordMobKill(0, 5, 0);
    EXPECT_EQ(liveChampions(), 0u);
    // Unmapped spawn zone id: same, falls back to empty.
    for (int i = 0; i < 3; ++i)
        champ.recordMobKill(0, 5, 424242);
    EXPECT_EQ(liveChampions(), 0u);
}

TEST_F(ChampFixture, CapBlocksSecondTemplateWhileActive)
{
    // max_active_per_zone=1: first template spawns, second template's
    // threshold is refused (counter reset, natural retry later).
    MobDataStruct tpl2 = makeTemplate();
    tpl2.id = 6;
    tpl2.slug = "boar";
    tpl2.name = "Boar";
    mobs.setListOfMobs({makeTemplate(), tpl2});
    config.setConfig({{"champion.max_active_per_zone", "1"}});
    for (int i = 0; i < 3; ++i)
        champ.recordMobKill(7, 5);
    ASSERT_EQ(liveChampions(), 1u);
    for (int i = 0; i < 3; ++i)
        champ.recordMobKill(7, 6);
    EXPECT_EQ(liveChampions(), 1u);
}

TEST_F(ChampFixture, TimedLoadAndTick)
{
    TimedChampionTemplate t;
    t.slug = "alpha";
    t.gameZoneId = 7;
    t.mobTemplateId = 5;
    t.intervalHours = 6;
    t.nextSpawnAt = 0; // uninitialised -> skipped
    champ.loadTimedChampions({t});
    champ.tickTimedChampions(); // must not crash, nothing due
    champ.tickSurvivalEvolution();
    SUCCEED();
}

TEST_F(ChampFixture, TimedReloadPreservesSpawnedState)
{
    // Regression: full template re-push (boot, heartbeat re-assert) used
    // to wipe spawned flags -> past-due schedules refired every reload
    // (champion flood, one per minute). Merge must preserve runtime state.
    int64_t fakeEpoch = 1700000000;
    champ.setEpochSecFn([&] { return fakeEpoch; });

    TimedChampionTemplate t;
    t.slug = "alpha";
    t.gameZoneId = 7;
    t.mobTemplateId = 5;
    t.intervalHours = 6;
    t.nextSpawnAt = fakeEpoch - 10; // due
    champ.loadTimedChampions({t});
    champ.tickTimedChampions();
    ASSERT_EQ(liveChampions(), 1u);

    champ.loadTimedChampions({t}); // re-push, same content
    champ.tickTimedChampions();
    EXPECT_EQ(liveChampions(), 1u); // no double spawn
}

TEST_F(ChampFixture, TimedKillWaitsForNextCycle)
{
    // Spawn advances the in-memory schedule: after a kill the next spawn
    // waits a full interval instead of refiring immediately (DB only moves
    // on kill; without the advance the past timestamp refires at once).
    int64_t fakeEpoch = 1700000000;
    champ.setEpochSecFn([&] { return fakeEpoch; });

    TimedChampionTemplate t;
    t.slug = "alpha";
    t.gameZoneId = 7;
    t.mobTemplateId = 5;
    t.intervalHours = 6;
    t.nextSpawnAt = fakeEpoch - 10; // due
    champ.loadTimedChampions({t});
    champ.tickTimedChampions();
    ASSERT_EQ(liveChampions(), 1u);

    const int uid = instances.getAllLivingInstances()[0].uid;
    instances.applyDamageToMob(uid, 1000000);
    champ.onChampionKilled(uid, 1, "alpha");
    ASSERT_EQ(liveChampions(), 0u);

    champ.tickTimedChampions();
    EXPECT_EQ(liveChampions(), 0u); // next cycle not reached

    fakeEpoch += 6 * 3600 + 1;
    champ.tickTimedChampions();
    EXPECT_EQ(liveChampions(), 1u); // next cycle fires
}

// ── Clock-seam pins (fake time, milliseconds, no DB/bots) ───────────────────

TEST_F(ChampFixture, TimedSpawnViaFakeClock)
{
    int64_t fakeEpoch = 1700000000;
    champ.setEpochSecFn([&] { return fakeEpoch; });

    TimedChampionTemplate t;
    t.slug = "alpha";
    t.gameZoneId = 7;
    t.mobTemplateId = 5;
    t.intervalHours = 6;
    t.nextSpawnAt = fakeEpoch + 100;
    champ.loadTimedChampions({t});

    champ.tickTimedChampions();
    EXPECT_EQ(liveChampions(), 0u); // 100 s out: pre-announce only, no spawn

    fakeEpoch += 100;
    champ.tickTimedChampions();
    ASSERT_EQ(liveChampions(), 1u); // window reached -> spawned

    champ.tickTimedChampions();
    EXPECT_EQ(liveChampions(), 1u); // state.spawned: no double spawn
}

TEST_F(ChampFixture, TimedKillReportsFakeEpoch)
{
    // killedAt must be the epoch seconds the game-server stores into
    // next_spawn_at (bigint). Pins the seam end of the reschedule contract.
    int64_t fakeEpoch = 1700000000;
    champ.setEpochSecFn([&] { return fakeEpoch; });

    TimedChampionTemplate t;
    t.slug = "alpha";
    t.gameZoneId = 7;
    t.mobTemplateId = 5;
    t.intervalHours = 6;
    t.nextSpawnAt = fakeEpoch;
    champ.loadTimedChampions({t});
    champ.tickTimedChampions();
    ASSERT_EQ(liveChampions(), 1u);

    std::vector<std::string> sent;
    champ.setSendToGameServerCallback([&](const std::string &pkt) { sent.push_back(pkt); });
    const int uid = instances.getAllLivingInstances()[0].uid;
    instances.applyDamageToMob(uid, 1000000);
    champ.onChampionKilled(uid, 1, "alpha");
    ASSERT_EQ(sent.size(), 1u);
    EXPECT_NE(sent[0].find(std::to_string(fakeEpoch)), std::string::npos);
}

TEST_F(ChampFixture, DespawnViaFakeSteady)
{
    auto t0 = std::chrono::steady_clock::now();
    auto fakeSteady = t0;
    champ.setSteadyFn([&] { return fakeSteady; });

    for (int i = 0; i < 3; ++i)
        champ.recordMobKill(7, 5);
    ASSERT_EQ(liveChampions(), 1u);
    const int uid = instances.getAllLivingInstances()[0].uid;

    fakeSteady = t0 + std::chrono::minutes(31); // past default 30 min window
    champ.tickTimedChampions(); // opens with checkDespawnedChampions()
    EXPECT_EQ(liveChampions(), 0u);
    EXPECT_EQ(instances.getMobInstance(uid).uid, 0);
}

TEST_F(ChampFixture, SurvivalEvolveViaFakeClock)
{
    config.setConfig({{"survival_champion.evolve_hours", "1"}});
    int64_t fakeEpoch = 1700000000;
    champ.setEpochSecFn([&] { return fakeEpoch; });

    MobDataStruct mob = makeTemplate();
    mob.uid = 9001;
    mob.canEvolve = true;
    mob.spawnEpochSec = fakeEpoch;
    mob.position.positionX = 5000.0f;
    mob.position.positionY = 5000.0f;
    ASSERT_TRUE(instances.registerMobInstance(mob));

    champ.tickSurvivalEvolution();
    EXPECT_FALSE(instances.getMobInstance(9001).hasEvolved); // 0 h < 1 h

    fakeEpoch += 2 * 3600;
    champ.tickSurvivalEvolution();
    EXPECT_TRUE(instances.getMobInstance(9001).hasEvolved);
}

// ── skipTime pins (admin-RPC time travel: composes onto injected clocks,
// runs both ticks; non-positive values are ignored) ──────────────────────────

TEST_F(ChampFixture, SkipTimeFiresTimedSpawn)
{
    int64_t fakeEpoch = 1700000000;
    champ.setEpochSecFn([&] { return fakeEpoch; });

    TimedChampionTemplate t;
    t.slug = "alpha";
    t.gameZoneId = 7;
    t.mobTemplateId = 5;
    t.intervalHours = 6;
    t.nextSpawnAt = fakeEpoch + 3600;
    champ.loadTimedChampions({t});
    champ.tickTimedChampions();
    ASSERT_EQ(liveChampions(), 0u); // not due yet

    champ.skipTime(3600); // advances clock AND ticks: spawn fires
    EXPECT_EQ(liveChampions(), 1u);
}

TEST_F(ChampFixture, SkipTimeAdvancesDespawn)
{
    auto t0 = std::chrono::steady_clock::now();
    auto fakeSteady = t0;
    champ.setSteadyFn([&] { return fakeSteady; });

    for (int i = 0; i < 3; ++i)
        champ.recordMobKill(7, 5);
    ASSERT_EQ(liveChampions(), 1u);

    champ.skipTime(31 * 60); // past default 30 min window -> despawned
    EXPECT_EQ(liveChampions(), 0u);
}

TEST_F(ChampFixture, SkipTimeIgnoresNonPositive)
{
    int64_t fakeEpoch = 1700000000;
    champ.setEpochSecFn([&] { return fakeEpoch; });

    TimedChampionTemplate t;
    t.slug = "alpha";
    t.gameZoneId = 7;
    t.mobTemplateId = 5;
    t.intervalHours = 6;
    t.nextSpawnAt = fakeEpoch + 3600;
    champ.loadTimedChampions({t});

    champ.skipTime(0);
    champ.skipTime(-10);
    champ.tickTimedChampions();
    EXPECT_EQ(liveChampions(), 0u); // clocks unmoved, nothing due
}

TEST_F(ChampFixture, SkipTimeStacks)
{
    int64_t fakeEpoch = 1700000000;
    champ.setEpochSecFn([&] { return fakeEpoch; });

    TimedChampionTemplate t;
    t.slug = "alpha";
    t.gameZoneId = 7;
    t.mobTemplateId = 5;
    t.intervalHours = 6;
    t.nextSpawnAt = fakeEpoch + 3600;
    champ.loadTimedChampions({t});

    champ.skipTime(1000);
    champ.tickTimedChampions();
    EXPECT_EQ(liveChampions(), 0u); // 2600 s still out

    champ.skipTime(2600); // stacked: 3600 total -> due
    EXPECT_EQ(liveChampions(), 1u);
}

TEST_F(ChampFixture, ResetThresholdStateClearsAndUnregisters)
{
    for (int i = 0; i < 3; ++i)
        champ.recordMobKill(7, 5);
    ASSERT_EQ(liveChampions(), 1u);
    const int uid = instances.getAllLivingInstances()[0].uid;

    champ.resetThresholdState();
    EXPECT_EQ(liveChampions(), 0u); // active instance unregistered
    EXPECT_EQ(instances.getMobInstance(uid).uid, 0);

    for (int i = 0; i < 3; ++i) // counter cleared: fires fresh
        champ.recordMobKill(7, 5);
    EXPECT_EQ(liveChampions(), 1u);
}

TEST_F(ChampFixture, SpawnNotifyCallbackFires)
{
    int64_t fakeEpoch = 1700000000;
    champ.setEpochSecFn([&] { return fakeEpoch; });
    std::vector<int> notified;
    champ.setSpawnNotifyCallback([&](const MobDataStruct &m) { notified.push_back(m.uid); });

    TimedChampionTemplate t;
    t.slug = "alpha";
    t.gameZoneId = 7;
    t.mobTemplateId = 5;
    t.intervalHours = 6;
    t.nextSpawnAt = fakeEpoch - 10; // due
    champ.loadTimedChampions({t});
    champ.tickTimedChampions();
    ASSERT_EQ(liveChampions(), 1u);
    ASSERT_EQ(notified.size(), 1u);
    EXPECT_EQ(notified[0], instances.getAllLivingInstances()[0].uid);
}

TEST_F(ChampFixture, ResetRearmsTimedSchedule)
{
    int64_t fakeEpoch = 1700000000;
    champ.setEpochSecFn([&] { return fakeEpoch; });

    TimedChampionTemplate t;
    t.slug = "alpha";
    t.gameZoneId = 7;
    t.mobTemplateId = 5;
    t.intervalHours = 6;
    t.nextSpawnAt = fakeEpoch - 10; // due
    champ.loadTimedChampions({t});
    champ.tickTimedChampions();
    ASSERT_EQ(liveChampions(), 1u);

    champ.resetThresholdState(); // culls instance, re-arms schedule
    EXPECT_EQ(liveChampions(), 0u);

    // Spawn had advanced nextSpawnAt by the interval; skip past it and the
    // re-armed schedule refires (reset+skip composition).
    champ.skipTime(6 * 3600 + 1);
    EXPECT_EQ(liveChampions(), 1u);
}
