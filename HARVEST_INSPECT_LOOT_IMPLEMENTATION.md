# Как добавить функцию просмотра лута трупа

## Текущая ситуация

В системе харвеста **НЕТ** отдельного события для просмотра лута трупа без подбора. Игрок может:
- ✅ Запросить список ближайших трупов (`getNearbyCorpses`)
- ✅ Начать харвестинг (`harvestStart`) 
- ✅ После завершения харвестинга получить список лута
- ✅ Подобрать конкретные предметы (`corpseLootPickup`)

Но **НЕЛЬЗЯ** просто посмотреть, какой лут есть в трупе, не подбирая его.

## Пакет для просмотра лута (НЕ РЕАЛИЗОВАНО)

Если вы хотите добавить эту функциональность, клиент должен будет отправлять:

```json
{
  "header": {
    "message": "inspect corpse loot",
    "hash": "user_session_hash_123",
    "clientId": "12345",
    "eventType": "inspectCorpseLoot"
  },
  "body": {
    "corpseUID": 1001
  }
}
```

## Как реализовать на сервере

### 1. Добавить новое событие

**Файл:** `include/events/Event.hpp`

```cpp
// В enum EventType добавить:
INSPECT_CORPSE_LOOT,     // Client requests to view corpse loot without pickup
```

### 2. Добавить обработчик в диспетчер

**Файл:** `src/events/EventDispatcher.cpp`

В метод `dispatch()` добавить:

```cpp
else if (context.eventType == "inspectCorpseLoot")
{
    handleInspectCorpseLoot(context, socket);
}
```

И добавить новый метод:

```cpp
void EventDispatcher::handleInspectCorpseLoot(const EventContext &context, 
                                              std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    // Check if character is valid
    if (context.characterData.characterId > 0)
    {
        gameServices_.getLogger().log("EventDispatcher handleInspectCorpseLoot - Character ID: " + 
                                      std::to_string(context.characterData.characterId), GREEN);

        try
        {
            // Parse corpse UID from message
            auto messageJson = nlohmann::json::parse(context.fullMessage);

            if (!messageJson.contains("body") || !messageJson["body"].contains("corpseUID"))
            {
                gameServices_.getLogger().logError("Missing corpseUID in inspect corpse loot request", RED);
                return;
            }

            int corpseUID = messageJson["body"]["corpseUID"];

            // Create inspect request
            nlohmann::json inspectData;
            inspectData["corpseUID"] = corpseUID;
            inspectData["characterId"] = context.characterData.characterId;

            // Create inspect corpse loot event
            Event inspectEvent(Event::INSPECT_CORPSE_LOOT, context.clientData.clientId, inspectData);
            eventsBatch_.push_back(inspectEvent);
        }
        catch (const std::exception &e)
        {
            gameServices_.getLogger().logError("Error creating inspect corpse loot event: " + 
                                              std::string(e.what()), RED);
        }
    }
}
```

### 3. Добавить обработчик в EventHandler

**Файл:** `src/events/EventHandler.cpp`

В метод `dispatchEvent()` добавить:

```cpp
case Event::INSPECT_CORPSE_LOOT:
    harvestEventHandler_->handleInspectCorpseLoot(event);
    break;
```

### 4. Добавить метод в HarvestEventHandler

**Файл:** `include/events/handlers/HarvestEventHandler.hpp`

```cpp
/**
 * @brief Handle inspect corpse loot request
 *
 * Responds with list of available loot in corpse without picking it up
 *
 * @param event Event containing corpse UID
 */
void handleInspectCorpseLoot(const Event &event);
```

**Файл:** `src/events/handlers/HarvestEventHandler.cpp`

