// Test-only stubs for world-coupled methods used on null-guarded paths.
//
// Why: LootManager/HarvestManager reference a few cross-manager methods that
// are only ever reached when GameServices/ClientManager/NetworkManager are
// set — unit tests never set them, but the linker still needs the symbols.
// Linking the real .cpps would drag sockets, the scheduler and the whole
// world into the unit binary.
//
// This file is compiled ONLY into unit_tests (see tests/CMakeLists.txt),
// never into MMOChunkServer: exactly one definition per binary.
// Stub bodies are unreachable in tests (all call sites are nullptr-guarded).
//
// RULE: a stub may exist here only while the real X.cpp is NOT linked into
// unit_tests. If you add a real X.cpp to tests/CMakeLists.txt, delete its
// stub here (ClientManager/GameZoneManager/InterestManager already link
// the real thing, so they must never reappear here).
#include "network/NetworkManager.hpp"
#include "services/ClientManager.hpp"
#include "services/CombatSystem.hpp"
#include "services/GameZoneManager.hpp"
#include "services/MobInstanceManager.hpp"
#include "services/ZoneEventManager.hpp"

void CombatSystem::processAIAttack(int /*mobId*/, int /*targetPlayerId*/,
    const std::string & /*forcedSkillSlug*/)
{
}

void CombatSystem::broadcastMobSkillInitiation(int /*mobId*/, int /*targetPlayerId*/,
    const SkillStruct & /*skill*/)
{
}

std::string NetworkManager::generateResponseMessage(
    const std::string & /*status*/, const nlohmann::json & /*message*/)
{
    return {};
}

void NetworkManager::sendResponse(
    std::shared_ptr<boost::asio::ip::tcp::socket> /*clientSocket*/, const std::string & /*responseString*/)
{
}

void NetworkManager::sendResponse(
    std::shared_ptr<boost::asio::ip::tcp::socket> /*clientSocket*/,
    std::shared_ptr<const std::string> /*data*/)
{
}
