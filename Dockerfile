# Single Container: Build & Run
FROM ubuntu:22.04

# Install required dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    make \
    libpqxx-dev \
    pkg-config \
    gdb \
    ccache \
    curl \
    ca-certificates \
    cargo \
    && rm -rf /var/lib/apt/lists/*

# ✅ Install Rust (required for watchexec)
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="/root/.cargo/bin:$PATH"

# ✅ Ensure Rust is up-to-date
RUN rustup update stable && rustup default stable

# ✅ Install `watchexec` with `--locked`
RUN cargo install watchexec-cli --locked

# Set working directory
WORKDIR /usr/src/app

# ✅ Copy necessary project files
COPY CMakeLists.txt ./  
COPY src ./src  
COPY include ./include  
COPY config.json ./config.json  

# Create build directory
RUN mkdir -p build

# ✅ Set working directory for build
WORKDIR /usr/src/app/build

# ✅ Build the project
RUN cmake .. && make -j$(nproc)

# ✅ Debug: Ensure the binary exists before running
RUN ls -lh /usr/src/app/build/MMOChunkServer || (echo "❌ ERROR: Binary not built!" && exit 1)

# ✅ Copy watchexec binary to system path
RUN cp /root/.cargo/bin/watchexec /usr/local/bin/watchexec && chmod +x /usr/local/bin/watchexec

# ✅ Strip unnecessary debug symbols to reduce size
RUN strip /usr/src/app/build/MMOChunkServer || true

# ✅ Ensure the binary is executable
RUN chmod +x /usr/src/app/build/MMOChunkServer

# ✅ Set working directory for execution
WORKDIR /usr/src/app

# ✅ Copy the watcher script
COPY watch_and_run.sh /usr/src/app/watch_and_run.sh  
RUN chmod +x /usr/src/app/watch_and_run.sh  

# ✅ Expose the application port
EXPOSE 27017

# ✅ Run the script that rebuilds & restarts automatically
CMD ["/bin/bash", "/usr/src/app/watch_and_run.sh"]
