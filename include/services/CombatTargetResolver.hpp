#pragma once
// Pure initiation range-resolution for combat (A4 extract).
//
// CombatSystem::initiateSkillUsage resolved caster/target positions inline
// (character vs mob casters; PLAYER/SELF direct lookup; MOB prefers the
// movement layer's lastSentPosition with instance fallback; AREA/NONE skip
// range entirely) and computed the XY distance for the initiation validator.
// That block now delegates here 1-1. Manager access enters as three tiny
// lookup lambdas, so the rules are unit-testable without GameServices.
#include "data/CombatStructs.hpp"
#include "data/DataStructs.hpp"

#include <cmath>
#include <functional>
#include <optional>

class CombatTargetResolver
{
  public:
    /// Position lookup: committed position or nullopt when unknown.
    using PosLookup = std::function<std::optional<PositionStruct>(int id)>;

    struct Result
    {
        bool checkRange = false; ///< false = skip range validation (AREA/NONE/unresolved)
        float distance = 0.0f;   ///< XY-plane distance, valid only when checkRange
    };

    /// Resolve the initiation range check inputs.
    ///
    /// @param casterId  attacker character id or mob uid
    /// @param targetId  target character id or mob uid (0 for AREA/NONE)
    /// @param targetType requested target kind
    /// @param charPos   character committed position (nullopt when unknown)
    /// @param mobSentPos mob last-sent movement position (nullopt when zero/unset)
    /// @param mobPos    mob instance position (nullopt when unknown)
    static Result resolveRange(int casterId,
        int targetId,
        CombatTargetType targetType,
        const PosLookup &charPos,
        const PosLookup &mobSentPos,
        const PosLookup &mobPos)
    {
        Result out;
        if (targetType == CombatTargetType::AREA || targetType == CombatTargetType::NONE)
            return out;

        // Caster position: players first, mobs second (same order as before).
        std::optional<PositionStruct> caster = charPos(casterId);
        if (!caster.has_value())
            caster = mobPos(casterId);
        if (!caster.has_value())
            return out;

        // Target position by kind.
        std::optional<PositionStruct> target;
        if (targetType == CombatTargetType::PLAYER || targetType == CombatTargetType::SELF)
        {
            target = charPos(targetId);
        }
        else if (targetType == CombatTargetType::MOB)
        {
            // Prefer the movement layer's last-sent position (what the
            // client actually renders); fall back to the instance position.
            target = mobSentPos(targetId);
            if (!target.has_value())
                target = mobPos(targetId);
        }
        else
        {
            return out;
        }
        if (!target.has_value())
            return out;

        const float dx = caster->positionX - target->positionX;
        const float dy = caster->positionY - target->positionY;
        out.checkRange = true;
        out.distance = std::sqrt(dx * dx + dy * dy);
        return out;
    }

    CombatTargetResolver() = delete;
};
