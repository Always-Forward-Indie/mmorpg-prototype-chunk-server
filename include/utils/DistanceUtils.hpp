#pragma once
// Single source of truth for Euclidean distances (Wave 2.1).
//
// Five identical 2D copies lived in MobMovementManager / LootManager /
// CharacterManager / MobAIController / HarvestManager plus one 3D copy in
// the deleted AttackSystem class. Identical today, but any tuning (e.g.
// 2D-vs-3D, fixed-point) would previously have to touch six places and
// could diverge silently. All copies now delegate here; behavior is 1-1.
//
// NOTE: the deleted AttackSystem's call sites intentionally used dist3D
// (their member was 3D). Whether hit-chance should be 3D is game design,
// not this refactor; do not "fix" it here.
#include "data/DataStructs.hpp"

#include <cmath>

class DistanceUtils
{
  public:
    // Planar distance (X/Y). The standard for movement, aggro, loot, harvest.
    static float dist2D(const PositionStruct &a, const PositionStruct &b)
    {
        const float dx = a.positionX - b.positionX;
        const float dy = a.positionY - b.positionY;
        return std::sqrt(dx * dx + dy * dy);
    }

    // Full 3D distance. Only for call sites that historically used it
    // (deleted-AttackSystem hit-chance / line-of-sight semantics).
    static float dist3D(const PositionStruct &a, const PositionStruct &b)
    {
        const float dx = a.positionX - b.positionX;
        const float dy = a.positionY - b.positionY;
        const float dz = a.positionZ - b.positionZ;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // Range checks without sqrt (squared comparison).
    // NOTE: dialogue range is 2D while vendor range is 3D. Same name, different
    // dimensionality — a real semantic difference, NOT a duplication to merge.
    // Do not unify without a product decision (floors/hills change behavior).
    // Boundary caveat: squared comparison can differ from sqrt-then-compare by
    // 1 ulp exactly at the boundary; interaction ranges are ~hundreds of units,
    // so this is unobservable in practice (and the vendor form was already
    // squared — that one is bit-identical).
    static bool withinRange2D(const PositionStruct &a, const PositionStruct &b, float radius)
    {
        const float dx = a.positionX - b.positionX;
        const float dy = a.positionY - b.positionY;
        return (dx * dx + dy * dy) <= (radius * radius);
    }

    // Range check without sqrt (squared comparison — bit-identical to the
    // hand-rolled form it replaces, e.g. VendorEventHandler::isPlayerInRange).
    static bool withinRange3D(const PositionStruct &a, const PositionStruct &b, float radius)
    {
        const float dx = a.positionX - b.positionX;
        const float dy = a.positionY - b.positionY;
        const float dz = a.positionZ - b.positionZ;
        return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
    }

    DistanceUtils() = delete;
};
