// Test-only stub for QuestManager::onItemObtained.
//
// Why: InventoryManager calls questManager_->onItemObtained() (nullptr-guarded
// at runtime), so the linker needs the symbol. Linking the real
// QuestManager.cpp would drag the whole world (CharacterManager,
// NetworkManager, GameServerWorker, ...) into the unit binary.
// This stub is compiled ONLY into unit_tests (see tests/CMakeLists.txt),
// never into MMOChunkServer, so there is no ODR clash: exactly one
// definition per binary. Tests never set a QuestManager, so the stub
// body never runs (the nullptr-guarded path is what gets exercised).
#include "services/QuestManager.hpp"

void QuestManager::onItemObtained(int /*characterId*/, int /*itemId*/, int /*quantity*/)
{
}
