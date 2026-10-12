#pragma once
// Crowd-control math: diminishing returns on repeated CC (pure, header-only).
//
// Rule (approved defaults, game_config cc.*): within cc.immune_sec of the
// last same-type application, the Nth CC lands at steps[N-1]:
//   1st x1.0, 2nd x0.5, 3rd x0.25, 4th+ immune (rejected).
// Outside the window the chain resets. Durations in seconds; history is a
// plain timestamp list the caller owns (CharacterManager / MobEffectStore).
#include <cstdint>
#include <vector>

class CrowdControl
{
  public:
    // Returns effective duration, or -1.0 when immune. history holds
    // application times (same clock as nowSec); prunes in place.
    static float applyWithDR(std::vector<int64_t> &history, int64_t nowSec,
        float baseDurationSec, float immuneWindowSec = 15.0f)
    {
        static constexpr float kSteps[] = {1.0f, 0.5f, 0.25f};
        int recent = 0;
        for (auto it = history.begin(); it != history.end();)
        {
            if (nowSec - *it > static_cast<int64_t>(immuneWindowSec))
                it = history.erase(it);
            else
            {
                ++recent;
                ++it;
            }
        }
        if (static_cast<size_t>(recent) >= sizeof(kSteps) / sizeof(kSteps[0]))
            return -1.0f; // immune
        history.push_back(nowSec);
        return baseDurationSec * kSteps[recent];
    }

    CrowdControl() = delete;
};
