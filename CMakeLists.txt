cmake_minimum_required(VERSION 3.10)

project(MMOChunkServer)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set compiler flags for debug mode (remove when compiling for production)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")

# Define your project's source files
set(SOURCE_FILES
    src/main.cpp
    src/chunk_server/ChunkServer.cpp
    src/services/CharacterManager.cpp
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
    include/services/CharacterManager.hpp
    include/network/NetworkManager.hpp
    include/data/DataStructs.hpp
    include/data/ClientData.hpp
    include/events/Event.hpp
    include/events/EventQueue.hpp
    include/events/EventHandler.hpp
    include/utils/JSONParser.hpp
    include/utils/ResponseBuilder.hpp
    include/utils/TerminalColors.hpp
    include/utils/Config.hpp
    include/utils/Logger.hpp

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