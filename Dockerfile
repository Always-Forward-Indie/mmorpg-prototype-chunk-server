# Stage 1: Build
FROM ubuntu:22.04 AS build

# Install required dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libpqxx-dev \
    pkg-config \
    gdb

# Set working directory
WORKDIR /usr/src/app

# Copy source code
COPY . .

# Create build directory
RUN mkdir -p build
WORKDIR /usr/src/app/build

# Run CMake and compile
RUN cmake .. && make

# Debugging: Ensure the binary exists
RUN ls -lh /usr/src/app/build/MMOChunkServer

# ✅ Copy pqxx headers to a temporary folder in the build stage
RUN mkdir -p /usr/src/app/docker_includes && cp -r /usr/include/pqxx /usr/src/app/docker_includes/

# Debugging: Ensure pqxx headers exist in build stage
RUN ls -lh /usr/src/app/docker_includes/pqxx



# Stage 2: Runtime (smaller final image)
FROM ubuntu:22.04

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libpqxx-dev \
    libpq5 \
    libtsan0 \
    libstdc++6

# Set working directory
WORKDIR /usr/src/app

# ✅ Ensure /usr/include/pqxx exists before copying
RUN mkdir -p /usr/include/pqxx

# Copy only the compiled executable from the build stage
COPY --from=build /usr/src/app/build/MMOChunkServer /usr/src/app/MMOChunkServer

# ✅ Fix: Copy pqxx headers correctly by making sure the target directory exists
COPY --from=build /usr/src/app/docker_includes/pqxx /usr/include/pqxx

#Copy config file
COPY --from=build /usr/src/app/config.json /usr/src/app/config.json

# Ensure the binary is executable
RUN chmod +x /usr/src/app/MMOChunkServer

# Expose the application port
EXPOSE 27017

# Run the application
CMD ["/usr/src/app/MMOChunkServer"]
