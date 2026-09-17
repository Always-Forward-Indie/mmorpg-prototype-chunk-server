#pragma once
// Pure server-authoritative movement validation math (Wave 3.5).
//
// handleMoveCharacterEvent validated speed inline: resolve the effective
// move_speed (attributes + live non-tick effects, 7.0 default), the max
// distance for the frame (MIN_DELTA clamp + buffer multiplier), and a
// sliding-window fallback for VPN bursts. All three live here, covered by
// unit tests; the handler keeps manager lookups, logging, correction
// responses and validation-state writes. Behavior 1-1, including the
// XY-plane-only rule (Z is altitude/terrain height, unbounded by move_speed).
#include "data/DataStructs.hpp"
#include "utils/DistanceUtils.hpp"
#include "utils/MovementUnits.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

struct MovementValidation
{
    // Default move_speed stat when the character has no such attribute.
    static constexpr float kDefaultMoveSpeed = 7.0f;
    // Minimum plausible frame interval: packets processed in one server batch
    // would otherwise get deltaMs≈0 and every follow-up packet rejected.
    static constexpr float kMinDeltaMs = 16.0f;

    struct ResolvedSpeed
    {
        float speed = kDefaultMoveSpeed;
        bool fromAttributes = false;
    };

    // Effective move_speed stat: attribute value (first match wins) plus
    // additive live non-tick effects (tickMs == 0, unexpired).
    static ResolvedSpeed resolveMoveSpeed(
        const std::vector<CharacterAttributeStruct> &attributes,
        const std::vector<ActiveEffectStruct> &effects,
        int64_t nowSec)
    {
        ResolvedSpeed out;
        for (const auto &attr : attributes)
        {
            if (attr.slug == "move_speed")
            {
                out.speed = static_cast<float>(attr.value);
                out.fromAttributes = true;
                break;
            }
        }
        for (const auto &eff : effects)
        {
            if (eff.attributeSlug == "move_speed" && eff.tickMs == 0 &&
                (eff.expiresAt == 0 || eff.expiresAt > nowSec))
            {
                out.speed += eff.value;
            }
        }
        return out;
    }

    // Max legal planar travel for one frame, in world units.
    static float maxDistanceFor(float moveSpeedUnits, float deltaMs, float speedBuffer)
    {
        const float effectiveDelta = (deltaMs < kMinDeltaMs) ? kMinDeltaMs : deltaMs;
        return moveSpeedUnits * (effectiveDelta / 1000.0f) * speedBuffer;
    }

    struct WindowVerdict
    {
        bool passed = false;
        float dist = 0.0f;
        float maxDist = 0.0f;
        float deltaMs = 0.0f;
        int samples = 0;
    };

    // Sliding-window fallback: average speed from the oldest buffered sample
    // to the new position. 1-1 with the inline version (count < 2 or
    // non-positive window delta => fail).
    static WindowVerdict checkWindow(const MovementSample *window,
        int count,
        int head,
        int capacity,
        const PositionStruct &newPosition,
        int64_t nowMs,
        float moveSpeedUnits,
        float speedBuffer)
    {
        WindowVerdict verdict;
        verdict.samples = count;
        if (count < 2)
            return verdict;
        const int oldestIdx = (count < capacity) ? 0 : head;
        const MovementSample &oldest = window[oldestIdx];
        const float windowDeltaMs = static_cast<float>(nowMs - oldest.srvMs);
        verdict.deltaMs = windowDeltaMs;
        if (windowDeltaMs <= 0.0f)
            return verdict;
        verdict.dist = DistanceUtils::dist2D(newPosition, oldest.position);
        verdict.maxDist = moveSpeedUnits * (windowDeltaMs / 1000.0f) * speedBuffer;
        verdict.passed = (verdict.dist <= verdict.maxDist);
        return verdict;
    }

    MovementValidation() = delete;
};