```cpp
void HarvestEventHandler::handleInspectCorpseLoot(const Event &event)
{
    gameServices_.getLogger().log("Handling inspect corpse loot request");

    try
    {
        int clientId = event.getClientID();
        const auto &data = event.getData();

        if (std::holds_alternative<nlohmann::json>(data))
        {
            nlohmann::json inspectData = std::get<nlohmann::json>(data);
            int corpseUID = inspectData["corpseUID"];

            // Get player for validation
            auto player = gameServices_.getCharacterManager().getCharacterById(clientId);
            if (player.characterId == 0)
            {
                gameServices_.getLogger().logError("Player not found for corpse loot inspection: " + 
                                                  std::to_string(clientId));
                return;
            }

            auto &harvestManager = gameServices_.getHarvestManager();
            auto &itemManager = gameServices_.getItemManager();

            // Check if corpse exists
            auto corpse = harvestManager.getCorpseByUID(corpseUID);
            if (corpse.mobUID == 0)
            {
                // Send "corpse not found" response
                auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
                if (clientSocket)
                {
                    auto clientData = gameServices_.getClientManager().getClientData(clientId);

                    nlohmann::json response = ResponseBuilder()
                                                  .setHeader("message", "Corpse not found")
                                                  .setHeader("hash", clientData.hash)
                                                  .setHeader("clientId", std::to_string(clientId))
                                                  .setHeader("eventType", "corpseLootInspection")
                                                  .setBody("type", "CORPSE_LOOT_INSPECTION")
                                                  .setBody("corpseUID", corpseUID)
                                                  .setBody("hasLoot", false)
                                                  .setBody("availableLoot", nlohmann::json::array())
                                                  .setBody("totalItems", 0)
                                                  .setBody("message", "Corpse not found")
                                                  .build();

                    std::string responseData = networkManager_.generateResponseMessage("error", response);
                    networkManager_.sendResponse(clientSocket, responseData);
                }
                return;
            }

            // Get available loot for this corpse
            auto availableLoot = harvestManager.getCorpseLoot(corpseUID);

            // Build response
            auto clientSocket = gameServices_.getClientManager().getClientSocket(clientId);
            if (clientSocket)
            {
                auto clientData = gameServices_.getClientManager().getClientData(clientId);

                nlohmann::json itemsArray = nlohmann::json::array();

                for (const auto &[itemId, quantity] : availableLoot)
                {
                    ItemDataStruct itemInfo = itemManager.getItemById(itemId);
                    nlohmann::json itemData;
                    itemData["itemId"] = itemId;
                    itemData["itemSlug"] = itemInfo.slug;
                    itemData["quantity"] = quantity;
                    itemData["name"] = itemInfo.name;
                    itemData["description"] = itemInfo.description;
                    itemData["rarityId"] = itemInfo.rarityId;
                    itemData["rarityName"] = itemInfo.rarityName;
                    itemData["itemType"] = itemInfo.itemTypeName;
                    itemData["weight"] = itemInfo.weight;
                    itemData["isHarvestItem"] = itemInfo.isHarvest;
                    itemsArray.push_back(itemData);
                }

                bool hasLoot = !availableLoot.empty();
                std::string message = hasLoot ? "Corpse loot information" : "No loot available";

                nlohmann::json response = ResponseBuilder()
                                              .setHeader("message", message)
                                              .setHeader("hash", clientData.hash)
                                              .setHeader("clientId", std::to_string(clientId))
                                              .setHeader("eventType", "corpseLootInspection")
                                              .setBody("type", "CORPSE_LOOT_INSPECTION")
                                              .setBody("corpseUID", corpseUID)
                                              .setBody("hasLoot", hasLoot)
                                              .setBody("availableLoot", itemsArray)
                                              .setBody("totalItems", availableLoot.size())
                                              .build();

                if (!hasLoot)
                {
                    response["data"]["body"]["message"] = "This corpse has no loot available";
                }

                std::string responseData = networkManager_.generateResponseMessage("success", response);
                networkManager_.sendResponse(clientSocket, responseData);

                gameServices_.getLogger().log("Sent corpse loot inspection to player " + 
                                              std::to_string(clientId) + " for corpse " + 
                                              std::to_string(corpseUID) + " (" + 
                                              std::to_string(availableLoot.size()) + " items)");
            }
        }
    }
    catch (const std::exception &e)
    {
        gameServices_.getLogger().logError("Exception in handleInspectCorpseLoot: " + std::string(e.what()));
    }
}
```

## Альтернативное решение (без изменения сервера)

Если вы не хотите изменять серверный код, можно использовать существующую функциональность:

1. **Запросить подбор с пустым массивом предметов:**
   ```json
   {
     "header": {
       "eventType": "corpseLootPickup"
     },
     "body": {
       "corpseUID": 1001,
       "playerId": 12345,
       "requestedItems": []
     }
   }
   ```

2. **Сервер ответит ошибкой, но в логах будет информация о доступном луте.**

## Готовая инфраструктура

✅ **Метод `getCorpseLoot()` уже существует** - он возвращает все доступные предметы в трупе  
✅ **Система EventDispatcher готова** - легко добавить новый тип события  
✅ **HarvestEventHandler готов** - можно добавить новый метод  
✅ **ResponseBuilder готов** - для формирования ответов клиенту  

**Заключение:** Функциональность просмотра лута трупа легко добавить, основная инфраструктура уже готова!
