-- =============================================================================
-- Migration 055: Quest Rewards — Hidden Flag
-- =============================================================================
-- Adds is_hidden column to quest_rewards.
-- When TRUE, the client renders the reward as "???" until the quest is
-- turned in, at which point quest_turned_in reveals the full reward.

ALTER TABLE public.quest_reward
    ADD COLUMN IF NOT EXISTS is_hidden BOOLEAN NOT NULL DEFAULT FALSE;

COMMENT ON COLUMN public.quest_reward.is_hidden IS
    'TRUE = client displays "???" instead of item/amount until quest_turned_in. Revealed in the rewardsReceived array of the quest_turned_in notification.';
