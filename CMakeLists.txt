cmake_minimum_required(VERSION 3.10)

project(MMOChunkServer)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set compiler flags for debug mode (remove when compiling for production)
set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g")
# set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined -g")
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer -g")
set(CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fsanitize=address")

# Define your project's source files
set(SOURCE_FILES
    src/main.cpp
    src/chunk_server/ChunkServer.cpp
    src/services/ChunkManager.cpp
    src/services/CharacterManager.cpp
    src/services/SpawnZoneManager.cpp
    src/services/SpawnManager.cpp
    src/services/MobManager.cpp
    src/services/MobInstanceManager.cpp
    src/services/MobMovementManager.cpp
    src/services/CombatCalculator.cpp
    src/services/SkillManager.cpp
    src/services/SkillSystem.cpp
    src/services/CombatSystem.cpp
    src/services/CombatResponseBuilder.cpp
    src/services/ItemManager.cpp
    src/services/LootManager.cpp
    src/services/InventoryManager.cpp
    src/services/HarvestManager.cpp
    src/services/ClientManager.cpp
    src/network/GameServerWorker.cpp
    src/network/NetworkManager.cpp
    src/network/ClientSession.cpp
    src/events/Event.cpp
    src/events/EventQueue.cpp
    src/events/EventHandler.cpp
    src/events/EventDispatcher.cpp
    src/events/handlers/BaseEventHandler.cpp
    src/events/handlers/ClientEventHandler.cpp
    src/events/handlers/CharacterEventHandler.cpp
    src/events/handlers/MobEventHandler.cpp
    src/events/handlers/ZoneEventHandler.cpp
    src/events/handlers/ChunkEventHandler.cpp
    src/events/handlers/CombatEventHandler.cpp
    src/events/handlers/ItemEventHandler.cpp
    src/events/handlers/HarvestEventHandler.cpp
    src/data/AttackSystem.cpp
    src/utils/Scheduler.cpp
    src/utils/ThreadPool.cpp
    src/utils/JSONParser.cpp
    src/utils/TimeConverter.cpp
    src/utils/Generators.cpp
    src/utils/Logger.cpp
    src/utils/Config.cpp
    src/utils/TimeUtils.cpp
    src/utils/TimestampUtils.cpp
    src/handlers/MessageHandler.cpp
    # ... other source files
)

# Define your project's header files
set(HEADER_FILES

    include/chunk_server/ChunkServer.hpp
    include/services/ChunkManager.hpp
    include/services/CharacterManager.hpp
    include/services/SpawnZoneManager.hpp
    include/services/SpawnManager.hpp
    include/services/MobManager.hpp
    include/services/MobInstanceManager.hpp
    include/services/MobMovementManager.hpp
    include/services/CombatCalculator.hpp
    include/services/SkillManager.hpp
    include/services/SkillSystem.hpp
    include/services/CombatSystem.hpp
    include/services/CombatResponseBuilder.hpp
    include/services/ItemManager.hpp
    include/services/LootManager.hpp
    include/services/InventoryManager.hpp
    include/services/HarvestManager.hpp
    include/services/ClientManager.hpp
    include/services/GameServices.hpp
    include/network/GameServerWorker.hpp
    include/network/NetworkManager.hpp
    include/network/ClientSession.hpp
    include/events/Event.hpp
    include/events/EventQueue.hpp
    include/events/EventHandler.hpp
    include/events/EventDispatcher.hpp
    include/events/EventData.hpp
    include/events/handlers/BaseEventHandler.hpp
    include/events/handlers/ClientEventHandler.hpp
    include/events/handlers/CharacterEventHandler.hpp
    include/events/handlers/MobEventHandler.hpp
    include/events/handlers/ZoneEventHandler.hpp
    include/events/handlers/ChunkEventHandler.hpp
    include/events/handlers/CombatEventHandler.hpp
    include/events/handlers/ItemEventHandler.hpp
    include/events/handlers/HarvestEventHandler.hpp
    include/data/DataStructs.hpp
    include/data/SkillStructs.hpp
    include/data/SpecialStructs.hpp
    include/data/CombatStructs.hpp
    include/data/AttackSystem.hpp
    include/utils/Scheduler.hpp
    include/utils/ThreadPool.hpp
    include/utils/JSONParser.hpp
    include/utils/ResponseBuilder.hpp
    include/utils/TimeConverter.hpp
    include/utils/Generators.hpp
    include/utils/Logger.hpp
    include/utils/TerminalColors.hpp
    include/utils/Config.hpp
    include/utils/TimeUtils.hpp
    include/utils/TimestampUtils.hpp
    include/handlers/MessageHandler.hpp
    # ... other header files
)

# Include directories for your project's headers
include_directories(include)

# Ensure libpqxx is correctly detected
find_package(PkgConfig REQUIRED)
pkg_check_modules(libpqxx REQUIRED IMPORTED_TARGET libpqxx)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})

# Link against libpqxx and PostgreSQL
target_link_libraries(${PROJECT_NAME} PkgConfig::libpqxx pq)