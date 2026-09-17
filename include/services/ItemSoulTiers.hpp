#pragma once
// Single source of truth for Item Soul kill-count tiers (Wave 2.4).
//
// The tier table lived in two places with hand-synced literals:
// MobKillRewardPipeline (flush/tier-crossing on kill) and
// CharacterStatsNotificationService (bonus resolution for stats packets).
// StatsPacketBuilder only applies pre-resolved values (no tier logic).
// Live values come from game_config; the kDefault* constants below are the
// fallbacks — a tuning change touches exactly one place.
#include "services/GameConfigService.hpp"

struct ItemSoulTiers
{
    static constexpr int kDefaultTier1Kills = 50;
    static constexpr int kDefaultTier2Kills = 200;
    static constexpr int kDefaultTier3Kills = 500;
    static constexpr int kDefaultTier1BonusFlat = 1;
    static constexpr int kDefaultTier2BonusFlat = 2;
    static constexpr int kDefaultTier3BonusFlat = 3;
    static constexpr int kDefaultDbFlushEveryKills = 5;

    struct Table
    {
        int tier1Kills = kDefaultTier1Kills;
        int tier2Kills = kDefaultTier2Kills;
        int tier3Kills = kDefaultTier3Kills;
        int tier1BonusFlat = kDefaultTier1BonusFlat;
        int tier2BonusFlat = kDefaultTier2BonusFlat;
        int tier3BonusFlat = kDefaultTier3BonusFlat;
        int dbFlushEveryKills = kDefaultDbFlushEveryKills;

        static Table load(GameConfigService &cfg)
        {
            Table t;
            t.tier1Kills = cfg.getInt("item_soul.tier1_kills", kDefaultTier1Kills);
            t.tier2Kills = cfg.getInt("item_soul.tier2_kills", kDefaultTier2Kills);
            t.tier3Kills = cfg.getInt("item_soul.tier3_kills", kDefaultTier3Kills);
            t.tier1BonusFlat = cfg.getInt("item_soul.tier1_bonus_flat", kDefaultTier1BonusFlat);
            t.tier2BonusFlat = cfg.getInt("item_soul.tier2_bonus_flat", kDefaultTier2BonusFlat);
            t.tier3BonusFlat = cfg.getInt("item_soul.tier3_bonus_flat", kDefaultTier3BonusFlat);
            t.dbFlushEveryKills = cfg.getInt("item_soul.db_flush_every_kills", kDefaultDbFlushEveryKills);
            return t;
        }
    };

    // Flat bonus for a kill count (0 below tier 1).
    static int bonusForKills(const Table &t, int killCount)
    {
        if (killCount >= t.tier3Kills)
            return t.tier3BonusFlat;
        if (killCount >= t.tier2Kills)
            return t.tier2BonusFlat;
        if (killCount >= t.tier1Kills)
            return t.tier1BonusFlat;
        return 0;
    }

    // True exactly on a tier boundary (stats re-push + DB flush trigger).
    static bool isTierBoundary(const Table &t, int killCount)
    {
        return killCount == t.tier1Kills || killCount == t.tier2Kills ||
               killCount == t.tier3Kills;
    }

    ItemSoulTiers() = delete;
};
