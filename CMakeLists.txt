cmake_minimum_required(VERSION 3.10)

project(MMOChunkServer)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)

# Define your project's source files
set(SOURCE_FILES
    src/main.cpp
    src/chunk_server/ChunkServer.cpp
    src/chunk_server/Authenticator.cpp
    src/chunk_server/ClientData.cpp
    src/chunk_server/GameServerWorker.cpp
    src/chunk_server/CharacterManager.cpp
    src/helpers/Database.cpp
    src/helpers/Config.cpp
    # ... other source files
)

# Define your project's header files
set(HEADER_FILES
    include/chunk_server/ChunkServer.hpp
    include/chunk_server/Authenticator.hpp
    include/chunk_server/ClientData.hpp
    include/chunk_server/GameServerWorker.hpp
    include/chunk_server/CharacterManager.hpp
    include/helpers/Database.hpp
    include/helpers/Config.hpp

    # ... other header files
)

# Include directories for your project's headers
include_directories(include)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})
target_link_libraries(${PROJECT_NAME} pqxx)
target_link_libraries(${PROJECT_NAME} pq)