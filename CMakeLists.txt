cmake_minimum_required(VERSION 3.10)

project(MMOChunkServer)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)

# Define your project's source files
set(SOURCE_FILES
    src/main.cpp
    src/chunk_server/ChunkServer.cpp
    src/chunk_server/CharacterManager.cpp
    src/network/NetworkManager.cpp
    src/data/ClientData.cpp
    src/events/Event.cpp
    src/events/EventQueue.cpp
    src/events/EventHandler.cpp
    src/utils/JSONParser.cpp
    src/utils/Config.cpp
    src/utils/Logger.cpp
    # ... other source files
)

# Define your project's header files
set(HEADER_FILES
    include/chunk_server/ChunkServer.hpp
    include/chunk_server/CharacterManager.hpp
    include/network/NetworkManager.hpp
    include/data/DataStructs.hpp
    include/data/ClientData.hpp
    include/events/Event.hpp
    include/events/EventQueue.hpp
    include/events/EventHandler.hpp
    include/utils/JSONParser.hpp
    include/utils/TerminalColors.hpp
    include/utils/Config.hpp
    include/utils/Logger.hpp

    # ... other header files
)

# Include directories for your project's headers
include_directories(include)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(${PROJECT_NAME} pqxx)
target_link_libraries(${PROJECT_NAME} pq)