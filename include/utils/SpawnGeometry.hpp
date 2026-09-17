#pragma once
// Single source of truth for zone-shape point sampling (A1 extract).
//
// The same RECT / CIRCLE / ANNULUS math lived in three places with
// hand-synced formulas: SpawnZoneManager::spawnMobsInZone (incl. the
// stratified-sector variant), RespawnZoneManager::getRandomPointInZone and
// ChampionManager::resolveChampionSpawnPoint. All three now delegate here;
// sampling math is pinned once by tests/test_spawn_geometry.cpp.
//
// Rules:
// - RNG only via RandomUtils (thread_local engine, function-local
//   distributions). Never add a static/shared engine or distribution.
// - Uniformity contracts: RECT uniform over the box; CIRCLE uniform over
//   the disc (r = R*sqrt(u)); ANNULUS equal-area (r = sqrt(in^2+u*(out^2-in^2))).
// - Degenerate inputs (zero extents/radii) collapse to the anchor point
//   instead of dividing by zero: RECT min==max is a legal fixed point,
//   CIRCLE R=0 returns the centre, ANNULUS out<=in returns the centre
//   (callers treat that as "no area" via isAreaDefined()).
#include "utils/RandomUtils.hpp"

#include <cmath>
#include <utility>

class SpawnGeometry
{
  public:
    // Uniform point in [minX,maxX] x [minY,maxY]. Degenerate (min==max)
    // axes are fixed points, never NaN.
    static std::pair<float, float> sampleRect(float minX, float maxX, float minY, float maxY)
    {
        const float x = (maxX > minX) ? minX + RandomUtils::uniform01() * (maxX - minX) : minX;
        const float y = (maxY > minY) ? minY + RandomUtils::uniform01() * (maxY - minY) : minY;
        return {x, y};
    }

    // Uniform point over the disc. R<=0 returns the centre.
    static std::pair<float, float> sampleCircle(float centerX, float centerY, float outerRadius)
    {
        if (outerRadius <= 0.0f)
            return {centerX, centerY};
        const float angle = RandomUtils::angle();
        const float r = outerRadius * std::sqrt(RandomUtils::uniform01());
        return {centerX + r * std::cos(angle), centerY + r * std::sin(angle)};
    }

    // Equal-area point in the ring. out<=in (or out<=0) returns the centre.
    static std::pair<float, float> sampleAnnulus(
        float centerX, float centerY, float innerRadius, float outerRadius)
    {
        if (outerRadius <= 0.0f || outerRadius <= innerRadius)
            return {centerX, centerY};
        const float angle = RandomUtils::angle();
        const float r2in = innerRadius * innerRadius;
        const float r2out = outerRadius * outerRadius;
        const float r = std::sqrt(r2in + RandomUtils::uniform01() * (r2out - r2in));
        return {centerX + r * std::cos(angle), centerY + r * std::sin(angle)};
    }

    // Stratified variant: uniform point within the slotIdx-th of totalSlots
    // equal angular sectors (totalSlots>=1). Spreads mobs evenly around
    // narrow rings instead of clumping. Falls back to sampleAnnulus for
    // degenerate slot counts.
    static std::pair<float, float> sampleAnnulusSector(
        float centerX, float centerY, float innerRadius, float outerRadius, int slotIdx, int totalSlots)
    {
        if (totalSlots < 1)
            return sampleAnnulus(centerX, centerY, innerRadius, outerRadius);
        constexpr float kTwoPi = 2.0f * 3.14159265358979323846f;
        const float sectorSize = kTwoPi / static_cast<float>(totalSlots);
        const float angle = static_cast<float>(slotIdx) * sectorSize + RandomUtils::range(0.0f, sectorSize);
        const float r2in = innerRadius * innerRadius;
        const float r2out = (outerRadius > innerRadius) ? outerRadius * outerRadius : 0.0f;
        const float r = std::sqrt(r2in + RandomUtils::uniform01() * (r2out - r2in));
        return {centerX + r * std::cos(angle), centerY + r * std::sin(angle)};
    }

    SpawnGeometry() = delete;
};
