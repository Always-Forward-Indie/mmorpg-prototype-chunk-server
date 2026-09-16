// Unit tests for GameConfigService + StatusEffectTemplateManager (Logger only).
#include "services/GameConfigService.hpp"
#include "services/StatusEffectTemplateManager.hpp"

#include <gtest/gtest.h>
#include <unordered_map>

TEST(GameConfigService, DefaultsWhenEmpty)
{
    Logger logger{"test"};
    GameConfigService cfg(logger);
    EXPECT_FALSE(cfg.isLoaded());
    EXPECT_FLOAT_EQ(cfg.getFloat("combat.defense_formula_k", 7.5f), 7.5f);
    EXPECT_EQ(cfg.getInt("aggro.base_radius", 500), 500);
    EXPECT_EQ(cfg.getBool("feature.x", true), true);
    EXPECT_EQ(cfg.getString("name", "def"), "def");
}

TEST(GameConfigService, TypedGettersAndReload)
{
    Logger logger{"test"};
    GameConfigService cfg(logger);
    cfg.setConfig({{"combat.defense_formula_k", "9.5"}, {"aggro.base_radius", "700"},
        {"feature.x", "true"}, {"name", "prod"}, {"broken_int", "abc"}});
    EXPECT_TRUE(cfg.isLoaded());
    EXPECT_FLOAT_EQ(cfg.getFloat("combat.defense_formula_k", 0.0f), 9.5f);
    EXPECT_EQ(cfg.getInt("aggro.base_radius", 0), 700);
    EXPECT_TRUE(cfg.getBool("feature.x", false));
    EXPECT_EQ(cfg.getString("name", ""), "prod");
    // Bad conversions fall back to defaults, never throw.
    EXPECT_EQ(cfg.getInt("broken_int", 42), 42);
    EXPECT_FLOAT_EQ(cfg.getFloat("broken_int", 1.5f), 1.5f);
    EXPECT_EQ(cfg.getInt("missing", 7), 7);
    // Reload replaces the whole map.
    cfg.setConfig({{"aggro.base_radius", "800"}});
    EXPECT_EQ(cfg.getInt("aggro.base_radius", 0), 800);
    EXPECT_FLOAT_EQ(cfg.getFloat("combat.defense_formula_k", 7.5f), 7.5f);
}

TEST(StatusEffectTemplates, LoadLookupReload)
{
    Logger logger{"test"};
    StatusEffectTemplateManager mgr(logger);
    EXPECT_FALSE(mgr.isLoaded());
    EXPECT_EQ(mgr.getTemplate("sick"), nullptr);

    StatusEffectTemplate a;
    a.slug = "sick";
    a.category = "debuff";
    a.durationSec = 60;
    StatusEffectTemplate b;
    b.slug = "regen";
    b.category = "hot";
    mgr.loadTemplates({a, b});
    EXPECT_TRUE(mgr.isLoaded());
    const StatusEffectTemplate *t = mgr.getTemplate("sick");
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(t->category, "debuff");
    EXPECT_EQ(t->durationSec, 60);
    EXPECT_EQ(mgr.getTemplate("nope"), nullptr);

    // Reload replaces the map.
    mgr.loadTemplates({b});
    EXPECT_EQ(mgr.getTemplate("sick"), nullptr);
    EXPECT_NE(mgr.getTemplate("regen"), nullptr);
}
