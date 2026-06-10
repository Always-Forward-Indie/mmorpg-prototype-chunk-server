#!/bin/bash

echo "🚀 Starting MMO Chunk Server with automatic rebuilds using watchexec..."
echo "📂 Current Directory: $(pwd)"
echo "📜 Listing contents of /usr/src/app:"
ls -lh /usr/src/app

# ✅ Ensure the binary exists
if [ ! -f "/usr/src/app/build/MMOChunkServer" ]; then
    echo "❌ ERROR: MMOChunkServer binary not found in /usr/src/app!"
    exit 1
fi

# ✅ Ensure the binary is executable
chmod +x /usr/src/app/build/MMOChunkServer

# ✅ Copy config.json to build folder if not present
if [ ! -f "/usr/src/app/build/config.json" ]; then
    echo "⚙️ Copying config.json to build directory..."
    cp /usr/src/app/config.json /usr/src/app/build/config.json
fi

# ✅ Check if watchexec is installed
if ! command -v watchexec &> /dev/null; then
    echo "❌ ERROR: watchexec could not be found!"
    exit 1
fi

# ✅ Start and watch for changes, but ignore unnecessary files
echo "🔄 Watching for changes, rebuilding, and restarting MMOChunkServer if needed..."
exec watchexec -r -e cpp,h,hpp \
    --ignore /usr/src/app/build \
    --ignore /usr/src/app/build/MMOChunkServer \
    --ignore /usr/src/app/build/*.o \
    --ignore /usr/src/app/build/*.log \
    --ignore /usr/src/app/build/CMakeCache.txt \
    --ignore /usr/src/app/build/CMakeFiles \
    --ignore /usr/src/app/build/Makefile \
    --ignore /usr/src/app/build/*.cmake \
    --ignore /usr/src/app/build/install_manifest.txt \
    -- \
    bash -c "
        cd /usr/src/app/build && \
        cmake -DCMAKE_BUILD_TYPE=Debug /usr/src/app && \
        make -j$(nproc) && \
        pgrep MMOChunkServer && pkill MMOChunkServer || true && \
        echo '✅ Server restarting...' && \
        exec /usr/src/app/build/MMOChunkServer
    "
