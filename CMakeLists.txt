cmake_minimum_required(VERSION 3.10)

project(MMOTestClientPrototype)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)

# Define your project's source files
set(SOURCE_FILES
    src/main.cpp
    # ... other source files
)

# Define your project's header files
set(HEADER_FILES

    # ... other header files
)

# Include directories for your project's headers
include_directories(include)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCE_FILES} ${HEADER_FILES})