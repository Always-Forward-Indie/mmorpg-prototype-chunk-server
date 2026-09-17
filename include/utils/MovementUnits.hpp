#pragma once
// Single source of truth for the move_speed stat → world units/s scale
// (Wave 2.5). Two function-local copies lived in MobMovementManager (mob
// chase speed) and CharacterEventHandler (player movement validation).
//
// PARITY WARNING: the Unreal client applies the same factor
// (BasicPlayer.h: MoveSpeedScale = 40.0f, via ApplyServerMoveSpeed). Server
// movement validation assumes identical conversion — change both together.
class MovementUnits
{
  public:
    static constexpr float kMoveSpeedScale = 40.0f;

    MovementUnits() = delete;
};
