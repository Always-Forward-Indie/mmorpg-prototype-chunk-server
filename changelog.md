v0.0.3
07.03.2026
================
New:
Per-subsystem logging via spdlog — each component (combat, mob, skill, character, item, harvest, dialogue, npc, chunk, client, zone, experience, spawn, network, events, gameloop, config) now logs to its own named channel. Each subsystem level can be set independently via environment variables (LOG_LEVEL_<NAME>).
Added JSONParser utility for convenient JSON data handling.
Added TimestampUtils utility.

Improvements:
MobMovementManager — deep refactor of movement and AI logic.
CombatSystem — major refactor of combat calculations and flow.
CombatCalculator — extended and improved.
CharacterManager — extended, improved character data handling.
SpawnZoneManager — reworked.
EventHandler / EventDispatcher — significantly extended.
GameServerWorker — refactored.
Scheduler — refactored.
Dockerfile — minor update.
docker-compose: LOG_LEVEL=info by default; network, gameloop, events set to warn to reduce log noise.

Removed:
Removed outdated .md documents (AGGRO_SYSTEM_GUIDE, ATTACK_SYSTEM_GUIDE, COMBAT_SKILL_PACKETS, etc.).
Removed test script test_request_id.py.

---

v0.0.2
28.02.2026
================
New:
Add changelog file to track changes and updates in the project.
Save current experience and level to game server DB immediately upon grant.
Save current player position to game server DB periodically and upon player logout.

Bug Fixes:
Fixed incorrect experience thresholds for current and next levels in character data responses.